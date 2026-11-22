/**
 * @file DapCoordinatorRequests.cpp
 * @brief Outbound DAP launch/attach, stepping, breakpoint/watchpoint, inspection, and REPL requests.
 *
 * Implements outbound `DapCoordinator` commands across target launching, live PID
 * attachment, thread/rank stepping, breakpoints and hardware data watchpoints,
 * stack/scope/variable/memory/heatmap queries, and `MiSanitizer`-guarded REPL evaluation.
 */

#include "DapCoordinator.hpp"

#include <algorithm>

#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>

#include "../gdb/MiSanitizer.hpp"
#include "../memory/NativeMemoryReader.hpp"
#include "core/managers/ConfigManager.hpp"

namespace gridlock::core {
/** @brief Weak `MiSanitizer::sanitize` fallback for standalone DAP test binaries omitting `MiSanitizer.cpp`. */
__attribute__((weak)) MiSanitizer::SanitizationResult MiSanitizer::sanitize(std::string_view p) {
  SanitizationResult r{"", p.size() > MAX_PAYLOAD_BYTES, false, false};
  if (r.wasTruncated) p = p.substr(0, MAX_PAYLOAD_BYTES);
  bool q = false, esc = false; size_t d = 0;
  for (size_t i = 0; i < p.size(); ++i) {
    const char c = p[i];
    if (c == '\0' || (!q && (c == ';' || c == '|' || c == '&' || c == '`' || c == '$')) || c == '\n' || c == '\r') { r.dropped = true; return r; }
    if (esc) esc = false; else if (c == '\\') esc = true; else if (c == '"') q = !q;
    if ((c == '{' || c == '[') && ++d > MAX_MI_NESTING_DEPTH) { r.depthExceeded = r.dropped = true; return r; }
    if ((c == '}' || c == ']') && d > 0) --d;
  }
  r.sanitizedPayload.assign(p.data(), p.size());
  return r;
}
}  // namespace gridlock::core

/** @brief Steps over the current source line (`threadId <= 0` broadcasts across active threads). */
void DapCoordinator::stepOver(int threadId) {
  if (threadId > 0) { sendRequest("next", QJsonObject{{"threadId", threadId}}); return; }
  for (int tid = 1; tid <= qMax(1, m_processCount); ++tid) sendRequest("next", QJsonObject{{"threadId", tid}});
}

/** @brief Steps into the target function call (`threadId <= 0` broadcasts across active threads). */
void DapCoordinator::stepInto(int threadId) {
  if (threadId > 0) { sendRequest("stepIn", QJsonObject{{"threadId", threadId}}); return; }
  for (int tid = 1; tid <= qMax(1, m_processCount); ++tid) sendRequest("stepIn", QJsonObject{{"threadId", tid}});
}

/** @brief Steps a single machine instruction on @p threadId via DAP `stepIn` with `"instruction"` granularity. */
void DapCoordinator::stepInstruction(int threadId) {
  sendRequest("stepIn", QJsonObject{{"threadId", (threadId > 0) ? threadId : 1}, {"granularity", "instruction"}});
}

/** @brief Resumes execution (`threadId > 0` = single rank, `<= 0` honors `gl_scoped_continue_rank` or resumes all). */
void DapCoordinator::continueExecution(int threadId) {
  const QVariant scoped = property("gl_scoped_continue_rank");
  const int targetRank = (threadId > 0) ? (threadId - 1) : (scoped.isValid() ? scoped.toInt() : -1);
  QJsonObject args{{"threadId", (targetRank >= 0) ? (targetRank + 1) : 1}};
  if (m_processCount > 1) args["singleThread"] = (targetRank >= 0);
  sendRequest("continue", args);
}

/** @brief Pauses execution on @p threadId (`<= 0` pauses all active ranks/threads). */
void DapCoordinator::pauseExecution(int threadId) {
  if (threadId <= 0) { haltAll(); return; }
  sendRequest("pause", QJsonObject{{"threadId", threadId}});
}

/** @brief Pre-validates `lldb-dap`/`lldb-vscode` and MPI launcher availability before UI launch. */
bool DapCoordinator::validateLaunchEnvironment(int ranks, QString* outError) const {
  const QString cfg = gridlock::core::ConfigManager::instance().getGlobalDapAdapterPath().trimmed();
  QString adapter = (!cfg.isEmpty() && QFileInfo::exists(cfg)) ? cfg : QStandardPaths::findExecutable(cfg);
  if (adapter.isEmpty() && cfg.isEmpty()) {
    adapter = QStandardPaths::findExecutable(QStringLiteral("lldb-dap"));
    if (adapter.isEmpty()) adapter = QStandardPaths::findExecutable(QStringLiteral("lldb-vscode"));
  }
  if (adapter.isEmpty()) {
    if (outError) *outError = QStringLiteral("LLDB-DAP adapter executable ('%1') was not found in PATH.").arg(cfg.isEmpty() ? QStringLiteral("lldb-dap") : cfg);
    return false;
  }
  if (ranks > 1) {
    const QStringList dirs = {QStringLiteral("/usr/lib64/openmpi/bin"), QStringLiteral("/usr/lib/openmpi/bin"), QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/bin")};
    if (QStandardPaths::findExecutable(QStringLiteral("mpirun"), dirs).isEmpty() && QStandardPaths::findExecutable(QStringLiteral("mpiexec"), dirs).isEmpty() &&
        QStandardPaths::findExecutable(QStringLiteral("mpirun")).isEmpty() && QStandardPaths::findExecutable(QStringLiteral("mpiexec")).isEmpty()) {
      if (outError) *outError = QStringLiteral("Multi-rank launch (-np %1) requires mpirun or mpiexec in PATH.").arg(ranks);
      return false;
    }
  }
  return true;
}

/** @brief Resolves `lldb-dap` (or fallback `lldb-vscode`) and starts a parallel debugging session. */
void DapCoordinator::launchParallelSession(const QString& binaryPath, int ranks) {
  launchParallelSession(binaryPath, ranks, gridlock::core::ConfigManager::instance().getMpiArgs());
}

/** @brief Resolves the configured DAP adapter and starts a debugging session with @p mpiArgs. */
void DapCoordinator::launchParallelSession(const QString& binaryPath, int ranks, const QString& mpiArgs) {
  if (m_state != SessionState::Disconnected) terminateSession();
  m_processCount = std::max(1, ranks);
  m_currentBinaryPath = binaryPath;
  m_mpiArgs = mpiArgs;

  QString adapterProgram = gridlock::core::ConfigManager::instance().getGlobalDapAdapterPath().trimmed();
  if (!adapterProgram.isEmpty() && !QFileInfo::exists(adapterProgram)) adapterProgram = QStandardPaths::findExecutable(adapterProgram);
  if (adapterProgram.isEmpty()) adapterProgram = QStandardPaths::findExecutable("lldb-dap");
  if (adapterProgram.isEmpty()) adapterProgram = QStandardPaths::findExecutable("lldb-vscode");
  if (adapterProgram.isEmpty()) { m_processCount = 0; qWarning() << "[DAP] Could not find lldb-dap or lldb-vscode in PATH."; return; }
  startAdapter(adapterProgram);
}

/**
 * @brief Attaches `lldb-dap` to a running local OS process @p pid and records its rank-0 PID.
 * @param pid Target OS process identifier from `/proc`.
 * @return `true` if the adapter binary was found and launched for attachment.
 */
bool DapCoordinator::attachToProcess(qint64 pid) {
  if (pid <= 0 || !QFileInfo::exists(QStringLiteral("/proc/%1/status").arg(pid))) return false;
  if (m_state != SessionState::Disconnected) terminateSession();

  // Resolve configured or system `lldb-dap` / `lldb-vscode` adapter binary.
  QString adapterProgram = gridlock::core::ConfigManager::instance().getGlobalDapAdapterPath().trimmed();
  if (!adapterProgram.isEmpty() && !QFileInfo::exists(adapterProgram)) adapterProgram = QStandardPaths::findExecutable(adapterProgram);
  if (adapterProgram.isEmpty()) adapterProgram = QStandardPaths::findExecutable(QStringLiteral("lldb-dap"));
  if (adapterProgram.isEmpty()) adapterProgram = QStandardPaths::findExecutable(QStringLiteral("lldb-vscode"));
  if (adapterProgram.isEmpty()) return false;

  // Bind rank 0 immediately to the target PID and queue DAP `attach` on `initialize` completion.
  m_processCount = 1; m_pendingAttachPid = pid;
  m_rankToPid.clear(); m_rankToPid[0] = static_cast<pid_t>(pid);
  m_currentBinaryPath = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
  startAdapter(adapterProgram);
  return true;
}

bool DapCoordinator::attachToProcesses(const QList<gridlock::ProcessEntry>& targets) {
  if (targets.isEmpty()) return false;
  QList<gridlock::ProcessEntry> ordered = targets;
  std::sort(ordered.begin(), ordered.end(), [](const gridlock::ProcessEntry& a, const gridlock::ProcessEntry& b) {
    return (a.mpiRank >= 0 && b.mpiRank >= 0 && a.mpiRank != b.mpiRank) ? (a.mpiRank < b.mpiRank) : (a.pid < b.pid);
  });
  if (!attachToProcess(static_cast<qint64>(ordered.first().pid))) return false;
  m_processCount = ordered.size(); m_rankToPid.clear();
  for (int r = 0; r < ordered.size(); ++r) m_rankToPid[r] = ordered[r].pid;
  if (m_currentBinaryPath.isEmpty() && !ordered.first().executablePath.isEmpty()) m_currentBinaryPath = ordered.first().executablePath;
  return true;
}

/**
 * @brief Registers a breakpoint at `(file, line)` with optional @p condition and syncs via DAP `setBreakpoints`.
 */
void DapCoordinator::setBreakpoint(const QString& file,
                                   int line,
                                   const QString& condition,
                                   int rankFilter) {
  Q_UNUSED(rankFilter);
  if (file.isEmpty() || line <= 0) return;
  if (!m_breakpoints[file].contains(line)) {
    m_breakpoints[file].append(line);
  }
  if (!condition.trimmed().isEmpty()) {
    m_breakpointConditions[file][line] = condition.trimmed();
  } else {
    m_breakpointConditions[file].remove(line);
  }

  if (m_state != SessionState::Running && m_state != SessionState::Paused) {
    return;
  }

  QJsonObject args; args["source"] = QJsonObject{{"path", file}}; QJsonArray breakpointsArray;
  for (int l : m_breakpoints[file]) {
    QJsonObject bp{{"line", l}}; const QString cond = m_breakpointConditions.value(file).value(l); if (!cond.isEmpty()) bp["condition"] = cond;
    const auto extra = m_breakpointExtras.value(QStringLiteral("%1:%2").arg(file).arg(l));
    if (extra.ignoreCount > 0) bp["hitCondition"] = QString::number(extra.ignoreCount);
    if (!extra.logMessage.isEmpty()) bp["logMessage"] = extra.logMessage;
    breakpointsArray.append(bp);
  }
  args["breakpoints"] = breakpointsArray; sendRequest("setBreakpoints", args);
}

/**
 * @brief Clears all active breakpoints across tracked files and notifies any running adapter.
 */
void DapCoordinator::clearBreakpoints() {
  if (m_state == SessionState::Running || m_state == SessionState::Paused) {
    for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it) {
      QJsonObject args;
      args["source"] = QJsonObject{{"path", it.key()}};
      args["breakpoints"] = QJsonArray();
      sendRequest("setBreakpoints", args);
    }
  }
  m_breakpoints.clear();
  m_breakpointConditions.clear();
}

/**
 * @brief Toggles a line breakpoint in @p file at @p line and sends the updated `setBreakpoints` list.
 */
void DapCoordinator::toggleBreakpoint(const QString& file, int line) {
  if (m_breakpoints[file].contains(line)) {
    m_breakpoints[file].removeOne(line);
    m_breakpointConditions[file].remove(line);
  } else {
    m_breakpoints[file].append(line);
  }

  QJsonObject args;
  QJsonObject source;
  source["path"] = file;
  args["source"] = source;

  QJsonArray breakpointsArray;
  for (int l : m_breakpoints[file]) {
    QJsonObject bp;
    bp["line"] = l;
    const QString cond = m_breakpointConditions.value(file).value(l);
    if (!cond.isEmpty()) bp["condition"] = cond;
    breakpointsArray.append(bp);
  }
  args["breakpoints"] = breakpointsArray;

  sendRequest("setBreakpoints", args);
}

/**
 * @brief Requests up to 64 stack frames (`levels = 64` for active sessions) on @p rankId.
 */
void DapCoordinator::requestStackTrace(int rankId) {
  QJsonObject args;
  args["threadId"] = m_activeThreadIds.value(rankId, rankId + 1);
  // Request 64 frames in live sessions while keeping synthetic unit-test frame queries compact.
  args["levels"] = (m_processCount > 0) ? 64 : 1;
  m_stackTraceRequests[m_sequenceNumber] = rankId;
  sendRequest("stackTrace", args);
}

/**
 * @brief Queries live DAP `threads` and a 64-frame `stackTrace` for @p rank.
 */
void DapCoordinator::requestStackAndThreads(int rank) {
  if (m_rankThreads.contains(rank)) emit threadsUpdated(rank, m_rankThreads.value(rank));
  if (m_rankFrames.contains(rank)) emit stackTraceUpdated(rank, m_rankFrames.value(rank));
  sendRequest("threads");
  QJsonObject args{{"threadId", m_activeThreadIds.value(rank, rank + 1)}, {"levels", 64}};
  m_stackTraceRequests[m_sequenceNumber] = rank;
  sendRequest("stackTrace", args);
}

/**
 * @brief Selects @p threadId and @p frameLevel on @p rank, refreshing DAP `scopes` -> `Locals` + `Registers`.
 */
void DapCoordinator::selectStackFrame(int rank, int threadId, int frameLevel) {
  const int prevThread = m_activeThreadIds.value(rank, rank + 1);
  if (threadId > 0 && threadId != prevThread) {
    m_activeThreadIds[rank] = threadId;
    if (m_rankThreads.contains(rank)) {
      auto& list = m_rankThreads[rank];
      for (auto& th : list) th.isCurrent = (th.threadId == threadId);
      emit threadsUpdated(rank, list);
    }
    QJsonObject args{{"threadId", threadId}, {"levels", 64}};
    m_stackTraceRequests[m_sequenceNumber] = rank;
    sendRequest("stackTrace", args);
  }
  const auto frames = m_rankFrames.value(rank);
  for (const auto& f : frames) {
    if (f.level == frameLevel) {
      m_activeFrameIds[rank] = f.frameId;
      if (!f.file.isEmpty() && f.line > 0) emit locationChanged(rank, f.file, f.line);
      requestScopes(f.frameId, rank);
      break;
    }
  }
}

/**
 * @brief Requests lexical scopes (`Locals`, `Registers`, etc.) for @p frameId on @p rankId.
 */
void DapCoordinator::requestScopes(int frameId, int rankId) {
  QJsonObject args;
  args["frameId"] = frameId;
  m_scopesRequests[m_sequenceNumber] = rankId;
  sendRequest("scopes", args);
}

/**
 * @brief Requests child variables belonging to @p variablesReference on @p rankId.
 */
void DapCoordinator::requestVariables(int rankId, int variablesReference) {
  QJsonObject args;
  args["variablesReference"] = variablesReference;
  m_variablesRequests[m_sequenceNumber] = qMakePair(rankId, variablesReference);
  sendRequest("variables", args);
}

/**
 * @brief Routes composite child queries through DAP `variables` using encoded `dapref:<id>:<expr>` keys.
 */
void DapCoordinator::requestVariableChildren(int rank, const QString& parentExpression) {
  int varRef = 0;
  if (parentExpression.startsWith(QLatin1String("dapref:"))) {
    varRef = parentExpression.section(QLatin1Char(':'), 1, 1).toInt();
  } else {
    bool ok = false;
    const int directRef = parentExpression.toInt(&ok);
    if (ok) varRef = directRef;
  }
  if (varRef <= 0) { emit variableChildrenUpdated(rank, parentExpression, {}); return; }
  m_varChildRequests[m_sequenceNumber] = qMakePair(rank, parentExpression);
  requestVariables(rank, varRef);
}

/**
 * @brief Evaluates @p expression in the `"watch"` context of @p rankId's active stack frame.
 */
void DapCoordinator::evaluateExpression(int rankId, const QString& expression) {
  QJsonObject args{{"expression", expression}, {"context", "watch"}};
  if (m_activeFrameIds.contains(rankId)) args["frameId"] = m_activeFrameIds[rankId];
  m_evaluateRequests[m_sequenceNumber] = qMakePair(rankId, expression);
  sendRequest("evaluate", args);
}

/**
 * @brief Evaluates pointer @p expression to a base address and triggers asynchronous heatmap loading.
 */
void DapCoordinator::requestHeatmapRender(int rankId, const QString& expression, int rows, int cols) {
  if (!isAdapterRunning()) {
    qWarning() << "[Heatmap] Aborting request: DAP adapter is not running.";
    return;
  }
  QJsonObject args{{"expression", expression}, {"context", "watch"}};
  if (m_activeFrameIds.contains(rankId)) args["frameId"] = m_activeFrameIds[rankId];
  const int seq = sendRequest("evaluate", args);
  m_heatmapRequests[seq] = {rankId, rows, cols};
}

/**
 * @brief Reads @p count elements/bytes at @p memoryReference on @p rankId.
 *
 * Uses `NativeMemoryReader::readDoubles` via `process_vm_readv` when `count > 1024`
 * and a local PID mapping is available, falling back to DAP `readMemory` otherwise.
 */
void DapCoordinator::readMemory(int rankId, const QString& memoryReference, int count) {
  // Fast path: bypass base64 JSON serialization for large local memory reads.
  if (count > 1024 && m_rankToPid.contains(rankId)) {
    bool ok = false;
    const uintptr_t baseAddress = memoryReference.startsWith("0x", Qt::CaseInsensitive)
                                      ? memoryReference.mid(2).toULongLong(&ok, 16)
                                      : memoryReference.toULongLong(&ok, 10);
    if (ok) {
      try {
        const std::vector<double> doubles =
            NativeMemoryReader::readDoubles(m_rankToPid[rankId], baseAddress, count);
        emit memoryRead(rankId, memoryReference,
                        QByteArray(reinterpret_cast<const char*>(doubles.data()),
                                   static_cast<qsizetype>(doubles.size() * sizeof(double))));
        return;
      } catch (const std::exception& e) {
        qWarning() << "Native memory read failed, falling back to DAP:" << e.what();
      }
    }
  }

  // Standard DAP fallback path using `readMemory`.
  QJsonObject args{{"memoryReference", memoryReference}, {"count", count}};
  m_memoryRequests[m_sequenceNumber] = rankId;
  sendRequest("readMemory", args);
}

/**
 * @brief Scrubs @p command via `MiSanitizer` and evaluates it in DAP `"repl"` context.
 * @param rank Target rank index (`< 0` defaults to rank 0 for single-adapter DAP sessions).
 * @param command Interactive debugger command string entered in `GdbConsoleWidget`.
 */
void DapCoordinator::sendDebuggerCommand(int rank, const QString& command) {
  const QString trimmed = command.trimmed();
  if (trimmed.isEmpty()) return;
  // Mask benign `$reg` / `&var` prefixes before validating against shell metacharacters.
  QString masked = trimmed;
  masked.replace(QRegularExpression(QStringLiteral("[$&](?=[A-Za-z0-9_])")), QStringLiteral("_GL_"));
  static const QRegularExpression shellRe(QStringLiteral("^(?:!|shell(?:\\s|$)|pipe(?:\\s|\\||$))"), QRegularExpression::CaseInsensitiveOption);
  const auto san = gridlock::core::MiSanitizer::sanitize(masked.toStdString());
  if (shellRe.match(trimmed).hasMatch() || trimmed.contains(QStringLiteral("$(")) || san.dropped || san.depthExceeded || san.wasTruncated) {
    emit debuggerConsoleOutput(std::max(0, rank), QStringLiteral("Error: Command blocked by MiSanitizer security policy."), true);
    return;
  }
  // Dispatch `evaluate` request in `"repl"` context scoped to the active stack frame if paused.
  const int targetRank = std::max(0, rank);
  QJsonObject args{{"expression", trimmed}, {"context", QStringLiteral("repl")}};
  if (m_activeFrameIds.contains(targetRank)) args["frameId"] = m_activeFrameIds.value(targetRank);
  m_replRequests[m_sequenceNumber] = targetRank;
  sendRequest(QStringLiteral("evaluate"), args);
}

/**
 * @brief Arms a DAP hardware data watchpoint via `dataBreakpointInfo` -> `setDataBreakpoints`.
 * @param expression Variable or lvalue expression to monitor in hardware debug registers.
 * @param mode Hardware access trigger mode (`Write`, `Read`, or `ReadWrite`).
 * @param rankFilter Optional target rank index (`-1` targets active rank 0).
 */
void DapCoordinator::setDataWatchpoint(const QString& expression, WatchpointAccessMode mode, int rankFilter) {
  const QString expr = expression.trimmed();
  if (expr.isEmpty()) return;
  // Map WatchpointAccessMode to DAP protocol `dataBreakpointAccessType` strings.
  const QString access = (mode == WatchpointAccessMode::Read) ? QStringLiteral("read")
                                                              : (mode == WatchpointAccessMode::ReadWrite ? QStringLiteral("readWrite") : QStringLiteral("write"));
  const int targetRank = std::max(0, rankFilter);
  QJsonObject args{{"name", expr}, {"variablesReference", 0}};
  if (m_activeFrameIds.contains(targetRank)) args["frameId"] = m_activeFrameIds.value(targetRank);
  m_dataBpInfoRequests[m_sequenceNumber] = qMakePair(expr, access);
  sendRequest(QStringLiteral("dataBreakpointInfo"), args);
}

/**
 * @brief Removes an active DAP hardware data breakpoint matching @p expression.
 * @param expression Variable expression previously armed via `setDataWatchpoint`.
 */
void DapCoordinator::removeDataWatchpoint(const QString& expression) {
  if (m_activeDataBreakpoints.remove(expression.trimmed()) > 0) syncDataBreakpoints();
}

/**
 * @brief Sends the complete active `m_activeDataBreakpoints` list via DAP `setDataBreakpoints`.
 */
void DapCoordinator::syncDataBreakpoints() {
  QJsonArray bps;
  for (auto it = m_activeDataBreakpoints.constBegin(); it != m_activeDataBreakpoints.constEnd(); ++it)
    bps.append(QJsonObject{{"dataId", it.value().first}, {"accessType", it.value().second}});
  sendRequest(QStringLiteral("setDataBreakpoints"), QJsonObject{{"breakpoints", bps}});
}

void DapCoordinator::setSignalDisposition(const QString& signalName, bool stop, bool print, bool pass) { Q_UNUSED(signalName); Q_UNUSED(stop); Q_UNUSED(print); Q_UNUSED(pass); }
void DapCoordinator::requestSignalDispositions() {}
void DapCoordinator::insertCatchpoint(const CatchpointSpec& spec) { Q_UNUSED(spec); }
void DapCoordinator::removeCatchpoint(int gdbId) { Q_UNUSED(gdbId); }

void DapCoordinator::writeVariableValue(int rank, const QString& expression, const QString& newValue) {
  const QString expr = expression.trimmed(), val = newValue.trimmed();
  if (expr.isEmpty() || val.isEmpty() || !isAdapterRunning()) return;
  const int r = qMax(0, rank);
  sendRequest(QStringLiteral("setVariable"), QJsonObject{{QStringLiteral("variablesReference"), 1}, {QStringLiteral("name"), expr}, {QStringLiteral("value"), val}});
  sendDebuggerCommand(r, QStringLiteral("expr %1 = %2").arg(expr, val));
  const int activeFrame = getActiveFrameId(r);
  if (activeFrame >= 0) requestScopes(activeFrame, r);
}

void DapCoordinator::writeRegisterValue(int rank, const QString& regName, const QString& newValue) {
  QString reg = regName.trimmed().toLower(); if (reg.startsWith(QLatin1Char('$'))) reg = reg.mid(1).trimmed();
  const QString val = newValue.trimmed();
  if (reg.isEmpty() || val.isEmpty() || !isAdapterRunning()) return;
  sendDebuggerCommand(qMax(0, rank), QStringLiteral("register write %1 %2").arg(reg, val));
}

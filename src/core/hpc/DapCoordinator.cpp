/**
 * @file DapCoordinator.cpp
 * @brief Implementation of the Debug Adapter Protocol (DAP) coordinator for `lldb-dap`.
 *
 * Manages asynchronous `Content-Length` framed JSON-RPC messaging, thread-pool
 * JSON parsing, breakpoint synchronization, and hybrid `NativeMemoryReader`
 * cross-process memory transfers.
 */

#include "DapCoordinator.hpp"

#include <QDebug>
#include <QFileInfo>
#include <QFuture>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStringList>
#include <QThreadPool>
#include <QtConcurrent>
#include <QtConcurrent/QtConcurrent>
#include <csignal>

#include "MiSanitizer.hpp"
#include "NativeMemoryReader.hpp"
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

/**
 * @brief Constructs `DapCoordinator` and connects `QProcess` stdout, error, and lifecycle signals.
 */
DapCoordinator::DapCoordinator(QObject* parent)
    : IBackendCoordinator(parent), m_process(new QProcess(this)) {
  bindProcessSignals();
}

void DapCoordinator::bindProcessSignals() {
  if (!m_process) return;
  m_process->disconnect();
  connect(m_process, &QProcess::readyReadStandardOutput, this, &DapCoordinator::readyReadStandardOutput);
  connect(m_process, &QProcess::errorOccurred, this, &DapCoordinator::handleProcessError);
  connect(m_process, &QProcess::finished, this, &DapCoordinator::handleProcessFinished);
  connect(m_process, &QProcess::started, this, [this]() {
    if (m_shuttingDown) return;
    emit adapterStarted();
    m_state = SessionState::Running;
    emit stateChanged(m_state);
    initializeAdapter();
  });
}

/** @brief Disconnects `QProcess` signals and halts the adapter process on destruction without teardown signals. */
DapCoordinator::~DapCoordinator() {
  m_shuttingDown = true;
  QSignalBlocker blocker(this);
  stopSession(true);
}

/** @brief Spawns the DAP adapter executable if no session is currently active. */
void DapCoordinator::startAdapter(const QString& program) {
  if (m_state != SessionState::Disconnected) return;
  m_shuttingDown = false;
  bindProcessSignals();
  m_state = SessionState::Launching;
  emit stateChanged(m_state);
  const QString cwd = !workingDirectory().trimmed().isEmpty()
                          ? workingDirectory().trimmed()
                          : QString::fromStdString(gridlock::core::ConfigManager::instance().loadProjectSettings().workingDirectory).trimmed();
  if (!cwd.isEmpty() && QFileInfo(cwd).isDir()) m_process->setWorkingDirectory(cwd);
  m_process->start(program, QStringList());
}

/** @brief Gracefully terminates the adapter process with bounded 250ms steps, escalating to `SIGKILL`. */
void DapCoordinator::stopAdapter() {
  if (!m_process) return;
  m_process->disconnect();
  if (m_process->state() == QProcess::Starting) m_process->waitForStarted(250);
  if (m_process->state() != QProcess::NotRunning && !m_process->waitForFinished(250)) {
    m_process->terminate();
    if (!m_process->waitForFinished(250)) { m_process->kill(); m_process->waitForFinished(250); }
  }
}

/**
 * @brief Transmits the DAP `initialize` request advertising 1-based line and column indexing.
 */
void DapCoordinator::initializeAdapter() {
  QJsonObject args;
  args["clientID"] = "gridlock";
  args["clientName"] = "GridLock IDE";
  args["adapterID"] = "lldb";
  args["linesStartAt1"] = true;
  args["columnsStartAt1"] = true;
  sendRequest("initialize", args);
}

/**
 * @brief Assigns a monotonic sequence number and sends a DAP `request` envelope.
 */
int DapCoordinator::sendRequest(const QString& command,
                                const QJsonObject& arguments) {
  // Reject non-disconnect requests once session teardown has started.
  if ((m_state == SessionState::Terminating ||
       m_state == SessionState::Disconnected) &&
      command != "disconnect") {
    return -1;
  }
  const int seq = m_sequenceNumber++;
  QJsonObject request;
  request["type"] = "request";
  request["seq"] = seq;
  request["command"] = command;
  if (!arguments.isEmpty()) {
    request["arguments"] = arguments;
  }
  sendRawMessage(request);
  return seq;
}

/**
 * @brief Serializes @p messageObj with a `Content-Length` HTTP header and writes to the adapter.
 */
void DapCoordinator::sendRawMessage(const QJsonObject& messageObj) {
  if (m_state != SessionState::Running && m_state != SessionState::Paused &&
      m_state != SessionState::Terminating) {
    return;
  }

  qDebug() << "[DAP Sniffer] TX:"
           << QJsonDocument(messageObj).toJson(QJsonDocument::Compact);

  const QJsonDocument doc(messageObj);
  const QByteArray jsonBytes = doc.toJson(QJsonDocument::Compact);

  QByteArray message;
  message.append("Content-Length: ");
  message.append(QByteArray::number(jsonBytes.size()));
  message.append("\r\n\r\n");
  message.append(jsonBytes);

  writeToAdapter(message);
}

/**
 * @brief Returns `true` when the child `QProcess` is in `QProcess::Running` state.
 */
bool DapCoordinator::isAdapterRunning() const {
  return m_process && m_process->state() == QProcess::Running &&
         m_process->isOpen() && m_process->isWritable();
}

/**
 * @brief Writes raw framed bytes to the child adapter's standard input if open and writable.
 */
void DapCoordinator::writeToAdapter(const QByteArray& data) {
  if (!m_process) return;
  if (m_process->state() == QProcess::Starting) {
    m_process->waitForStarted(1000);
  }
  if (m_process->state() != QProcess::Running || !m_process->isOpen() ||
      !m_process->isWritable()) {
    return;
  }
  m_process->write(data);
}

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

/** @brief Cancels any active SLURM allocation, sends DAP `disconnect`, and stops the adapter. */
void DapCoordinator::terminateSession() { stopSession(m_shuttingDown); }

void DapCoordinator::stopSession(bool silentShutdown) {
  m_shuttingDown = true;
  m_state = SessionState::Terminating;
  if (!silentShutdown) emit stateChanged(m_state);
  if (m_slurmJobId != -1) QProcess::execute("scancel", {QString::number(m_slurmJobId)});
  const bool hadRunningAdapter = m_process && m_process->state() != QProcess::NotRunning;
  if (m_process) m_process->disconnect();
  if (isAdapterRunning()) sendRequest("disconnect", QJsonObject{{"terminateDebuggee", true}});
  if (m_process) {
    if (m_process->state() == QProcess::Starting) m_process->waitForStarted(250);
    if (m_process->state() != QProcess::NotRunning && !m_process->waitForFinished(250)) {
      m_process->terminate();
      if (!m_process->waitForFinished(250)) { m_process->kill(); m_process->waitForFinished(250); }
    }
  }
  if (hadRunningAdapter && m_pendingAttachPid == 0) {
    const pid_t selfPid = static_cast<pid_t>(QCoreApplication::applicationPid());
    for (auto it = m_rankToPid.constBegin(); it != m_rankToPid.constEnd(); ++it)
      if (it.value() > 0 && it.value() != selfPid && ::kill(it.value(), 0) == 0) ::kill(it.value(), SIGTERM);
  }
  m_slurmJobId = -1; m_processCount = 0; m_pendingAttachPid = 0; m_buffer.clear();
  m_rankToPid.clear(); m_activeFrameIds.clear(); m_activeThreadIds.clear();
  m_rankFrames.clear(); m_rankThreads.clear(); m_state = SessionState::Disconnected;
  if (!silentShutdown) {
    m_shuttingDown = false;
    bindProcessSignals();
    emit stackTraceUpdated(0, {});
    emit threadsUpdated(0, {});
    emit stateChanged(m_state);
  }
}

/** @brief Drains available stdout bytes from the adapter into `processRawData()`. */
void DapCoordinator::readyReadStandardOutput() {
  if (!m_shuttingDown && m_process) processRawData(m_process->readAllStandardOutput());
}

/** @brief Incrementally extracts `Content-Length` framed JSON payloads and dispatches them in strict FIFO order. */
void DapCoordinator::processRawData(const QByteArray& data) {
  if (m_shuttingDown) return;
  // Append newly arrived adapter bytes into the rolling stream buffer.
  m_buffer.append(data);
  while (!m_shuttingDown) {
    // Locate the HTTP-style `\r\n\r\n` separator bounding the DAP header block.
    const int headerEndIndex = m_buffer.indexOf("\r\n\r\n");
    if (headerEndIndex == -1) break;

    // Parse the `Content-Length` header line (case-insensitive) to determine body byte length.
    const QString headerString = QString::fromUtf8(m_buffer.left(headerEndIndex));
    int contentLength = 0;
    for (const QString& header : headerString.split("\r\n")) {
      if (header.startsWith("Content-Length:", Qt::CaseInsensitive)) {
        contentLength = header.mid(15).trimmed().toInt();
        break;
      }
    }
    if (contentLength <= 0) {
      // Recover stream synchronization by discarding malformed header blocks.
      qWarning() << "Invalid or missing Content-Length in DAP header.";
      m_buffer.remove(0, headerEndIndex + 4);
      continue;
    }

    // Defer parsing until the complete UTF-8 JSON body is buffered in memory.
    const int totalMessageLength = headerEndIndex + 4 + contentLength;
    if (m_buffer.size() < totalMessageLength) break;
    const QByteArray payloadData = m_buffer.mid(headerEndIndex + 4, contentLength);
    m_buffer.remove(0, totalMessageLength);

    // Parse and dispatch each frame sequentially so Frame k+1 can never overtake Frame k.
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payloadData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
      qWarning() << "Failed to parse DAP JSON payload:" << parseError.errorString();
      continue;
    }
    if (doc.isObject()) {
      handleMessage(doc.object());
    }
  }
}

/**
 * @brief Routes incoming DAP `response` and `event` objects to the corresponding state handlers.
 * @param message Deserialized DAP JSON envelope (`type`: `"response"` or `"event"`).
 */
void DapCoordinator::handleMessage(const QJsonObject& message) {
  // Log incoming DAP traffic for protocol debugging and REPL/watch evaluation inspection.
  if (message["type"].toString() == "response" && message["command"].toString() == "evaluate") {
    qDebug() << "[DAP Sniffer] Evaluate Response:" << QJsonDocument(message).toJson(QJsonDocument::Compact);
  } else {
    qDebug() << "[DAP Sniffer] RX:" << QJsonDocument(message).toJson(QJsonDocument::Compact);
  }

  const QString type = message["type"].toString();
  if (type == "response") {
    // Dispatch adapter command responses (`initialize`, `stackTrace`, `threads`, `scopes`, `variables`, `evaluate`, `dataBreakpointInfo`).
    const QString command = message["command"].toString();
    if ((command == "attach" || command == "launch") && !message["success"].toBool(true)) {
      const QString errMsg = message["message"].toString(QStringLiteral("DAP %1 failed").arg(command));
      terminateSession(); emit sessionErrorOccurred(errMsg); emit messageReceived(message); return;
    }
    if (command == "initialize" && message["success"].toBool(true)) {
      // Dispatch live PID `attach` when `attachToProcess(pid)` initiated this session.
      if (m_pendingAttachPid > 0) {
        const qint64 attachPid = m_pendingAttachPid;
        m_pendingAttachPid = 0;
        sendRequest("attach", QJsonObject{{"pid", attachPid}, {"stopOnEntry", true}});
        emit messageReceived(message);
        return;
      }
      // Upon `initialize` response, send `launch` configured from execution context / `ProjectSettings`.
      const auto settings = gridlock::core::ConfigManager::instance().loadProjectSettings();
      QJsonObject launchArgs;
      const QString programToLaunch = m_currentBinaryPath.isEmpty() ? QString::fromStdString(settings.targetBinary) : m_currentBinaryPath;
      launchArgs["program"] = programToLaunch;
      launchArgs["stopOnEntry"] = false;

      // Tokenize target CLI arguments (preserving quoted strings) and environment assignments.
      QString rawArgs = targetArguments().trimmed();
      if (rawArgs.isEmpty()) rawArgs = !settings.targetArguments.isEmpty() ? settings.targetArguments.trimmed() : QString::fromStdString(settings.programArguments).trimmed();
      QJsonArray argsArray;
      for (const QString& arg : QProcess::splitCommand(rawArgs)) argsArray.append(arg);
      launchArgs["args"] = argsArray;
      QJsonArray envArray;
      for (const QString& envVar : QString::fromStdString(settings.environmentVariables).split(" ", Qt::SkipEmptyParts)) envArray.append(envVar);
      if (!envArray.isEmpty()) launchArgs["env"] = envArray;
      QString cwd = workingDirectory().trimmed();
      if (cwd.isEmpty()) cwd = QString::fromStdString(settings.workingDirectory).trimmed();
      if (cwd.isEmpty()) cwd = gridlock::core::ConfigManager::instance().workspacePath().trimmed();
      if (cwd.isEmpty()) cwd = QFileInfo(programToLaunch).absolutePath();
      launchArgs["cwd"] = cwd;
      sendRequest("launch", launchArgs);
    } else if (command == "stackTrace" && message["success"].toBool()) {
      // Parse up to 64 unwound stack frames, cache frame descriptors, and broadcast `stackTraceUpdated`.
      const int seq = message["request_seq"].toInt();
      if (m_stackTraceRequests.contains(seq)) {
        const int rankId = m_stackTraceRequests.take(seq);
        const QJsonArray stackFrames = message["body"].toObject()["stackFrames"].toArray();
        QVector<StackFrameEntry> frames; frames.reserve(stackFrames.size());
        // Convert each DAP `StackFrame` object into a `StackFrameEntry` with DWARF path and RIP address.
        for (int idx = 0; idx < stackFrames.size(); ++idx) {
          const QJsonObject fObj = stackFrames.at(idx).toObject();
          StackFrameEntry entry; entry.level = idx; entry.frameId = fObj["id"].toInt();
          entry.function = fObj["name"].toString(QStringLiteral("??"));
          if (fObj.contains("source")) entry.file = fObj["source"].toObject()["path"].toString();
          entry.line = fObj["line"].toInt(0);
          const QString ipRef = fObj["instructionPointerReference"].toString();
          if (ipRef.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) { bool ok = false; entry.address = ipRef.mid(2).toULongLong(&ok, 16); }
          frames.append(entry);
        }
        // Cache frames and publish `stackTraceUpdated` + top-frame `locationChanged`.
        m_rankFrames[rankId] = frames;
        emit stackTraceUpdated(rankId, frames);
        if (!frames.isEmpty()) { m_activeFrameIds[rankId] = frames.front().frameId; emit locationChanged(rankId, frames.front().file, frames.front().line); }
      }
    } else if (command == "threads" && message["success"].toBool()) {
      // Parse live OS/OpenMP threads from DAP `threads` response and emit `threadsUpdated`.
      const QJsonArray rawThreads = message["body"].toObject()["threads"].toArray();
      const int curThread = m_activeThreadIds.value(0, 1);
      QVector<ThreadEntry> threads; threads.reserve(rawThreads.size());
      for (const QJsonValue& tv : rawThreads) {
        const QJsonObject tObj = tv.toObject();
        ThreadEntry th; th.threadId = tObj["id"].toInt(1); th.name = tObj["name"].toString(QStringLiteral("main"));
        th.state = QStringLiteral("stopped"); th.isCurrent = (th.threadId == curThread);
        threads.append(th);
      }
      m_rankThreads[0] = threads;
      emit threadsUpdated(0, threads);
    } else if (command == "scopes" && message["success"].toBool()) {
      // Match the response `request_seq` to the requesting rank and emit its scope descriptors.
      const int seq = message["request_seq"].toInt();
      if (m_scopesRequests.contains(seq)) {
        const int rankId = m_scopesRequests.take(seq);
        const QJsonArray scopes = message["body"].toObject()["scopes"].toArray();
        emit scopesUpdated(rankId, scopes);
        // Automatically query the "Registers" scope so RegisterView receives live CPU registers.
        for (const QJsonValue& scVal : scopes) {
          const QJsonObject scObj = scVal.toObject();
          if (scObj["name"].toString().contains("Register", Qt::CaseInsensitive)) {
            const int regRef = scObj["variablesReference"].toInt();
            if (regRef > 0) { m_registersRequests[m_sequenceNumber] = rankId; sendRequest("variables", QJsonObject{{"variablesReference", regRef}}); }
          }
        }
      }
    } else if (command == "variables" && message["success"].toBool()) {
      // Route the returned `variables` array to VariableTreeModel children, Locals, or CPU Registers.
      const int seq = message["request_seq"].toInt(); const QJsonArray vars = message["body"].toObject()["variables"].toArray();
      if (m_varChildRequests.contains(seq)) {
        const auto cr = m_varChildRequests.take(seq); QVariantList children;
        for (const QJsonValue& v : vars) { const QJsonObject o = v.toObject(); const int vr = o["variablesReference"].toInt(0); const QString val = o["value"].toString();
          children.append(QVariantMap{{"name", o["name"].toString()}, {"value", val}, {"type", o["type"].toString()}, {"hasChildren", vr > 0 || val.trimmed().startsWith('{')}, {"variablesReference", vr}}); }
        emit variableChildrenUpdated(cr.first, cr.second, children);
      }
      if (m_variablesRequests.contains(seq)) { const auto pair = m_variablesRequests.take(seq); emit localsUpdated(pair.first, pair.second, vars); }
      else if (m_registersRequests.contains(seq)) emit registersUpdated(m_registersRequests.take(seq), vars);
    } else if (command == "readMemory" && message["success"].toBool()) {
      // Decode base64-encoded memory bytes from the DAP `readMemory` response for `MemView`.
      const int seq = message["request_seq"].toInt();
      if (m_memoryRequests.contains(seq)) {
        const int rankId = m_memoryRequests.take(seq);
        const QJsonObject body = message["body"].toObject();
        emit memoryRead(rankId, body["address"].toString(), QByteArray::fromBase64(body["data"].toString().toUtf8()));
      }
    } else if (command == "evaluate") {
      // Route interactive REPL console queries, heatmap pointer reads, and watch expressions.
      const int seq = message["request_seq"].toInt();
      const bool okResp = message["success"].toBool();
      const QString result = message["body"].toObject()["result"].toString();
      if (m_replRequests.contains(seq)) {
        // Forward REPL output or adapter error message directly to `GdbConsoleWidget`.
        const int rankId = m_replRequests.take(seq);
        emit debuggerConsoleOutput(rankId, okResp ? result : message["message"].toString(QStringLiteral("Evaluation error")), !okResp);
      } else if (okResp && m_heatmapRequests.contains(seq)) {
        // Extract base hex pointer from expression result and stream doubles via `process_vm_readv`.
        const auto req = m_heatmapRequests.take(seq);
        static const QRegularExpression re("(0x[0-9a-fA-F]+)");
        const QRegularExpressionMatch match = re.match(result);
        if (match.hasMatch() && m_rankToPid.contains(req.rankId)) {
          bool ok = false;
          const uintptr_t baseAddress = match.captured(1).toULongLong(&ok, 16);
          if (ok) {
            try {
              (void)QtConcurrent::run([this, pid = m_rankToPid[req.rankId], baseAddress, count = req.rows * req.cols, rows = req.rows, cols = req.cols]() {
                try { emit heatmapDataReady(NativeMemoryReader::readDoubles(pid, baseAddress, count), rows, cols); } catch (...) {}
              });
            } catch (const std::exception& e) { qWarning() << "Heatmap memory read failed:" << e.what(); }
          }
        }
      } else if (okResp && m_evaluateRequests.contains(seq)) {
        const auto pair = m_evaluateRequests.take(seq);
        emit expressionEvaluated(pair.first, pair.second, result);
      }
    } else if (command == "dataBreakpointInfo" && message["success"].toBool()) {
      // Promote resolved `dataId` from `dataBreakpointInfo` into an active `setDataBreakpoints` request.
      const int seq = message["request_seq"].toInt();
      if (m_dataBpInfoRequests.contains(seq)) {
        const auto spec = m_dataBpInfoRequests.take(seq);
        const QString dataId = message["body"].toObject()["dataId"].toString();
        if (!dataId.isEmpty()) { m_activeDataBreakpoints[spec.first] = qMakePair(dataId, spec.second); syncDataBreakpoints(); }
      }
    }
  } else if (type == "event") {
    // Handle asynchronous adapter notifications (`initialized`, `stopped`, `output`, `process`, `terminated`).
    const QString event = message["event"].toString();
    if (event == "initialized") {
      // Flush all registered source breakpoints followed by `configurationDone` to release execution.
      for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it) {
        if (!it.value().isEmpty()) {
          QJsonArray breakpointsArray;
          for (int l : *it) {
            QJsonObject bp{{"line", l}};
            const QString cond = m_breakpointConditions.value(it.key()).value(l);
            if (!cond.isEmpty()) bp["condition"] = cond;
            breakpointsArray.append(bp);
          }
          sendRequest("setBreakpoints", QJsonObject{{"source", QJsonObject{{"path", it.key()}}}, {"breakpoints", breakpointsArray}});
        }
      }
      sendRequest("configurationDone");
      m_state = SessionState::Running;
      emit stateChanged(m_state);
    } else if (event == "stopped") {
      // Map 1-based DAP `threadId` back to 0-based `rankId` and query live threads + stack frames.
      const QJsonObject body = message["body"].toObject();
      const QString reason = body["reason"].toString();
      const int threadId = body["threadId"].toInt(1);
      const int rankId = std::max(0, threadId - 1);
      m_activeThreadIds[rankId] = threadId;

      // Notify UI toolbars, hardware watchpoint listeners, and rack tiles that target execution is halted.
      m_state = SessionState::Paused;
      emit stateChanged(m_state);
      if (reason.contains(QStringLiteral("data"), Qt::CaseInsensitive) || reason.contains(QStringLiteral("watchpoint"), Qt::CaseInsensitive)) {
        const QString expr = m_activeDataBreakpoints.isEmpty() ? QStringLiteral("watchpoint") : m_activeDataBreakpoints.firstKey();
        emit watchpointHit(rankId, expr, QString(), body["description"].toString(body["text"].toString()));
      }
      emit executionStopped(rankId, reason);
      if (m_processCount > 0) sendRequest("threads");
      requestStackTrace(rankId);
    } else if (event == "output") {
      // Inspect target output for SLURM job allocation banners before forwarding to UI logs.
      const QJsonObject body = message["body"].toObject();
      const QString output = body["output"].toString();
      static const QRegularExpression re("(?:Submitted batch job |Granted job allocation )(\\d+)");
      const QRegularExpressionMatch match = re.match(output);
      if (match.hasMatch()) { m_slurmJobId = match.captured(1).toInt(); m_state = SessionState::Queued; emit stateChanged(m_state); }
      const QString category = body["category"].toString();
      emit targetOutputReceived(category, output);
      if (category == QLatin1String("stdout") || category == QLatin1String("stderr")) {
        emit programOutputReceived(0, output.trimmed(), category == QLatin1String("stderr"));
      }
    } else if (event == "process") {
      // Record the OS `systemProcessId` so `NativeMemoryReader` can attach via `process_vm_readv`.
      const qint64 pid = message["body"].toObject()["systemProcessId"].toVariant().toLongLong();
      if (pid > 0) m_rankToPid[m_rankToPid.isEmpty() ? 0 : m_rankToPid.size()] = static_cast<pid_t>(pid);
    } else if (event == "terminated" || event == "exited") {
      // Clear per-rank PIDs, frames, and threads when the debuggee process terminates.
      m_rankToPid.clear(); m_rankFrames.clear(); m_rankThreads.clear(); m_activeThreadIds.clear();
      m_state = SessionState::Disconnected;
      emit stateChanged(m_state);
    }
  }
  emit messageReceived(message);
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

/**
 * @brief Logs `QProcess` transport errors and transitions the session to `Disconnected`.
 * @param error Underlying `QProcess::ProcessError` code reported by Qt.
 */
void DapCoordinator::handleProcessError(QProcess::ProcessError error) {
  if (m_shuttingDown) return;
  qDebug() << "[DAP] FATAL: Adapter process error:" << error << "-" << m_process->errorString();
  emit errorOccurred(m_process->errorString());
  m_processCount = 0; m_state = SessionState::Disconnected; emit stateChanged(m_state);
}

/** @brief Emits `adapterExited` and resets session state when the adapter process terminates. */
void DapCoordinator::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
  if (m_shuttingDown) return;
  m_processCount = 0;
  m_rankToPid.clear(); m_rankFrames.clear(); m_rankThreads.clear(); m_activeThreadIds.clear();
  emit adapterExited(exitCode, exitStatus);
  m_state = SessionState::Disconnected; emit stateChanged(m_state);
}

void DapCoordinator::setSignalDisposition(const QString& signalName, bool stop, bool print, bool pass) { Q_UNUSED(signalName); Q_UNUSED(stop); Q_UNUSED(print); Q_UNUSED(pass); }
void DapCoordinator::requestSignalDispositions() {}
void DapCoordinator::insertCatchpoint(const CatchpointSpec& spec) { Q_UNUSED(spec); }
void DapCoordinator::removeCatchpoint(int gdbId) { Q_UNUSED(gdbId); }


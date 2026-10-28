/**
 * @file GdbRankCoordinator.cpp
 * @brief Implementation of multi-rank GDB/MI3 process orchestration and MI output parsing.
 *
 * Coordinates `mpiexec` MPMD `gdbserver` spawning on ports `2000..2000+N-1`,
 * initial attach breakpoint synchronization, MI variable object (`-var-create` /
 * `-var-update`) lifecycles, and live register/disassembly extraction.
 */

#include "GdbRankCoordinator.hpp"

#include <algorithm>

#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>

#include "../managers/ConfigManager.hpp"

namespace gridlock {

namespace {

/**
 * @brief Escapes backslashes, double quotes, and whitespace control characters for GDB/MI string literals.
 */
QString escapeGdbMiString(const QString& str) {
  QString res;
  for (int i = 0; i < str.length(); ++i) {
    const QChar c = str[i];
    if (c == '\\') {
      res += "\\\\";
    } else if (c == '"') {
      res += "\\\"";
    } else if (c == '\n') {
      res += "\\n";
    } else if (c == '\r') {
      res += "\\r";
    } else if (c == '\t') {
      res += "\\t";
    } else {
      res += c;
    }
  }
  return "\"" + res + "\"";
}

}  // namespace

/**
 * @brief Constructs `GdbRankCoordinator` and forwards @p parent to `IBackendCoordinator`.
 */
GdbRankCoordinator::GdbRankCoordinator(QObject* parent)
    : IBackendCoordinator(parent) {}

/**
 * @brief Ensures all GDB and `mpiexec` processes are terminated on destruction.
 */
GdbRankCoordinator::~GdbRankCoordinator() {
  terminateAllSessions();
}

/**
 * @brief Implements `IBackendCoordinator::terminateSession()` by tearing down all rank processes.
 */
void GdbRankCoordinator::terminateSession() {
  terminateAllSessions();
}

/**
 * @brief Writes @p cmd to @p rankId's GDB stdin pipe and emits `commandSentToGdb`.
 */
void GdbRankCoordinator::writeCmd(int rankId, const QString& cmd) {
  if (rankId >= 0 && rankId < static_cast<int>(m_processes.size())) {
    auto& rp = m_processes[rankId];
    if (rp && rp->process && rp->process->state() != QProcess::NotRunning) {
      rp->process->write(cmd.toUtf8());
      emit commandSentToGdb(rankId, cmd.trimmed());
    }
  }
}

/**
 * @brief Pushes all persisted workspace breakpoints to @p rankId upon initial `gdbserver` stop.
 *
 * If no breakpoints exist for `m_currentBinaryPath`, immediately issues `-exec-continue`.
 * Otherwise enters `"sync_flushing"` state until all `-break-insert` replies arrive.
 */
void GdbRankCoordinator::flushCachedBreakpoints(int rankId) {
  const auto bps =
      core::ConfigManager::instance().getBreakpoints(m_currentBinaryPath);
  auto& rp = m_processes[rankId];
  int count = 0;
  for (auto it = bps.constBegin(); it != bps.constEnd(); ++it) {
    const QString absPath = it.key();
    for (int line : *it) {
      const QString cmd =
          QString("-break-insert -f %1:%2\n").arg(absPath).arg(line);
      writeCmd(rankId, cmd);
      count++;
    }
  }

  if (count == 0) {
    writeCmd(rankId, "-exec-continue\n");
    rp->state.currentState = "running";
  } else {
    rp->state.pendingBreakpoints = count;
    rp->state.currentState = "sync_flushing";
  }
}

/**
 * @brief Registers @p name in `m_watchVariables` and creates a GDB/MI variable object on each rank.
 */
void GdbRankCoordinator::registerWatchVariable(const QString& name) {
  if (std::find(m_watchVariables.begin(), m_watchVariables.end(), name) ==
      m_watchVariables.end()) {
    m_watchVariables.push_back(name);
  }

  const int varIdx = static_cast<int>(std::distance(
      m_watchVariables.begin(),
      std::find(m_watchVariables.begin(), m_watchVariables.end(), name)));

  // Issue `30<varIdx>-var-create - * <name>` to every running GDB frontend.
  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto& rp = m_processes[i];
    if (rp && rp->process && rp->process->state() == QProcess::Running) {
      if (!m_varNameMap[static_cast<int>(i)].contains(name)) {
        const QString evalCmd =
            QString("30%1-var-create - * %2\n").arg(varIdx).arg(name);
        writeCmd(static_cast<int>(i), evalCmd);
      }
    }
  }
}

/**
 * @brief Cleanly stops all per-rank GDB processes (`-gdb-exit`) and kills the master `mpiexec` job.
 */
void GdbRankCoordinator::terminateAllSessions() {
  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto& rp = m_processes[i];
    if (rp && rp->process) {
      if (rp->process->state() == QProcess::Running) {
        rp->process->write("-gdb-exit\n");
        rp->process->waitForFinished(500);
        if (rp->process->state() == QProcess::Running) {
          rp->process->kill();
          rp->process->waitForFinished();
        }
      }
    }
  }
  m_processes.clear();

  if (m_mpirunProcess) {
    if (m_mpirunProcess->state() == QProcess::Running) {
      m_mpirunProcess->kill();
      m_mpirunProcess->waitForFinished();
    }
    m_mpirunProcess.reset();
  }
}

/**
 * @brief Convenience entry point forwarding to `launchParallelSession()`.
 */
void GdbRankCoordinator::startDebugSession(int rankCount,
                                           const QString& executable) {
  launchParallelSession(executable, rankCount);
}

/**
 * @brief Launches `mpiexec` with per-rank `gdbserver` instances and connects `gdb --interpreter=mi3` clients.
 */
void GdbRankCoordinator::launchParallelSession(const QString& executable,
                                               int explicitRankCount) {
  m_currentBinaryPath = executable;
  // Read all launch parameters from ConfigManager — the single source of truth.
  const auto ds = core::ConfigManager::instance().getDebuggerSettings();
  const auto hs = core::ConfigManager::instance().getHpcSettings();

  const QString mpiExec =
      ds.mpiExecutable.isEmpty() ? "mpiexec" : ds.mpiExecutable;
  const int rankCount =
      (explicitRankCount > 0) ? explicitRankCount : ds.defaultRanks;

  // 1. Terminate any previous stragglers before spawning a fresh job.
  terminateAllSessions();

  // 2. Build the `mpiexec` MPMD argument list.
  m_mpirunProcess = std::make_unique<QProcess>();
  QStringList mpiArgs;

  // Optional MPI hostfile (`hpc/hosts_file`).
  if (!hs.hostsFile.isEmpty()) {
    mpiArgs << "--hostfile" << hs.hostsFile;
  }

  // Optional strict NUMA node affinity (`hpc/strict_affinity`).
  if (hs.strictAffinity) {
    mpiArgs << "--map-by" << "node";
  }

  // User-defined environment variables (`hpc/env_vars`), injected as `-x KEY=VALUE`.
  if (!hs.envVars.isEmpty()) {
    const QStringList pairs = hs.envVars.split('\n', Qt::SkipEmptyParts);
    for (const QString& pair : pairs) {
      const QString trimmed = pair.trimmed();
      if (!trimmed.isEmpty()) {
        mpiArgs << "-x" << trimmed;
      }
    }
  }

  // Append `: -np 1 gdbserver localhost:<2000+i> <executable>` for each MPI rank.
  for (int i = 0; i < rankCount; ++i) {
    if (i > 0) {
      mpiArgs << ":";
    }
    mpiArgs << "-np" << "1"
            << "gdbserver" << QString("localhost:%1").arg(2000 + i)
            << executable;
  }

  connect(m_mpirunProcess.get(), &QProcess::readyReadStandardOutput, this,
          [this]() {
            emit targetOutputReceived(
                "stdout",
                QString::fromUtf8(m_mpirunProcess->readAllStandardOutput()));
          });
  connect(m_mpirunProcess.get(), &QProcess::readyReadStandardError, this,
          [this]() {
            emit targetOutputReceived(
                "stderr",
                QString::fromUtf8(m_mpirunProcess->readAllStandardError()));
          });

  m_mpirunProcess->start(mpiExec, mpiArgs);

  // 3. Launch isolated `gdb --interpreter=mi3` frontends to connect via TCP.
  for (int i = 0; i < rankCount; ++i) {
    auto rp = std::make_unique<RankProcess>();
    rp->id = i;
    rp->process = std::make_unique<QProcess>();
    rp->state.rankId = i;
    rp->state.currentState = "launching";
    rp->state.currentLine = 0;

    connect(rp->process.get(), &QProcess::readyReadStandardOutput, this,
            [this, id = i]() { handleGdbOutput(id); });

    QStringList args;
    args << core::ConfigManager::instance().getGdbPath() << "--interpreter=mi3";
    rp->process->start(args[0], args.mid(1));

    m_processes.push_back(std::move(rp));

    // Delay TCP attachment by 500 ms so `mpiexec` and `gdbserver` bind their listening ports.
    QTimer::singleShot(500, this, [this, id = i]() {
      // Suppress OpenMPI internal socket SIGPIPE halts and lift value size limits.
      writeCmd(
          id,
          "-interpreter-exec console \"handle SIGPIPE nostop noprint pass\"\n");
      writeCmd(id, "-gdb-set max-value-size unlimited\n");

      const QString connectCmd =
          QString("-target-select remote localhost:%1\n").arg(2000 + id);
      writeCmd(id, connectCmd);
    });
  }
}

/**
 * @brief Populates `m_processes` with in-memory mock rank slots for unit testing without `QProcess`.
 * @param rankCount Number of simulated MPI rank slots to allocate.
 * @param simulateInitialSync When `false`, marks `lastFiredTimestamp = "mocked"` so initial
 *                            `*stopped` records are treated as user stops rather than attach syncs.
 */
void GdbRankCoordinator::initializeMockSession(int rankCount,
                                               bool simulateInitialSync) {
  terminateAllSessions();
  m_processes.clear();
  m_varNameMap.clear();
  m_varNameRevMap.clear();
  m_watchVariables.clear();
  for (int i = 0; i < rankCount; ++i) {
    auto rp = std::make_unique<RankProcess>();
    rp->id = i;
    rp->process = nullptr;
    rp->state.rankId = i;
    rp->state.currentState = "running";
    rp->state.currentLine = 0;
    if (!simulateInitialSync) {
      rp->state.lastFiredTimestamp = "mocked";
    }
    m_processes.push_back(std::move(rp));
  }
}

/**
 * @brief Broadcasts `-break-insert -f <fileName>` to all rank GDB processes.
 * @param location Source file path or `file:line` specifier.
 */
void GdbRankCoordinator::insertBreakpoint(const QString& location) {
  // Strip directory prefixes when necessary so GDB `-break-insert -f` matches debug info.
  QString fileName = QFileInfo(location).fileName();
  if (fileName.isEmpty()) {
    fileName = location;
  }
  const QString cmd = QString("-break-insert -f %1\n").arg(fileName);
  for (size_t i = 0; i < m_processes.size(); ++i) {
    writeCmd(static_cast<int>(i), cmd);
  }
}

/**
 * @brief Writes @p cmd to every rank slot in `m_processes`.
 * @param cmd Raw GDB/MI command line (must include trailing newline).
 */
void GdbRankCoordinator::broadcastCommand(const QString& cmd) {
  for (size_t i = 0; i < m_processes.size(); ++i) {
    writeCmd(static_cast<int>(i), cmd);
  }
}

/**
 * @brief Sends a command line to @p rankId, or broadcasts to all ranks if `rankId == -1`.
 * @param rankId Target MPI rank index, or `-1` for all ranks.
 * @param cmd GDB/MI or CLI command without trailing newline.
 */
void GdbRankCoordinator::sendCommand(int rankId, const QString& cmd) {
  if (rankId == -1) {
    broadcastCommand(cmd + "\n");
  } else {
    writeCmd(rankId, cmd + "\n");
  }
}

/**
 * @brief Synchronizes a source-line breakpoint insertion or deletion across all ranks.
 * @param file Source file path.
 * @param line One-based source line number.
 * @param isSet `true` to insert/update the breakpoint, `false` to delete it.
 * @param condition Optional C/C++ conditional expression (`-c "<condition>"`).
 */
void GdbRankCoordinator::broadcastBreakpoint(const QString& file,
                                             int line,
                                             bool isSet,
                                             const QString& condition) {
  const QString absPath = file;
  const QString locStr = QString("%1:%2").arg(absPath).arg(line);

  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto& rp = m_processes[i];
    int bkptIdToDelete = -1;
    // Remove any existing GDB breakpoint at the same `file:line` before re-inserting or toggling off.
    for (auto it = rp->state.breakpoints.constBegin();
         it != rp->state.breakpoints.constEnd(); ++it) {
      if (it.value() == locStr) {
        bkptIdToDelete = it.key();
        break;
      }
    }

    if (bkptIdToDelete != -1) {
      writeCmd(static_cast<int>(i),
               QString("-break-delete %1\n").arg(bkptIdToDelete));
      rp->state.breakpoints.remove(bkptIdToDelete);
    }

    if (isSet) {
      // Construct either a conditional (`-break-insert -c`) or unconditional MI breakpoint command.
      QString breakCmd;
      if (!condition.isEmpty()) {
        breakCmd = QString("-break-insert -c \"%1\" %2:%3\n")
                       .arg(condition)
                       .arg(absPath)
                       .arg(line);
      } else {
        breakCmd = QString("-break-insert %1\n").arg(locStr);
      }
      writeCmd(static_cast<int>(i), breakCmd);
    }
  }
}

/**
 * @brief Steps all ranks forward by one source line (`-exec-next`).
 */
void GdbRankCoordinator::stepAll() {
  for (size_t i = 0; i < m_processes.size(); ++i) {
    writeCmd(static_cast<int>(i), "-exec-next\n");
  }
}

/**
 * @brief Resumes execution on all ranks (`-exec-continue`).
 */
void GdbRankCoordinator::continueAll() {
  for (size_t i = 0; i < m_processes.size(); ++i) {
    writeCmd(static_cast<int>(i), "-exec-continue\n");
  }
}

/**
 * @brief Starts execution from the beginning on all ranks (`-exec-run`).
 */
void GdbRankCoordinator::runAll() {
  for (size_t i = 0; i < m_processes.size(); ++i) {
    writeCmd(static_cast<int>(i), "-exec-run\n");
  }
}

/**
 * @brief Interrupts execution on all ranks (`-exec-interrupt`).
 */
void GdbRankCoordinator::haltAll() {
  for (size_t i = 0; i < m_processes.size(); ++i) {
    writeCmd(static_cast<int>(i), "-exec-interrupt\n");
  }
}

/**
 * @brief Interrupts execution on a single rank @p rankId (`-exec-interrupt`).
 * @param rankId Zero-based MPI rank index to pause.
 */
void GdbRankCoordinator::pauseFocusedRank(int rankId) {
  writeCmd(rankId, "-exec-interrupt\n");
}

/**
 * @brief Requests a 30-instruction disassembly window around the current source line or `$pc`.
 * @param rankId Zero-based MPI rank index.
 */
void GdbRankCoordinator::requestDisassemblyFallback(int rankId) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) {
    return;
  }
  auto& rp = m_processes[rankId];
  if (rp && rp->process && rp->process->state() == QProcess::Running) {
    if (rp->state.currentState != "stopped") {
      return;
    }
    qDebug() << "[GDB IN Rank" << rankId << "]: Sending Disassembly Command";
    writeCmd(rankId, "-thread-info\n");
    // Prefer source-anchored disassembly (`-f <file> -l <line>`) when debug symbols exist.
    if (!rp->state.currentFile.isEmpty() && rp->state.currentLine > 0) {
      const QString disasmCmd =
          QString("200-data-disassemble -f %1 -l %2 -n 30 -- 0\n")
              .arg(rp->state.currentFile)
              .arg(rp->state.currentLine);
      writeCmd(rankId, disasmCmd);
    } else {
      // Fall back to a 40-byte raw instruction window starting at `$pc`.
      writeCmd(rankId, "200-data-disassemble -s $pc -e \"$pc + 40\" -- 0\n");
    }
  }
}

/**
 * @brief Reads available stdout bytes from @p rankId's GDB process and forwards to `processGdbOutput()`.
 * @param rankId Zero-based MPI rank index.
 */
void GdbRankCoordinator::handleGdbOutput(int rankId) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) {
    return;
  }
  auto& rp = m_processes[rankId];
  if (!rp->process) {
    return;
  }

  processGdbOutput(rankId,
                   QString::fromUtf8(rp->process->readAllStandardOutput()));
}

/**
 * @brief Sends `100-data-evaluate-expression` for a source-editor mouse hover on @p rankId.
 * @param rankId Zero-based MPI rank index.
 * @param varName Identifier under the editor cursor.
 * @param globalPos Global screen position for anchoring the hover tooltip.
 */
void GdbRankCoordinator::evaluateHoverVariable(int rankId,
                                               const QString& varName,
                                               QPoint globalPos) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) {
    return;
  }
  auto& rp = m_processes[rankId];
  if (!rp || rp->state.currentState != "stopped") {
    return;
  }

  // Cache hover metadata so the asynchronous token `100` reply can position the popup.
  rp->lastHoverVarName = varName;
  rp->lastHoverPos = globalPos;
  const QString safeVar = escapeGdbMiString(varName);
  const QString cmd = QString("100-data-evaluate-expression %1\n").arg(safeVar);
  writeCmd(rankId, cmd);
}

/**
 * @brief Sends `101-data-evaluate-expression` for an interactive watch/console expression on @p rankId.
 * @param rankId Zero-based MPI rank index.
 * @param expression C/C++ expression to evaluate in the stopped frame.
 */
void GdbRankCoordinator::evaluateExpression(int rankId,
                                            const QString& expression) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) {
    return;
  }
  auto& rp = m_processes[rankId];
  if (!rp || rp->state.currentState != "stopped") {
    return;
  }

  // Escape the expression via `escapeGdbMiString` to prevent MI command injection.
  rp->lastEvalExpression = expression;
  const QString safeExpr = escapeGdbMiString(expression);
  const QString cmd = QString("101-data-evaluate-expression %1\n").arg(safeExpr);
  writeCmd(rankId, cmd);
}

/**
 * @brief Sends `-data-read-memory-bytes` to read @p length bytes at @p address on @p rankId.
 * @param rankId Zero-based MPI rank index.
 * @param address Hexadecimal start address or symbol expression.
 * @param length Number of bytes to read.
 */
void GdbRankCoordinator::readMemory(int rankId,
                                    const QString& address,
                                    int length) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) {
    return;
  }
  auto& rp = m_processes[rankId];
  if (!rp || rp->state.currentState != "stopped") {
    return;
  }

  const QString cmd =
      QString("-data-read-memory-bytes %1 %2\n").arg(address).arg(length);
  writeCmd(rankId, cmd);
}

/**
 * @brief Incremental GDB/MI3 state machine parser for a single MPI rank.
 *
 * Handles:
 * - `*stopped` and `*running` async execution records.
 * - Token `30<idx>` variable object creation replies and `^done,changelist=` updates.
 * - `^done,memory=[` hex dumps and `^done,register-values=[` register tables.
 * - `^done,bkpt={` breakpoint acknowledgements and initial `"sync_flushing"` barrier.
 * - Token `100` (hover), `101` (expression), and `200` (disassembly) replies.
 *
 * @param rankId Zero-based MPI rank index whose stdout stream produced @p output.
 * @param output Raw UTF-8 chunk read from the GDB child process stdout pipe.
 */
void GdbRankCoordinator::processGdbOutput(int rankId, const QString& output) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) {
    return;
  }
  auto& rp = m_processes[rankId];

  // Append incoming bytes to the rank's persistent line reassembly buffer.
  rp->buffer += output;

  int newlineIdx = -1;
  while ((newlineIdx = rp->buffer.indexOf('\n')) != -1) {
    // Extract one complete newline-delimited GDB/MI record and notify raw listeners.
    const QString line = rp->buffer.left(newlineIdx).trimmed();
    rp->buffer.remove(0, newlineIdx + 1);
    emit gdbOutputReceived(rankId, line);

    qDebug() << "[GDB OUT Rank" << rankId << "]:" << line;

    if (line.startsWith("*stopped")) {
      // Immediately catch and cleanly sink process exit notifications so we do not
      // issue `-data-disassemble` or `-thread-select` against a dead target.
      if (line.contains("reason=\"exited") ||
          line.contains("reason=\"exited-normally\"")) {
        rp->state.currentState = "exited";
        emit rankStateChanged(rankId, rp->state);
        continue;
      }

      // Accumulate elapsed wall-clock execution time for rank load-imbalance telemetry.
      rp->state.currentState = "stopped";
      if (rp->state.executionTimer.isValid()) {
        rp->state.totalRuntimeMs += rp->state.executionTimer.elapsed();
        rp->state.executionTimer.invalidate();
      }

      // Parse stopped source file path and 1-based line number from the MI frame tuple.
      static const QRegularExpression rxFile("fullname=\"([^\"]+)\"");
      static const QRegularExpression rxLine("frame=\\{.*?line=\"(\\d+)\"");
      QString file = rp->state.currentFile;
      int lineNum = rp->state.currentLine;

      const auto matchFile = rxFile.match(line);
      if (matchFile.hasMatch()) {
        file = matchFile.captured(1);
      }

      const auto matchLine = rxLine.match(line);
      if (matchLine.hasMatch()) {
        lineNum = matchLine.captured(1).toInt();
      } else if (lineNum <= 0) {
        lineNum = -1;
      }

      rp->state.currentFile = file;
      rp->state.currentLine = lineNum;

      // Request a 40-byte disassembly window starting at the stopped frame address.
      // Token `200` tags the request so the response handler can chain `-var-update`.
      static const QRegularExpression rxAddr("frame=\\{[^}]*addr=\"([^\"]+)\"");
      const auto matchAddr = rxAddr.match(line);
      const QString stopAddress =
          matchAddr.hasMatch() ? matchAddr.captured(1) : "$pc";

      const QString asmCmd =
          QString("200-data-disassemble -s %1 -e \"%1 + 40\" -- 0\n")
              .arg(stopAddress);
      writeCmd(rankId, asmCmd);

      // Refresh general-purpose register values on every stop for the Register Matrix view.
      writeCmd(rankId, "-data-list-register-values r\n");

      // Select the stopped thread explicitly so subsequent variable queries evaluate in-context.
      static const QRegularExpression threadRe("thread-id=\"(\\d+)\"");
      const auto matchThread = threadRe.match(line);
      if (matchThread.hasMatch()) {
        const QString threadId = matchThread.captured(1);
        const QString threadCmd = QString("-thread-select %1\n").arg(threadId);
        writeCmd(rankId, threadCmd);
      } else {
        writeCmd(rankId, "-thread-info\n");
      }

      // Detect whether this is the initial `gdbserver` attach stop requiring breakpoint flush.
      // On first stop (`lastFiredTimestamp` empty), flush cached breakpoints before continuing.
      bool isInternalSync = !rp->state.breakpoints.contains(-1);
      if (rp->state.lastFiredTimestamp.isEmpty()) {
        rp->state.lastFiredTimestamp = "synced";
        isInternalSync = true;
      } else {
        isInternalSync = false;
      }

      if (isInternalSync) {
        flushCachedBreakpoints(rankId);
      } else {
        // If stopped in an unmapped runtime trampoline (`??`), auto-continue into user code.
        if (rp->state.currentFile.isEmpty() || rp->state.currentFile == "??") {
          writeCmd(rankId, "-exec-continue\n");
          rp->state.currentState = "running";
        } else {
          // Ensure all registered watch variables have MI variable objects on this rank.
          // Token `30<varIdx>` correlates the async `-var-create` reply back to `m_watchVariables`.
          for (int varIdx = 0;
               varIdx < static_cast<int>(m_watchVariables.size()); ++varIdx) {
            const QString varName = m_watchVariables[varIdx];
            if (!m_varNameMap[rankId].contains(varName)) {
              const QString evalCmd =
                  QString("30%1-var-create - * %2\n").arg(varIdx).arg(varName);
              writeCmd(rankId, evalCmd);
            }
          }
          emit rankStateChanged(rankId, rp->state);
        }
      }
    } else if (line.startsWith("*running")) {
      // Transition rank state to running and restart the high-resolution runtime stopwatch.
      rp->state.currentState = "running";
      rp->state.executionTimer.start();
      emit rankStateChanged(rankId, rp->state);
    } else if (line.startsWith("30")) {
      // Handle `30<varIdx>-var-create` replies for watch variable registration.
      if (line.contains("^done")) {
        // Record bidirectional mapping between user variable name and GDB internal `varId`.
        static const QRegularExpression evalRe(
            "30(\\d+)\\^done,name=\"([^\"]+)\".*?value=\"([^\"]+)\"");
        const auto match = evalRe.match(line);
        if (match.hasMatch()) {
          const int varIdx = match.captured(1).toInt();
          if (varIdx >= 0 &&
              varIdx < static_cast<int>(m_watchVariables.size())) {
            const QString varName = m_watchVariables[varIdx];
            const QString varId = match.captured(2);
            const QString value = match.captured(3);
            m_varNameMap[rankId][varName] = varId;
            m_varNameRevMap[rankId][varId] = varName;
            rp->state.variableWatches[varName] = value;
            emit rankStateChanged(rankId, rp->state);
          }
        }
      } else if (line.contains("^error")) {
        // Variable is not visible in the current stack frame; mark `<Out of Scope>`.
        static const QRegularExpression errRe("30(\\d+)\\^error");
        const auto match = errRe.match(line);
        if (match.hasMatch()) {
          const int varIdx = match.captured(1).toInt();
          if (varIdx >= 0 &&
              varIdx < static_cast<int>(m_watchVariables.size())) {
            const QString varName = m_watchVariables[varIdx];
            rp->state.variableWatches[varName] = "<Out of Scope>";
            m_varNameMap[rankId].remove(varName);
            emit rankStateChanged(rankId, rp->state);
          }
        }
      }
    } else if (line.contains("^done,changelist=")) {
      // Parse `-var-update 1 *` changelist tuples and evict out-of-scope variable objects.
      static const QRegularExpression objRe("\\{([^}]+)\\}");
      auto i = objRe.globalMatch(line);
      bool updated = false;
      while (i.hasNext()) {
        const QString objStr = i.next().captured(1);
        static const QRegularExpression nameRe("name=\"([^\"]+)\"");
        static const QRegularExpression valRe("value=\"([^\"]+)\"");
        static const QRegularExpression inScopeRe("in_scope=\"([^\"]+)\"");

        const auto nameMatch = nameRe.match(objStr);
        if (nameMatch.hasMatch()) {
          const QString varId = nameMatch.captured(1);
          if (m_varNameRevMap.contains(rankId) &&
              m_varNameRevMap[rankId].contains(varId)) {
            const QString varName = m_varNameRevMap[rankId][varId];
            const auto scopeMatch = inScopeRe.match(objStr);
            const bool inScope =
                !scopeMatch.hasMatch() || scopeMatch.captured(1) == "true";

            if (inScope) {
              // Update cached watch value in `RankState` when `in_scope="true"`.
              const auto valMatch = valRe.match(objStr);
              if (valMatch.hasMatch()) {
                rp->state.variableWatches[varName] = valMatch.captured(1);
                updated = true;
              }
            } else {
              // Delete the stale GDB variable object so it can be recreated on future stops.
              rp->state.variableWatches[varName] = "Out of scope";
              writeCmd(rankId, QString("-var-delete %1\n").arg(varId));
              m_varNameMap[rankId].remove(varName);
              m_varNameRevMap[rankId].remove(varId);
              updated = true;
            }
          }
        }
      }
      if (updated) {
        emit rankStateChanged(rankId, rp->state);
      }
    } else if (line.startsWith("^done,memory=[")) {
      // Extract base address and hex byte string from `-data-read-memory-bytes`.
      static const QRegularExpression memRe(
          "begin=\"([^\"]+)\".*?contents=\"([^\"]+)\"");
      const auto match = memRe.match(line);
      if (match.hasMatch()) {
        bool ok = false;
        const qint64 beginAddress = match.captured(1).toULongLong(&ok, 16);
        const QString contents = match.captured(2);
        emit memoryDataReady(rankId, beginAddress, contents);
      }
    } else if (line.startsWith("^done,register-values=[")) {
      // Populate `rp->state.registers` from `-data-list-register-values r`.
      // Each entry pairs a GDB register index (`number`) with its formatted value string.
      static const QRegularExpression regRe(
          "number=\"(\\d+)\".*?value=\"([^\"]+)\"");
      auto i = regRe.globalMatch(line);
      bool updated = false;
      while (i.hasNext()) {
        const auto match = i.next();
        const int regNum = match.captured(1).toInt();
        const QString regVal = match.captured(2);
        rp->state.registers[regNum] = regVal;
        updated = true;
      }
      if (updated) {
        emit rankStateChanged(rankId, rp->state);
      }
    } else if (line.startsWith("^done,bkpt={")) {
      // Record newly inserted breakpoint ID and advance `"sync_flushing"` barrier if active.
      // Once `pendingBreakpoints` reaches zero, automatically resume target execution.
      static const QRegularExpression bkptRe(
          "number=\"(\\d+)\".*?file=\"([^\"]+)\".*?line=\"(\\d+)\"");
      const auto match = bkptRe.match(line);
      if (match.hasMatch()) {
        const int bkptId = match.captured(1).toInt();
        const QString file = match.captured(2);
        const QString lineNum = match.captured(3);
        rp->state.breakpoints[bkptId] = QString("%1:%2").arg(file).arg(lineNum);

        if (rp->state.currentState == "sync_flushing") {
          rp->state.pendingBreakpoints--;
          if (rp->state.pendingBreakpoints <= 0) {
            writeCmd(rankId, "-exec-continue\n");
            rp->state.currentState = "running";
          }
        } else {
          emit rankStateChanged(rankId, rp->state);
        }
      }
    } else if (line.startsWith("100")) {
      // Token 100: Editor hover variable evaluation response (`-data-evaluate-expression`).
      if (line.contains("^done")) {
        static const QRegularExpression valRe("100\\^done,value=\"([^\"]+)\"");
        const auto match = valRe.match(line);
        if (match.hasMatch()) {
          emit hoverEvaluationComplete(rp->lastHoverVarName, match.captured(1),
                                       rp->lastHoverPos);
        }
      } else if (line.contains("^error")) {
        static const QRegularExpression errRe("msg=\"([^\"]+)\"");
        const auto match = errRe.match(line);
        if (match.hasMatch()) {
          emit hoverEvaluationComplete(rp->lastHoverVarName, match.captured(1),
                                       rp->lastHoverPos);
        }
      }
    } else if (line.startsWith("101")) {
      // Token 101: General watch/console expression evaluation response.
      if (line.contains("^done")) {
        static const QRegularExpression valRe("101\\^done,value=\"([^\"]+)\"");
        const auto match = valRe.match(line);
        if (match.hasMatch()) {
          emit expressionEvaluated(rankId, rp->lastEvalExpression,
                                   match.captured(1));
        }
      } else if (line.contains("^error")) {
        static const QRegularExpression errRe("msg=\"([^\"]+)\"");
        const auto match = errRe.match(line);
        if (match.hasMatch()) {
          emit expressionEvaluated(rankId, rp->lastEvalExpression,
                                   "Error: " + match.captured(1));
        }
      }
    } else if (line.startsWith("200")) {
      // Token 200: Disassembly window response (`-data-disassemble`).
      // Formats `address` and `inst` pairs into newline-separated assembly lines.
      if (line.contains("asm_insns=")) {
        QString prettyAsm;
        static const QRegularExpression instRe(
            "address=\"([^\"]+)\".*?inst=\"([^\"]+)\"");
        auto i = instRe.globalMatch(line);
        while (i.hasNext()) {
          const auto match = i.next();
          prettyAsm += QString("%1: %2\n")
                           .arg(match.captured(1))
                           .arg(match.captured(2));
        }
        if (!prettyAsm.isEmpty()) {
          rp->state.disassemblyText = prettyAsm;
          emit rankStateChanged(rankId, rp->state);
        }
      } else if (line.contains("^error")) {
        // If `gdbserver` is still initializing its socket, schedule a 500ms reconnect retry.
        static const QRegularExpression errorRe("msg=\"([^\"]+)\"");
        const auto match = errorRe.match(line);
        const QString errMsg =
            match.hasMatch() ? match.captured(1) : "Unknown Error";

        if (errMsg.contains("Connection refused") ||
            errMsg.contains("Connection timed out")) {
          QTimer::singleShot(500, this, [this, rankId]() {
            const QString connectCmd =
                QString("-target-select remote localhost:%1\n")
                    .arg(2000 + rankId);
            writeCmd(rankId, connectCmd);
          });
          return;
        }

        rp->state.disassemblyText = QString("; GDB Error: %1").arg(errMsg);
        emit rankStateChanged(rankId, rp->state);
      }

      // Chain variable update evaluation after disassembly finishes.
      writeCmd(rankId, "-var-update 1 *\n");
    } else if (line.contains("^error") && !line.startsWith("30") &&
               !line.startsWith("40") && !line.startsWith("200")) {
      // Untokenized MI error: retry TCP connection if `gdbserver` was not yet listening.
      static const QRegularExpression errorRe("msg=\"([^\"]+)\"");
      const auto match = errorRe.match(line);
      const QString errMsg =
          match.hasMatch() ? match.captured(1) : "Unknown Error";

      if (errMsg.contains("Connection refused") ||
          errMsg.contains("Connection timed out")) {
        QTimer::singleShot(500, this, [this, rankId]() {
          const QString connectCmd =
              QString("-target-select remote localhost:%1\n")
                  .arg(2000 + rankId);
          writeCmd(rankId, connectCmd);
        });
        return;
      }

      rp->state.disassemblyText = QString("; GDB Error: %1").arg(errMsg);
      emit rankStateChanged(rankId, rp->state);
    } else if (line.contains("asm_insns=") && !line.startsWith("200")) {
      // Untokenized `-data-disassemble` output fallback for manual console requests.
      QString prettyAsm;
      static const QRegularExpression instRe(
          "address=\"([^\"]+)\".*?inst=\"([^\"]+)\"");
      auto i = instRe.globalMatch(line);
      while (i.hasNext()) {
        const auto match = i.next();
        prettyAsm +=
            QString("%1: %2\n").arg(match.captured(1)).arg(match.captured(2));
      }
      if (!prettyAsm.isEmpty()) {
        rp->state.disassemblyText = prettyAsm;
        emit rankStateChanged(rankId, rp->state);
      }
    }
  }
}

}  // namespace gridlock
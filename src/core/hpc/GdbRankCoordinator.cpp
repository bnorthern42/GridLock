/**
 * @file GdbRankCoordinator.cpp
 * @brief Implementation of multi-rank GDB/MI3 process orchestration and MI output parsing.
 *
 * Coordinates multi-rank MPI debugging by launching per-rank `gdb --interpreter=mi3`
 * instances over non-blocking POSIX named pipes (`mkfifo`) under `mpirun`/`mpiexec`
 * (or standalone `gdb` when `rankCount == 1` without MPI), synchronizing breakpoints,
 * watch variable objects (`-var-create` / `-var-update`), stack-frame locals, live CPU
 * registers, and machine-instruction disassembly.
 */

#include "GdbRankCoordinator.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#include "../managers/ConfigManager.hpp"

namespace gridlock {

namespace {

/**
 * @brief Escapes backslashes, quotes, and control characters for GDB/MI string literals.
 * @param str Unescaped command or expression string.
 * @return Double-quoted GDB/MI3 c-string literal.
 */
QString escapeGdbMiString(const QString& str) {
  QString res;
  for (int i = 0; i < str.length(); ++i) {
    const QChar c = str[i];
    if (c == '\\') res += "\\\\";
    else if (c == '"') res += "\\\"";
    else if (c == '\n') res += "\\n";
    else if (c == '\r') res += "\\r";
    else if (c == '\t') res += "\\t";
    else res += c;
  }
  return "\"" + res + "\"";
}
/**
 * @brief Maps standard x86_64 GDB register numbers (0..17) to canonical lowercase mnemonics.
 * @param idx Zero-based GDB register index (`0` = `rax`, `7` = `rsp`, `16` = `rip`).
 * @return Lowercase mnemonic string, or empty QString when @p idx is outside `0..17`.
 */
QString x86RegName(int idx) {
  static const char* const kNames[] = {"rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", "r8",
                                       "r9",  "r10", "r11", "r12", "r13", "r14", "r15", "rip", "eflags"};
  return (idx >= 0 && idx < 18) ? QLatin1String(kNames[idx]) : QString();
}

/**
 * @brief Formats `address="..."` and `inst="..."` tuples from `-data-disassemble` MI output.
 */
QString formatMiDisassembly(const QString& miLine) {
  QString prettyAsm;
  static const QRegularExpression instRe("address=\"([^\"]+)\".*?inst=\"([^\"]+)\"");
  auto it = instRe.globalMatch(miLine);
  while (it.hasNext()) {
    const auto m = it.next();
    prettyAsm += QString("%1: %2\n").arg(m.captured(1), m.captured(2));
  }
  return prettyAsm;
}

/**
 * @brief Builds a GDB/MI `-break-insert` command string for `file:line` and optional @p condition.
 */
QString buildBreakInsertCmd(const QString& file, int line, const QString& condition, int ignoreCount = 0) {
  const QString loc = QString("%1:%2").arg(file).arg(line);
  const QString ign = ignoreCount > 0 ? QString("-i %1 ").arg(ignoreCount) : QString();
  if (!condition.trimmed().isEmpty()) return QString("-break-insert %1-c %2 %3\n").arg(ign, escapeGdbMiString(condition.trimmed()), loc);
  return QString("-break-insert -f %1%2\n").arg(ign, loc);
}

}  // namespace

/** @brief Constructs `GdbRankCoordinator` and forwards @p parent to `IBackendCoordinator`. */
GdbRankCoordinator::GdbRankCoordinator(QObject* parent) : IBackendCoordinator(parent) {}

/** @brief Ensures all GDB, FIFO, and `mpiexec` resources are cleaned up on destruction without emitting teardown signals. */
GdbRankCoordinator::~GdbRankCoordinator() { QSignalBlocker b(this); terminateAllSessions(); }

/** @brief Implements `IBackendCoordinator::terminateSession()` by tearing down all rank processes. */
void GdbRankCoordinator::terminateSession() { terminateAllSessions(); }

/** @brief Returns the live inferior OS PID for @p rank if known, or `0` otherwise. */
qint64 GdbRankCoordinator::getPidForRank(int rank) const {
  if (m_rankPids.contains(rank) && m_rankPids.value(rank) > 0) return m_rankPids.value(rank);
  if (rank >= 0 && rank < static_cast<int>(m_processes.size()) && m_processes[rank] && m_processes[rank]->inferiorPid > 0) {
    return static_cast<qint64>(m_processes[rank]->inferiorPid);
  }
  return 0;
}

/**
 * @brief Attaches a live `gdb --interpreter=mi3 -p <pid>` session to a running local OS process.
 * @param pid Target OS process identifier from `/proc`.
 * @return `true` if GDB started and initiated attachment to @p pid.
 */
bool GdbRankCoordinator::attachToProcess(qint64 pid) {
  if (pid <= 0 || !QFileInfo::exists(QStringLiteral("/proc/%1/status").arg(pid))) return false;
  terminateAllSessions();

  // Resolve `gdb` executable from user settings or standard system PATH locations.
  QString gdbPath = core::ConfigManager::instance().getGdbPath().trimmed();
  if (gdbPath.isEmpty() || !QFileInfo::exists(gdbPath)) {
    gdbPath = QStandardPaths::findExecutable(gdbPath.isEmpty() ? QStringLiteral("gdb") : gdbPath);
  }
  if (gdbPath.isEmpty()) return false;

  // Inspect `/proc/<pid>/exe` symlink so static ELF symbols and breakpoints bind to the binary.
  m_currentBinaryPath = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();

  auto rp = std::make_unique<RankProcess>();
  rp->id = 0;
  rp->process = std::make_unique<QProcess>();
  rp->inferiorPid = static_cast<pid_t>(pid);
  rp->state.rankId = 0;
  rp->state.currentState = "launching";
  rp->state.lastFiredTimestamp = "synced";
  m_rankPids[0] = pid;
  connect(rp->process.get(), &QProcess::readyReadStandardOutput, this, [this]() { handleGdbOutput(0); });
  // Start MI3 frontend directly attached to target PID with debuginfod disabled.
  rp->process->start(gdbPath, {QStringLiteral("--quiet"), QStringLiteral("--nx"), QStringLiteral("-iex"),
                               QStringLiteral("set debuginfod enabled off"), QStringLiteral("--interpreter=mi3"),
                               QStringLiteral("-p"), QString::number(pid)});
  const bool started = rp->process->waitForStarted(2000);
  m_processes.push_back(std::move(rp));
  if (!started) { terminateAllSessions(); return false; }

  // Disable interactive pagination/prompts and query thread groups to confirm inferior state.
  writeCmd(0, "-gdb-set pagination off\n");
  writeCmd(0, "-gdb-set confirm off\n");
  writeCmd(0, "-list-thread-groups\n");
  emit rankStateChanged(0, m_processes[0]->state);
  return true;
}

/**
 * @brief Registers a breakpoint in `m_pendingBreakpoints` and pushes it to live rank slots.
 */
void GdbRankCoordinator::queueOrWriteCmd(int rankId, const QString& cmd) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size()) || !m_processes[rankId]) return;
  auto& rp = m_processes[rankId];
  if (rp->state.currentState == "running") {
    rp->pendingAfterStopCmds.append(cmd);
    // Interrupt once per batch so all queued commands flush together before auto-continuing.
    if (!rp->autoContinueAfterPending) {
      rp->autoContinueAfterPending = true; ++rp->pendingInterruptSignals;
      // Prefer direct SIGINT to the inferior PID so GDB stops promptly inside blocking MPI syscalls.
      if (rp->inferiorPid > 0) ::kill(rp->inferiorPid, SIGINT); else writeCmd(rankId, "-exec-interrupt\n");
    }
    return;
  }
  writeCmd(rankId, cmd);
}

/**
 * @brief Registers a breakpoint in `m_pendingBreakpoints` and pushes it to live rank slots.
 */
void GdbRankCoordinator::setBreakpoint(const QString& file, int line, const QString& condition, int rankFilter) {
  if (file.isEmpty() || line <= 0) return;
  m_breakpointsCleared = false;
  const auto extra = m_breakpointExtras.value(QStringLiteral("%1:%2").arg(file).arg(line));
  m_pendingBreakpoints.erase(std::remove_if(m_pendingBreakpoints.begin(), m_pendingBreakpoints.end(), [&](const PendingBreakpoint& bp) { return bp.file == file && bp.line == line; }), m_pendingBreakpoints.end());
  m_pendingBreakpoints.push_back({file, line, condition.trimmed(), rankFilter, extra.ignoreCount, extra.logMessage});
  const QString locFull = QString("%1:%2").arg(file).arg(line), locBase = QString("%1:%2").arg(QFileInfo(file).fileName()).arg(line), cmd = buildBreakInsertCmd(file, line, condition, extra.ignoreCount);
  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto& rp = m_processes[i];
    if (rankFilter >= 0 && rankFilter != static_cast<int>(i)) {
      if (rp) { QList<int> rem; for (auto it = rp->state.breakpoints.constBegin(); it != rp->state.breakpoints.constEnd(); ++it) if (it.value() == locFull || it.value() == locBase) rem.append(it.key());
        for (int id : rem) { queueOrWriteCmd(static_cast<int>(i), QString("-break-delete %1\n").arg(id)); rp->state.breakpoints.remove(id); } }
      continue;
    }
    queueOrWriteCmd(static_cast<int>(i), cmd);
  }
}
void GdbRankCoordinator::configureBreakpointOptions(const QString& file, int line, int ignoreCount, const QString& logMessage) {
  IBackendCoordinator::configureBreakpointOptions(file, line, ignoreCount, logMessage);
  const int cleanIgn = qMax(0, ignoreCount); const QString cleanLog = logMessage.trimmed();
  for (auto& bp : m_pendingBreakpoints) if (bp.file == file && bp.line == line) { bp.ignoreCount = cleanIgn; bp.logMessage = cleanLog; }
  const QString locFull = QString("%1:%2").arg(file).arg(line), locBase = QString("%1:%2").arg(QFileInfo(file).fileName()).arg(line);
  for (size_t i = 0; i < m_processes.size(); ++i) if (m_processes[i]) for (auto it = m_processes[i]->state.breakpoints.constBegin(); it != m_processes[i]->state.breakpoints.constEnd(); ++it)
    if (it.value() == locFull || it.value() == locBase) queueOrWriteCmd(static_cast<int>(i), QString("-break-after %1 %2\n").arg(it.key()).arg(cleanIgn));
}

/** @brief Clears all pending breakpoints and deletes active GDB breakpoints across all ranks. */
void GdbRankCoordinator::clearBreakpoints() {
  m_pendingBreakpoints.clear();
  m_breakpointsCleared = true;
  for (size_t i = 0; i < m_processes.size(); ++i) {
    if (m_processes[i]) { m_processes[i]->state.breakpoints.clear(); queueOrWriteCmd(static_cast<int>(i), "-break-delete\n"); }
  }
}

/**
 * @brief Writes @p cmd to @p rankId's FIFO or `QProcess` stdin and emits `commandSentToGdb`.
 */
void GdbRankCoordinator::writeCmd(int rankId, const QString& cmd) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  if (!rp) return;
  // Never emit frame, thread, register, data, or varobj MI queries while target rank is not stopped.
  static const QRegularExpression kInspectMiRe(QStringLiteral("^\\d*-(?:stack|thread|var|data)-"));
  if (rp->state.currentState != QLatin1String("stopped") && kInspectMiRe.match(cmd.trimmed()).hasMatch()) return;

  // Prefer the non-blocking POSIX FIFO bridge when running under `mpirun` MPMD mode.
  if (rp->fifoInFd >= 0) {
    const QByteArray bytes = cmd.toUtf8();
    if (::write(rp->fifoInFd, bytes.constData(), static_cast<size_t>(bytes.size())) > 0) emit commandSentToGdb(rankId, cmd.trimmed());
    return;
  }

  // Fallback path for standalone `QProcess` GDB instances.
  if (rp->process) {
    if (rp->process->state() == QProcess::Starting) rp->process->waitForStarted(1000);
    if (rp->process->state() == QProcess::Running && rp->process->isOpen() && rp->process->isWritable()) {
      rp->process->write(cmd.toUtf8());
      emit commandSentToGdb(rankId, cmd.trimmed());
    }
  }
}

/**
 * @brief Pushes persisted workspace breakpoints to @p rankId upon initial attach stop.
 */
void GdbRankCoordinator::flushCachedBreakpoints(int rankId) {
  const auto bps = core::ConfigManager::instance().getBreakpoints(m_currentBinaryPath);
  auto& rp = m_processes[rankId];
  int count = 0;
  for (auto it = bps.constBegin(); it != bps.constEnd(); ++it) {
    for (int line : *it) { writeCmd(rankId, QString("-break-insert -f %1:%2\n").arg(it.key()).arg(line)); count++; }
  }
  // Resume immediately when zero breakpoints exist; otherwise wait for bkpt acknowledgements.
  if (count == 0) {
    writeCmd(rankId, "-exec-continue\n");
    rp->state.currentState = "running";
  } else {
    rp->state.pendingBreakpoints = count;
    rp->state.currentState = "sync_flushing";
  }
}

/**
 * @brief Registers @p name in `m_watchVariables` and creates a GDB/MI variable object on stopped ranks.
 */
void GdbRankCoordinator::registerWatchVariable(const QString& name) {
  if (std::find(m_watchVariables.begin(), m_watchVariables.end(), name) == m_watchVariables.end()) m_watchVariables.push_back(name);
  const int varIdx = static_cast<int>(std::distance(
      m_watchVariables.begin(), std::find(m_watchVariables.begin(), m_watchVariables.end(), name)));

  // Issue `30<varIdx>-var-create - * <name>` only to actively stopped GDB rank slots.
  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto& rp = m_processes[i];
    const bool stoppedActive = rp && rp->state.currentState == QLatin1String("stopped") &&
                               ((rp->fifoInFd >= 0) || (rp->process && rp->process->state() == QProcess::Running));
    if (stoppedActive && !m_varNameMap[static_cast<int>(i)].contains(name)) {
      writeCmd(static_cast<int>(i), QString("30%1-var-create - * %2\n").arg(varIdx).arg(name));
    }
  }
}

/**
 * @brief Cleanly stops all per-rank GDB sessions, closes FIFOs, kills `mpirun`, and removes temp files.
 */
void GdbRankCoordinator::terminateAllSessions() {
  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto& rp = m_processes[i];
    if (!rp) continue;
    // Disable and destroy FIFO socket notifier before closing POSIX file descriptors.
    if (rp->fifoNotifier) {
      rp->fifoNotifier->setEnabled(false);
      delete rp->fifoNotifier;
      rp->fifoNotifier = nullptr;
    }
    // Issue graceful `-gdb-exit` over the input FIFO and close both pipe endpoints.
    if (rp->fifoInFd >= 0) {
      [[maybe_unused]] const ssize_t n = ::write(rp->fifoInFd, "-gdb-exit\n", 10);
      ::close(rp->fifoInFd);
      rp->fifoInFd = -1;
    }
    if (rp->fifoOutFd >= 0) { ::close(rp->fifoOutFd); rp->fifoOutFd = -1; }
    // Reap spawned FIFO child rank processes; detach cleanly from externally attached PIDs.
    if (rp->fifoInFd >= 0 && rp->inferiorPid > 0) { ::kill(rp->inferiorPid, SIGKILL); rp->inferiorPid = 0; }
    // Tear down standalone `QProcess` GDB frontend without writing to closed pipes.
    if (rp->process) {
      rp->process->disconnect();
      if (rp->process->state() == QProcess::Running && rp->process->isOpen() && rp->process->isWritable()) {
        rp->process->write("-target-detach\n-gdb-exit\n");
      }
      if (rp->process->state() != QProcess::NotRunning && !rp->process->waitForFinished(300)) {
        rp->process->kill();
        rp->process->waitForFinished(300);
      }
    }
  }
  // Clear rank process slots, OS PID map, stack/thread caches, and GDB variable-object lookup tables.
  const bool hadActiveSession = !m_processes.empty() || !m_rankFrames.isEmpty() || !m_rankThreads.isEmpty();
  m_processes.clear(); m_rankPids.clear(); m_varNameMap.clear(); m_varNameRevMap.clear();
  m_rankFrames.clear(); m_rankThreads.clear(); m_activeThreadIds.clear(); m_pendingFrameSelect.clear();
  m_pendingConsoleCmdCount.clear(); m_consoleReplyBuffer.clear(); m_watchpointIds.clear();
  if (hadActiveSession) { emit stackTraceUpdated(0, {}); emit threadsUpdated(0, {}); }

  // Terminate and reap the master `mpirun` / `mpiexec` launcher process cleanly.
  if (m_mpirunProcess) {
    m_mpirunProcess->disconnect();
    if (m_mpirunProcess->state() != QProcess::NotRunning) {
      m_mpirunProcess->terminate();
      if (!m_mpirunProcess->waitForFinished(300)) {
        m_mpirunProcess->kill();
        m_mpirunProcess->waitForFinished(500);
      }
    }
    m_mpirunProcess.reset();
  }
  // Remove the per-session FIFO directory from `/tmp`.
  if (!m_fifoTempDir.isEmpty()) { QDir(m_fifoTempDir).removeRecursively(); m_fifoTempDir.clear(); }
  m_currentBinaryPath.clear();
}

/** @brief Convenience entry point forwarding to `launchParallelSession()`. */
void GdbRankCoordinator::startDebugSession(int rankCount, const QString& executable) {
  launchParallelSession(executable, rankCount);
}

/** @brief Spawns `mpirun`/`mpiexec` using configured MPI arguments from `ConfigManager`. */
void GdbRankCoordinator::launchParallelSession(const QString& executable, int explicitRankCount) {
  launchParallelSession(executable, explicitRankCount, core::ConfigManager::instance().getMpiArgs());
}

/**
 * @brief Launches `N` parallel GDB/MI3 sessions across MPI ranks via POSIX FIFOs (or direct `gdb`).
 * @param executable Absolute or relative path to the compiled ELF target binary.
 * @param explicitRankCount Requested MPI rank count (`-np`), or `<= 0` for `ConfigManager` default.
 * @param customMpiArgs Additional CLI flags forwarded to `mpirun`/`mpiexec` (e.g. `--oversubscribe`).
 */
void GdbRankCoordinator::launchParallelSession(const QString& executable, int explicitRankCount,
                                               const QString& customMpiArgs) {
  // Tear down any previous session before allocating fresh FIFO pipes and rank slots.
  terminateAllSessions();
  if (executable.trimmed().isEmpty()) return;

  const auto ds = core::ConfigManager::instance().getDebuggerSettings();
  const auto hs = core::ConfigManager::instance().getHpcSettings();
  const int rankCount = std::max(1, (explicitRankCount > 0) ? explicitRankCount : ds.defaultRanks);
  m_currentBinaryPath = executable;

  // Resolve MPI launcher (`mpirun` / `mpiexec`) and `gdb` across system + OpenMPI/MPICH paths.
  const QStringList searchDirs = {QStringLiteral("/usr/lib64/openmpi/bin"), QStringLiteral("/usr/lib/openmpi/bin"),
                                  QStringLiteral("/usr/lib64/mpich/bin"), QStringLiteral("/usr/lib/mpich/bin"),
                                  QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/bin")};

  // Locate `gdb` binary from user configuration or standard system binary directories.
  QString gdbPath = core::ConfigManager::instance().getGdbPath().trimmed();
  if (gdbPath.isEmpty() || !QFileInfo::exists(gdbPath)) {
    gdbPath = QStandardPaths::findExecutable(gdbPath.isEmpty() ? QStringLiteral("gdb") : gdbPath);
    if (gdbPath.isEmpty()) gdbPath = QStandardPaths::findExecutable(QStringLiteral("gdb"), searchDirs);
  }
  if (gdbPath.isEmpty()) { qWarning() << "[GDB] Could not locate gdb executable in PATH."; return; }

  // Locate preferred MPI job launcher (`mpirun` or `mpiexec`) across standard HPC prefixes.
  const QString preferredMpi = ds.mpiExecutable.trimmed().isEmpty() ? QStringLiteral("mpirun") : ds.mpiExecutable.trimmed();
  QString mpiBin = QFileInfo::exists(preferredMpi) ? preferredMpi : QStandardPaths::findExecutable(preferredMpi);
  if (mpiBin.isEmpty()) mpiBin = QStandardPaths::findExecutable(preferredMpi, searchDirs);
  if (mpiBin.isEmpty()) mpiBin = QStandardPaths::findExecutable(QStringLiteral("mpirun"), searchDirs);
  if (mpiBin.isEmpty()) mpiBin = QStandardPaths::findExecutable(QStringLiteral("mpiexec"), searchDirs);

  const auto projCfg = core::ConfigManager::instance().loadProjectSettings();
  QString execArgs = targetArguments().trimmed();
  if (execArgs.isEmpty()) execArgs = !projCfg.targetArguments.isEmpty() ? projCfg.targetArguments.trimmed() : QString::fromStdString(projCfg.programArguments).trimmed();
  QString workDir = workingDirectory().trimmed();
  if (workDir.isEmpty()) workDir = QString::fromStdString(projCfg.workingDirectory).trimmed();
  if (workDir.isEmpty()) workDir = core::ConfigManager::instance().workspacePath().trimmed();
  if (workDir.isEmpty()) workDir = QFileInfo(executable).absolutePath();
  const QStringList splitExecArgs = execArgs.isEmpty() ? QStringList{} : QProcess::splitCommand(execArgs);

  // Build process environment with MPI search paths, user envVars, and OpenMP anti-livelock defaults.
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("PATH"), searchDirs.join(QLatin1Char(':')) + QLatin1Char(':') + env.value(QStringLiteral("PATH")));
  env.insert(QStringLiteral("LD_LIBRARY_PATH"), QStringLiteral("/usr/lib64/openmpi/lib:/usr/lib/openmpi/lib:") + env.value(QStringLiteral("LD_LIBRARY_PATH")));
  for (const QString& pair : hs.envVars.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
    if (const int eq = pair.indexOf(QLatin1Char('=')); eq > 0) env.insert(pair.left(eq).trimmed(), pair.mid(eq + 1).trimmed());
  }
  // Parse custom MPI launcher flags while lifting `-x KEY=VAL` into `env` so MPMD ranks inherit symmetrically.
  QStringList mpiArgs;
  const QStringList rawMpiTokens = QProcess::splitCommand(customMpiArgs);
  for (int ti = 0; ti < rawMpiTokens.size(); ++ti) {
    const QString& tok = rawMpiTokens[ti];
    if ((tok == QStringLiteral("-np") || tok == QStringLiteral("-n")) && ti + 1 < rawMpiTokens.size()) { ++ti; continue; }
    if (tok == QStringLiteral("-x") && ti + 1 < rawMpiTokens.size() && rawMpiTokens[ti + 1].contains(QLatin1Char('='))) {
      const QString kv = rawMpiTokens[++ti]; const int eq = kv.indexOf(QLatin1Char('='));
      if (eq > 0) env.insert(kv.left(eq).trimmed(), kv.mid(eq + 1).trimmed());
      continue;
    }
    mpiArgs << tok;
  }
  // Auto-tune OpenMP thread count (`max(1, idealThreadCount / rankCount)`) and passive barrier wait policy.
  // Prevents `N * idealThreadCount` spinning OpenMP worker threads from starving ptrace and MPI_Barrier.
  if (env.value(QStringLiteral("OMP_NUM_THREADS")).trimmed().isEmpty() && !customMpiArgs.contains(QStringLiteral("OMP_NUM_THREADS")))
    env.insert(QStringLiteral("OMP_NUM_THREADS"), QString::number(qMax(1, QThread::idealThreadCount() / qMax(1, rankCount))));
  if (env.value(QStringLiteral("OMP_WAIT_POLICY")).trimmed().isEmpty() && !customMpiArgs.contains(QStringLiteral("OMP_WAIT_POLICY")))
    env.insert(QStringLiteral("OMP_WAIT_POLICY"), QStringLiteral("PASSIVE"));

  // Record human-readable CWD, OpenMP tuning, and launch command banner for the Program Output dock.
  const QString ompDisp = QStringLiteral("OMP_NUM_THREADS=%1 OMP_WAIT_POLICY=%2 ").arg(env.value(QStringLiteral("OMP_NUM_THREADS")), env.value(QStringLiteral("OMP_WAIT_POLICY")));
  const QString mpiDisp = mpiBin.isEmpty() ? QString() : (QFileInfo(mpiBin).fileName() + (customMpiArgs.trimmed().isEmpty() ? QString() : QStringLiteral(" ") + customMpiArgs.trimmed()) + QStringLiteral(" -np %1 ").arg(rankCount));
  m_lastLaunchCommandSummary = QStringLiteral("[CWD: %1] %2%3%4%5").arg(workDir, ompDisp, mpiDisp, executable, execArgs.isEmpty() ? QString() : QStringLiteral(" ") + execArgs);
  // Helper configuring initial GDB settings, cwd, inferior args, breakpoints, and `-exec-run` on rank `id`.
  const auto bootstrapRankGdb = [this, execArgs, workDir](int id) {
    writeCmd(id, "-gdb-set pagination off\n"); writeCmd(id, "-gdb-set confirm off\n");
    writeCmd(id, "-gdb-set non-stop off\n"); writeCmd(id, "-gdb-set breakpoint pending on\n");
    for (const char* sig : {"SIGPIPE", "SIGUSR1", "SIGUSR2", "SIG32", "SIG33", "SIG34"})
      writeCmd(id, QString("-interpreter-exec console \"handle %1 nostop noprint pass\"\n").arg(sig).toUtf8().constData());
    writeCmd(id, "-gdb-set max-value-size unlimited\n");
    const QString memTrackSo = qEnvironmentVariable("GRIDLOCK_MEMTRACK_SO");
    if (!memTrackSo.isEmpty() && QFileInfo::exists(memTrackSo))
      writeCmd(id, QStringLiteral("-gdb-set environment LD_PRELOAD %1\n").arg(memTrackSo).toUtf8().constData());
    if (!workDir.isEmpty() && QFileInfo(workDir).isDir()) {
      QString escWd = workDir; escWd.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
      writeCmd(id, QStringLiteral("-environment-cd \"%1\"\n").arg(escWd).toUtf8().constData());
    }
    if (!execArgs.isEmpty()) writeCmd(id, QStringLiteral("-exec-arguments %1\n").arg(execArgs).toUtf8().constData());
    if (!m_pendingBreakpoints.empty()) {
      for (const auto& bp : m_pendingBreakpoints)
        if (bp.rankFilter < 0 || bp.rankFilter == id) writeCmd(id, buildBreakInsertCmd(bp.file, bp.line, bp.condition, bp.ignoreCount));
    } else if (!m_breakpointsCleared) {
      const auto persisted = core::ConfigManager::instance().getBreakpoints(m_currentBinaryPath);
      for (auto it = persisted.constBegin(); it != persisted.constEnd(); ++it)
        for (int ln : *it) writeCmd(id, buildBreakInsertCmd(it.key(), ln, QString()));
    }
    writeCmd(id, "-exec-run\n");
    m_processes[id]->state.currentState = "running"; m_processes[id]->state.executionTimer.start();
  };
  // Fallback for single-process debugging when no MPI launcher is installed on the host.
  if (mpiBin.isEmpty()) {
    auto rp = std::make_unique<RankProcess>();
    rp->id = 0; rp->process = std::make_unique<QProcess>(); rp->state.rankId = 0;
    rp->state.currentState = "launching"; rp->state.lastFiredTimestamp = "synced"; rp->process->setProcessEnvironment(env);
    connect(rp->process.get(), &QProcess::readyReadStandardOutput, this, [this]() { handleGdbOutput(0); });
    if (!workDir.isEmpty() && QFileInfo(workDir).isDir()) rp->process->setWorkingDirectory(workDir);
    QStringList gdbArgs = {QStringLiteral("--quiet"), QStringLiteral("--nx"), QStringLiteral("-iex"),
                           QStringLiteral("set debuginfod enabled off"), QStringLiteral("--interpreter=mi3")};
    if (!splitExecArgs.isEmpty()) gdbArgs << QStringLiteral("--args") << executable << splitExecArgs;
    else gdbArgs << executable;
    rp->process->start(gdbPath, gdbArgs); m_processes.push_back(std::move(rp));
    bootstrapRankGdb(0); emit rankStateChanged(0, m_processes[0]->state); return;
  }
  static std::atomic<int> s_sessionSeq{1};
  m_fifoTempDir = QStringLiteral("/tmp/gridlock_mi_%1_%2").arg(QCoreApplication::applicationPid()).arg(s_sessionSeq.fetch_add(1));
  QDir().mkpath(m_fifoTempDir);
  if (!mpiArgs.contains(QStringLiteral("--oversubscribe")) && mpiBin.contains(QStringLiteral("openmpi"))) mpiArgs << QStringLiteral("--oversubscribe");
  if (!hs.hostsFile.isEmpty()) mpiArgs << QStringLiteral("--hostfile") << hs.hostsFile;
  if (hs.strictAffinity) mpiArgs << QStringLiteral("--map-by") << QStringLiteral("node");
  for (int i = 0; i < rankCount; ++i) {
    const QString inFifo = QStringLiteral("%1/r%2.in").arg(m_fifoTempDir).arg(i), outFifo = QStringLiteral("%1/r%2.out").arg(m_fifoTempDir).arg(i);
    ::mkfifo(inFifo.toUtf8().constData(), 0600); ::mkfifo(outFifo.toUtf8().constData(), 0600);
    const int inFd = ::open(inFifo.toUtf8().constData(), O_RDWR | O_NONBLOCK), outFd = ::open(outFifo.toUtf8().constData(), O_RDWR | O_NONBLOCK);
    auto rp = std::make_unique<RankProcess>();
    rp->id = i; rp->fifoInFd = inFd; rp->fifoOutFd = outFd;
    rp->state.rankId = i; rp->state.currentState = "launching"; rp->state.lastFiredTimestamp = "synced";
    if (outFd >= 0) {
      rp->fifoNotifier = new QSocketNotifier(outFd, QSocketNotifier::Read, this);
      connect(rp->fifoNotifier, &QSocketNotifier::activated, this, [this, id = i]() { handleGdbOutput(id); });
    }
    m_processes.push_back(std::move(rp));
    if (i > 0) mpiArgs << QStringLiteral(":");
    mpiArgs << QStringLiteral("-np") << QStringLiteral("1") << QStringLiteral("/bin/sh") << QStringLiteral("-c")
            << QStringLiteral("in=\"$1\"; out=\"$2\"; shift 2; exec \"$@\" <\"$in\" >\"$out\" 2>&1")
            << QStringLiteral("sh") << inFifo << outFifo << gdbPath << QStringLiteral("--quiet") << QStringLiteral("--nx")
            << QStringLiteral("-iex") << QStringLiteral("set debuginfod enabled off") << QStringLiteral("--interpreter=mi3");
    if (!splitExecArgs.isEmpty()) mpiArgs << QStringLiteral("--args") << executable << splitExecArgs;
    else mpiArgs << executable;
  }
  // Configure OpenMPI/MPICH runtime environment and start the MPMD `mpirun` master process.
  m_mpirunProcess = std::make_unique<QProcess>();
  if (!workDir.isEmpty() && QFileInfo(workDir).isDir()) m_mpirunProcess->setWorkingDirectory(workDir);
  m_mpirunProcess->setProcessEnvironment(env);
  connect(m_mpirunProcess.get(), &QProcess::readyReadStandardOutput, this, [this]() { emit targetOutputReceived("stdout", QString::fromUtf8(m_mpirunProcess->readAllStandardOutput())); });
  connect(m_mpirunProcess.get(), &QProcess::readyReadStandardError, this, [this]() { emit targetOutputReceived("stderr", QString::fromUtf8(m_mpirunProcess->readAllStandardError())); });
  m_mpirunProcess->start(mpiBin, mpiArgs);
  // Queue initial MI setup, user breakpoints, and `-exec-run` into each rank's input FIFO.
  for (int i = 0; i < rankCount; ++i) { bootstrapRankGdb(i); emit rankStateChanged(i, m_processes[i]->state); }
}

/**
 * @brief Populates `m_processes` with in-memory mock rank slots for unit testing without `QProcess`.
 */
void GdbRankCoordinator::initializeMockSession(int rankCount, bool simulateInitialSync) {
  terminateAllSessions(); m_watchVariables.clear();
  for (int i = 0; i < rankCount; ++i) {
    auto rp = std::make_unique<RankProcess>();
    rp->id = i; rp->state.rankId = i; rp->state.currentState = "running"; rp->state.currentLine = 0;
    if (!simulateInitialSync) rp->state.lastFiredTimestamp = "mocked";
    m_processes.push_back(std::move(rp));
  }
}

/**
 * @brief Broadcasts `-break-insert -f <fileName>` to all rank GDB processes.
 */
void GdbRankCoordinator::insertBreakpoint(const QString& location) {
  QString fileName = QFileInfo(location).fileName();
  if (fileName.isEmpty()) fileName = location;
  broadcastCommand(QString("-break-insert -f %1\n").arg(fileName));
}

/**
 * @brief Writes @p cmd to every rank slot in `m_processes`.
 */
void GdbRankCoordinator::broadcastCommand(const QString& cmd) {
  for (size_t i = 0; i < m_processes.size(); ++i) writeCmd(static_cast<int>(i), cmd);
}

/**
 * @brief Sends a command line to @p rankId, or broadcasts to all ranks if `rankId == -1`.
 */
void GdbRankCoordinator::sendCommand(int rankId, const QString& cmd) {
  if (rankId == -1) broadcastCommand(cmd + "\n");
  else writeCmd(rankId, cmd + "\n");
}

/**
 * @brief Synchronizes a source-line breakpoint insertion or deletion across all ranks.
 */
void GdbRankCoordinator::broadcastBreakpoint(const QString& file, int line, bool isSet, const QString& condition) {
  int resolvedRank = -1;
  const QString locStr = QString("%1:%2").arg(file).arg(line);
  if (property("gl_pending_bp_rank_key").toString() == locStr) {
    resolvedRank = property("gl_pending_bp_rank_val").toInt();
    setProperty("gl_pending_bp_rank_key", QString());
  }
  if (isSet) { setBreakpoint(file, line, condition, resolvedRank); return; }
  m_pendingBreakpoints.erase(std::remove_if(m_pendingBreakpoints.begin(), m_pendingBreakpoints.end(),
      [&](const PendingBreakpoint& bp) { return bp.file == file && bp.line == line; }), m_pendingBreakpoints.end());
  const QString locBase = QString("%1:%2").arg(QFileInfo(file).fileName()).arg(line);
  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto& rp = m_processes[i];
    if (!rp) continue;
    int bkptIdToDelete = -1;
    for (auto it = rp->state.breakpoints.constBegin(); it != rp->state.breakpoints.constEnd(); ++it)
      if (it.value() == locStr || it.value() == locBase) { bkptIdToDelete = it.key(); break; }
    if (bkptIdToDelete != -1) {
      queueOrWriteCmd(static_cast<int>(i), QString("-break-delete %1\n").arg(bkptIdToDelete));
      rp->state.breakpoints.remove(bkptIdToDelete);
    }
  }
}

/**
 * @brief Steps stopped ranks forward by one source line (`-exec-next`).
 */
void GdbRankCoordinator::stepAll() {
  for (size_t i = 0; i < m_processes.size(); ++i) {
    const auto& st = m_processes[i]->state.currentState;
    if (st != "running" && st != "exited") writeCmd(static_cast<int>(i), "-exec-next\n");
  }
}

/** @brief Steps into callees (`-exec-step`) on @p threadId (`rankId + 1`). */
void GdbRankCoordinator::stepInto(int threadId) {
  if (m_processes.empty()) { stepAll(); return; }
  const int rankId = std::clamp(threadId - 1, 0, static_cast<int>(m_processes.size()) - 1);
  if (const auto& st = m_processes[rankId]->state.currentState; st != "running" && st != "exited") writeCmd(rankId, "-exec-step\n");
}

/** @brief Steps a single machine instruction (`-exec-step-instruction`) on @p threadId (`rankId + 1`). */
void GdbRankCoordinator::stepInstruction(int threadId) {
  if (m_processes.empty()) return;
  const int rankId = std::clamp(threadId - 1, 0, static_cast<int>(m_processes.size()) - 1);
  if (const auto& st = m_processes[rankId]->state.currentState; st != "running" && st != "exited") writeCmd(rankId, "-exec-step-instruction\n");
}

/** @brief Resumes execution across all paused ranks (`-exec-continue`). */
void GdbRankCoordinator::continueAll() {
  // Resume every stopped rank together so collective MPI barriers never deadlock on a paused peer.
  for (size_t i = 0; i < m_processes.size(); ++i) {
    if (auto& st = m_processes[i]->state; st.currentState != "running" && st.currentState != "exited") {
      // Mark running synchronously before writing MI so immediate breakpoint edits queue cleanly.
      st.currentState = "running"; st.executionTimer.start();
      writeCmd(static_cast<int>(i), "-exec-continue\n");
    }
  }
}

/** @brief Starts execution from the beginning on all ranks (`-exec-run`). */
void GdbRankCoordinator::runAll() { broadcastCommand("-exec-run\n"); }

/** @brief Interrupts execution across all running ranks (`SIGINT` + `-exec-interrupt`). */
void GdbRankCoordinator::haltAll() {
  // Delegate per-rank interruption so both inferiorPid SIGINT and MI interrupt paths run.
  for (size_t i = 0; i < m_processes.size(); ++i) pauseFocusedRank(static_cast<int>(i));
}

/**
 * @brief Interrupts execution on a single rank @p rankId via `SIGINT` and `-exec-interrupt`.
 */
void GdbRankCoordinator::pauseFocusedRank(int rankId) {
  if (rankId >= 0 && rankId < static_cast<int>(m_processes.size()) && m_processes[rankId] &&
      m_processes[rankId]->inferiorPid > 0 && m_processes[rankId]->state.currentState == "running")
    ::kill(m_processes[rankId]->inferiorPid, SIGINT);
  writeCmd(rankId, "-exec-interrupt\n");
}

/**
 * @brief Requests a 30-instruction disassembly window around the current source line or `$pc`.
 */
void GdbRankCoordinator::requestDisassemblyFallback(int rankId) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  if (!rp || rp->state.currentState != "stopped") return;
  writeCmd(rankId, "-thread-info\n");
  if (!rp->state.currentFile.isEmpty() && rp->state.currentLine > 0)
    writeCmd(rankId, QString("200-data-disassemble -f %1 -l %2 -n 30 -- 0\n").arg(rp->state.currentFile).arg(rp->state.currentLine));
  else
    writeCmd(rankId, "200-data-disassemble -s $pc -e \"$pc + 64\" -- 0\n");
}

/**
 * @brief Drains available bytes from @p rankId's FIFO or `QProcess` into `processGdbOutput()`.
 */
void GdbRankCoordinator::handleGdbOutput(int rankId) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  if (!rp) return;
  if (rp->fifoOutFd >= 0) {
    QByteArray acc;
    char buf[8192];
    while (true) {
      const ssize_t n = ::read(rp->fifoOutFd, buf, sizeof(buf));
      if (n <= 0) break;
      acc.append(buf, static_cast<qsizetype>(n));
    }
    if (!acc.isEmpty()) processGdbOutput(rankId, QString::fromUtf8(acc));
    return;
  }
  if (rp->process) processGdbOutput(rankId, QString::fromUtf8(rp->process->readAllStandardOutput()));
}

/**
 * @brief Sends `100-data-evaluate-expression` for a source-editor mouse hover on @p rankId.
 */
void GdbRankCoordinator::evaluateHoverVariable(int rankId, const QString& varName, QPoint globalPos) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  if (!rp || rp->state.currentState != "stopped") return;
  rp->lastHoverVarName = varName;
  rp->lastHoverPos = globalPos;
  writeCmd(rankId, QString("100-data-evaluate-expression %1\n").arg(escapeGdbMiString(varName)));
}

// Maps unique `101xxx` MI tokens to their original expression strings for concurrent evaluations.
static QHash<int, QString>& evalTokenExprMap() { static QHash<int, QString> m; return m; }

/**
 * @brief Sends `101xxx-data-evaluate-expression` for an interactive watch/console expression on @p rankId.
 */
void GdbRankCoordinator::evaluateExpression(int rankId, const QString& expression) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  if (!rp || rp->state.currentState != "stopped") return;
  rp->lastEvalExpression = expression;
  // Use rotating `101xxx` tokens so batched dotted-member ghost evaluations never clobber each other.
  static int s_evalSeq = 0;
  const int tok = 101000 + (++s_evalSeq % 900);
  evalTokenExprMap().insert(tok, expression);
  writeCmd(rankId, QString("%1-data-evaluate-expression %2\n").arg(tok).arg(escapeGdbMiString(expression)));
}

/**
 * @brief Sends `-data-read-memory-bytes` to read @p length bytes at @p address on @p rankId.
 */
void GdbRankCoordinator::readMemory(int rankId, const QString& address, int length) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  if (!rp || rp->state.currentState != "stopped") return;
  writeCmd(rankId, QString("-data-read-memory-bytes %1 %2\n").arg(address).arg(length));
}

/**
 * @brief Incremental GDB/MI3 state machine parser for a single MPI rank.
 * @param rankId Zero-based MPI rank index whose stdout stream produced @p output.
 * @param output Raw UTF-8 chunk read from the rank's non-blocking FIFO or QProcess pipe.
 */
void GdbRankCoordinator::processGdbOutput(int rankId, const QString& output) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  rp->buffer += output;

  // Consume newline-delimited GDB/MI3 async (`*`/`=`) and result (`^`) records incrementally.
  QStringList batchedProgramOut;
  int newlineIdx = -1;
  while ((newlineIdx = rp->buffer.indexOf('\n')) != -1) {
    const QString line = rp->buffer.left(newlineIdx).trimmed();
    rp->buffer.remove(0, newlineIdx + 1);
    if (line.isEmpty()) continue;
    // Broadcast raw MI3 record line to GdbConsoleWidget and protocol test listeners.
    emit gdbOutputReceived(rankId, line);

    // Capture or clear live inferior OS PID on GDB thread-group lifecycle and group-list records.
    if (line.startsWith("=thread-group-started") || line.startsWith("^done,groups=[")) {
      static const QRegularExpression pidRe("pid=\"(\\d+)\"");
      const auto pm = pidRe.match(line);
      if (pm.hasMatch()) { const qint64 pid = pm.captured(1).toLongLong(); rp->inferiorPid = static_cast<pid_t>(pid); m_rankPids[rankId] = pid; emit rankStateChanged(rankId, rp->state); }
      continue;
    }
    if (line.startsWith("=thread-group-exited")) { rp->inferiorPid = 0; m_rankPids.remove(rankId); continue; }

    if (line.startsWith("*stopped")) {
      if (line.contains("reason=\"exited") || line.contains("reason=\"exited-normally\"")) {
        rp->state.currentState = "exited"; rp->inferiorPid = 0; m_rankPids.remove(rankId);
        emit rankStateChanged(rankId, rp->state); continue;
      }
      const bool syntheticSigint = line.contains("signal-name=\"SIGINT\"") && rp->pendingInterruptSignals > 0;
      if (syntheticSigint) --rp->pendingInterruptSignals;
      // Flush breakpoints queued while running and resume immediately unless a real stop fired concurrently.
      if (rp->autoContinueAfterPending || syntheticSigint) {
        rp->autoContinueAfterPending = false;
        const QStringList pending = std::move(rp->pendingAfterStopCmds); rp->pendingAfterStopCmds.clear();
        for (const QString& c : pending) writeCmd(rankId, c);
        static const QRegularExpression rxBkptNo("bkptno=\"(\\d+)\""); const auto mBkpt = rxBkptNo.match(line);
        const bool activeBkptHit = line.contains("breakpoint-hit") && (!mBkpt.hasMatch() || rp->state.breakpoints.contains(mBkpt.captured(1).toInt()));
        if (syntheticSigint || (!activeBkptHit && !line.contains("watchpoint-trigger") && !line.contains("end-stepping-range"))) { writeCmd(rankId, "-exec-continue\n"); rp->state.currentState = "running"; continue; }
      }
      rp->state.currentState = "stopped";
      if (rp->state.executionTimer.isValid()) { rp->state.totalRuntimeMs += rp->state.executionTimer.elapsed(); rp->state.executionTimer.invalidate(); }
      // Extract source file, line number, function symbol, and frame PC from the MI stop record.
      static const QRegularExpression rxFile("fullname=\"([^\"]+)\""), rxLine("frame=\\{.*?line=\"(\\d+)\"");
      static const QRegularExpression rxFunc("frame=\\{[^}]*func=\"([^\"]+)\""), rxAddr("frame=\\{[^}]*addr=\"([^\"]+)\"");
      const auto matchFile = rxFile.match(line), matchLine = rxLine.match(line);
      rp->state.currentFile = matchFile.hasMatch() ? matchFile.captured(1) : rp->state.currentFile;
      rp->state.currentLine = matchLine.hasMatch() ? matchLine.captured(1).toInt() : (rp->state.currentLine > 0 ? rp->state.currentLine : -1);
      // Record active symbol name and seed x86_64 instruction pointer slot 16 ($rip) from frame address.
      if (const auto matchFunc = rxFunc.match(line); matchFunc.hasMatch()) rp->state.currentFunction = matchFunc.captured(1);
      const auto matchAddr = rxAddr.match(line);
      const QString stopAddress = matchAddr.hasMatch() ? matchAddr.captured(1) : QStringLiteral("$pc");
      if (matchAddr.hasMatch()) rp->state.registers[16] = stopAddress;
      // Emit watchpointHit immediately when hardware debug registers fire a data watchpoint.
      const bool isWatchTrigger = line.contains("watchpoint-trigger");
      if (isWatchTrigger) handleWatchpointStopRecord(rankId, line);
      // Handle initial attach synchronization if `lastFiredTimestamp` was not pre-synced.
      if (!isWatchTrigger && rp->state.lastFiredTimestamp.isEmpty()) {
        rp->state.lastFiredTimestamp = "synced"; flushCachedBreakpoints(rankId); continue;
      }
      // Non-suspending Logpoint: format {expr} from frame args (or token 209 locals) and immediately continue.
      if (line.contains("breakpoint-hit")) {
        bool handledLogpoint = false; const QString curBase = QFileInfo(rp->state.currentFile).fileName();
        for (const auto& bp : m_pendingBreakpoints) {
          if (!bp.logMessage.isEmpty() && bp.line == rp->state.currentLine && (bp.file == rp->state.currentFile || QFileInfo(bp.file).fileName() == curBase)) {
            QMap<QString, QString> vals; static const QRegularExpression rxArg(QStringLiteral("\\{name=\"([^\"]+)\",value=\"((?:\\\\.|[^\"])*)\"\\}")), rxPh(QStringLiteral("\\{([A-Za-z_][A-Za-z0-9_]*)\\}"));
            for (auto it = rxArg.globalMatch(line); it.hasNext();) { const auto m = it.next(); vals.insert(m.captured(1), m.captured(2)); }
            bool needLocals = false; for (auto phIt = rxPh.globalMatch(bp.logMessage); phIt.hasNext();) if (!vals.contains(phIt.next().captured(1))) { needLocals = true; break; }
            if (needLocals) { rp->pendingLogpointMessage = bp.logMessage; rp->pendingLogpointArgs = vals; writeCmd(rankId, "209-stack-list-variables --simple-values\n"); }
            else { QString msg = bp.logMessage; for (auto vIt = vals.constBegin(); vIt != vals.constEnd(); ++vIt) msg.replace(QStringLiteral("{%1}").arg(vIt.key()), vIt.value());
              emit programOutputReceived(rankId, QStringLiteral("[LOGPOINT Rank %1] %2\n").arg(rankId).arg(msg), false); writeCmd(rankId, "-exec-continue\n"); rp->state.currentState = "running"; }
            handledLogpoint = true; break;
          }
        }
        if (handledLogpoint) continue;
      }
      // Preserve auto-continue on unmapped breakpoints while allowing explicit steps/signals/watchpoints/history-boundaries to stop.
      const bool isStepOrSignal = isWatchTrigger || line.contains("reason=\"end-stepping-range\"") || line.contains("reason=\"no-history-boundary\"") || line.contains("reason=\"signal-received\"");
      if ((rp->state.currentFile.isEmpty() || rp->state.currentFile == "??") && !isStepOrSignal) {
        writeCmd(rankId, "-exec-continue\n"); rp->state.currentState = "running";
      } else {
        // Request live disassembly, CPU registers, frame locals, and active thread only when remaining stopped.
        writeCmd(rankId, QString("200-data-disassemble -s %1 -e \"%1 + 64\" -- 0\n").arg(stopAddress));
        writeCmd(rankId, "-data-list-register-values r\n");
        writeCmd(rankId, "201-stack-list-variables --simple-values\n");
        static const QRegularExpression threadRe("thread-id=\"(\\d+)\"");
        const auto matchThread = threadRe.match(line);
        writeCmd(rankId, matchThread.hasMatch() ? QString("-thread-select %1\n").arg(matchThread.captured(1)) : QStringLiteral("-thread-info\n"));
        // Materialize any newly registered watch variable objects in the stopped frame scope.
        for (int varIdx = 0; varIdx < static_cast<int>(m_watchVariables.size()); ++varIdx) {
          const QString& varName = m_watchVariables[varIdx];
          if (!m_varNameMap[rankId].contains(varName)) writeCmd(rankId, QString("30%1-var-create - * %2\n").arg(varIdx).arg(varName));
        }
        if (matchThread.hasMatch()) m_activeThreadIds[rankId] = matchThread.captured(1).toInt();
        requestStackAndThreads(rankId);
        emit rankStateChanged(rankId, rp->state);
        if (rp->state.currentLine > 0) emit locationChanged(rankId, rp->state.currentFile, rp->state.currentLine);
      }
    } else if (line.startsWith("*running")) {
      // Transition rank slot to running state and restart its execution stopwatch.
      rp->state.currentState = "running"; rp->state.executionTimer.start();
      emit rankStateChanged(rankId, rp->state);
    } else if (line.startsWith("30")) {
      // Token `30<varIdx>`: Watch variable `-var-create` response binding user name <-> GDB varobj ID.
      if (line.contains("^done")) {
        static const QRegularExpression evalRe("30(\\d+)\\^done,name=\"([^\"]+)\".*?value=\"([^\"]+)\"");
        if (const auto match = evalRe.match(line); match.hasMatch()) {
          const int varIdx = match.captured(1).toInt();
          if (varIdx >= 0 && varIdx < static_cast<int>(m_watchVariables.size())) {
            const QString& varName = m_watchVariables[varIdx]; const QString varId = match.captured(2);
            m_varNameMap[rankId][varName] = varId; m_varNameRevMap[rankId][varId] = varName;
            rp->state.variableWatches[varName] = match.captured(3); emit rankStateChanged(rankId, rp->state);
          }
        }
      } else if (line.contains("^error")) {
        // Mark watch variable `<Out of Scope>` when expression cannot be resolved in active frame.
        static const QRegularExpression errRe("30(\\d+)\\^error");
        if (const auto match = errRe.match(line); match.hasMatch()) {
          const int varIdx = match.captured(1).toInt();
          if (varIdx >= 0 && varIdx < static_cast<int>(m_watchVariables.size())) {
            const QString& varName = m_watchVariables[varIdx];
            rp->state.variableWatches[varName] = "<Out of Scope>"; m_varNameMap[rankId].remove(varName);
            emit rankStateChanged(rankId, rp->state);
          }
        }
      }
    } else if (line.contains("^done,changelist=")) {
      // Update watch variable values from `-var-update 1 *` changelist tuples.
      static const QRegularExpression objRe("\\{([^}]+)\\}");
      static const QRegularExpression nameRe("name=\"([^\"]+)\"");
      static const QRegularExpression valRe("value=\"([^\"]+)\"");
      static const QRegularExpression inScopeRe("in_scope=\"([^\"]+)\"");
      auto it = objRe.globalMatch(line); bool updated = false;
      // Inspect each changed GDB varobj tuple for updated scalar text or scope exit.
      while (it.hasNext()) {
        const QString objStr = it.next().captured(1);
        const auto nameMatch = nameRe.match(objStr);
        if (!nameMatch.hasMatch()) continue;
        const QString varId = nameMatch.captured(1);
        if (!m_varNameRevMap.contains(rankId) || !m_varNameRevMap[rankId].contains(varId)) continue;
        const QString varName = m_varNameRevMap[rankId][varId];
        const auto scopeMatch = inScopeRe.match(objStr);
        // Refresh value when variable remains in scope; delete stale varobj when leaving scope.
        if (!scopeMatch.hasMatch() || scopeMatch.captured(1) == "true") {
          const auto valMatch = valRe.match(objStr);
          if (valMatch.hasMatch()) { rp->state.variableWatches[varName] = valMatch.captured(1); updated = true; }
        } else {
          rp->state.variableWatches[varName] = "Out of scope";
          writeCmd(rankId, QString("-var-delete %1\n").arg(varId));
          m_varNameMap[rankId].remove(varName); m_varNameRevMap[rankId].remove(varId); updated = true;
        }
      }
      if (updated) emit rankStateChanged(rankId, rp->state);
    } else if (line.startsWith("^done,memory=[")) {
      // Decode hexadecimal payload returned by `-data-read-memory-bytes` for MemView hex/diff panes.
      // Emits both string-formatted `memoryDataReady` and raw byte-array `memoryRead` signals.
      static const QRegularExpression memRe("begin=\"([^\"]+)\".*?contents=\"([^\"]+)\"");
      const auto match = memRe.match(line);
      if (match.hasMatch()) {
        bool ok = false; const qint64 beginAddress = match.captured(1).toULongLong(&ok, 16);
        const QString hex = match.captured(2);
        emit memoryDataReady(rankId, beginAddress, hex);
        emit memoryRead(rankId, match.captured(1), QByteArray::fromHex(hex.toLatin1()));
      }
    } else if (line.startsWith("^done,register-values=[")) {
      // Populate `rp->state.registers` and broadcast `registersUpdated` + `rankStateChanged`.
      // Preserves per-rank GPR and `$rip`/`$rsp` cache for status bar and MemView `$rsp` lookups.
      static const QRegularExpression regRe("number=\"(\\d+)\".*?value=\"([^\"]+)\"");
      auto it = regRe.globalMatch(line); QJsonArray jsonRegs; bool updated = false;
      // Map numeric GDB x86_64 register indices to canonical mnemonics (`rax`..`rip`, `eflags`).
      while (it.hasNext()) {
        const auto m = it.next();
        const int regNum = m.captured(1).toInt(); const QString regVal = m.captured(2);
        rp->state.registers[regNum] = regVal;
        if (const QString name = x86RegName(regNum); !name.isEmpty()) jsonRegs.append(QJsonObject{{"name", name}, {"value", regVal}});
        updated = true;
      }
      // Emit structured JSON register bank when at least one hardware register value was parsed.
      if (updated) { if (!jsonRegs.isEmpty()) emit registersUpdated(rankId, jsonRegs); emit rankStateChanged(rankId, rp->state); }
    } else if (line.startsWith("^done,bkpt={")) {
      static const QRegularExpression bkptRe("number=\"(\\d+)\".*?file=\"([^\"]+)\".*?line=\"(\\d+)\"");
      if (const auto match = bkptRe.match(line); match.hasMatch()) {
        const int bkptId = match.captured(1).toInt(), bkptLn = match.captured(3).toInt();
        const QString bkptBase = QFileInfo(match.captured(2)).fileName();
        rp->state.breakpoints[bkptId] = QString("%1:%2").arg(match.captured(2), match.captured(3));
        for (const auto& bp : m_pendingBreakpoints) if (bp.ignoreCount > 0 && bp.line == bkptLn && QFileInfo(bp.file).fileName() == bkptBase)
          writeCmd(rankId, QString("-break-after %1 %2\n").arg(bkptId).arg(bp.ignoreCount));
        if (rp->state.currentState == "sync_flushing") { if (--rp->state.pendingBreakpoints <= 0) { writeCmd(rankId, "-exec-continue\n"); rp->state.currentState = "running"; } }
        else if (rp->state.currentState != "stopped") emit rankStateChanged(rankId, rp->state);
      }
    } else if (line.startsWith("100")) {
      static const QRegularExpression val100Re("100\\^done,value=\"([^\"]+)\""), err100Re("msg=\"([^\"]+)\"");
      if (const auto match = line.contains("^done") ? val100Re.match(line) : err100Re.match(line); match.hasMatch()) emit hoverEvaluationComplete(rp->lastHoverVarName, match.captured(1), rp->lastHoverPos);
    } else if (line.startsWith("101")) {
      // Token 101xxx: Resolve exact expression key from rotating token map for watch/inline evaluations.
      const int caret = line.indexOf('^'); const QString resolvedExpr = (caret > 0 && evalTokenExprMap().contains(line.left(caret).toInt())) ? evalTokenExprMap().take(line.left(caret).toInt()) : rp->lastEvalExpression;
      auto unesc = [](QString s) { return s.replace(QLatin1String("\\\""), QLatin1String("\"")).replace(QLatin1String("\\\\"), QLatin1String("\\")); };
      if (line.contains("^done")) { static const QRegularExpression valRe("\\^done,value=\"((?:\\\\.|[^\"])*)\""); if (const auto m = valRe.match(line); m.hasMatch()) emit expressionEvaluated(rankId, resolvedExpr, unesc(m.captured(1))); }
      else if (line.contains("^error")) { static const QRegularExpression errRe("msg=\"((?:\\\\.|[^\"])*)\""); if (const auto m = errRe.match(line); m.hasMatch()) emit expressionEvaluated(rankId, resolvedExpr, "Error: " + unesc(m.captured(1))); }
    } else if (line.startsWith("200")) {
      // Token 200: Stop-point disassembly window (`-data-disassemble`) followed by `-var-update`.
      if (line.contains("asm_insns=")) { const QString prettyAsm = formatMiDisassembly(line); if (!prettyAsm.isEmpty()) { rp->state.disassemblyText = prettyAsm; emit rankStateChanged(rankId, rp->state); } }
      writeCmd(rankId, "-var-update 1 *\n");
    } else if (line.startsWith("201^done,variables=[")) {
      // Token 201: Parse stack-frame locals (`name`, `type`, `value`), filtering `@entry` and duplicates.
      static const QRegularExpression varTupleRe("\\{([^{}]+)\\}"), vNameRe("name=\"([^\"]+)\""), vTypeRe("type=\"([^\"]+)\""), vValRe("value=\"((?:\\\\.|[^\"])*)\"");
      QJsonArray locals; QHash<QString, int> idxByName; auto it = varTupleRe.globalMatch(line);
      while (it.hasNext()) {
        const QString tuple = it.next().captured(1); const auto nm = vNameRe.match(tuple); if (!nm.hasMatch()) continue;
        // Skip GDB `@entry` shadow entries so each variable appears only once.
        const QString varName = nm.captured(1); if (varName.endsWith(QStringLiteral("@entry"))) continue;
        const auto tm = vTypeRe.match(tuple); const auto vm = vValRe.match(tuple);
        const QString varVal = vm.hasMatch() ? vm.captured(1).replace(QLatin1String("\\\""), QLatin1String("\"")).replace(QLatin1String("\\\\"), QLatin1String("\\")) : QStringLiteral("{...}");
        const QJsonObject entry{{"name", varName}, {"type", tm.hasMatch() ? tm.captured(1) : QStringLiteral("compound")}, {"value", varVal}, {"variablesReference", 0}};
        // Deduplicate repeated MI tuples for the same symbol, preferring non-opaque values.
        if (idxByName.contains(varName)) {
          const int prevIdx = idxByName.value(varName); const QString prevVal = locals.at(prevIdx).toObject().value("value").toString();
          if ((prevVal == QLatin1String("{...}") || prevVal.isEmpty()) && varVal != QLatin1String("{...}") && !varVal.isEmpty()) locals[prevIdx] = entry;
          continue;
        }
        idxByName.insert(varName, locals.size()); locals.append(entry);
      }
      emit localsUpdated(rankId, locals);
    } else if (handleMiStackOrThreadRecord(rankId, line)) {
      // Token 202..206: Multi-frame backtrace, thread list, REPL output, and watchpoint records.
    } else if (line.contains("asm_insns=")) {
      // Untokenized fallback `-data-disassemble` response handler.
      const QString prettyAsm = formatMiDisassembly(line);
      if (!prettyAsm.isEmpty()) { rp->state.disassemblyText = prettyAsm; emit rankStateChanged(rankId, rp->state); }
    } else if (line.startsWith("@\"")) {
      // MI target-stream inferior output record: unescape and queue for batched Program Output.
      QString txt = line.mid(2, line.endsWith('"') ? line.size() - 3 : line.size() - 2);
      txt.replace("\\n", "\n").replace("\\t", "\t").replace("\\\"", "\"").replace("\\\\", "\\");
      if (const QString cleaned = txt.trimmed(); !cleaned.isEmpty()) batchedProgramOut << cleaned;
    } else {
      // Filter out GDB/MI protocol & console noise, queueing genuine inferior stdout/stderr.
      static const QRegularExpression miPrefRe("^\\d*[\\^*+=~&@]|^\\(gdb\\)");
      if (!miPrefRe.match(line).hasMatch() && !line.startsWith("[New ") && !line.startsWith("[Thread ") && !line.startsWith("[Inferior ") && !line.startsWith("[Detaching") && !line.startsWith("Reading symbols") && !line.startsWith("warning:"))
        batchedProgramOut << line;
    }
  }
  if (!batchedProgramOut.isEmpty()) emit programOutputReceived(rankId, batchedProgramOut.join(QLatin1Char('\n')), false);
}

}  // namespace gridlock
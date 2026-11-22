/**
 * @file GdbRankCoordinator.cpp
 * @brief Multi-rank GDB/MI3 process lifecycle, attach, FIFO/QProcess transport, and session teardown.
 *
 * Coordinates multi-rank MPI debugging by launching per-rank `gdb --interpreter=mi3`
 * instances over non-blocking POSIX named pipes (`mkfifo`) under `mpirun`/`mpiexec`
 * (or standalone `gdb` when `rankCount == 1` without MPI). MI output parsing resides in
 * `GdbMiParser.cpp`, stack/thread/varobj inspection in `GdbRankInspection.cpp`, and
 * execution/breakpoint/watchpoint/catchpoint command builders in `GdbRankCommands.cpp`.
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
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QThread>

#include "../../managers/ConfigManager.hpp"

namespace gridlock {

namespace detail {
QString escapeGdbMiString(const QString& str);
QString buildBreakInsertCmd(const QString& file, int line, const QString& condition,
                            int ignoreCount = 0, bool temporary = false, bool disabled = false);
}  // namespace detail

/** @brief Constructs `GdbRankCoordinator` and forwards @p parent to `IBackendCoordinator`. */
GdbRankCoordinator::GdbRankCoordinator(QObject* parent) : IBackendCoordinator(parent) {}

/** @brief Ensures all GDB, FIFO, and `mpiexec` resources are cleaned up on destruction without emitting teardown signals. */
GdbRankCoordinator::~GdbRankCoordinator() { m_shuttingDown = true; QSignalBlocker b(this); stopAll(true); }

/** @brief Implements `IBackendCoordinator::terminateSession()` by tearing down all rank processes. */
void GdbRankCoordinator::terminateSession() { stopAll(m_shuttingDown); }

/** @brief Returns the live inferior OS PID for @p rank if known, or `0` otherwise. */
qint64 GdbRankCoordinator::getPidForRank(int rank) const {
  if (m_rankPids.contains(rank) && m_rankPids.value(rank) > 0) return m_rankPids.value(rank);
  if (rank >= 0 && rank < static_cast<int>(m_processes.size()) && m_processes[rank] && m_processes[rank]->inferiorPid > 0)
    return static_cast<qint64>(m_processes[rank]->inferiorPid);
  return 0;
}

/** @brief Pre-validates `gdb` and `mpirun`/`mpiexec` executables before UI marks ranks `RUNNING`. */
bool GdbRankCoordinator::validateLaunchEnvironment(int ranks, QString* outError) const {
  const QStringList searchDirs = {QStringLiteral("/usr/lib64/openmpi/bin"), QStringLiteral("/usr/lib/openmpi/bin"),
                                  QStringLiteral("/usr/lib64/mpich/bin"), QStringLiteral("/usr/lib/mpich/bin"),
                                  QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/bin")};
  const QString cfgGdb = core::ConfigManager::instance().getGdbPath().trimmed();
  QString gdbBin = (!cfgGdb.isEmpty() && QFileInfo::exists(cfgGdb)) ? cfgGdb : QStandardPaths::findExecutable(cfgGdb.isEmpty() ? QStringLiteral("gdb") : cfgGdb);
  if (gdbBin.isEmpty() && (cfgGdb.isEmpty() || cfgGdb == QStringLiteral("gdb"))) gdbBin = QStandardPaths::findExecutable(QStringLiteral("gdb"), searchDirs);
  if (gdbBin.isEmpty()) { if (outError) *outError = QStringLiteral("GDB debugger executable ('%1') was not found in PATH.").arg(cfgGdb.isEmpty() ? QStringLiteral("gdb") : cfgGdb); return false; }
  const QString prefMpi = core::ConfigManager::instance().getDebuggerSettings().mpiExecutable.trimmed();
  const auto findMpi = [&](const QString& name) -> QString {
    if (name.isEmpty()) return {};
    if (QFileInfo::exists(name) && QFileInfo(name).isExecutable()) return name;
    QString f = QStandardPaths::findExecutable(name);
    return f.isEmpty() ? QStandardPaths::findExecutable(name, searchDirs) : f;
  };
  if (!prefMpi.isEmpty() && prefMpi != QStringLiteral("mpirun") && prefMpi != QStringLiteral("mpiexec") && findMpi(prefMpi).isEmpty()) {
    if (outError) *outError = QStringLiteral("Configured MPI launcher ('%1') was not found.").arg(prefMpi);
    return false;
  }
  if (ranks > 1 && findMpi(prefMpi).isEmpty() && findMpi(QStringLiteral("mpirun")).isEmpty() && findMpi(QStringLiteral("mpiexec")).isEmpty()) {
    if (outError) *outError = QStringLiteral("Multi-rank MPI launch (-np %1) requires mpirun or mpiexec in PATH.").arg(ranks);
    return false;
  }
  return true;
}

/** @brief Attaches a single GDB/MI3 rank session to running OS process @p pid. */
bool GdbRankCoordinator::attachToProcess(qint64 pid) { return attachToPids({pid}); }

/** @brief Attaches one GDB/MI3 rank session per target `ProcessEntry` (ordered by `mpiRank`). */
bool GdbRankCoordinator::attachToProcesses(const QList<ProcessEntry>& targets) {
  if (targets.isEmpty()) return false;
  QList<ProcessEntry> ordered = targets;
  std::sort(ordered.begin(), ordered.end(), [](const ProcessEntry& a, const ProcessEntry& b) {
    return (a.mpiRank >= 0 && b.mpiRank >= 0 && a.mpiRank != b.mpiRank) ? (a.mpiRank < b.mpiRank) : (a.pid < b.pid);
  });
  QList<qint64> pids;
  for (const ProcessEntry& e : ordered) pids.append(static_cast<qint64>(e.pid));
  return attachToPids(pids);
}

/** @brief Spawns one GDB/MI3 session per PID in @p pids and issues `-target-attach <pid>`. */
bool GdbRankCoordinator::attachToPids(const QList<qint64>& pids) {
  if (pids.isEmpty()) return false;
  for (qint64 pid : pids) if (pid <= 0 || !QFileInfo::exists(QStringLiteral("/proc/%1/status").arg(pid))) return false;
  terminateAllSessions();
  QString gdbPath = core::ConfigManager::instance().getGdbPath().trimmed();
  if (gdbPath.isEmpty() || !QFileInfo::exists(gdbPath)) gdbPath = QStandardPaths::findExecutable(gdbPath.isEmpty() ? QStringLiteral("gdb") : gdbPath);
  if (gdbPath.isEmpty()) return false;

  m_currentBinaryPath = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pids.first())).symLinkTarget();
  for (int r = 0; r < pids.size(); ++r) {
    const qint64 pid = pids[r];
    auto rp = std::make_unique<RankProcess>();
    rp->id = r; rp->process = std::make_unique<QProcess>(); rp->inferiorPid = static_cast<pid_t>(pid);
    rp->pendingAttach = true; rp->isAttached = true; rp->state.rankId = r; rp->state.currentState = "launching"; rp->state.lastFiredTimestamp = "synced";
    m_rankPids[r] = pid;
    connect(rp->process.get(), &QProcess::readyReadStandardOutput, this, [this, r]() { handleGdbOutput(r); });
    QStringList args{QStringLiteral("--quiet"), QStringLiteral("--nx"), QStringLiteral("-iex"), QStringLiteral("set debuginfod enabled off"), QStringLiteral("--interpreter=mi3")};
    if (!m_currentBinaryPath.isEmpty() && QFileInfo::exists(m_currentBinaryPath)) args.append(m_currentBinaryPath);
    rp->process->start(gdbPath, args);
    const bool started = rp->process->waitForStarted(2000);
    m_processes.push_back(std::move(rp));
    if (!started) { terminateAllSessions(); return false; }
    writeCmd(r, "-gdb-set pagination off\n"); writeCmd(r, "-gdb-set confirm off\n");
    writeCmd(r, QStringLiteral("-target-attach %1\n").arg(pid)); writeCmd(r, "-list-thread-groups\n");
    emit rankStateChanged(r, m_processes[r]->state);
  }
  return true;
}

/** @brief Connects one GDB/MI3 session per remote `gdbserver` / `qemu-user -g` endpoint. */
bool GdbRankCoordinator::attachToRemoteTargets(const QList<RemoteTargetSpec>& targets) {
  if (targets.isEmpty()) return false;
  for (const RemoteTargetSpec& t : targets) if (t.endpoint.trimmed().isEmpty()) return false;
  terminateAllSessions();
  m_currentBinaryPath = targets.first().localSymbolBinary.trimmed();
  for (int r = 0; r < targets.size(); ++r) {
    const RemoteTargetSpec& t = targets[r];
    QString gdbPath = t.gdbBinary.trimmed();
    if (gdbPath.isEmpty()) gdbPath = core::ConfigManager::instance().getGdbPath().trimmed();
    if (!QFileInfo::exists(gdbPath)) gdbPath = QStandardPaths::findExecutable(gdbPath);
    if (gdbPath.isEmpty()) gdbPath = QStandardPaths::findExecutable(QStringLiteral("gdb-multiarch"));
    if (gdbPath.isEmpty()) gdbPath = QStandardPaths::findExecutable(QStringLiteral("gdb"));
    if (gdbPath.isEmpty()) { terminateAllSessions(); return false; }

    auto rp = std::make_unique<RankProcess>();
    rp->id = r; rp->process = std::make_unique<QProcess>(); rp->pendingAttach = true; rp->isAttached = true;
    rp->state.rankId = r; rp->state.currentState = "stopped"; rp->state.lastFiredTimestamp = "synced";
    connect(rp->process.get(), &QProcess::readyReadStandardOutput, this, [this, r]() { handleGdbOutput(r); });
    rp->process->start(gdbPath, {QStringLiteral("--quiet"), QStringLiteral("--nx"), QStringLiteral("-iex"), QStringLiteral("set debuginfod enabled off"), QStringLiteral("--interpreter=mi3")});
    const bool started = rp->process->waitForStarted(2000);
    m_processes.push_back(std::move(rp));
    if (!started) { terminateAllSessions(); return false; }
    writeCmd(r, "-gdb-set pagination off\n"); writeCmd(r, "-gdb-set confirm off\n");
    const QString symBin = t.localSymbolBinary.trimmed();
    if (!symBin.isEmpty()) writeCmd(r, QStringLiteral("-file-exec-and-symbols %1\n").arg(detail::escapeGdbMiString(symBin)));
    writeCmd(r, QStringLiteral("-target-select %1 %2\n").arg(t.extendedRemote ? QStringLiteral("extended-remote") : QStringLiteral("remote"), t.endpoint.trimmed()));
    flushCachedBreakpoints(r);
    emit rankStateChanged(r, m_processes[r]->state);
  }
  return true;
}

/** @brief Queues @p cmd behind a transient SIGINT if @p rankId is running, or writes immediately when stopped. */
void GdbRankCoordinator::queueOrWriteCmd(int rankId, const QString& cmd) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size()) || !m_processes[rankId]) return;
  auto& rp = m_processes[rankId];
  if (rp->state.currentState == "running") {
    rp->pendingAfterStopCmds.append(cmd);
    if (!rp->autoContinueAfterPending) {
      rp->autoContinueAfterPending = true; ++rp->pendingInterruptSignals;
      if (rp->inferiorPid > 0) ::kill(rp->inferiorPid, SIGINT); else writeCmd(rankId, "-exec-interrupt\n");
    }
    return;
  }
  writeCmd(rankId, cmd);
}

/** @brief Writes @p cmd to @p rankId's FIFO or `QProcess` stdin and emits `commandSentToGdb`. */
void GdbRankCoordinator::writeCmd(int rankId, const QString& cmd) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  if (!rp) return;
  static const QRegularExpression kInspectMiRe(QStringLiteral("^\\d*-(?:stack|thread|var|data)-"));
  if (rp->state.currentState != QLatin1String("stopped") && kInspectMiRe.match(cmd.trimmed()).hasMatch()) return;
  if (rp->fifoInFd >= 0) {
    const QByteArray bytes = cmd.toUtf8();
    if (::write(rp->fifoInFd, bytes.constData(), static_cast<size_t>(bytes.size())) > 0) emit commandSentToGdb(rankId, cmd.trimmed());
    return;
  }
  if (rp->process) {
    if (rp->process->state() == QProcess::Starting) rp->process->waitForStarted(1000);
    if (rp->process->state() == QProcess::Running && rp->process->isOpen() && rp->process->isWritable()) {
      rp->process->write(cmd.toUtf8());
      emit commandSentToGdb(rankId, cmd.trimmed());
    }
  } else {
    emit commandSentToGdb(rankId, cmd.trimmed());
  }
}

/** @brief Pushes persisted workspace breakpoints to @p rankId upon initial attach stop. */
void GdbRankCoordinator::flushCachedBreakpoints(int rankId) {
  const auto bps = core::ConfigManager::instance().getBreakpoints(m_currentBinaryPath);
  auto& rp = m_processes[rankId];
  int count = 0;
  for (auto it = bps.constBegin(); it != bps.constEnd(); ++it)
    for (int line : *it) { writeCmd(rankId, QString("-break-insert -f %1:%2\n").arg(it.key()).arg(line)); count++; }
  if (count == 0) { writeCmd(rankId, "-exec-continue\n"); rp->state.currentState = "running"; }
  else { rp->state.pendingBreakpoints = count; rp->state.currentState = "sync_flushing"; }
}

/** @brief Cleanly stops all per-rank GDB sessions, closes FIFOs, kills `mpirun`, and removes temp files. */
void GdbRankCoordinator::terminateAllSessions() { stopAll(m_shuttingDown); }

void GdbRankCoordinator::stopAll(bool silentShutdown) {
  const bool prevShuttingDown = m_shuttingDown;
  m_shuttingDown = true;
  QVector<pid_t> spawnedPids;
  if (m_mpirunProcess) m_mpirunProcess->disconnect();
  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto& rp = m_processes[i];
    if (!rp) continue;
    const bool wasFifo = (rp->fifoInFd >= 0 || rp->fifoOutFd >= 0);
    const bool wasAttached = rp->isAttached || rp->pendingAttach;
    if (rp->fifoNotifier) { rp->fifoNotifier->setEnabled(false); rp->fifoNotifier->disconnect(); delete rp->fifoNotifier; rp->fifoNotifier = nullptr; }
    if (rp->process) rp->process->disconnect();
    if (!wasAttached && rp->inferiorPid > 0) {
      spawnedPids.append(rp->inferiorPid);
      ::kill(rp->inferiorPid, SIGINT);
    }
    if (rp->fifoInFd >= 0) {
      [[maybe_unused]] const ssize_t n = ::write(rp->fifoInFd, "-gdb-exit\n", 10);
      ::close(rp->fifoInFd); rp->fifoInFd = -1;
    }
    if (rp->fifoOutFd >= 0) { ::close(rp->fifoOutFd); rp->fifoOutFd = -1; }
    if (wasFifo && !wasAttached && rp->inferiorPid > 0) ::kill(rp->inferiorPid, SIGTERM);
    if (rp->process) {
      if (rp->process->state() == QProcess::Starting) rp->process->waitForStarted(250);
      if (rp->process->state() == QProcess::Running && rp->process->isOpen() && rp->process->isWritable()) {
        rp->process->write(wasAttached ? "-target-detach\n-gdb-exit\n" : "-gdb-exit\n");
      }
      if (rp->process->state() != QProcess::NotRunning && !rp->process->waitForFinished(250)) {
        rp->process->terminate();
        if (!rp->process->waitForFinished(250)) { rp->process->kill(); rp->process->waitForFinished(250); }
      }
    }
    rp->inferiorPid = 0;
  }
  const bool hadActiveSession = !m_processes.empty() || !m_rankFrames.isEmpty() || !m_rankThreads.isEmpty();
  m_processes.clear(); m_rankPids.clear(); m_varNameMap.clear(); m_varNameRevMap.clear();
  m_rankFrames.clear(); m_rankThreads.clear(); m_activeThreadIds.clear(); m_pendingFrameSelect.clear();
  m_pendingConsoleCmdCount.clear(); m_consoleReplyBuffer.clear(); m_watchpointIds.clear();
  if (m_mpirunProcess) {
    if (m_mpirunProcess->state() == QProcess::Starting) m_mpirunProcess->waitForStarted(250);
    if (m_mpirunProcess->state() != QProcess::NotRunning && !m_mpirunProcess->waitForFinished(250)) {
      m_mpirunProcess->terminate();
      if (!m_mpirunProcess->waitForFinished(250)) { m_mpirunProcess->kill(); m_mpirunProcess->waitForFinished(250); }
    }
    m_mpirunProcess.reset();
  }
  for (pid_t pid : spawnedPids) {
    if (pid > 0 && ::kill(pid, 0) == 0) ::kill(pid, SIGKILL);
  }
  if (!m_fifoTempDir.isEmpty()) { QDir(m_fifoTempDir).removeRecursively(); m_fifoTempDir.clear(); }
  m_currentBinaryPath.clear();
  if (!silentShutdown) {
    m_shuttingDown = prevShuttingDown;
    if (hadActiveSession) { emit stackTraceUpdated(0, {}); emit threadsUpdated(0, {}); }
  }
}

/** @brief Convenience entry point forwarding to `launchParallelSession()`. */
void GdbRankCoordinator::startDebugSession(int rankCount, const QString& executable) {
  launchParallelSession(executable, rankCount);
}

/** @brief Spawns `mpirun`/`mpiexec` using configured MPI arguments from `ConfigManager`. */
void GdbRankCoordinator::launchParallelSession(const QString& executable, int explicitRankCount) {
  launchParallelSession(executable, explicitRankCount, core::ConfigManager::instance().getMpiArgs());
}

/** @brief Launches `N` parallel GDB/MI3 sessions across MPI ranks via POSIX FIFOs (or direct `gdb`). */
void GdbRankCoordinator::launchParallelSession(const QString& executable, int explicitRankCount,
                                               const QString& customMpiArgs) {
  terminateAllSessions();
  if (executable.trimmed().isEmpty()) return;

  const auto ds = core::ConfigManager::instance().getDebuggerSettings();
  const auto hs = core::ConfigManager::instance().getHpcSettings();
  const int rankCount = std::max(1, (explicitRankCount > 0) ? explicitRankCount : ds.defaultRanks);
  m_currentBinaryPath = executable;

  const QStringList searchDirs = {QStringLiteral("/usr/lib64/openmpi/bin"), QStringLiteral("/usr/lib/openmpi/bin"),
                                  QStringLiteral("/usr/lib64/mpich/bin"), QStringLiteral("/usr/lib/mpich/bin"),
                                  QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/bin")};

  QString gdbPath = core::ConfigManager::instance().getGdbPath().trimmed();
  if (gdbPath.isEmpty() || !QFileInfo::exists(gdbPath)) {
    gdbPath = QStandardPaths::findExecutable(gdbPath.isEmpty() ? QStringLiteral("gdb") : gdbPath);
    if (gdbPath.isEmpty()) gdbPath = QStandardPaths::findExecutable(QStringLiteral("gdb"), searchDirs);
  }
  if (gdbPath.isEmpty()) { qWarning() << "[GDB] Could not locate gdb executable in PATH."; return; }

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

  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("PATH"), searchDirs.join(QLatin1Char(':')) + QLatin1Char(':') + env.value(QStringLiteral("PATH")));
  env.insert(QStringLiteral("LD_LIBRARY_PATH"), QStringLiteral("/usr/lib64/openmpi/lib:/usr/lib/openmpi/lib:") + env.value(QStringLiteral("LD_LIBRARY_PATH")));
  for (const QString& pair : hs.envVars.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
    if (const int eq = pair.indexOf(QLatin1Char('=')); eq > 0) env.insert(pair.left(eq).trimmed(), pair.mid(eq + 1).trimmed());
  }
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
  if (env.value(QStringLiteral("OMP_NUM_THREADS")).trimmed().isEmpty() && !customMpiArgs.contains(QStringLiteral("OMP_NUM_THREADS")))
    env.insert(QStringLiteral("OMP_NUM_THREADS"), QString::number(qMax(1, QThread::idealThreadCount() / qMax(1, rankCount))));
  if (env.value(QStringLiteral("OMP_WAIT_POLICY")).trimmed().isEmpty() && !customMpiArgs.contains(QStringLiteral("OMP_WAIT_POLICY")))
    env.insert(QStringLiteral("OMP_WAIT_POLICY"), QStringLiteral("PASSIVE"));

  const QString ompDisp = QStringLiteral("OMP_NUM_THREADS=%1 OMP_WAIT_POLICY=%2 ").arg(env.value(QStringLiteral("OMP_NUM_THREADS")), env.value(QStringLiteral("OMP_WAIT_POLICY")));
  const QString mpiDisp = mpiBin.isEmpty() ? QString() : (QFileInfo(mpiBin).fileName() + (customMpiArgs.trimmed().isEmpty() ? QString() : QStringLiteral(" ") + customMpiArgs.trimmed()) + QStringLiteral(" -np %1 ").arg(rankCount));
  m_lastLaunchCommandSummary = QStringLiteral("[CWD: %1] %2%3%4%5").arg(workDir, ompDisp, mpiDisp, executable, execArgs.isEmpty() ? QString() : QStringLiteral(" ") + execArgs);

  const auto bootstrapRankGdb = [this, execArgs, workDir](int id) {
    writeCmd(id, "-gdb-set pagination off\n"); writeCmd(id, "-gdb-set confirm off\n");
    writeCmd(id, "-gdb-set non-stop off\n"); writeCmd(id, "-gdb-set breakpoint pending on\n");
    for (const char* sig : {"SIGPIPE", "SIGUSR1", "SIGUSR2", "SIG32", "SIG33", "SIG34"})
      writeCmd(id, QString("-interpreter-exec console \"handle %1 nostop noprint pass\"\n").arg(sig));
    for (auto it = m_signalDispositions.constBegin(); it != m_signalDispositions.constEnd(); ++it)
      writeCmd(id, QStringLiteral("-interpreter-exec console \"handle %1 %2 %3 %4\"\n")
                       .arg(it.value().signal, it.value().stop ? QStringLiteral("stop") : QStringLiteral("nostop"),
                            it.value().print ? QStringLiteral("print") : QStringLiteral("noprint"),
                            it.value().pass ? QStringLiteral("pass") : QStringLiteral("nopass")));
    writeCmd(id, "-gdb-set max-value-size unlimited\n");
    const QString memTrackSo = qEnvironmentVariable("GRIDLOCK_MEMTRACK_SO");
    if (!memTrackSo.isEmpty() && QFileInfo::exists(memTrackSo))
      writeCmd(id, QStringLiteral("-gdb-set environment LD_PRELOAD %1\n").arg(memTrackSo));
    if (!workDir.isEmpty() && QFileInfo(workDir).isDir()) {
      QString escWd = workDir; escWd.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
      writeCmd(id, QStringLiteral("-environment-cd \"%1\"\n").arg(escWd));
    }
    if (!execArgs.isEmpty()) writeCmd(id, QStringLiteral("-exec-arguments %1\n").arg(execArgs));
    if (!m_pendingBreakpoints.empty()) {
      for (const auto& bp : m_pendingBreakpoints)
        if (bp.rankFilter < 0 || bp.rankFilter == id) writeCmd(id, detail::buildBreakInsertCmd(bp.file, bp.line, bp.condition, bp.ignoreCount, bp.temporary, !bp.enabled));
    } else if (!m_breakpointsCleared) {
      const auto persisted = core::ConfigManager::instance().getBreakpoints(m_currentBinaryPath);
      for (auto it = persisted.constBegin(); it != persisted.constEnd(); ++it) for (int ln : *it) writeCmd(id, detail::buildBreakInsertCmd(it.key(), ln, QString()));
    }
    for (const QString& rx : m_pendingRegexBreakpoints) writeCmd(id, QStringLiteral("-interpreter-exec console \"rbreak %1\"\n").arg(rx));
    for (const auto& cp : m_activeCatchpoints) {
      if (!cp.enabled) continue;
      QString f = cp.filter.trimmed();
      if (cp.kind == CatchpointSpec::Kind::Syscall) {
        f.replace(QLatin1Char(','), QLatin1Char(' ')); f = f.simplified();
        writeCmd(id, f.isEmpty() ? QStringLiteral("-interpreter-exec console \"catch syscall\"\n") : QStringLiteral("-interpreter-exec console \"catch syscall %1\"\n").arg(f));
      } else {
        const QString base = (cp.kind == CatchpointSpec::Kind::CppThrow) ? QStringLiteral("-catch-throw") : (cp.kind == CatchpointSpec::Kind::CppRethrow) ? QStringLiteral("-catch-rethrow") : (cp.kind == CatchpointSpec::Kind::CppCatch) ? QStringLiteral("-catch-catch") : (cp.kind == CatchpointSpec::Kind::LoadLib) ? QStringLiteral("-catch-load") : QStringLiteral("-catch-unload");
        writeCmd(id, f.isEmpty() ? (base + QLatin1Char('\n')) : QStringLiteral("%1 -r %2\n").arg(base, f));
      }
    }
    writeCmd(id, "-exec-run\n");
    m_processes[id]->state.currentState = "running"; m_processes[id]->state.executionTimer.start();
  };

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
  m_mpirunProcess = std::make_unique<QProcess>();
  if (!workDir.isEmpty() && QFileInfo(workDir).isDir()) m_mpirunProcess->setWorkingDirectory(workDir);
  m_mpirunProcess->setProcessEnvironment(env);
  connect(m_mpirunProcess.get(), &QProcess::readyReadStandardOutput, this, [this]() { emit targetOutputReceived("stdout", QString::fromUtf8(m_mpirunProcess->readAllStandardOutput())); });
  connect(m_mpirunProcess.get(), &QProcess::readyReadStandardError, this, [this]() { emit targetOutputReceived("stderr", QString::fromUtf8(m_mpirunProcess->readAllStandardError())); });
  m_mpirunProcess->start(mpiBin, mpiArgs);
  for (int i = 0; i < rankCount; ++i) { bootstrapRankGdb(i); emit rankStateChanged(i, m_processes[i]->state); }
}

/** @brief Populates `m_processes` with in-memory mock rank slots for unit testing without `QProcess`. */
void GdbRankCoordinator::initializeMockSession(int rankCount, bool simulateInitialSync) {
  terminateAllSessions(); m_watchVariables.clear();
  for (int i = 0; i < rankCount; ++i) {
    auto rp = std::make_unique<RankProcess>();
    rp->id = i; rp->state.rankId = i; rp->state.currentState = "running"; rp->state.currentLine = 0;
    if (!simulateInitialSync) rp->state.lastFiredTimestamp = "mocked";
    m_processes.push_back(std::move(rp));
  }
}

/** @brief Drains available bytes from @p rankId's FIFO or `QProcess` into `processGdbOutput()`. */
void GdbRankCoordinator::handleGdbOutput(int rankId) {
  if (m_shuttingDown || rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId]; if (!rp) return;
  if (rp->fifoOutFd >= 0) {
    QByteArray acc; char buf[8192];
    while (!m_shuttingDown) { const ssize_t n = ::read(rp->fifoOutFd, buf, sizeof(buf)); if (n <= 0) break; acc.append(buf, static_cast<qsizetype>(n)); }
    if (!m_shuttingDown && !acc.isEmpty()) processGdbOutput(rankId, QString::fromUtf8(acc));
    return;
  }
  if (!m_shuttingDown && rp->process) processGdbOutput(rankId, QString::fromUtf8(rp->process->readAllStandardOutput()));
}

}  // namespace gridlock
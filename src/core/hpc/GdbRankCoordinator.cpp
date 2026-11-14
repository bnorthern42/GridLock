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
#include <QStandardPaths>
#include <QTimer>

#include "../managers/ConfigManager.hpp"

namespace gridlock {

namespace {

/**
 * @brief Escapes backslashes, quotes, and control characters for GDB/MI string literals.
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
QString buildBreakInsertCmd(const QString& file, int line, const QString& condition) {
  const QString loc = QString("%1:%2").arg(file).arg(line);
  if (!condition.trimmed().isEmpty()) {
    return QString("-break-insert -c %1 %2\n").arg(escapeGdbMiString(condition.trimmed()), loc);
  }
  return QString("-break-insert -f %1\n").arg(loc);
}

}  // namespace

/** @brief Constructs `GdbRankCoordinator` and forwards @p parent to `IBackendCoordinator`. */
GdbRankCoordinator::GdbRankCoordinator(QObject* parent) : IBackendCoordinator(parent) {}

/** @brief Ensures all GDB, FIFO, and `mpiexec` resources are cleaned up on destruction. */
GdbRankCoordinator::~GdbRankCoordinator() { terminateAllSessions(); }

/** @brief Implements `IBackendCoordinator::terminateSession()` by tearing down all rank processes. */
void GdbRankCoordinator::terminateSession() { terminateAllSessions(); }

/** @brief Returns the live inferior OS PID for @p rankId if known, or `0` otherwise. */
pid_t GdbRankCoordinator::getPidForRank(int rankId) const {
  if (rankId >= 0 && rankId < static_cast<int>(m_processes.size()) && m_processes[rankId]) {
    return m_processes[rankId]->inferiorPid;
  }
  return 0;
}

/**
 * @brief Registers a breakpoint in `m_pendingBreakpoints` and pushes it to live rank slots.
 */
void GdbRankCoordinator::setBreakpoint(const QString& file, int line, const QString& condition, int rankFilter) {
  if (file.isEmpty() || line <= 0) return;
  // Deduplicate existing entry at `(file, line, rankFilter)` before inserting updated metadata.
  m_pendingBreakpoints.erase(
      std::remove_if(m_pendingBreakpoints.begin(), m_pendingBreakpoints.end(),
                     [&](const PendingBreakpoint& bp) {
                       return bp.file == file && bp.line == line && bp.rankFilter == rankFilter;
                     }),
      m_pendingBreakpoints.end());
  m_pendingBreakpoints.push_back({file, line, condition.trimmed(), rankFilter});

  // Synchronize immediately to any already-running or paused rank slots matching `rankFilter`.
  const QString cmd = buildBreakInsertCmd(file, line, condition);
  for (size_t i = 0; i < m_processes.size(); ++i) {
    if (rankFilter < 0 || rankFilter == static_cast<int>(i)) writeCmd(static_cast<int>(i), cmd);
  }
}

/** @brief Clears all pending breakpoints and deletes active GDB breakpoints across all ranks. */
void GdbRankCoordinator::clearBreakpoints() {
  m_pendingBreakpoints.clear();
  for (size_t i = 0; i < m_processes.size(); ++i) {
    if (m_processes[i]) {
      m_processes[i]->state.breakpoints.clear();
      writeCmd(static_cast<int>(i), "-break-delete\n");
    }
  }
}

/**
 * @brief Writes @p cmd to @p rankId's FIFO or `QProcess` stdin and emits `commandSentToGdb`.
 */
void GdbRankCoordinator::writeCmd(int rankId, const QString& cmd) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  if (!rp) return;

  // Prefer the non-blocking POSIX FIFO bridge when running under `mpirun` MPMD mode.
  if (rp->fifoInFd >= 0) {
    const QByteArray bytes = cmd.toUtf8();
    if (::write(rp->fifoInFd, bytes.constData(), static_cast<size_t>(bytes.size())) > 0) {
      emit commandSentToGdb(rankId, cmd.trimmed());
    }
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
    for (int line : *it) {
      writeCmd(rankId, QString("-break-insert -f %1:%2\n").arg(it.key()).arg(line));
      count++;
    }
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
 * @brief Registers @p name in `m_watchVariables` and creates a GDB/MI variable object on each rank.
 */
void GdbRankCoordinator::registerWatchVariable(const QString& name) {
  if (std::find(m_watchVariables.begin(), m_watchVariables.end(), name) == m_watchVariables.end()) {
    m_watchVariables.push_back(name);
  }
  const int varIdx = static_cast<int>(std::distance(
      m_watchVariables.begin(), std::find(m_watchVariables.begin(), m_watchVariables.end(), name)));

  // Issue `30<varIdx>-var-create - * <name>` to every active GDB rank slot.
  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto& rp = m_processes[i];
    const bool active = rp && ((rp->fifoInFd >= 0) || (rp->process && rp->process->state() == QProcess::Running));
    if (active && !m_varNameMap[static_cast<int>(i)].contains(name)) {
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
    // Ensure any lingering attached inferior process is reaped immediately.
    if (rp->inferiorPid > 0) { ::kill(rp->inferiorPid, SIGKILL); rp->inferiorPid = 0; }
    // Tear down standalone `QProcess` GDB frontend without writing to closed pipes.
    if (rp->process) {
      rp->process->disconnect();
      if (rp->process->state() == QProcess::Running && rp->process->isOpen() && rp->process->isWritable()) {
        rp->process->write("-gdb-exit\n");
      }
      if (rp->process->state() != QProcess::NotRunning && !rp->process->waitForFinished(300)) {
        rp->process->kill();
        rp->process->waitForFinished(300);
      }
    }
  }
  // Clear rank process slots and reset per-rank GDB variable-object lookup tables.
  m_processes.clear();
  m_varNameMap.clear();
  m_varNameRevMap.clear();

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

  // Helper configuring initial GDB settings, breakpoints, and `-exec-run` on rank `id`.
  const auto bootstrapRankGdb = [this](int id) {
    // Disable interactive paging/prompts and ignore benign MPI transport SIGPIPE signals.
    writeCmd(id, "-gdb-set pagination off\n");
    writeCmd(id, "-gdb-set confirm off\n");
    writeCmd(id, "-interpreter-exec console \"handle SIGPIPE nostop noprint pass\"\n");
    writeCmd(id, "-gdb-set max-value-size unlimited\n");
    // Push explicit UI breakpoints matching `rankFilter`, falling back to persisted TOML breakpoints.
    if (!m_pendingBreakpoints.empty()) {
      for (const auto& bp : m_pendingBreakpoints) {
        if (bp.rankFilter < 0 || bp.rankFilter == id) writeCmd(id, buildBreakInsertCmd(bp.file, bp.line, bp.condition));
      }
    } else {
      const auto persisted = core::ConfigManager::instance().getBreakpoints(m_currentBinaryPath);
      for (auto it = persisted.constBegin(); it != persisted.constEnd(); ++it) {
        for (int ln : *it) writeCmd(id, buildBreakInsertCmd(it.key(), ln, QString()));
      }
    }
    // Start inferior execution immediately so user breakpoints trap at line granularity.
    m_processes[id]->state.currentState = "running";
    m_processes[id]->state.executionTimer.start();
    writeCmd(id, "-exec-run\n");
  };

  // Fallback for single-process debugging when no MPI launcher is installed on the host.
  if (mpiBin.isEmpty()) {
    auto rp = std::make_unique<RankProcess>();
    rp->id = 0;
    rp->process = std::make_unique<QProcess>();
    rp->state.rankId = 0;
    rp->state.currentState = "launching";
    rp->state.lastFiredTimestamp = "synced";
    connect(rp->process.get(), &QProcess::readyReadStandardOutput, this, [this]() { handleGdbOutput(0); });
    // Disable debuginfod network fetches so headless attach never stalls on symbol servers.
    rp->process->start(gdbPath, {QStringLiteral("--quiet"), QStringLiteral("--nx"), QStringLiteral("-iex"),
                                 QStringLiteral("set debuginfod enabled off"), QStringLiteral("--interpreter=mi3"), executable});
    m_processes.push_back(std::move(rp));
    bootstrapRankGdb(0);
    return;
  }

  // Create a private `/tmp` directory housing per-rank input/output named pipes (`mkfifo`).
  static std::atomic<int> s_sessionSeq{1};
  m_fifoTempDir = QStringLiteral("/tmp/gridlock_mi_%1_%2").arg(QCoreApplication::applicationPid()).arg(s_sessionSeq.fetch_add(1));
  QDir().mkpath(m_fifoTempDir);

  // Assemble global `mpirun` flags (oversubscription, hostfile, node affinity, exported env vars).
  QStringList mpiArgs;
  for (const QString& tok : QProcess::splitCommand(customMpiArgs)) mpiArgs << tok;
  if (!mpiArgs.contains(QStringLiteral("--oversubscribe")) && mpiBin.contains(QStringLiteral("openmpi"))) {
    mpiArgs << QStringLiteral("--oversubscribe");
  }
  if (!hs.hostsFile.isEmpty()) mpiArgs << QStringLiteral("--hostfile") << hs.hostsFile;
  if (hs.strictAffinity) mpiArgs << QStringLiteral("--map-by") << QStringLiteral("node");
  if (!hs.envVars.isEmpty()) {
    for (const QString& pair : hs.envVars.split('\n', Qt::SkipEmptyParts)) {
      if (!pair.trimmed().isEmpty()) mpiArgs << QStringLiteral("-x") << pair.trimmed();
    }
  }

  // Allocate non-blocking FIFO pairs and construct MPMD rank launch clauses (`-np 1 ... : -np 1 ...`).
  for (int i = 0; i < rankCount; ++i) {
    const QString inFifo = QStringLiteral("%1/r%2.in").arg(m_fifoTempDir).arg(i);
    const QString outFifo = QStringLiteral("%1/r%2.out").arg(m_fifoTempDir).arg(i);
    ::mkfifo(inFifo.toUtf8().constData(), 0600);
    ::mkfifo(outFifo.toUtf8().constData(), 0600);
    // Opening `O_RDWR | O_NONBLOCK` prevents blocking and suppresses premature EOF before child attach.
    const int inFd = ::open(inFifo.toUtf8().constData(), O_RDWR | O_NONBLOCK);
    const int outFd = ::open(outFifo.toUtf8().constData(), O_RDWR | O_NONBLOCK);

    auto rp = std::make_unique<RankProcess>();
    rp->id = i;
    rp->fifoInFd = inFd;
    rp->fifoOutFd = outFd;
    rp->state.rankId = i;
    rp->state.currentState = "launching";
    rp->state.lastFiredTimestamp = "synced";
    // Attach event-driven QSocketNotifier to stream GDB/MI3 stdout lines without polling threads.
    if (outFd >= 0) {
      rp->fifoNotifier = new QSocketNotifier(outFd, QSocketNotifier::Read, this);
      connect(rp->fifoNotifier, &QSocketNotifier::activated, this, [this, id = i]() { handleGdbOutput(id); });
    }
    m_processes.push_back(std::move(rp));

    // Append colon-delimited MPMD application context binding rank `i`'s stdio to its FIFO pair.
    if (i > 0) mpiArgs << QStringLiteral(":");
    mpiArgs << QStringLiteral("-np") << QStringLiteral("1") << QStringLiteral("/bin/sh") << QStringLiteral("-c")
            << QStringLiteral("exec \"$1\" --quiet --nx -iex \"set debuginfod enabled off\" --interpreter=mi3 \"$2\" <\"$3\" >\"$4\" 2>&1")
            << QStringLiteral("sh") << gdbPath << executable << inFifo << outFifo;
  }

  // Configure OpenMPI/MPICH runtime environment (`PATH` and `LD_LIBRARY_PATH`) and start `mpirun`.
  m_mpirunProcess = std::make_unique<QProcess>();
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("PATH"), searchDirs.join(QLatin1Char(':')) + QLatin1Char(':') + env.value(QStringLiteral("PATH")));
  env.insert(QStringLiteral("LD_LIBRARY_PATH"), QStringLiteral("/usr/lib64/openmpi/lib:/usr/lib/openmpi/lib:") + env.value(QStringLiteral("LD_LIBRARY_PATH")));
  m_mpirunProcess->setProcessEnvironment(env);
  connect(m_mpirunProcess.get(), &QProcess::readyReadStandardOutput, this, [this]() {
    emit targetOutputReceived("stdout", QString::fromUtf8(m_mpirunProcess->readAllStandardOutput()));
  });
  connect(m_mpirunProcess.get(), &QProcess::readyReadStandardError, this, [this]() {
    emit targetOutputReceived("stderr", QString::fromUtf8(m_mpirunProcess->readAllStandardError()));
  });
  m_mpirunProcess->start(mpiBin, mpiArgs);

  // Queue initial MI setup, user breakpoints, and `-exec-run` into each rank's input FIFO.
  for (int i = 0; i < rankCount; ++i) {
    bootstrapRankGdb(i);
    emit rankStateChanged(i, m_processes[i]->state);
  }
}

/**
 * @brief Populates `m_processes` with in-memory mock rank slots for unit testing without `QProcess`.
 */
void GdbRankCoordinator::initializeMockSession(int rankCount, bool simulateInitialSync) {
  terminateAllSessions();
  m_watchVariables.clear();
  for (int i = 0; i < rankCount; ++i) {
    auto rp = std::make_unique<RankProcess>();
    rp->id = i;
    rp->state.rankId = i;
    rp->state.currentState = "running";
    rp->state.currentLine = 0;
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
  if (isSet) {
    setBreakpoint(file, line, condition, -1);
    return;
  }
  m_pendingBreakpoints.erase(
      std::remove_if(m_pendingBreakpoints.begin(), m_pendingBreakpoints.end(),
                     [&](const PendingBreakpoint& bp) { return bp.file == file && bp.line == line; }),
      m_pendingBreakpoints.end());

  const QString locStr = QString("%1:%2").arg(file).arg(line);
  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto& rp = m_processes[i];
    if (!rp) continue;
    int bkptIdToDelete = -1;
    for (auto it = rp->state.breakpoints.constBegin(); it != rp->state.breakpoints.constEnd(); ++it) {
      if (it.value() == locStr) { bkptIdToDelete = it.key(); break; }
    }
    if (bkptIdToDelete != -1) {
      writeCmd(static_cast<int>(i), QString("-break-delete %1\n").arg(bkptIdToDelete));
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

/**
 * @brief Steps into callees (`-exec-step`) on @p threadId (`rankId + 1`).
 */
void GdbRankCoordinator::stepInto(int threadId) {
  if (m_processes.empty()) { stepAll(); return; }
  const int rankId = std::clamp(threadId - 1, 0, static_cast<int>(m_processes.size()) - 1);
  const auto& st = m_processes[rankId]->state.currentState;
  if (st != "running" && st != "exited") writeCmd(rankId, "-exec-step\n");
}

/**
 * @brief Steps a single machine instruction (`-exec-step-instruction`) on @p threadId (`rankId + 1`).
 */
void GdbRankCoordinator::stepInstruction(int threadId) {
  if (m_processes.empty()) return;
  const int rankId = std::clamp(threadId - 1, 0, static_cast<int>(m_processes.size()) - 1);
  const auto& st = m_processes[rankId]->state.currentState;
  if (st != "running" && st != "exited") writeCmd(rankId, "-exec-step-instruction\n");
}

/**
 * @brief Resumes execution across all paused ranks (`-exec-continue`).
 */
void GdbRankCoordinator::continueAll() {
  for (size_t i = 0; i < m_processes.size(); ++i) {
    const auto& st = m_processes[i]->state.currentState;
    if (st != "running" && st != "exited") writeCmd(static_cast<int>(i), "-exec-continue\n");
  }
}

/**
 * @brief Starts execution from the beginning on all ranks (`-exec-run`).
 */
void GdbRankCoordinator::runAll() { broadcastCommand("-exec-run\n"); }

/**
 * @brief Interrupts execution across all running ranks (`SIGINT` + `-exec-interrupt`).
 */
void GdbRankCoordinator::haltAll() {
  for (size_t i = 0; i < m_processes.size(); ++i) pauseFocusedRank(static_cast<int>(i));
}

/**
 * @brief Interrupts execution on a single rank @p rankId via `SIGINT` and `-exec-interrupt`.
 */
void GdbRankCoordinator::pauseFocusedRank(int rankId) {
  if (rankId >= 0 && rankId < static_cast<int>(m_processes.size()) && m_processes[rankId] &&
      m_processes[rankId]->inferiorPid > 0 && m_processes[rankId]->state.currentState == "running") {
    ::kill(m_processes[rankId]->inferiorPid, SIGINT);
  }
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
  if (!rp->state.currentFile.isEmpty() && rp->state.currentLine > 0) {
    writeCmd(rankId, QString("200-data-disassemble -f %1 -l %2 -n 30 -- 0\n")
                         .arg(rp->state.currentFile).arg(rp->state.currentLine));
  } else {
    writeCmd(rankId, "200-data-disassemble -s $pc -e \"$pc + 64\" -- 0\n");
  }
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

/**
 * @brief Sends `101-data-evaluate-expression` for an interactive watch/console expression on @p rankId.
 */
void GdbRankCoordinator::evaluateExpression(int rankId, const QString& expression) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
  auto& rp = m_processes[rankId];
  if (!rp || rp->state.currentState != "stopped") return;
  rp->lastEvalExpression = expression;
  writeCmd(rankId, QString("101-data-evaluate-expression %1\n").arg(escapeGdbMiString(expression)));
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
  int newlineIdx = -1;
  while ((newlineIdx = rp->buffer.indexOf('\n')) != -1) {
    const QString line = rp->buffer.left(newlineIdx).trimmed();
    rp->buffer.remove(0, newlineIdx + 1);
    if (line.isEmpty()) continue;
    // Broadcast raw MI3 record line to GdbConsoleWidget and protocol test listeners.
    emit gdbOutputReceived(rankId, line);

    // Capture live inferior OS PID when GDB launches the target rank process.
    if (line.startsWith("=thread-group-started")) {
      static const QRegularExpression pidRe("pid=\"(\\d+)\"");
      const auto pm = pidRe.match(line);
      if (pm.hasMatch()) rp->inferiorPid = static_cast<pid_t>(pm.captured(1).toInt());
      continue;
    }

    if (line.startsWith("*stopped")) {
      // Handle normal or signal-induced inferior termination without querying dead frames.
      if (line.contains("reason=\"exited") || line.contains("reason=\"exited-normally\"")) {
        rp->state.currentState = "exited";
        rp->inferiorPid = 0;
        emit rankStateChanged(rankId, rp->state);
        continue;
      }

      // Accumulate elapsed wall-clock runtime across running intervals for rack telemetry.
      rp->state.currentState = "stopped";
      if (rp->state.executionTimer.isValid()) {
        rp->state.totalRuntimeMs += rp->state.executionTimer.elapsed();
        rp->state.executionTimer.invalidate();
      }

      // Extract source file, line number, function symbol, and frame PC from the MI stop record.
      static const QRegularExpression rxFile("fullname=\"([^\"]+)\"");
      static const QRegularExpression rxLine("frame=\\{.*?line=\"(\\d+)\"");
      static const QRegularExpression rxFunc("frame=\\{[^}]*func=\"([^\"]+)\"");
      static const QRegularExpression rxAddr("frame=\\{[^}]*addr=\"([^\"]+)\"");

      const auto matchFile = rxFile.match(line);
      const QString file = matchFile.hasMatch() ? matchFile.captured(1) : rp->state.currentFile;
      const auto matchLine = rxLine.match(line);
      int lineNum = matchLine.hasMatch() ? matchLine.captured(1).toInt() : (rp->state.currentLine > 0 ? rp->state.currentLine : -1);
      rp->state.currentFile = file;
      rp->state.currentLine = lineNum;
      // Record active symbol name for MPI Diagnostics and Call-Stack displays.
      const auto matchFunc = rxFunc.match(line);
      if (matchFunc.hasMatch()) rp->state.currentFunction = matchFunc.captured(1);

      // Seed x86_64 instruction pointer slot 16 ($rip) immediately from frame stop address.
      const auto matchAddr = rxAddr.match(line);
      const QString stopAddress = matchAddr.hasMatch() ? matchAddr.captured(1) : QStringLiteral("$pc");
      if (matchAddr.hasMatch()) rp->state.registers[16] = stopAddress;

      // Request live disassembly, CPU registers, and frame local variables on stop.
      writeCmd(rankId, QString("200-data-disassemble -s %1 -e \"%1 + 64\" -- 0\n").arg(stopAddress));
      writeCmd(rankId, "-data-list-register-values r\n");
      writeCmd(rankId, "201-stack-list-variables --simple-values\n");

      // Select stopped thread explicitly so subsequent variable queries inspect the active frame.
      static const QRegularExpression threadRe("thread-id=\"(\\d+)\"");
      const auto matchThread = threadRe.match(line);
      writeCmd(rankId, matchThread.hasMatch() ? QString("-thread-select %1\n").arg(matchThread.captured(1)) : QStringLiteral("-thread-info\n"));

      // Handle initial attach synchronization if `lastFiredTimestamp` was not pre-synced.
      if (rp->state.lastFiredTimestamp.isEmpty()) {
        rp->state.lastFiredTimestamp = "synced";
        flushCachedBreakpoints(rankId);
        continue;
      }

      // Preserve auto-continue on unmapped breakpoints while allowing explicit steps/signals to stop.
      const bool isStepOrSignal = line.contains("reason=\"end-stepping-range\"") || line.contains("reason=\"signal-received\"");
      if ((rp->state.currentFile.isEmpty() || rp->state.currentFile == "??") && !isStepOrSignal) {
        writeCmd(rankId, "-exec-continue\n");
        rp->state.currentState = "running";
      } else {
        // Materialize any newly registered watch variable objects in the stopped frame scope.
        for (int varIdx = 0; varIdx < static_cast<int>(m_watchVariables.size()); ++varIdx) {
          const QString& varName = m_watchVariables[varIdx];
          if (!m_varNameMap[rankId].contains(varName)) writeCmd(rankId, QString("30%1-var-create - * %2\n").arg(varIdx).arg(varName));
        }
        emit rankStateChanged(rankId, rp->state);
        if (rp->state.currentLine > 0) emit locationChanged(rankId, rp->state.currentFile, rp->state.currentLine);
      }
    } else if (line.startsWith("*running")) {
      // Transition rank slot to running state and restart its execution stopwatch.
      rp->state.currentState = "running";
      rp->state.executionTimer.start();
      emit rankStateChanged(rankId, rp->state);
    } else if (line.startsWith("30")) {
      // Token `30<varIdx>`: Watch variable `-var-create` response binding user name <-> GDB varobj ID.
      if (line.contains("^done")) {
        static const QRegularExpression evalRe("30(\\d+)\\^done,name=\"([^\"]+)\".*?value=\"([^\"]+)\"");
        const auto match = evalRe.match(line);
        if (match.hasMatch()) {
          const int varIdx = match.captured(1).toInt();
          if (varIdx >= 0 && varIdx < static_cast<int>(m_watchVariables.size())) {
            const QString& varName = m_watchVariables[varIdx];
            const QString varId = match.captured(2);
            m_varNameMap[rankId][varName] = varId;
            m_varNameRevMap[rankId][varId] = varName;
            rp->state.variableWatches[varName] = match.captured(3);
            emit rankStateChanged(rankId, rp->state);
          }
        }
      } else if (line.contains("^error")) {
        // Mark watch variable `<Out of Scope>` when expression cannot be resolved in active frame.
        static const QRegularExpression errRe("30(\\d+)\\^error");
        const auto match = errRe.match(line);
        if (match.hasMatch()) {
          const int varIdx = match.captured(1).toInt();
          if (varIdx >= 0 && varIdx < static_cast<int>(m_watchVariables.size())) {
            const QString& varName = m_watchVariables[varIdx];
            rp->state.variableWatches[varName] = "<Out of Scope>";
            m_varNameMap[rankId].remove(varName);
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
      auto it = objRe.globalMatch(line);
      bool updated = false;
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
          m_varNameMap[rankId].remove(varName);
          m_varNameRevMap[rankId].remove(varId);
          updated = true;
        }
      }
      if (updated) emit rankStateChanged(rankId, rp->state);
    } else if (line.startsWith("^done,memory=[")) {
      // Decode hexadecimal payload returned by `-data-read-memory-bytes` for MemView hex/diff panes.
      static const QRegularExpression memRe("begin=\"([^\"]+)\".*?contents=\"([^\"]+)\"");
      const auto match = memRe.match(line);
      if (match.hasMatch()) {
        bool ok = false;
        const qint64 beginAddress = match.captured(1).toULongLong(&ok, 16);
        const QString hex = match.captured(2);
        emit memoryDataReady(rankId, beginAddress, hex);
        emit memoryRead(rankId, match.captured(1), QByteArray::fromHex(hex.toLatin1()));
      }
    } else if (line.startsWith("^done,register-values=[")) {
      // Populate `rp->state.registers` and broadcast `registersUpdated` + `rankStateChanged`.
      static const QRegularExpression regRe("number=\"(\\d+)\".*?value=\"([^\"]+)\"");
      auto it = regRe.globalMatch(line);
      QJsonArray jsonRegs;
      bool updated = false;
      while (it.hasNext()) {
        const auto m = it.next();
        const int regNum = m.captured(1).toInt();
        const QString regVal = m.captured(2);
        rp->state.registers[regNum] = regVal;
        const QString name = x86RegName(regNum);
        if (!name.isEmpty()) jsonRegs.append(QJsonObject{{"name", name}, {"value", regVal}});
        updated = true;
      }
      if (updated) {
        if (!jsonRegs.isEmpty()) emit registersUpdated(rankId, jsonRegs);
        emit rankStateChanged(rankId, rp->state);
      }
    } else if (line.startsWith("^done,bkpt={")) {
      // Record GDB breakpoint number -> `file:line` mapping and release `sync_flushing` barrier.
      static const QRegularExpression bkptRe("number=\"(\\d+)\".*?file=\"([^\"]+)\".*?line=\"(\\d+)\"");
      const auto match = bkptRe.match(line);
      if (match.hasMatch()) {
        rp->state.breakpoints[match.captured(1).toInt()] = QString("%1:%2").arg(match.captured(2), match.captured(3));
        if (rp->state.currentState == "sync_flushing") {
          if (--rp->state.pendingBreakpoints <= 0) { writeCmd(rankId, "-exec-continue\n"); rp->state.currentState = "running"; }
        } else {
          emit rankStateChanged(rankId, rp->state);
        }
      }
    } else if (line.startsWith("100")) {
      // Token 100: Editor hover variable evaluation response (`-data-evaluate-expression`).
      static const QRegularExpression val100Re("100\\^done,value=\"([^\"]+)\"");
      static const QRegularExpression err100Re("msg=\"([^\"]+)\"");
      const auto match = line.contains("^done") ? val100Re.match(line) : err100Re.match(line);
      if (match.hasMatch()) emit hoverEvaluationComplete(rp->lastHoverVarName, match.captured(1), rp->lastHoverPos);
    } else if (line.startsWith("101")) {
      // Token 101: Interactive console / watch expression evaluation response.
      if (line.contains("^done")) {
        static const QRegularExpression valRe("101\\^done,value=\"([^\"]+)\"");
        const auto m = valRe.match(line);
        if (m.hasMatch()) emit expressionEvaluated(rankId, rp->lastEvalExpression, m.captured(1));
      } else if (line.contains("^error")) {
        static const QRegularExpression errRe("msg=\"([^\"]+)\"");
        const auto m = errRe.match(line);
        if (m.hasMatch()) emit expressionEvaluated(rankId, rp->lastEvalExpression, "Error: " + m.captured(1));
      }
    } else if (line.startsWith("200")) {
      // Token 200: Stop-point disassembly window (`-data-disassemble`) followed by `-var-update`.
      if (line.contains("asm_insns=")) {
        const QString prettyAsm = formatMiDisassembly(line);
        if (!prettyAsm.isEmpty()) { rp->state.disassemblyText = prettyAsm; emit rankStateChanged(rankId, rp->state); }
      }
      writeCmd(rankId, "-var-update 1 *\n");
    } else if (line.startsWith("201^done,variables=[")) {
      // Token 201: Parse stack-frame locals (`name`, `type`, `value`) for `VariablesDockWidget`.
      static const QRegularExpression varTupleRe("\\{([^{}]+)\\}");
      static const QRegularExpression vNameRe("name=\"([^\"]+)\"");
      static const QRegularExpression vTypeRe("type=\"([^\"]+)\"");
      static const QRegularExpression vValRe("value=\"((?:\\\\.|[^\"])*)\"");
      QJsonArray locals;
      auto it = varTupleRe.globalMatch(line);
      while (it.hasNext()) {
        const QString tuple = it.next().captured(1);
        const auto nm = vNameRe.match(tuple);
        if (!nm.hasMatch()) continue;
        const auto tm = vTypeRe.match(tuple);
        const auto vm = vValRe.match(tuple);
        locals.append(QJsonObject{{"name", nm.captured(1)},
                                  {"type", tm.hasMatch() ? tm.captured(1) : QStringLiteral("compound")},
                                  {"value", vm.hasMatch() ? vm.captured(1) : QStringLiteral("{...}")},
                                  {"variablesReference", 0}});
      }
      emit localsUpdated(rankId, locals);
    } else if (line.contains("asm_insns=")) {
      // Untokenized fallback `-data-disassemble` response handler.
      const QString prettyAsm = formatMiDisassembly(line);
      if (!prettyAsm.isEmpty()) { rp->state.disassemblyText = prettyAsm; emit rankStateChanged(rankId, rp->state); }
    }
  }
}

}  // namespace gridlock
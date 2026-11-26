/**
 * @file GdbRankCoordinator.cpp
 * @brief Multi-rank GDB/MI3 process lifecycle, attach, FIFO/QProcess transport, and session teardown.
 */

#include "GdbRankCoordinator.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QHash>
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

GdbRankCoordinator::GdbRankCoordinator(QObject* parent) : IBackendCoordinator(parent) {}
GdbRankCoordinator::~GdbRankCoordinator() { m_shuttingDown = true; QSignalBlocker b(this); stopAll(true); }
void GdbRankCoordinator::launchParallelSession(const QString& executable, int explicitRankCount) {
  launchParallelSession(executable, explicitRankCount, core::ConfigManager::instance().getMpiArgs());
}

namespace {
const QStringList& defaultDebuggerSearchDirs() {
  static const QStringList kDirs = {QStringLiteral("/opt/rocm/bin"), QStringLiteral("/usr/local/cuda/bin"),
                                    QStringLiteral("/usr/lib64/openmpi/bin"), QStringLiteral("/usr/lib/openmpi/bin"),
                                    QStringLiteral("/usr/lib64/mpich/bin"), QStringLiteral("/usr/lib/mpich/bin"),
                                    QStringLiteral("/usr/local/bin"), QStringLiteral("/usr/bin")};
  return kDirs;
}

QString resolveCachedToolPath(const QString& rawName, const QStringList& fallbackDirs) {
  const QString name = rawName.trimmed();
  if (name.isEmpty()) return {};
  if (name.contains(QLatin1Char('/'))) {
    const QFileInfo fi(name);
    return (fi.exists() && fi.isFile() && fi.isExecutable()) ? fi.absoluteFilePath() : QString();
  }
  static QHash<QString, QString> s_cache;
  const QString key = name + QLatin1Char('|') + qEnvironmentVariable("PATH");
  if (const QString hit = s_cache.value(key); !hit.isEmpty() && QFileInfo(hit).isExecutable()) return hit;
  QString found = QStandardPaths::findExecutable(name);
  if (found.isEmpty() && !fallbackDirs.isEmpty()) found = QStandardPaths::findExecutable(name, fallbackDirs);
  if (!found.isEmpty()) s_cache.insert(key, found);
  return found;
}

struct ResolvedGdbBinary { QString executablePath, extraLdLibraryPath, pythonHome, errorReason; };

bool probeRocgdbVersion(const QString& binPath, const QString& extraLdLib, const QString& pyHome) {
  QProcess proc;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  if (!extraLdLib.isEmpty()) {
    const QString curLd = env.value(QStringLiteral("LD_LIBRARY_PATH"));
    env.insert(QStringLiteral("LD_LIBRARY_PATH"), curLd.isEmpty() ? extraLdLib : (extraLdLib + QLatin1Char(':') + curLd));
  }
  if (!pyHome.isEmpty()) env.insert(QStringLiteral("PYTHONHOME"), pyHome);
  proc.setProcessEnvironment(env);
  proc.start(binPath, {QStringLiteral("--version")});
  if (!proc.waitForFinished(1500) || proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) return false;
  const QString out = QString::fromUtf8(proc.readAllStandardOutput()) + QString::fromUtf8(proc.readAllStandardError());
  return out.contains(QStringLiteral("GNU gdb")) && !out.contains(QStringLiteral("No working ROCgdb")) &&
         !out.contains(QStringLiteral("Python initialization failed"));
}

QStringList collectCandidatePythonPrefixes() {
  QStringList prefixes{QStringLiteral("/usr"), QStringLiteral("/usr/local")};
  const QString home = QDir::homePath();
  auto appendSubdirs = [&](const QString& root, const QStringList& filters = {}) {
    QDir d(root);
    if (!d.exists()) return;
    const auto entries = filters.isEmpty() ? d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)
                                           : d.entryInfoList(filters, QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : entries) prefixes.append(fi.absoluteFilePath());
  };
  appendSubdirs(home + QStringLiteral("/.local/share/uv/python"));
  appendSubdirs(home + QStringLiteral("/.conda/envs"));
  appendSubdirs(home + QStringLiteral("/.conda/pkgs"), {QStringLiteral("python-3.*")});
  for (const QString& dist : {QStringLiteral("/miniforge3"), QStringLiteral("/miniconda3"), QStringLiteral("/anaconda3")})
    if (QDir(home + dist).exists()) { prefixes.append(home + dist); appendSubdirs(home + dist + QStringLiteral("/envs")); }
  appendSubdirs(home + QStringLiteral("/.pyenv/versions"));
  if (QDir visitDir(QStringLiteral("/usr/local/visit")); visitDir.exists()) {
    for (const QFileInfo& ver : visitDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
      for (const QFileInfo& arch : QDir(ver.absoluteFilePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        prefixes.append(arch.absoluteFilePath());
  }
  prefixes.removeDuplicates();
  return prefixes;
}

ResolvedGdbBinary resolveEffectiveGdbLaunch(const QString& explicitOverride = QString()) {
  const auto& cfg = core::ConfigManager::instance();
  const QString engine = cfg.debuggerEngine().trimmed().toLower();
  const QString cfgGdb = explicitOverride.trimmed().isEmpty() ? cfg.getGdbPath().trimmed() : explicitOverride.trimmed();
  const bool isDefaultToken = cfgGdb.isEmpty() || cfgGdb == QStringLiteral("gdb") ||
                              cfgGdb == QStringLiteral("cuda-gdb") || cfgGdb == QStringLiteral("rocgdb");
  QString targetName = cfgGdb.isEmpty() ? QStringLiteral("gdb") : cfgGdb;
  if (explicitOverride.trimmed().isEmpty() && isDefaultToken && (engine == QStringLiteral("rocgdb") || engine == QStringLiteral("cuda-gdb")))
    targetName = engine;
  const QString rawBin = resolveCachedToolPath(targetName, isDefaultToken ? defaultDebuggerSearchDirs() : QStringList{});
  if (rawBin.isEmpty()) return {QString(), QString(), QString(), QStringLiteral("Debugger executable ('%1') was not found in PATH.").arg(targetName)};
  const QFileInfo rawInfo(rawBin);
  const QString canonicalBin = rawInfo.canonicalFilePath().isEmpty() ? rawBin : rawInfo.canonicalFilePath();
  if (!QFileInfo(canonicalBin).fileName().startsWith(QStringLiteral("rocgdb"))) return {rawBin, QString(), QString(), QString()};

  static QHash<QString, ResolvedGdbBinary> s_rocgdbCache;
  const QString cacheKey = canonicalBin + QLatin1Char('|') + qEnvironmentVariable("LD_LIBRARY_PATH") + QLatin1Char('|') + qEnvironmentVariable("PYTHONHOME");
  if (s_rocgdbCache.contains(cacheKey)) return s_rocgdbCache.value(cacheKey);

  const QString binDir = QFileInfo(canonicalBin).absolutePath();
  QStringList rocmLibList{QStringLiteral("/usr/lib64"), QStringLiteral("/lib64")};
  for (const QString& p : {QFileInfo(binDir + QStringLiteral("/../lib")).canonicalFilePath(), QStringLiteral("/opt/rocm/lib"), QStringLiteral("/opt/rocm/lib64")})
    if (!p.isEmpty() && QDir(p).exists() && !rocmLibList.contains(p)) rocmLibList.append(p);
  const QString baseRocmLd = rocmLibList.join(QLatin1Char(':'));

  if (QFileInfo(canonicalBin).fileName() == QStringLiteral("rocgdb")) {
    QFileInfoList pyVariants = QDir(binDir).entryInfoList({QStringLiteral("rocgdb-py_3.*")}, QDir::Files | QDir::Executable);
    std::sort(pyVariants.begin(), pyVariants.end(), [](const QFileInfo& a, const QFileInfo& b) {
      return a.fileName().section(QLatin1Char('.'), -1).toInt() > b.fileName().section(QLatin1Char('.'), -1).toInt();
    });
    for (const QFileInfo& cand : pyVariants) if (probeRocgdbVersion(cand.absoluteFilePath(), baseRocmLd, QString())) {
      const ResolvedGdbBinary ok{cand.absoluteFilePath(), baseRocmLd, QString(), QString()};
      s_rocgdbCache.insert(cacheKey, ok); return ok;
    }
    if (!pyVariants.isEmpty()) {
      const QStringList prefixes = collectCandidatePythonPrefixes();
      for (const QFileInfo& cand : pyVariants) {
        const QString pyVer = cand.fileName().mid(QStringLiteral("rocgdb-py_").size());
        for (const QString& pref : prefixes) {
          if (!QDir(pref + QStringLiteral("/lib/python") + pyVer + QStringLiteral("/encodings")).exists()) continue;
          for (const QString& libSub : {QStringLiteral("/lib"), QStringLiteral("/lib64")}) {
            const QString libDir = pref + libSub;
            if (!QDir(libDir).entryList({QStringLiteral("libpython") + pyVer + QStringLiteral(".so*")}, QDir::Files).isEmpty()) {
              const QString fullLd = baseRocmLd + QLatin1Char(':') + libDir;
              if (probeRocgdbVersion(cand.absoluteFilePath(), fullLd, pref)) {
                const ResolvedGdbBinary ok{cand.absoluteFilePath(), fullLd, pref, QString()};
                s_rocgdbCache.insert(cacheKey, ok); return ok;
              }
            }
          }
        }
      }
    }
  }
  if (probeRocgdbVersion(canonicalBin, baseRocmLd, QString())) {
    const ResolvedGdbBinary ok{canonicalBin, baseRocmLd, QString(), QString()};
    s_rocgdbCache.insert(cacheKey, ok); return ok;
  }
  const ResolvedGdbBinary fail{QString(), QString(), QString(), QStringLiteral("ROCgdb ('%1') could not initialize a compatible Python 3 runtime.").arg(canonicalBin)};
  s_rocgdbCache.insert(cacheKey, fail);
  return fail;
}

void applyResolvedGdbEnv(QProcessEnvironment* env, const ResolvedGdbBinary& resolved) {
  if (!env) return;
  if (!resolved.extraLdLibraryPath.isEmpty()) {
    const QString cur = env->value(QStringLiteral("LD_LIBRARY_PATH"));
    env->insert(QStringLiteral("LD_LIBRARY_PATH"), cur.isEmpty() ? resolved.extraLdLibraryPath : (resolved.extraLdLibraryPath + QLatin1Char(':') + cur));
  }
  if (!resolved.pythonHome.isEmpty()) env->insert(QStringLiteral("PYTHONHOME"), resolved.pythonHome);
}
}  // namespace

bool GdbRankCoordinator::validateLaunchEnvironment(int ranks, QString* outError) const {
  const QStringList& searchDirs = defaultDebuggerSearchDirs();
  const ResolvedGdbBinary resolved = resolveEffectiveGdbLaunch();
  if (resolved.executablePath.isEmpty()) { if (outError) *outError = resolved.errorReason; return false; }
  const QString prefMpi = core::ConfigManager::instance().getDebuggerSettings().mpiExecutable.trimmed();
  if (!prefMpi.isEmpty() && prefMpi != QStringLiteral("mpirun") && prefMpi != QStringLiteral("mpiexec") && resolveCachedToolPath(prefMpi, searchDirs).isEmpty()) {
    if (outError) *outError = QStringLiteral("Configured MPI launcher ('%1') was not found.").arg(prefMpi);
    return false;
  }
  if (ranks > 1 && resolveCachedToolPath(prefMpi, searchDirs).isEmpty() && resolveCachedToolPath(QStringLiteral("mpirun"), searchDirs).isEmpty() &&
      resolveCachedToolPath(QStringLiteral("mpiexec"), searchDirs).isEmpty()) {
    if (outError) *outError = QStringLiteral("Multi-rank MPI launch (-np %1) requires mpirun or mpiexec in PATH.").arg(ranks);
    return false;
  }
  return true;
}

bool GdbRankCoordinator::attachToProcess(qint64 pid) { return attachToPids({pid}); }

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

bool GdbRankCoordinator::attachToPids(const QList<qint64>& pids) {
  if (pids.isEmpty()) return false;
  for (qint64 pid : pids) if (pid <= 0 || !QFileInfo::exists(QStringLiteral("/proc/%1/status").arg(pid))) return false;
  terminateAllSessions();
  const ResolvedGdbBinary resolved = resolveEffectiveGdbLaunch();
  if (resolved.executablePath.isEmpty()) return false;
  QProcessEnvironment procEnv = QProcessEnvironment::systemEnvironment();
  applyResolvedGdbEnv(&procEnv, resolved);
  m_currentBinaryPath = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pids.first())).symLinkTarget();
  for (int r = 0; r < pids.size(); ++r) {
    const qint64 pid = pids[r];
    auto rp = std::make_unique<RankProcess>();
    rp->id = r; rp->process = std::make_unique<QProcess>(); rp->inferiorPid = static_cast<pid_t>(pid);
    rp->pendingAttach = true; rp->isAttached = true; rp->state.rankId = r; rp->state.currentState = "launching"; rp->state.lastFiredTimestamp = "synced";
    rp->process->setProcessEnvironment(procEnv); m_rankPids[r] = pid;
    connect(rp->process.get(), &QProcess::readyReadStandardOutput, this, [this, r]() { handleGdbOutput(r); });
    QStringList args{QStringLiteral("--quiet"), QStringLiteral("--nx"), QStringLiteral("-iex"), QStringLiteral("set debuginfod enabled off"), QStringLiteral("--interpreter=mi3")};
    if (!m_currentBinaryPath.isEmpty() && QFileInfo::exists(m_currentBinaryPath)) args.append(m_currentBinaryPath);
    rp->process->start(resolved.executablePath, args);
    const bool started = rp->process->waitForStarted(2000);
    m_processes.push_back(std::move(rp));
    if (!started) { terminateAllSessions(); return false; }
    writeCmd(r, "-gdb-set pagination off\n"); writeCmd(r, "-gdb-set confirm off\n");
    writeCmd(r, QStringLiteral("-target-attach %1\n").arg(pid)); writeCmd(r, "-list-thread-groups\n");
    emit rankStateChanged(r, m_processes[r]->state);
  }
  return true;
}

bool GdbRankCoordinator::attachToRemoteTargets(const QList<RemoteTargetSpec>& targets) {
  if (targets.isEmpty()) return false;
  for (const RemoteTargetSpec& t : targets) if (t.endpoint.trimmed().isEmpty()) return false;
  terminateAllSessions();
  m_currentBinaryPath = targets.first().localSymbolBinary.trimmed();
  for (int r = 0; r < targets.size(); ++r) {
    const RemoteTargetSpec& t = targets[r];
    QString gdbPath = t.gdbBinary.trimmed().isEmpty() ? core::ConfigManager::instance().getGdbPath().trimmed() : t.gdbBinary.trimmed();
    if (!QFileInfo::exists(gdbPath)) gdbPath = QStandardPaths::findExecutable(gdbPath);
    if (gdbPath.isEmpty()) gdbPath = QStandardPaths::findExecutable(QStringLiteral("gdb-multiarch"));
    if (gdbPath.isEmpty()) gdbPath = QStandardPaths::findExecutable(QStringLiteral("gdb"));
    if (gdbPath.isEmpty()) { terminateAllSessions(); return false; }
    auto rp = std::make_unique<RankProcess>();
    rp->id = r; rp->process = std::make_unique<QProcess>(); rp->pendingAttach = true; rp->isAttached = true; rp->isRemoteAttach = true;
    rp->state.rankId = r; rp->state.currentState = QStringLiteral("launching"); rp->state.lastFiredTimestamp = QStringLiteral("synced");
    connect(rp->process.get(), &QProcess::readyReadStandardOutput, this, [this, r]() { handleGdbOutput(r); });
    rp->process->start(gdbPath, {QStringLiteral("--quiet"), QStringLiteral("--nx"), QStringLiteral("-iex"), QStringLiteral("set debuginfod enabled off"), QStringLiteral("--interpreter=mi3")});
    const bool started = rp->process->waitForStarted(2000);
    m_processes.push_back(std::move(rp));
    if (!started) { terminateAllSessions(); return false; }
    writeCmd(r, "-gdb-set pagination off\n"); writeCmd(r, "-gdb-set confirm off\n");
    if (const QString symBin = t.localSymbolBinary.trimmed(); !symBin.isEmpty())
      writeCmd(r, QStringLiteral("-file-exec-and-symbols %1\n").arg(detail::escapeGdbMiString(symBin)));
    writeCmd(r, QStringLiteral("-target-select %1 %2\n").arg(t.extendedRemote ? QStringLiteral("extended-remote") : QStringLiteral("remote"), t.endpoint.trimmed()));
    for (const auto& bp : m_pendingBreakpoints) {
      if (bp.rankFilter < 0 || bp.rankFilter == r)
        writeCmd(r, detail::buildBreakInsertCmd(bp.file, bp.line, bp.condition, bp.ignoreCount, bp.temporary, !bp.enabled));
    }
    if (!m_currentBinaryPath.isEmpty()) {
      const auto cachedBps = core::ConfigManager::instance().getBreakpoints(m_currentBinaryPath);
      for (auto it = cachedBps.constBegin(); it != cachedBps.constEnd(); ++it) {
        for (int line : *it) writeCmd(r, detail::buildBreakInsertCmd(it.key(), line, QString()));
      }
    }
    emit rankStateChanged(r, m_processes[r]->state);
  }
  return true;
}

void GdbRankCoordinator::queueOrWriteCmd(int rankId, const QString& cmd) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size()) || !m_processes[rankId]) return;
  auto& rp = m_processes[rankId];
  if (rp->state.currentState == QLatin1String("exited")) return;
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

void GdbRankCoordinator::writeCmd(int rankId, const QString& cmd) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size()) || !m_processes[rankId]) return;
  auto& rp = m_processes[rankId];
  const QString trimmedCmd = cmd.trimmed();
  if (rp->state.currentState == QLatin1String("exited") &&
      !trimmedCmd.startsWith(QLatin1String("-gdb-exit")) &&
      !trimmedCmd.startsWith(QLatin1String("-target-detach"))) {
    return;
  }
  static const QRegularExpression kInspectMiRe(QStringLiteral("^\\d*-(?:stack|thread|var|data)-"));
  if (rp->state.currentState != QLatin1String("stopped") && kInspectMiRe.match(trimmedCmd).hasMatch()) return;
  if (rp->fifoInFd >= 0) {
    const QByteArray bytes = cmd.toUtf8();
    const char* ptr = bytes.constData();
    size_t remaining = static_cast<size_t>(bytes.size());
    while (remaining > 0u) {
      const ssize_t n = ::write(rp->fifoInFd, ptr, remaining);
      if (n > 0) { ptr += n; remaining -= static_cast<size_t>(n); continue; }
      if (n < 0 && errno == EINTR) continue;
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        struct pollfd pfd{rp->fifoInFd, POLLOUT, 0};
        if (::poll(&pfd, 1, 2000) > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) == 0) continue;
      }
      break;
    }
    if (remaining == 0u && !bytes.isEmpty()) emit commandSentToGdb(rankId, cmd.trimmed());
    return;
  }
  if (rp->process) {
    if (rp->process->state() == QProcess::Starting) rp->process->waitForStarted(1000);
    if (rp->process->state() == QProcess::Running && rp->process->isOpen() && rp->process->isWritable()) {
      rp->process->write(cmd.toUtf8()); emit commandSentToGdb(rankId, cmd.trimmed());
    }
  } else {
    emit commandSentToGdb(rankId, cmd.trimmed());
  }
}

void GdbRankCoordinator::flushCachedBreakpoints(int rankId) {
  const auto bps = core::ConfigManager::instance().getBreakpoints(m_currentBinaryPath);
  auto& rp = m_processes[rankId];
  int count = 0;
  for (auto it = bps.constBegin(); it != bps.constEnd(); ++it)
    for (int line : *it) { writeCmd(rankId, detail::buildBreakInsertCmd(it.key(), line, QString())); count++; }
  if (count == 0) { writeCmd(rankId, "-exec-continue\n"); rp->state.currentState = "running"; }
  else { rp->state.pendingBreakpoints = count; rp->state.currentState = "sync_flushing"; }
}

void GdbRankCoordinator::stopAll(bool silentShutdown) {
  const bool prevShuttingDown = m_shuttingDown;
  m_shuttingDown = true;
  QVector<pid_t> spawnedPids;
  if (m_mpirunProcess) m_mpirunProcess->disconnect();
  for (auto& rp : m_processes) {
    if (!rp) continue;
    const bool wasFifo = (rp->fifoInFd >= 0 || rp->fifoOutFd >= 0), wasAttached = rp->isAttached || rp->pendingAttach;
    if (rp->fifoNotifier) { rp->fifoNotifier->setEnabled(false); rp->fifoNotifier->disconnect(); delete rp->fifoNotifier; rp->fifoNotifier = nullptr; }
    if (rp->process) rp->process->disconnect();
    if (!wasAttached && rp->inferiorPid > 0) { spawnedPids.append(rp->inferiorPid); ::kill(rp->inferiorPid, SIGINT); }
    if (rp->fifoInFd >= 0) { [[maybe_unused]] const ssize_t n = ::write(rp->fifoInFd, "-gdb-exit\n", 10); ::close(rp->fifoInFd); rp->fifoInFd = -1; }
    if (rp->fifoOutFd >= 0) { ::close(rp->fifoOutFd); rp->fifoOutFd = -1; }
    if (wasFifo && !wasAttached && rp->inferiorPid > 0) ::kill(rp->inferiorPid, SIGTERM);
    if (rp->process) {
      if (rp->process->state() == QProcess::Starting) rp->process->waitForStarted(250);
      if (rp->process->state() == QProcess::Running && rp->process->isOpen() && rp->process->isWritable())
        rp->process->write(wasAttached ? "-target-detach\n-gdb-exit\n" : "-gdb-exit\n");
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
  m_rankArch.clear(); m_activeLaneIds.clear(); m_pendingPluginMemReads.clear(); m_pendingPluginMemWrites.clear(); m_pendingPluginEvals.clear();
  m_pendingConsoleCmdCount.clear(); m_consoleReplyBuffer.clear(); m_watchpointIds.clear();
  if (m_mpirunProcess) {
    if (m_mpirunProcess->state() == QProcess::Starting) m_mpirunProcess->waitForStarted(250);
    if (m_mpirunProcess->state() != QProcess::NotRunning && !m_mpirunProcess->waitForFinished(250)) {
      m_mpirunProcess->terminate();
      if (!m_mpirunProcess->waitForFinished(250)) { m_mpirunProcess->kill(); m_mpirunProcess->waitForFinished(250); }
    }
    m_mpirunProcess.reset();
  }
  for (pid_t pid : spawnedPids) if (pid > 0 && ::kill(pid, 0) == 0) ::kill(pid, SIGKILL);
  if (!m_fifoTempDir.isEmpty()) { QDir(m_fifoTempDir).removeRecursively(); m_fifoTempDir.clear(); }
  m_currentBinaryPath.clear();
  if (!silentShutdown) {
    m_shuttingDown = prevShuttingDown;
    if (hadActiveSession) { emit stackTraceUpdated(0, {}); emit threadsUpdated(0, {}); }
  }
}

void GdbRankCoordinator::launchParallelSession(const QString& executable, int explicitRankCount, const QString& customMpiArgs) {
  terminateAllSessions();
  if (executable.trimmed().isEmpty()) return;
  const auto ds = core::ConfigManager::instance().getDebuggerSettings();
  const auto hs = core::ConfigManager::instance().getHpcSettings();
  const int rankCount = std::max(1, (explicitRankCount > 0) ? explicitRankCount : ds.defaultRanks);
  m_currentBinaryPath = executable;

  const QStringList& searchDirs = defaultDebuggerSearchDirs();
  const ResolvedGdbBinary resolvedGdb = resolveEffectiveGdbLaunch();
  const QString gdbPath = resolvedGdb.executablePath;
  if (gdbPath.isEmpty()) { qWarning() << "[GDB]" << resolvedGdb.errorReason; emit sessionErrorOccurred(resolvedGdb.errorReason); return; }

  const QString preferredMpi = ds.mpiExecutable.trimmed().isEmpty() ? QStringLiteral("mpirun") : ds.mpiExecutable.trimmed();
  QString mpiBin = resolveCachedToolPath(preferredMpi, searchDirs);
  if (mpiBin.isEmpty()) mpiBin = resolveCachedToolPath(QStringLiteral("mpirun"), searchDirs);
  if (mpiBin.isEmpty()) mpiBin = resolveCachedToolPath(QStringLiteral("mpiexec"), searchDirs);

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
  applyResolvedGdbEnv(&env, resolvedGdb);
  for (const QString& pair : hs.envVars.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
    if (const int eq = pair.indexOf(QLatin1Char('=')); eq > 0) env.insert(pair.left(eq).trimmed(), pair.mid(eq + 1).trimmed());

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
    if (const QString memTrackSo = qEnvironmentVariable("GRIDLOCK_MEMTRACK_SO"); !memTrackSo.isEmpty() && QFileInfo::exists(memTrackSo))
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
    QStringList gdbArgs = {QStringLiteral("--quiet"), QStringLiteral("--nx"), QStringLiteral("-iex"), QStringLiteral("set debuginfod enabled off"), QStringLiteral("--interpreter=mi3")};
    if (!splitExecArgs.isEmpty()) gdbArgs << QStringLiteral("--args") << executable << splitExecArgs; else gdbArgs << executable;
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
    if (!splitExecArgs.isEmpty()) mpiArgs << QStringLiteral("--args") << executable << splitExecArgs; else mpiArgs << executable;
  }
  m_mpirunProcess = std::make_unique<QProcess>();
  if (!workDir.isEmpty() && QFileInfo(workDir).isDir()) m_mpirunProcess->setWorkingDirectory(workDir);
  m_mpirunProcess->setProcessEnvironment(env);
  connect(m_mpirunProcess.get(), &QProcess::readyReadStandardOutput, this, [this]() { emit targetOutputReceived("stdout", QString::fromUtf8(m_mpirunProcess->readAllStandardOutput())); });
  connect(m_mpirunProcess.get(), &QProcess::readyReadStandardError, this, [this]() { emit targetOutputReceived("stderr", QString::fromUtf8(m_mpirunProcess->readAllStandardError())); });
  m_mpirunProcess->start(mpiBin, mpiArgs);
  for (int i = 0; i < rankCount; ++i) { bootstrapRankGdb(i); emit rankStateChanged(i, m_processes[i]->state); }
}

void GdbRankCoordinator::handleGdbOutput(int rankId) {
  if (m_shuttingDown || rankId < 0 || rankId >= static_cast<int>(m_processes.size()) || !m_processes[rankId]) return;
  auto& rp = m_processes[rankId];
  if (rp->fifoOutFd >= 0) {
    QByteArray acc; char buf[8192];
    while (!m_shuttingDown) { const ssize_t n = ::read(rp->fifoOutFd, buf, sizeof(buf)); if (n <= 0) break; acc.append(buf, static_cast<qsizetype>(n)); }
    if (!m_shuttingDown && !acc.isEmpty()) processGdbOutput(rankId, QString::fromUtf8(acc));
    return;
  }
  if (!m_shuttingDown && rp->process) processGdbOutput(rankId, QString::fromUtf8(rp->process->readAllStandardOutput()));
}

}  // namespace gridlock

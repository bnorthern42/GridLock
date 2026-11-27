/**
 * @file LocalSlurmBackend.cpp
 * @brief Implementation of real Slurm submission, queue polling, node discovery, SSH port forwarding, and scancel cleanup.
 */

#include "LocalSlurmBackend.hpp"

#include "../../managers/ConfigManager.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

namespace gridlock::core {

namespace {

QString escapeShellArg(const QString& raw) {
  QString out;
  out.reserve(raw.size() + 8);
  for (QChar ch : raw) {
    if (ch == QLatin1Char('\\') || ch == QLatin1Char('"') ||
        ch == QLatin1Char('$') || ch == QLatin1Char('`')) {
      out.append(QLatin1Char('\\'));
    }
    out.append(ch);
  }
  return QStringLiteral("\"%1\"").arg(out);
}

int parseJobIdFromSbatchStdout(const QString& stdoutText) {
  const QString trimmed = stdoutText.trimmed();
  static const QRegularExpression parsableRx(QStringLiteral("^(\\d+)(?:;.*)?$"),
                                             QRegularExpression::MultilineOption);
  if (const auto m = parsableRx.match(trimmed); m.hasMatch()) return m.captured(1).toInt();
  static const QRegularExpression bannerRx(QStringLiteral("(?:Submitted batch job\\s+)?(\\d+)"));
  if (const auto m = bannerRx.match(trimmed); m.hasMatch()) return m.captured(1).toInt();
  return -1;
}

bool isPendingSlurmState(const QString& state) {
  return state == QLatin1String("PENDING") || state == QLatin1String("PD") ||
         state == QLatin1String("CONFIGURING") || state == QLatin1String("CF") ||
         state == QLatin1String("RESV_DEL_HOLD") || state == QLatin1String("REQUEUE_HOLD") ||
         state == QLatin1String("REQUEUED") || state == QLatin1String("STAGE_OUT");
}

bool isRunningSlurmState(const QString& state) {
  return state == QLatin1String("RUNNING") || state == QLatin1String("R");
}

bool isTerminalFailureSlurmState(const QString& state) {
  return state == QLatin1String("FAILED") || state == QLatin1String("F") ||
         state == QLatin1String("CANCELLED") || state == QLatin1String("CA") ||
         state == QLatin1String("TIMEOUT") || state == QLatin1String("TO") ||
         state == QLatin1String("NODE_FAIL") || state == QLatin1String("NF") ||
         state == QLatin1String("OUT_OF_MEMORY") || state == QLatin1String("OOM") ||
         state == QLatin1String("BOOT_FAIL") || state == QLatin1String("BF") ||
         state == QLatin1String("PREEMPTED") || state == QLatin1String("PR") ||
         state == QLatin1String("DEADLINE") || state == QLatin1String("DL") ||
         state == QLatin1String("COMPLETED") || state == QLatin1String("CD") ||
         state == QLatin1String("COMPLETING") || state == QLatin1String("CG");
}

}  // namespace

LocalSlurmBackend::LocalSlurmBackend(QObject* parent)
    : HpcBackend(parent), m_pollTimer(new QTimer(this)) {
  m_pollTimer->setInterval(m_pollIntervalMs);
  connect(m_pollTimer, &QTimer::timeout, this, &LocalSlurmBackend::pollActiveJobState);
}

LocalSlurmBackend::~LocalSlurmBackend() {
  if (m_pollTimer) m_pollTimer->stop();
  stopSshTunnel();
  for (QProcess* childProc : findChildren<QProcess*>()) {
    if (childProc && childProc->state() != QProcess::NotRunning) {
      if (!childProc->waitForFinished(250)) {
        childProc->kill();
        childProc->waitForFinished(100);
      }
    }
  }
}

void LocalSlurmBackend::setPollIntervalMs(int intervalMs) {
  m_pollIntervalMs = qMax(10, intervalMs);
  if (m_pollTimer) m_pollTimer->setInterval(m_pollIntervalMs);
}

void LocalSlurmBackend::setBasePort(int basePort) {
  m_basePort = (basePort > 0 && basePort < 65000) ? basePort : 2345;
}

void LocalSlurmBackend::setStagingDirectory(const QString& dirPath) {
  m_stagingDirOverride = dirPath.trimmed();
}

QString LocalSlurmBackend::resolveSlurmExecutable(const QString& configuredPath,
                                                  const QString& defaultName,
                                                  const QString& siblingDirHint) {
  const QString cand = configuredPath.trimmed().isEmpty() ? defaultName : configuredPath.trimmed();
  if (cand.contains(QLatin1Char('/'))) {
    const QFileInfo fi(cand);
    return (fi.exists() && fi.isFile() && fi.isExecutable()) ? fi.absoluteFilePath() : QString();
  }
  if (!siblingDirHint.trimmed().isEmpty()) {
    const QFileInfo sib(QDir(siblingDirHint).filePath(cand));
    if (sib.exists() && sib.isFile() && sib.isExecutable()) return sib.absoluteFilePath();
  }
  return QStandardPaths::findExecutable(cand);
}

int LocalSlurmBackend::inferTotalRanksFromScript(const QString& scriptText,
                                                 int fallbackNodes,
                                                 int fallbackTasksPerNode) {
  int nodes = qMax(1, fallbackNodes);
  int explicitNodes = -1;
  int explicitNtasks = -1;
  int explicitTasksPerNode = -1;

  static const QRegularExpression rxLong(QStringLiteral("^\\s*#SBATCH\\s+--([a-zA-Z0-9_-]+)(?:=|\\s+)(\\S+)"));
  static const QRegularExpression rxShort(QStringLiteral("^\\s*#SBATCH\\s+-([Nn])\\s*=?(?:\\s*)(\\d+)"));
  for (const QString& raw : scriptText.split(QLatin1Char('\n'))) {
    const QString ln = raw.trimmed();
    if (const auto m = rxLong.match(ln); m.hasMatch()) {
      const QString key = m.captured(1).toLower();
      const int v = m.captured(2).toInt();
      if (v > 0) {
        if (key == QLatin1String("nodes")) explicitNodes = v;
        else if (key == QLatin1String("ntasks")) explicitNtasks = v;
        else if (key == QLatin1String("ntasks-per-node")) explicitTasksPerNode = v;
      }
      continue;
    }
    if (const auto m = rxShort.match(ln); m.hasMatch()) {
      const QString f = m.captured(1);
      const int v = m.captured(2).toInt();
      if (v > 0) {
        if (f == QLatin1String("N")) explicitNodes = v;
        else if (f == QLatin1String("n")) explicitNtasks = v;
      }
    }
  }
  if (explicitNodes > 0) nodes = explicitNodes;
  if (explicitNtasks > 0) return explicitNtasks;
  if (explicitTasksPerNode > 0) return nodes * explicitTasksPerNode;
  return nodes * qMax(1, fallbackTasksPerNode);
}

QStringList LocalSlurmBackend::buildComputeRankEndpoints(const QStringList& hostnames,
                                                         int totalRanks,
                                                         int basePort) {
  const int ranks = qMax(1, totalRanks);
  const QStringList hosts = hostnames.isEmpty() ? QStringList{QStringLiteral("localhost")} : hostnames;
  const int hostCount = hosts.size();
  const int ranksPerNode = qMax(1, (ranks + hostCount - 1) / hostCount);
  QStringList out;
  out.reserve(ranks);
  for (int r = 0; r < ranks; ++r) {
    const int nodeIdx = qMin(hostCount - 1, r / ranksPerNode);
    out.append(QStringLiteral("%1:%2").arg(hosts[nodeIdx].trimmed()).arg(basePort + r));
  }
  return out;
}

QString LocalSlurmBackend::resolveSharedStagingDirectory(const QString& workingDir,
                                                         const QString& targetBinary) const {
  if (!m_stagingDirOverride.isEmpty()) {
    QDir().mkpath(m_stagingDirOverride);
    return m_stagingDirOverride;
  }
  QStringList bases;
  if (!workingDir.trimmed().isEmpty()) bases << workingDir.trimmed();
  if (const QString ws = ConfigManager::instance().workspacePath().trimmed(); !ws.isEmpty()) bases << ws;
  if (!targetBinary.trimmed().isEmpty()) bases << QFileInfo(targetBinary).absolutePath();
  if (const QString testCfg = qEnvironmentVariable("GRIDLOCK_TEST_CONFIG_DIR").trimmed(); !testCfg.isEmpty()) bases << testCfg;
  bases << QDir::currentPath();

  for (const QString& base : bases) {
    if (base.isEmpty()) continue;
    const QString dirPath = QDir(base).filePath(QStringLiteral(".gridlock/slurm_staging"));
    if (QDir().mkpath(dirPath)) return dirPath;
  }
  const QString fallback = QDir(QDir::homePath()).filePath(QStringLiteral(".gridlock/slurm_staging"));
  QDir().mkpath(fallback);
  return fallback;
}

QString LocalSlurmBackend::buildStagedBatchScript(const QString& rawScript,
                                                  const QString& wrapperPath,
                                                  const QString& targetBinary,
                                                  const QString& targetArgs,
                                                  const QString& stagingDir) const {
  QString base = rawScript;
  if (base.trimmed().isEmpty()) {
    const auto sl = ConfigManager::instance().getSlurmSettings();
    base = QStringLiteral("#!/bin/bash\n#SBATCH --job-name=%1\n#SBATCH --partition=%2\n#SBATCH --nodes=%3\n#SBATCH --ntasks-per-node=%4\n%5 \"{FILE}\"\n")
               .arg(sl.jobName.isEmpty() ? QStringLiteral("gridlock_debug") : sl.jobName,
                    sl.partition.isEmpty() ? QStringLiteral("batch") : sl.partition)
               .arg(qMax(1, sl.nodes))
               .arg(qMax(1, sl.tasksPerNode))
               .arg(sl.mpiLauncher.trimmed().isEmpty() ? QStringLiteral("srun") : sl.mpiLauncher.trimmed());
  }

  QString wrappedCmd = escapeShellArg(wrapperPath);
  if (!targetBinary.trimmed().isEmpty()) wrappedCmd += QLatin1Char(' ') + escapeShellArg(targetBinary.trimmed());
  if (!targetArgs.trimmed().isEmpty()) wrappedCmd += QLatin1Char(' ') + targetArgs.trimmed();

  bool replacedPlaceholder = false;
  for (const QString& tok : {QStringLiteral("\"{FILE}\""), QStringLiteral("\"%%FILE%%\""),
                             QStringLiteral("'{FILE}'"), QStringLiteral("'%%FILE%%'"),
                             QStringLiteral("{FILE}"), QStringLiteral("%%FILE%%")}) {
    if (base.contains(tok)) {
      base.replace(tok, wrappedCmd);
      replacedPlaceholder = true;
    }
  }

  QStringList lines = base.split(QLatin1Char('\n'));
  if (lines.isEmpty() || !lines.first().trimmed().startsWith(QStringLiteral("#!"))) {
    lines.prepend(QStringLiteral("#!/bin/bash"));
  }
  int lastHeaderIdx = 0;
  for (int i = 1; i < lines.size(); ++i) {
    const QString t = lines[i].trimmed();
    if (t.startsWith(QStringLiteral("#SBATCH")) || t.isEmpty() || t.startsWith(QLatin1Char('#'))) {
      if (t.startsWith(QStringLiteral("#SBATCH"))) lastHeaderIdx = i;
    } else {
      break;
    }
  }

  const QString nodelistMarkerFile = QDir(stagingDir).filePath(QStringLiteral("gridlock_nodelist_${SLURM_JOB_ID}.txt"));
  const QString envPrelude = QStringLiteral(
      "export GRIDLOCK_GDBSERVER_BASE_PORT=%1\n"
      "if [ -n \"${SLURM_JOB_ID:-}\" ] && [ -n \"${SLURM_NODELIST:-}\" ]; then printf '%s\\n' \"$SLURM_NODELIST\" > \"%2\"; fi")
                                 .arg(m_basePort)
                                 .arg(nodelistMarkerFile);
  lines.insert(lastHeaderIdx + 1, envPrelude);

  if (!replacedPlaceholder && !targetBinary.trimmed().isEmpty()) {
    static const QRegularExpression launchRx(QStringLiteral("^(\\s*(?:srun(?:\\s+--mpi=\\S+)?|mpirun|mpiexec)\\b)(.*)$"));
    bool wrappedExisting = false;
    for (int i = lastHeaderIdx + 2; i < lines.size(); ++i) {
      if (const auto m = launchRx.match(lines[i]); m.hasMatch()) {
        lines[i] = QStringLiteral("%1 %2").arg(m.captured(1), wrappedCmd);
        wrappedExisting = true;
        break;
      }
    }
    if (!wrappedExisting) lines.append(QStringLiteral("srun %1").arg(wrappedCmd));
  }
  return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

void LocalSlurmBackend::fetchPackages() {
  const auto sl = ConfigManager::instance().getSlurmSettings();
  QString spackBin;
  if (!sl.spackRoot.trimmed().isEmpty()) {
    const QFileInfo cand(QDir(sl.spackRoot.trimmed()).filePath(QStringLiteral("bin/spack")));
    if (cand.exists() && cand.isExecutable()) spackBin = cand.absoluteFilePath();
  }
  if (spackBin.isEmpty()) spackBin = QStandardPaths::findExecutable(QStringLiteral("spack"));
  if (spackBin.isEmpty()) {
    HpcResult r;
    r.success = false;
    r.errorOutput = QStringLiteral("Spack executable was not found in '%1/bin/spack' or PATH.").arg(sl.spackRoot);
    emit packagesReady(r);
    return;
  }
  auto* proc = new QProcess(this);
  connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, proc](int code, QProcess::ExitStatus status) {
            HpcResult r;
            r.output = QString::fromUtf8(proc->readAllStandardOutput());
            r.errorOutput = QString::fromUtf8(proc->readAllStandardError());
            r.success = (status == QProcess::NormalExit && code == 0);
            if (!r.success && r.errorOutput.trimmed().isEmpty()) r.errorOutput = r.output;
            proc->deleteLater();
            emit packagesReady(r);
          });
  proc->start(spackBin, {QStringLiteral("find")});
}

void LocalSlurmBackend::submitSlurmJob(const QString& script) {
  submitDebugJob(script, m_stagedTargetBinary);
}

void LocalSlurmBackend::submitDebugJob(const QString& scriptTemplate,
                                       const QString& targetBinary,
                                       const QString& targetArgs,
                                       const QString& workingDir) {
  if (m_pollTimer) m_pollTimer->stop();
  stopSshTunnel();
  m_pollInFlight = false;
  m_emptyPollRetries = 0;
  m_lastReportedState.clear();
  m_resolvedComputeEndpoints.clear();

  const auto sl = ConfigManager::instance().getSlurmSettings();
  const QString sbatchExe = resolveSlurmExecutable(sl.sbatchPath, QStringLiteral("sbatch"));
  if (sbatchExe.isEmpty()) {
    const QString configured = sl.sbatchPath.trimmed().isEmpty() ? QStringLiteral("sbatch") : sl.sbatchPath.trimmed();
    const QString err = QStringLiteral(
        "Slurm 'sbatch' executable was not found in PATH or configured path ('%1'). "
        "Run GridLock on a cluster login / Open OnDemand node or set 'sbatch Path' in Preferences -> HPC Integration.")
                            .arg(configured);
    HpcResult r{false, QString(), err};
    emit statusMessage(err, true);
    emit slurmJobSubmitted(r);
    emit jobFailed(-1, err);
    return;
  }

  m_activeSiblingBinDir = QFileInfo(sbatchExe).absolutePath();
  m_activeTargetBinary = targetBinary.trimmed();
  m_expectedRanks = inferTotalRanksFromScript(scriptTemplate, sl.nodes, sl.tasksPerNode);
  m_activeStagingDir = resolveSharedStagingDirectory(workingDir, m_activeTargetBinary);

  m_lastWrapperScriptPath = QDir(m_activeStagingDir).filePath(QStringLiteral("gridlock_gdbserver_wrap.sh"));
  {
    QFile wf(m_lastWrapperScriptPath);
    if (!wf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
      const QString err = QStringLiteral("Failed to write gdbserver wrapper script to shared directory '%1': %2")
                              .arg(m_lastWrapperScriptPath, wf.errorString());
      emit statusMessage(err, true);
      emit slurmJobSubmitted(HpcResult{false, QString(), err});
      emit jobFailed(-1, err);
      return;
    }
    const QByteArray wrapBody =
        "#!/bin/sh\n"
        "RANK=\"${SLURM_PROCID:-${PMI_RANK:-${OMPI_COMM_WORLD_RANK:-${MV2_COMM_WORLD_RANK:-0}}}}\"\n"
        "BASE_PORT=\"${GRIDLOCK_GDBSERVER_BASE_PORT:-2345}\"\n"
        "PORT=$((BASE_PORT + RANK))\n"
        "exec gdbserver --multi \":${PORT}\" \"$@\"\n";
    wf.write(wrapBody);
    wf.close();
    QFile::setPermissions(m_lastWrapperScriptPath,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                              QFileDevice::ReadGroup | QFileDevice::ExeGroup);
  }

  m_lastStagedScriptPath = QDir(m_activeStagingDir)
                               .filePath(QStringLiteral("gridlock_job_%1.sbatch").arg(QDateTime::currentMSecsSinceEpoch()));
  {
    QFile sf(m_lastStagedScriptPath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
      const QString err = QStringLiteral("Failed to write staged Slurm script '%1': %2")
                              .arg(m_lastStagedScriptPath, sf.errorString());
      emit statusMessage(err, true);
      emit slurmJobSubmitted(HpcResult{false, QString(), err});
      emit jobFailed(-1, err);
      return;
    }
    const QString rendered = buildStagedBatchScript(
        scriptTemplate, m_lastWrapperScriptPath, m_activeTargetBinary, targetArgs, m_activeStagingDir);
    sf.write(rendered.toUtf8());
    sf.close();
    QFile::setPermissions(m_lastStagedScriptPath,
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                              QFileDevice::ReadGroup | QFileDevice::ExeGroup);
  }

  auto* proc = new QProcess(this);
  if (!workingDir.trimmed().isEmpty() && QFileInfo(workingDir.trimmed()).isDir()) {
    proc->setWorkingDirectory(workingDir.trimmed());
  }
  connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
    if (proc->property("gl_handled").toBool()) return;
    proc->setProperty("gl_handled", true);
    const QString err = QStringLiteral("Failed to execute sbatch: %1").arg(proc->errorString());
    proc->deleteLater();
    emit statusMessage(err, true);
    emit slurmJobSubmitted(HpcResult{false, QString(), err});
    emit jobFailed(-1, err);
  });
  connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, proc](int exitCode, QProcess::ExitStatus exitStatus) {
            if (proc->property("gl_handled").toBool()) return;
            proc->setProperty("gl_handled", true);
            const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
            const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            proc->deleteLater();

            if (exitStatus != QProcess::NormalExit || exitCode != 0) {
              const QString msg = err.isEmpty()
                                      ? QStringLiteral("sbatch exited with code %1: %2").arg(exitCode).arg(out)
                                      : err;
              emit statusMessage(msg, true);
              emit slurmJobSubmitted(HpcResult{false, out, msg});
              emit jobFailed(-1, msg);
              return;
            }

            const int jobId = parseJobIdFromSbatchStdout(out);
            if (jobId <= 0) {
              const QString msg = QStringLiteral("Unable to parse numeric SLURM_JOB_ID from sbatch output: '%1'").arg(out);
              emit statusMessage(msg, true);
              emit slurmJobSubmitted(HpcResult{false, out, msg});
              emit jobFailed(-1, msg);
              return;
            }

            m_activeJobId = jobId;
            m_lastReportedState = QStringLiteral("SUBMITTED");
            const QString okMsg = QStringLiteral("Submitted Slurm batch job %1 (%2 rank%3); polling queue state...")
                                      .arg(jobId)
                                      .arg(m_expectedRanks)
                                      .arg(m_expectedRanks == 1 ? QString() : QStringLiteral("s"));
            emit statusMessage(okMsg, false);
            emit slurmJobSubmitted(HpcResult{true, okMsg, QString()});
            emit jobStateChanged(jobId, m_lastReportedState);
            m_pollTimer->start();
            QTimer::singleShot(qMin(50, m_pollIntervalMs), this, &LocalSlurmBackend::pollActiveJobState);
          });
  proc->start(sbatchExe, {QStringLiteral("--parsable"), m_lastStagedScriptPath});
}

void LocalSlurmBackend::pollActiveJobState() {
  if (m_activeJobId <= 0 || m_pollInFlight) return;
  const auto sl = ConfigManager::instance().getSlurmSettings();
  const QString squeueExe = resolveSlurmExecutable(sl.squeuePath, QStringLiteral("squeue"), m_activeSiblingBinDir);
  if (squeueExe.isEmpty()) {
    m_pollTimer->stop();
    const int failedId = m_activeJobId;
    m_activeJobId = -1;
    const QString err = QStringLiteral("Slurm 'squeue' executable was not found in PATH or '%1'.").arg(sl.squeuePath);
    emit statusMessage(err, true);
    emit jobFailed(failedId, err);
    return;
  }

  m_pollInFlight = true;
  const int polledJobId = m_activeJobId;
  auto* proc = new QProcess(this);
  connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, proc, polledJobId](int exitCode, QProcess::ExitStatus exitStatus) {
            m_pollInFlight = false;
            const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
            const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            proc->deleteLater();
            if (m_activeJobId != polledJobId) return;

            if (exitStatus != QProcess::NormalExit || exitCode != 0) {
              m_pollTimer->stop();
              m_activeJobId = -1;
              const QString msg = err.isEmpty() ? QStringLiteral("squeue exited with code %1").arg(exitCode) : err;
              emit statusMessage(msg, true);
              emit jobFailed(polledJobId, msg);
              return;
            }

            QString rawToken = out.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0).toUpper();
            if (rawToken.endsWith(QLatin1Char('+'))) rawToken.chop(1);

            if (rawToken.isEmpty()) {
              if (++m_emptyPollRetries >= 3) {
                m_pollTimer->stop();
                m_activeJobId = -1;
                const QString msg = QStringLiteral("Slurm job %1 exited or disappeared from squeue before attach.").arg(polledJobId);
                emit statusMessage(msg, true);
                emit jobFailed(polledJobId, msg);
              }
              return;
            }
            m_emptyPollRetries = 0;

            if (isPendingSlurmState(rawToken)) {
              if (m_lastReportedState != rawToken) {
                m_lastReportedState = rawToken;
                emit jobStateChanged(polledJobId, rawToken);
                emit statusMessage(QStringLiteral("Slurm job %1 state: %2").arg(polledJobId).arg(rawToken), false);
              }
              return;
            }

            if (isRunningSlurmState(rawToken)) {
              m_pollTimer->stop();
              m_lastReportedState = QStringLiteral("RUNNING");
              emit jobStateChanged(polledJobId, m_lastReportedState);
              emit statusMessage(QStringLiteral("Slurm job %1 is RUNNING; discovering allocated compute nodes...").arg(polledJobId), false);
              resolveRunningJobNodes(polledJobId);
              return;
            }

            if (isTerminalFailureSlurmState(rawToken)) {
              m_pollTimer->stop();
              m_activeJobId = -1;
              m_lastReportedState = rawToken;
              emit jobStateChanged(polledJobId, rawToken);
              const QString msg = QStringLiteral("Slurm job %1 ended in state %2 before debugger attach.").arg(polledJobId).arg(rawToken);
              emit statusMessage(msg, true);
              emit jobFailed(polledJobId, msg);
            }
          });
  proc->start(squeueExe, {QStringLiteral("-j"), QString::number(polledJobId),
                          QStringLiteral("-h"), QStringLiteral("-o"), QStringLiteral("%T")});
}

void LocalSlurmBackend::resolveRunningJobNodes(int jobId) {
  const QString markerPath = QDir(m_activeStagingDir)
                                 .filePath(QStringLiteral("gridlock_nodelist_%1.txt").arg(jobId));
  if (QFile mf(markerPath); mf.open(QIODevice::ReadOnly | QIODevice::Text)) {
    const QString markerExpr = QString::fromUtf8(mf.readAll()).trimmed();
    if (!markerExpr.isEmpty()) {
      runScontrolShowHostnames(jobId, markerExpr);
      return;
    }
  }

  const auto sl = ConfigManager::instance().getSlurmSettings();
  const QString squeueExe = resolveSlurmExecutable(sl.squeuePath, QStringLiteral("squeue"), m_activeSiblingBinDir);
  if (squeueExe.isEmpty()) {
    runScontrolShowHostnames(jobId, QString());
    return;
  }

  auto* proc = new QProcess(this);
  connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, proc, jobId](int exitCode, QProcess::ExitStatus exitStatus) {
            QString nodelist = (exitStatus == QProcess::NormalExit && exitCode == 0)
                                   ? QString::fromUtf8(proc->readAllStandardOutput()).trimmed()
                                   : QString();
            proc->deleteLater();
            if (m_activeJobId != jobId) return;
            const QString upper = nodelist.toUpper();
            if (isRunningSlurmState(upper) || isPendingSlurmState(upper)) nodelist.clear();
            runScontrolShowHostnames(jobId, nodelist);
          });
  proc->start(squeueExe, {QStringLiteral("-j"), QString::number(jobId),
                          QStringLiteral("-h"), QStringLiteral("-o"), QStringLiteral("%N")});
}

void LocalSlurmBackend::runScontrolShowHostnames(int jobId, const QString& nodelistExpr) {
  const auto sl = ConfigManager::instance().getSlurmSettings();
  const QString scontrolExe = resolveSlurmExecutable(sl.scontrolPath, QStringLiteral("scontrol"), m_activeSiblingBinDir);
  if (scontrolExe.isEmpty()) {
    const int failedId = m_activeJobId;
    m_activeJobId = -1;
    const QString err = QStringLiteral("Slurm 'scontrol' executable was not found in PATH or '%1'.").arg(sl.scontrolPath);
    emit statusMessage(err, true);
    emit jobFailed(failedId, err);
    return;
  }

  auto launchHostnames = [this, scontrolExe, jobId](const QString& expr) {
    auto* proc = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (!expr.isEmpty()) env.insert(QStringLiteral("SLURM_NODELIST"), expr);
    proc->setProcessEnvironment(env);
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc, jobId](int exitCode, QProcess::ExitStatus exitStatus) {
              const QString out = QString::fromUtf8(proc->readAllStandardOutput());
              const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
              proc->deleteLater();
              if (m_activeJobId != jobId) return;
              if (exitStatus != QProcess::NormalExit || exitCode != 0) {
                m_activeJobId = -1;
                const QString msg = err.isEmpty()
                                        ? QStringLiteral("scontrol show hostnames exited with code %1").arg(exitCode)
                                        : err;
                emit statusMessage(msg, true);
                emit jobFailed(jobId, msg);
                return;
              }
              QStringList hosts;
              for (const QString& ln : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QString h = ln.trimmed();
                if (!h.isEmpty()) hosts.append(h);
              }
              finalizeAttachTargets(jobId, hosts);
            });
    QStringList args{QStringLiteral("show"), QStringLiteral("hostnames")};
    if (!expr.isEmpty()) args.append(expr);
    proc->start(scontrolExe, args);
  };

  if (!nodelistExpr.trimmed().isEmpty()) {
    launchHostnames(nodelistExpr.trimmed());
    return;
  }

  auto* jobProc = new QProcess(this);
  connect(jobProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, jobProc, jobId, launchHostnames](int, QProcess::ExitStatus) {
            const QString out = QString::fromUtf8(jobProc->readAllStandardOutput());
            jobProc->deleteLater();
            if (m_activeJobId != jobId) return;
            QString extracted;
            static const QRegularExpression nlRx(QStringLiteral("\\bNodeList=(\\S+)"));
            if (const auto m = nlRx.match(out); m.hasMatch() && m.captured(1) != QLatin1String("(null)")) {
              extracted = m.captured(1).trimmed();
            }
            launchHostnames(extracted);
          });
  jobProc->start(scontrolExe, {QStringLiteral("show"), QStringLiteral("job"), QString::number(jobId)});
}

void LocalSlurmBackend::finalizeAttachTargets(int jobId, const QStringList& hostnames) {
  if (hostnames.isEmpty()) {
    m_activeJobId = -1;
    const QString err = QStringLiteral("scontrol show hostnames returned zero compute nodes for job %1.").arg(jobId);
    emit statusMessage(err, true);
    emit jobFailed(jobId, err);
    return;
  }

  m_resolvedComputeEndpoints = buildComputeRankEndpoints(hostnames, m_expectedRanks, m_basePort);
  const auto ssh = ConfigManager::instance().getSshSettings();
  const QString gdbPath = ConfigManager::instance().getGdbPath().trimmed().isEmpty()
                              ? QStringLiteral("gdb")
                              : ConfigManager::instance().getGdbPath().trimmed();

  bool useSshForwards = !ssh.host.trimmed().isEmpty();
  if (useSshForwards) {
    const QString sshExe = resolveSlurmExecutable(QStringLiteral("ssh"), QStringLiteral("ssh"), m_activeSiblingBinDir);
    if (sshExe.isEmpty()) {
      m_activeJobId = -1;
      const QString err = QStringLiteral("SSH Host '%1' is configured, but 'ssh' executable was not found in PATH.").arg(ssh.host);
      emit statusMessage(err, true);
      emit jobFailed(jobId, err);
      return;
    }
    QStringList sshArgs{QStringLiteral("-N"),
                        QStringLiteral("-o"), QStringLiteral("ExitOnForwardFailure=yes"),
                        QStringLiteral("-o"), QStringLiteral("BatchMode=yes")};
    if (!ssh.keyPath.trimmed().isEmpty()) {
      QString kp = ssh.keyPath.trimmed();
      if (kp.startsWith(QStringLiteral("~/"))) kp = QDir::homePath() + kp.mid(1);
      sshArgs << QStringLiteral("-i") << kp;
    }
    for (int r = 0; r < m_resolvedComputeEndpoints.size(); ++r) {
      const int localPort = m_basePort + r;
      sshArgs << QStringLiteral("-L")
              << QStringLiteral("127.0.0.1:%1:%2").arg(localPort).arg(m_resolvedComputeEndpoints[r]);
    }
    const QString targetLogin = ssh.user.trimmed().isEmpty()
                                    ? ssh.host.trimmed()
                                    : QStringLiteral("%1@%2").arg(ssh.user.trimmed(), ssh.host.trimmed());
    sshArgs << targetLogin;

    m_sshTunnelProcess = std::make_unique<QProcess>();
    m_sshTunnelProcess->start(sshExe, sshArgs);
    if (!m_sshTunnelProcess->waitForStarted(2500)) {
      const QString err = QStringLiteral("Failed to start SSH port-forwarding tunnel to '%1': %2")
                              .arg(targetLogin, m_sshTunnelProcess->errorString());
      m_sshTunnelProcess.reset();
      m_activeJobId = -1;
      emit statusMessage(err, true);
      emit jobFailed(jobId, err);
      return;
    }
  }

  QList<gridlock::RemoteTargetSpec> specs;
  specs.reserve(m_resolvedComputeEndpoints.size());
  for (int r = 0; r < m_resolvedComputeEndpoints.size(); ++r) {
    gridlock::RemoteTargetSpec spec;
    spec.endpoint = useSshForwards ? QStringLiteral("127.0.0.1:%1").arg(m_basePort + r)
                                   : m_resolvedComputeEndpoints[r];
    spec.localSymbolBinary = m_activeTargetBinary;
    spec.gdbBinary = gdbPath;
    spec.extendedRemote = true;
    spec.mpiRank = r;
    specs.append(spec);
  }

  emit statusMessage(QStringLiteral("Slurm job %1 nodes resolved (%2); attaching %3 rank%4...")
                         .arg(jobId)
                         .arg(m_resolvedComputeEndpoints.join(QStringLiteral(", ")))
                         .arg(specs.size())
                         .arg(specs.size() == 1 ? QString() : QStringLiteral("s")),
                     false);
  emit attachTargetsReady(jobId, specs, m_resolvedComputeEndpoints);
}

void LocalSlurmBackend::stopSshTunnel() {
  if (!m_sshTunnelProcess) return;
  if (m_sshTunnelProcess->state() != QProcess::NotRunning) {
    m_sshTunnelProcess->terminate();
    if (!m_sshTunnelProcess->waitForFinished(300)) {
      m_sshTunnelProcess->kill();
      m_sshTunnelProcess->waitForFinished(200);
    }
  }
  m_sshTunnelProcess.reset();
}

void LocalSlurmBackend::cancelActiveJob() {
  if (m_pollTimer) m_pollTimer->stop();
  stopSshTunnel();
  if (m_activeJobId > 0) {
    const int id = m_activeJobId;
    m_activeJobId = -1;
    cancelSlurmJob(id);
  }
}

void LocalSlurmBackend::cancelSlurmJob(int jobId) {
  if (jobId <= 0) return;
  if (m_activeJobId == jobId) {
    if (m_pollTimer) m_pollTimer->stop();
    m_activeJobId = -1;
    stopSshTunnel();
  }
  const auto sl = ConfigManager::instance().getSlurmSettings();
  QString sibDir = m_activeSiblingBinDir;
  if (sibDir.isEmpty() && sl.sbatchPath.contains(QLatin1Char('/'))) {
    sibDir = QFileInfo(sl.sbatchPath.trimmed()).absolutePath();
  }
  const QString scancelExe = resolveSlurmExecutable(QStringLiteral("scancel"), QStringLiteral("scancel"), sibDir);
  if (scancelExe.isEmpty()) {
    HpcResult r{false, QString(), QStringLiteral("Slurm 'scancel' executable not found for job %1.").arg(jobId)};
    emit statusMessage(r.errorOutput, true);
    emit commandFinished(QStringLiteral("scancel"), r);
    return;
  }

  auto* proc = new QProcess(this);
  connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
          [this, proc, jobId](int exitCode, QProcess::ExitStatus exitStatus) {
            HpcResult r;
            const QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
            const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            proc->deleteLater();
            r.success = (exitStatus == QProcess::NormalExit && exitCode == 0);
            r.output = out.isEmpty() ? QStringLiteral("Cancelled Slurm job %1 (scancel)").arg(jobId) : out;
            r.errorOutput = err;
            emit statusMessage(r.success ? r.output : (err.isEmpty() ? QStringLiteral("scancel %1 failed").arg(jobId) : err), !r.success);
            emit commandFinished(QStringLiteral("scancel"), r);
          });
  proc->start(scancelExe, {QString::number(jobId)});
}

}  // namespace gridlock::core

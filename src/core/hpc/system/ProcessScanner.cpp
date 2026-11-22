/**
 * @file ProcessScanner.cpp
 * @brief Implementation of Linux `/proc` process discovery, MPI rank grouping, and ptrace_scope checks.
 */

#include "ProcessScanner.hpp"

#include <pwd.h>
#include <unistd.h>
#include <algorithm>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>

namespace gridlock {

namespace {

/** @brief Resolves a POSIX UID to its login username, caching lookups across `/proc` entries. */
QString resolveUserName(uid_t uid, QHash<uid_t, QString>& cache) {
  auto it = cache.find(uid);
  if (it != cache.end()) return it.value();
  const struct passwd* pw = ::getpwuid(uid);
  const QString name = (pw && pw->pw_name)
                           ? QString::fromLocal8Bit(pw->pw_name)
                           : QString::number(uid);
  cache.insert(uid, name);
  return name;
}

/** @brief Parses NUL- or newline-delimited `/proc/[pid]/environ` key=value pairs. */
QMap<QString, QString> parseProcEnviron(const QByteArray& rawBytes) {
  QMap<QString, QString> env;
  if (rawBytes.isEmpty()) return env;
  // Support both standard NUL-delimited procfs environ files and newline-delimited test fixtures.
  const QList<QByteArray> tokens = rawBytes.contains('\0')
                                       ? rawBytes.split('\0')
                                       : rawBytes.split('\n');
  for (const QByteArray& tok : tokens) {
    const int eqIdx = tok.indexOf('=');
    if (eqIdx <= 0) continue;
    const QString key = QString::fromUtf8(tok.left(eqIdx)).trimmed();
    const QString val = QString::fromUtf8(tok.mid(eqIdx + 1)).trimmed();
    if (!key.isEmpty()) env.insert(key, val);
  }
  return env;
}

/** @brief Returns the first non-empty integer environment variable value `>= 0`, or `-1`. */
int firstNonNegativeEnvInt(const QMap<QString, QString>& env,
                           const QStringList& keys) {
  for (const QString& k : keys) {
    auto it = env.find(k);
    if (it == env.end() || it.value().isEmpty()) continue;
    bool ok = false;
    const int v = it.value().toInt(&ok);
    if (ok && v >= 0) return v;
  }
  return -1;
}

}  // namespace

QList<ProcessEntry> ProcessScanner::scanRunningProcesses(const QString& procRoot,
                                                         bool currentUserOnly) {
  QList<ProcessEntry> entries;
  const QDir procDir(procRoot);
  if (!procDir.exists()) return entries;

  const uid_t currentUid = ::geteuid();
  const bool isRealProc = (QFileInfo(procRoot).canonicalFilePath() == QStringLiteral("/proc") ||
                           procRoot == QStringLiteral("/proc"));
  const qint64 selfPid = isRealProc ? QCoreApplication::applicationPid() : -1;
  QHash<uid_t, QString> userCache;

  for (const QString& dirName : procDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    bool pidOk = false;
    const qint64 parsedPid = dirName.toLongLong(&pidOk);
    if (!pidOk || parsedPid <= 0 || parsedPid == selfPid) continue;

    const QString pidDir = procDir.filePath(dirName);
    ProcessEntry entry;
    entry.pid = static_cast<pid_t>(parsedPid);
    entry.uid = currentUid;

    // 1. Read `/proc/[pid]/comm` if present.
    QFile commFile(QDir(pidDir).filePath(QStringLiteral("comm")));
    if (commFile.open(QIODevice::ReadOnly)) {
      entry.name = QString::fromUtf8(commFile.readAll()).trimmed();
    }

    // 2. Read `/proc/[pid]/status` for `Name:`, `PPid:`, and `Uid:`.
    bool hasStatusOrComm = !entry.name.isEmpty();
    QFile statusFile(QDir(pidDir).filePath(QStringLiteral("status")));
    if (statusFile.open(QIODevice::ReadOnly)) {
      const QByteArray statusBytes = statusFile.readAll();
      if (!statusBytes.isEmpty()) hasStatusOrComm = true;
      for (const QByteArray& rawLine : statusBytes.split('\n')) {
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.startsWith(QStringLiteral("Name:")) && entry.name.isEmpty()) {
          entry.name = line.mid(5).trimmed();
        } else if (line.startsWith(QStringLiteral("PPid:"))) {
          entry.ppid = static_cast<pid_t>(line.mid(5).trimmed().toLongLong());
        } else if (line.startsWith(QStringLiteral("Uid:"))) {
          const QStringList parts = line.mid(4).split(
              QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
          if (!parts.isEmpty()) {
            entry.uid = static_cast<uid_t>(parts[0].toUInt());
          }
        }
      }
    }
    if (!hasStatusOrComm) continue;
    if (currentUserOnly && entry.uid != currentUid) continue;
    if (isRealProc && entry.name.compare(QStringLiteral("gridlock"), Qt::CaseInsensitive) == 0) {
      continue;
    }
    entry.user = resolveUserName(entry.uid, userCache);

    // 3. Read `/proc/[pid]/cmdline` (NUL-separated arguments).
    QFile cmdFile(QDir(pidDir).filePath(QStringLiteral("cmdline")));
    QString firstArg;
    if (cmdFile.open(QIODevice::ReadOnly)) {
      QByteArray rawCmd = cmdFile.readAll();
      const QList<QByteArray> argv = rawCmd.split('\0');
      if (!argv.isEmpty()) firstArg = QString::fromUtf8(argv.first()).trimmed();
      for (char& ch : rawCmd) {
        if (ch == '\0') ch = ' ';
      }
      entry.cmdline = QString::fromUtf8(rawCmd).trimmed();
    }
    if (entry.name.isEmpty() && !firstArg.isEmpty()) {
      entry.name = QFileInfo(firstArg).fileName();
    }
    if (entry.cmdline.isEmpty()) {
      entry.cmdline = QStringLiteral("[%1]").arg(entry.name);
    }

    // 4. Resolve `/proc/[pid]/exe` symlink (or fixture file content / argv[0] fallback).
    const QString exeFilePath = QDir(pidDir).filePath(QStringLiteral("exe"));
    const QFileInfo exeInfo(exeFilePath);
    if (exeInfo.isSymLink()) {
      entry.executablePath = exeInfo.symLinkTarget();
    } else if (exeInfo.exists() && exeInfo.isFile()) {
      QFile ef(exeFilePath);
      if (ef.open(QIODevice::ReadOnly)) {
        entry.executablePath = QString::fromUtf8(ef.readAll()).trimmed();
      }
    }
    if (entry.executablePath.isEmpty() && !firstArg.isEmpty() && !firstArg.startsWith(QLatin1Char('['))) {
      entry.executablePath = firstArg;
    }

    // 5. Inspect `/proc/[pid]/environ` for OpenMPI, MPICH/PMI, PMIx, and SLURM rank variables.
    QFile envFile(QDir(pidDir).filePath(QStringLiteral("environ")));
    if (envFile.open(QIODevice::ReadOnly)) {
      const QMap<QString, QString> env = parseProcEnviron(envFile.readAll());
      entry.mpiRank = firstNonNegativeEnvInt(
          env, {QStringLiteral("OMPI_COMM_WORLD_RANK"),
                QStringLiteral("PMI_RANK"),
                QStringLiteral("PMIX_RANK"),
                QStringLiteral("SLURM_PROCID"),
                QStringLiteral("MV2_COMM_WORLD_RANK")});
      entry.mpiWorldSize = firstNonNegativeEnvInt(
          env, {QStringLiteral("OMPI_COMM_WORLD_SIZE"),
                QStringLiteral("PMI_SIZE"),
                QStringLiteral("SLURM_NTASKS"),
                QStringLiteral("MV2_COMM_WORLD_SIZE")});

      if (entry.mpiRank >= 0) {
        QString jobId = env.value(QStringLiteral("OMPI_MCA_ess_base_jobid"));
        if (jobId.isEmpty()) jobId = env.value(QStringLiteral("PMIX_NAMESPACE"));
        if (jobId.isEmpty()) jobId = env.value(QStringLiteral("PMI_JOBID"));
        if (jobId.isEmpty()) jobId = env.value(QStringLiteral("SLURM_STEP_ID"));
        if (jobId.isEmpty()) jobId = env.value(QStringLiteral("SLURM_JOB_ID"));
        if (jobId.isEmpty() && entry.ppid > 0) {
          jobId = QStringLiteral("ppid_%1").arg(entry.ppid);
        }
        const QString binBase = !entry.executablePath.isEmpty()
                                    ? QFileInfo(entry.executablePath).fileName()
                                    : entry.name;
        entry.mpiJobTag = jobId.isEmpty()
                              ? binBase
                              : QStringLiteral("%1 (%2)").arg(binBase, jobId);
      }
    }

    entries.append(entry);
  }

  // Backfill `mpiWorldSize` for any MPI job where only rank IDs were exported in `environ`.
  QHash<QString, int> jobCounts;
  for (const ProcessEntry& e : entries) {
    if (!e.mpiJobTag.isEmpty()) jobCounts[e.mpiJobTag] += 1;
  }
  for (ProcessEntry& e : entries) {
    if (!e.mpiJobTag.isEmpty() && e.mpiWorldSize <= 0) {
      e.mpiWorldSize = jobCounts.value(e.mpiJobTag, 1);
    }
  }

  // Sort MPI processes first (grouped by mpiJobTag, then ascending mpiRank, then PID),
  // followed by standalone processes ordered by newest PID first.
  std::sort(entries.begin(), entries.end(), [](const ProcessEntry& a, const ProcessEntry& b) {
    const bool aMpi = a.isMpiProcess();
    const bool bMpi = b.isMpiProcess();
    if (aMpi != bMpi) return aMpi > bMpi;
    if (aMpi) {
      if (a.mpiJobTag != b.mpiJobTag) return a.mpiJobTag < b.mpiJobTag;
      if (a.mpiRank != b.mpiRank) return a.mpiRank < b.mpiRank;
      return a.pid < b.pid;
    }
    return a.pid > b.pid;
  });

  return entries;
}

int ProcessScanner::checkPtraceScope(const QString& sysctlPath) {
  QFile f(sysctlPath);
  if (!f.exists() || !f.open(QIODevice::ReadOnly)) return 0;
  bool ok = false;
  const int scope = QString::fromUtf8(f.readAll()).trimmed().toInt(&ok);
  return (ok && scope >= 0) ? scope : 0;
}

QList<RemoteTargetSpec> ProcessScanner::expandRemoteEndpoints(
    const QString& specText,
    const QString& localSymbolBinary,
    const QString& gdbBinary,
    bool extendedRemote) {
  QList<RemoteTargetSpec> specs;
  const QString trimmedSpec = specText.trimmed();
  if (trimmedSpec.isEmpty()) return {};

  const QString cleanSym = localSymbolBinary.trimmed();
  const QString cleanGdb = gdbBinary.trimmed().isEmpty()
                               ? QStringLiteral("gdb")
                               : gdbBinary.trimmed();

  const QStringList segments = trimmedSpec.split(QLatin1Char(','));
  for (const QString& rawSeg : segments) {
    const QString seg = rawSeg.trimmed();
    if (seg.isEmpty()) return {};
    const int colonIdx = seg.lastIndexOf(QLatin1Char(':'));
    if (colonIdx < 0) return {};

    QString host = seg.left(colonIdx).trimmed();
    if (host.isEmpty()) host = QStringLiteral("localhost");
    if (host.contains(QRegularExpression(QStringLiteral("\\s")))) return {};

    const QString portPart = seg.mid(colonIdx + 1).trimmed();
    if (portPart.isEmpty()) return {};

    int startPort = -1;
    int endPort = -1;
    if (portPart.contains(QStringLiteral(".."))) {
      const QStringList bounds = portPart.split(QStringLiteral(".."));
      if (bounds.size() != 2) return {};
      bool ok1 = false;
      bool ok2 = false;
      startPort = bounds[0].trimmed().toInt(&ok1);
      endPort = bounds[1].trimmed().toInt(&ok2);
      if (!ok1 || !ok2) return {};
    } else if (portPart.contains(QLatin1Char('-'))) {
      const QStringList bounds = portPart.split(QLatin1Char('-'));
      if (bounds.size() != 2) return {};
      bool ok1 = false;
      bool ok2 = false;
      startPort = bounds[0].trimmed().toInt(&ok1);
      endPort = bounds[1].trimmed().toInt(&ok2);
      if (!ok1 || !ok2) return {};
    } else {
      bool ok = false;
      startPort = portPart.toInt(&ok);
      if (!ok) return {};
      endPort = startPort;
    }

    if (startPort < 1 || startPort > 65535 || endPort < 1 || endPort > 65535 ||
        startPort > endPort || (endPort - startPort) > 1024) {
      return {};
    }

    for (int p = startPort; p <= endPort; ++p) {
      RemoteTargetSpec item;
      item.endpoint = QStringLiteral("%1:%2").arg(host).arg(p);
      item.localSymbolBinary = cleanSym;
      item.gdbBinary = cleanGdb;
      item.extendedRemote = extendedRemote;
      item.mpiRank = specs.size();
      specs.append(item);
    }
  }
  return specs;
}

}  // namespace gridlock

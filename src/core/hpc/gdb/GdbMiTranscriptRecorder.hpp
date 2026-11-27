/**
 * @file GdbMiTranscriptRecorder.hpp
 * @brief Thread-safe bounded ring-buffer recorder and sanitizer for GDB/MI session traces.
 *
 * Captures outgoing (`->`) MI commands and incoming (`<-`) MI responses per rank into a
 * bounded FIFO ring buffer (`4,096` lines / `2 MiB` default) and exports deterministic,
 * privacy-sanitized transcripts ready to drop directly into `GdbMiParser` unit tests.
 */

#pragma once

#include <cstddef>
#include <deque>
#include <mutex>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QSysInfo>

namespace gridlock::core {

/**
 * @class GdbMiTranscriptRecorder
 * @brief Bounded ring buffer and deterministic sanitizer for reproducible GDB/MI bug reports.
 */
class GdbMiTranscriptRecorder {
 public:
  static constexpr size_t kDefaultMaxLines = 4096u;
  static constexpr size_t kDefaultMaxBytes = 2u * 1024u * 1024u;

  /**
   * @struct SanitizeContext
   * @brief Host environment tokens redacted during transcript export.
   */
  struct SanitizeContext {
    QString homeDir;
    QString userName;
    QString hostName;

    static SanitizeContext fromCurrentEnvironment() {
      SanitizeContext ctx;
      ctx.homeDir = QDir::cleanPath(QDir::homePath());
      ctx.userName = qEnvironmentVariable("USER").trimmed();
      if (ctx.userName.isEmpty()) ctx.userName = qEnvironmentVariable("LOGNAME").trimmed();
      ctx.hostName = QSysInfo::machineHostName().trimmed();
      return ctx;
    }
  };

  /**
   * @struct Entry
   * @brief Single timestamped GDB/MI wire event for one MPI rank.
   */
  struct Entry {
    QString timestampIso;
    int rankId = 0;
    bool outgoing = false;
    QString line;
    size_t byteSize = 0u;
  };

  explicit GdbMiTranscriptRecorder(size_t maxLines = kDefaultMaxLines,
                                   size_t maxBytes = kDefaultMaxBytes)
      : m_maxLines(maxLines > 0u ? maxLines : kDefaultMaxLines),
        m_maxBytes(maxBytes > 0u ? maxBytes : kDefaultMaxBytes) {}

  void setMaxLimits(size_t maxLines, size_t maxBytes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (maxLines > 0u) m_maxLines = maxLines;
    if (maxBytes > 0u) m_maxBytes = maxBytes;
    evictLocked();
  }

  void setGridLockVersion(const QString& version) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!version.trimmed().isEmpty()) m_gridlockVersion = version.trimmed();
  }

  void setGdbVersion(const QString& version) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!version.trimmed().isEmpty()) m_gdbVersion = version.trimmed();
  }

  void setMpiLauncherMode(const QString& mode) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!mode.trimmed().isEmpty()) m_mpiLauncherMode = mode.trimmed();
  }

  void clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
    m_totalBytes = 0u;
  }

  size_t entryCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries.size();
  }

  size_t totalBytes() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_totalBytes;
  }

  /**
   * @brief Appends one GDB/MI command (`outgoing == true`) or reply (`outgoing == false`).
   */
  void record(int rankId, bool outgoing, const QString& rawLine) {
    const QString cleaned = rawLine.trimmed();
    if (cleaned.isEmpty()) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    // Capture GDB version banner if observed in console stream output.
    if (!outgoing && m_gdbVersion.isEmpty() && cleaned.contains(QLatin1String("GNU gdb"))) {
      static const QRegularExpression kGdbVerRe(QStringLiteral("(GNU gdb[^\"\\\\\\r\\n]+)"));
      if (const auto m = kGdbVerRe.match(cleaned); m.hasMatch()) {
        m_gdbVersion = m.captured(1).trimmed();
      }
    }

    Entry e;
    e.timestampIso = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    e.rankId = rankId;
    e.outgoing = outgoing;
    e.line = cleaned;
    e.byteSize = static_cast<size_t>(e.line.toUtf8().size()) + 48u;

    // Truncate single pathological lines larger than the entire byte budget.
    if (e.byteSize > m_maxBytes && m_maxBytes > 64u) {
      e.line = e.line.left(static_cast<qsizetype>(m_maxBytes - 64u)) + QStringLiteral("...[truncated]");
      e.byteSize = static_cast<size_t>(e.line.toUtf8().size()) + 48u;
    }

    m_totalBytes += e.byteSize;
    m_entries.push_back(std::move(e));
    evictLocked();
  }

  /**
   * @brief Redacts home directories, usernames, hostnames, and `-gdb-set environment` values while preserving MI syntax.
   */
  static QString sanitizeLine(const QString& rawLine,
                              const SanitizeContext& ctx = SanitizeContext::fromCurrentEnvironment()) {
    QString out = rawLine;

    // 1. Redact environment variable values in `-gdb-set environment KEY=VAL` or `KEY VAL`.
    static const QRegularExpression kMiSetEnvEqRe(
        QStringLiteral("^(\\d*-gdb-set\\s+environment\\s+[A-Za-z_][A-Za-z0-9_]*\\s*=\\s*)(.+)$"));
    if (const auto m = kMiSetEnvEqRe.match(out); m.hasMatch()) {
      out = m.captured(1) + QStringLiteral("<REDACTED>");
    } else {
      static const QRegularExpression kMiSetEnvSpRe(
          QStringLiteral("^(\\d*-gdb-set\\s+environment\\s+[A-Za-z_][A-Za-z0-9_]*\\s+)(.+)$"));
      if (const auto m2 = kMiSetEnvSpRe.match(out); m2.hasMatch()) {
        out = m2.captured(1) + QStringLiteral("<REDACTED>");
      }
    }

    // Also redact console-wrapped `set environment KEY=VAL` or `set environment KEY VAL`.
    static const QRegularExpression kConsoleSetEnvRe(
        QStringLiteral("(set\\s+environment\\s+[A-Za-z_][A-Za-z0-9_]*(?:\\s*=\\s*|\\s+))([^\"\\r\\n]+)"));
    out.replace(kConsoleSetEnvRe, QStringLiteral("\\1<REDACTED>"));

    // 2. Redact explicit home directory prefix first (preserving root prefix like /home/<USER>).
    if (!ctx.homeDir.isEmpty() && ctx.homeDir != QLatin1String("/")) {
      const QString cleanHome = QDir::cleanPath(ctx.homeDir);
      const QString parent = QDir::cleanPath(QFileInfo(cleanHome).path());
      const QString replacement =
          (parent == QLatin1String("/") || parent.isEmpty())
              ? QStringLiteral("/home/<USER>")
              : (parent + QStringLiteral("/<USER>"));
      out.replace(cleanHome, replacement);
    }

    // 3. Redact generic POSIX/macOS/HPC user home/work/scratch path segments.
    static const QRegularExpression kUserPathRe(
        QStringLiteral("(/(?:home|Users|work|scratch)/)(?!<USER>(?:/|$|\"|'|\\s))([^/\"'\\s,}\\]]+)"));
    out.replace(kUserPathRe, QStringLiteral("\\1<USER>"));

    // 4. Redact explicit username when appearing as user@host or isolated identifier token in paths/strings.
    if (ctx.userName.size() >= 2) {
      const QString escapedUser = QRegularExpression::escape(ctx.userName);
      const QRegularExpression userAtRe(QStringLiteral("\\b%1@").arg(escapedUser));
      out.replace(userAtRe, QStringLiteral("<USER>@"));
      const QRegularExpression userSegRe(QStringLiteral("/%1(?=/|\"|'|\\s|$)").arg(escapedUser));
      out.replace(userSegRe, QStringLiteral("/<USER>"));
    }

    // 5. Redact explicit hostname (avoiding short 1-2 char false positives).
    if (ctx.hostName.size() >= 3) {
      const QString escapedHost = QRegularExpression::escape(ctx.hostName);
      const QRegularExpression hostRe(QStringLiteral("(?<![A-Za-z0-9_-])%1(?![A-Za-z0-9_-])").arg(escapedHost));
      out.replace(hostRe, QStringLiteral("<HOST>"));
    }

    return out;
  }

  /**
   * @brief Formats the complete sanitized transcript with GridLock, Qt, GDB, and MPI metadata headers.
   */
  QString exportSanitizedTranscript(const SanitizeContext& ctx = SanitizeContext::fromCurrentEnvironment()) const {
    std::deque<Entry> snapshot;
    QString glVer;
    QString gdbVer;
    QString mpiMode;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      snapshot = m_entries;
      glVer = m_gridlockVersion;
      gdbVer = m_gdbVersion;
      mpiMode = m_mpiLauncherMode;
    }
    if (glVer.isEmpty()) glVer = QCoreApplication::applicationVersion().trimmed();
    if (glVer.isEmpty()) glVer = QStringLiteral("unknown");

    QStringList lines;
    lines.reserve(static_cast<qsizetype>(snapshot.size()) + 8);
    lines.append(QStringLiteral("# GridLock Sanitized GDB/MI Session Trace"));
    lines.append(QStringLiteral("# GridLock Version: %1").arg(glVer));
    lines.append(QStringLiteral("# Qt Version: %1").arg(QString::fromLatin1(qVersion())));
    lines.append(QStringLiteral("# GDB Version: %1").arg(gdbVer.isEmpty() ? QStringLiteral("unknown") : sanitizeLine(gdbVer, ctx)));
    lines.append(QStringLiteral("# MPI Launcher Mode: %1").arg(mpiMode.isEmpty() ? QStringLiteral("unknown") : sanitizeLine(mpiMode, ctx)));
    lines.append(QStringLiteral("# Recorded Entries: %1").arg(snapshot.size()));
    lines.append(QString());

    for (const Entry& e : snapshot) {
      lines.append(QStringLiteral("[%1] [rank %2] %3 %4")
                       .arg(e.timestampIso)
                       .arg(e.rankId)
                       .arg(e.outgoing ? QStringLiteral("->") : QStringLiteral("<-"))
                       .arg(sanitizeLine(e.line, ctx)));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
  }

  /**
   * @brief Writes the sanitized GDB/MI session transcript to @p filePath.
   */
  bool exportToFile(const QString& filePath,
                    QString* outError = nullptr,
                    const SanitizeContext& ctx = SanitizeContext::fromCurrentEnvironment()) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
      if (outError) *outError = file.errorString();
      return false;
    }
    const QByteArray payload = exportSanitizedTranscript(ctx).toUtf8();
    const qint64 written = file.write(payload);
    file.close();
    if (written != payload.size()) {
      if (outError) *outError = QStringLiteral("Short write while saving GDB/MI trace.");
      return false;
    }
    return true;
  }

 private:
  void evictLocked() {
    while (!m_entries.empty() && (m_entries.size() > m_maxLines || m_totalBytes > m_maxBytes)) {
      const size_t frontBytes = m_entries.front().byteSize;
      m_totalBytes = (m_totalBytes >= frontBytes) ? (m_totalBytes - frontBytes) : 0u;
      m_entries.pop_front();
    }
  }

  mutable std::mutex m_mutex;
  std::deque<Entry> m_entries;
  size_t m_maxLines = kDefaultMaxLines;
  size_t m_maxBytes = kDefaultMaxBytes;
  size_t m_totalBytes = 0u;
  QString m_gridlockVersion;
  QString m_gdbVersion;
  QString m_mpiLauncherMode;
};

}  // namespace gridlock::core

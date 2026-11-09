/**
 * @file MainWindowExecution.cpp
 * @brief Implementation of target resolution, tutorial discovery, and execution workflows.
 *
 * Provides resolution helpers for bundled tutorial examples across local development
 * paths and AppImage read-only deployment directories (/usr/share/gridlock/tutorial/
 * or $APPDIR/usr/share/gridlock/tutorial/). Supports on-demand copy/execution for
 * read-only standalone deployments.
 */

#include "MainWindow.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QString>

namespace gridlock::ui {

/**
 * @brief Resolves a tutorial target name or relative path across local and AppImage share directories.
 * @param target Target name or relative path (e.g. "deadlock_ring", "tutorial/deadlock_ring.cpp").
 * @return Canonical or absolute path to the resolved tutorial source or binary file.
 *
 * Checks in order:
 * 1. Current working directory (./tutorial/<name>.cpp)
 * 2. Parent directory for builddir test execution (../tutorial/<name>.cpp)
 * 3. AppDir share directory ($APPDIR/usr/share/gridlock/tutorial/<name>.cpp)
 * 4. Application directory share path (QCoreApplication::applicationDirPath() + "/../share/gridlock/tutorial/")
 * 5. Direct AppDir directory ($APPDIR/tutorial/<name>.cpp)
 * 6. Local application binary directory (QCoreApplication::applicationDirPath() + "/tutorial/")
 */
QString resolveTutorialTarget(const QString& target) {
    QString rel = target.trimmed();
    if (rel.isEmpty()) return QString();

    // Normalize bare target names into relative tutorial source file paths.
    if (!rel.endsWith(QStringLiteral(".cpp")) && !rel.contains(QLatin1Char('/'))) {
        rel = QStringLiteral("tutorial/") + rel + QStringLiteral(".cpp");
    } else if (!rel.startsWith(QStringLiteral("tutorial/")) && !rel.startsWith(QLatin1Char('/'))) {
        rel = QStringLiteral("tutorial/") + rel;
    }

    // 1. Direct hit from current working directory (standard repository root execution).
    if (QFileInfo::exists(rel)) {
        const QFileInfo fi(rel);
        return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    }

    // 2. Secondary lookup one level up (out-of-source builddir/ test harness runner).
    const QString parentCand = QStringLiteral("../") + rel;
    if (QFileInfo::exists(parentCand)) {
        const QFileInfo fi(parentCand);
        return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    }

    // 3. AppImage deployment environment variable ($APPDIR/usr/share/gridlock/tutorial/...).
    const QString envAppDir = qEnvironmentVariable("APPDIR");
    if (!envAppDir.isEmpty()) {
        const QString appDirShare = envAppDir + QStringLiteral("/usr/share/gridlock/") + rel;
        if (QFileInfo::exists(appDirShare)) {
            const QFileInfo fi(appDirShare);
            return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
        }
        const QString appDirDirect = envAppDir + QStringLiteral("/") + rel;
        if (QFileInfo::exists(appDirDirect)) {
            const QFileInfo fi(appDirDirect);
            return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
        }
    }

    // 4. Relative to application binary dir (e.g. AppDir/usr/bin/../share/gridlock/tutorial/...).
    const QString appDirPath = QCoreApplication::applicationDirPath();
    const QString shareCand = appDirPath + QStringLiteral("/../share/gridlock/") + rel;
    if (QFileInfo::exists(shareCand)) {
        const QFileInfo fi(shareCand);
        return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    }

    // 5. Direct companion directory in binary folder (e.g. AppDir/usr/bin/tutorial/...).
    const QString localCand = appDirPath + QStringLiteral("/") + rel;
    if (QFileInfo::exists(localCand)) {
        const QFileInfo fi(localCand);
        return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    }

    // 6. Default to expected share or appdir path when simulating outside repository.
    if (!envAppDir.isEmpty()) {
        return envAppDir + QStringLiteral("/usr/share/gridlock/") + rel;
    }
    return shareCand;
}

/**
 * @brief Ensures a writable execution path for tutorial targets in read-only AppImage environments.
 * @param sourcePath Canonical path to tutorial source file.
 * @return Writable binary path in /tmp/gridlock_tutorials/ or direct binary path.
 */
QString ensureWritableTutorialBinaryPath(const QString& sourcePath) {
    if (sourcePath.isEmpty()) return QString();
    const QFileInfo srcInfo(sourcePath);
    const QString baseName = srcInfo.completeBaseName();
    const QString companionBin = srcInfo.dir().filePath(baseName);

    // If existing companion binary exists and directory is read-only, use it or copy to /tmp.
    if (!srcInfo.dir().exists() || !QFileInfo(srcInfo.dir().absolutePath()).isWritable()) {
        const QString cacheDir = QStringLiteral("/tmp/gridlock_tutorials");
        QDir().mkpath(cacheDir);
        const QString targetBin = cacheDir + QStringLiteral("/") + baseName;
        if (QFileInfo::exists(companionBin) && (!QFileInfo::exists(targetBin) || QFileInfo(targetBin).lastModified() < QFileInfo(companionBin).lastModified())) {
            QFile::remove(targetBin);
            QFile::copy(companionBin, targetBin);
            QFile::setPermissions(targetBin, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup);
        }
        return targetBin;
    }
    return companionBin;
}

}  // namespace gridlock::ui

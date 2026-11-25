/**
 * @file PluginInstaller.cpp
 * @brief Implementation of the shell-free GitHub repository clone, build-system auto-detector, and atomic plugin installer.
 */

#include "PluginInstaller.hpp"

#include <cstdio>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

#include "../managers/ConfigManager.hpp"

namespace gridlock::core::plugins {

namespace {

bool containsControlOrSpace(const QString& s) {
  for (const QChar c : s) {
    if (c.isSpace() || c == QChar('\0') || c.category() == QChar::Other_Control) return true;
  }
  return false;
}

QString hostSharedLibSuffix() {
#if defined(_WIN32)
  return QStringLiteral(".dll");
#elif defined(__APPLE__)
  return QStringLiteral(".dylib");
#else
  return QStringLiteral(".so");
#endif
}

bool isCandidateBuiltPluginArtifact(const QFileInfo& fi) {
  if (!fi.exists() || !fi.isFile() || fi.isSymLink() || fi.size() < 64) return false;
  const QString path = fi.absoluteFilePath();
  const QString name = fi.fileName();
  if (path.contains(QStringLiteral("/.git/")) || path.contains(QStringLiteral("/CMakeFiles/")) ||
      path.contains(QStringLiteral("/meson-private/")) || path.contains(QStringLiteral(".p/")) ||
      name.contains(QStringLiteral("CompilerId"), Qt::CaseInsensitive) ||
      name.contains(QStringLiteral("sanitycheck"), Qt::CaseInsensitive)) {
    return false;
  }
#if defined(_WIN32)
  return name.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive);
#elif defined(__APPLE__)
  return name.endsWith(QStringLiteral(".dylib"), Qt::CaseInsensitive);
#else
  return name.endsWith(QStringLiteral(".so"));
#endif
}

QString computeSha256Hex(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) return QString();
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&f)) return QString();
  return QString::fromLatin1(hash.result().toHex()).toLower();
}

bool hasBuildManifestOrSources(const QString& dirPath) {
  const QDir d(dirPath);
  if (!d.exists()) return false;
  if (d.exists(QStringLiteral("meson.build")) || d.exists(QStringLiteral("CMakeLists.txt")) ||
      d.exists(QStringLiteral("Makefile")) || d.exists(QStringLiteral("GNUmakefile"))) {
    return true;
  }
  return !d.entryList({QStringLiteral("*.cpp"), QStringLiteral("*.cc"), QStringLiteral("*.cxx"), QStringLiteral("*.c")},
                      QDir::Files | QDir::NoSymLinks)
              .isEmpty();
}

}  // namespace

PluginInstaller::PluginInstaller(QObject* parent) : QObject(parent) {
  qRegisterMetaType<gridlock::core::plugins::PluginBuildResult>("gridlock::core::plugins::PluginBuildResult");
}

PluginInstaller::~PluginInstaller() {
  cancelInstall();
}

bool PluginInstaller::validateGitHubUrl(const QString& rawUrl, QString* normalizedOut,
                                        QString* repoSlugOut, QString* errorOut) {
  if (rawUrl.isEmpty()) {
    if (errorOut) *errorOut = QStringLiteral("Repository URL must not be empty.");
    return false;
  }
  if (rawUrl.startsWith(QLatin1Char('-'))) {
    if (errorOut) *errorOut = QStringLiteral("Repository URL must not begin with '-'.");
    return false;
  }
  if (containsControlOrSpace(rawUrl)) {
    if (errorOut) *errorOut = QStringLiteral("Repository URL must not contain whitespace or control characters.");
    return false;
  }

  static const QRegularExpression kHttpsRe(
      QStringLiteral("^https://github\\.com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(?:\\.git)?/?$"));
  static const QRegularExpression kSshRe(
      QStringLiteral("^git@github\\.com:([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+?)(?:\\.git)?$"));

  QRegularExpressionMatch match = kHttpsRe.match(rawUrl);
  const bool isHttps = match.hasMatch();
  if (!isHttps) match = kSshRe.match(rawUrl);
  if (!match.hasMatch()) {
    if (errorOut) {
      *errorOut = QStringLiteral(
          "URL must match https://github.com/<owner>/<repo>[.git] or git@github.com:<owner>/<repo>.git.");
    }
    return false;
  }

  const QString owner = match.captured(1);
  const QString repo = match.captured(2);
  if (owner == QLatin1String(".") || owner == QLatin1String("..") || owner.startsWith(QLatin1Char('-')) ||
      repo == QLatin1String(".") || repo == QLatin1String("..") || repo.startsWith(QLatin1Char('-'))) {
    if (errorOut) *errorOut = QStringLiteral("Invalid GitHub owner or repository path segment.");
    return false;
  }

  if (normalizedOut) {
    *normalizedOut = isHttps ? QStringLiteral("https://github.com/%1/%2.git").arg(owner, repo)
                             : QStringLiteral("git@github.com:%1/%2.git").arg(owner, repo);
  }
  if (repoSlugOut) *repoSlugOut = repo;
  return true;
}

bool PluginInstaller::validateGitRef(const QString& rawRef, QString* errorOut) {
  if (rawRef.isEmpty()) return true;
  if (rawRef.size() > 128) {
    if (errorOut) *errorOut = QStringLiteral("Branch or tag name exceeds 128 characters.");
    return false;
  }
  if (rawRef.startsWith(QLatin1Char('-')) || rawRef.startsWith(QLatin1Char('/')) ||
      rawRef.endsWith(QLatin1Char('/')) || rawRef.endsWith(QStringLiteral(".lock")) ||
      rawRef.contains(QStringLiteral("..")) || rawRef.contains(QStringLiteral("@{")) ||
      containsControlOrSpace(rawRef)) {
    if (errorOut) *errorOut = QStringLiteral("Invalid or unsafe git branch/tag reference.");
    return false;
  }
  static const QRegularExpression kSafeRefRe(QStringLiteral("^[A-Za-z0-9._/-]+$"));
  if (!kSafeRefRe.match(rawRef).hasMatch()) {
    if (errorOut) *errorOut = QStringLiteral("Git reference may only contain [A-Za-z0-9._/-].");
    return false;
  }
  return true;
}

QString PluginInstaller::resolveGridLockIncludeDir() {
  QStringList candidates;
  if (qEnvironmentVariableIsSet("GRIDLOCK_INCLUDE_DIR")) {
    candidates.append(qEnvironmentVariable("GRIDLOCK_INCLUDE_DIR").trimmed());
  }
  const QString appDir = QCoreApplication::applicationDirPath();
  if (!appDir.isEmpty()) {
    candidates.append(QDir(appDir).filePath(QStringLiteral("../include")));
    candidates.append(QDir(appDir).filePath(QStringLiteral("include")));
  }
  if (const QString envApp = qEnvironmentVariable("APPDIR"); !envApp.isEmpty()) {
    candidates.append(QDir(envApp).filePath(QStringLiteral("usr/include")));
  }
  candidates.append(QDir::current().filePath(QStringLiteral("include")));
  candidates.append(QDir::current().filePath(QStringLiteral("../include")));
  candidates.append(QStringLiteral("/usr/local/include"));
  candidates.append(QStringLiteral("/usr/include"));

  for (const QString& raw : candidates) {
    if (raw.isEmpty()) continue;
    const QFileInfo hdr(QDir(raw).filePath(QStringLiteral("gridlock/plugin_api.h")));
    if (hdr.exists() && hdr.isFile()) {
      const QFileInfo dirInfo(raw);
      return !dirInfo.canonicalFilePath().isEmpty() ? dirInfo.canonicalFilePath() : dirInfo.absoluteFilePath();
    }
  }
  return QDir::current().filePath(QStringLiteral("include"));
}

QString PluginInstaller::resolveTargetInstallDir() const {
  if (!m_targetDirOverride.trimmed().isEmpty()) return m_targetDirOverride.trimmed();
  if (m_scope == PluginScope::Workspace) {
    const QString ws = ConfigManager::instance().workspacePath().trimmed();
    if (!ws.isEmpty()) return QDir(ws).filePath(QStringLiteral(".gridlock/plugins"));
  }
  return QDir(ConfigManager::configDirectory()).filePath(QStringLiteral("plugins"));
}

bool PluginInstaller::startInstallFromGitHub(const QString& repoUrl, const QString& gitRef,
                                             PluginScope scope, const QString& targetDirOverride) {
  if (m_running) return false;

  m_fullLog.clear();
  m_partialLine.clear();
  m_warnings.clear();
  m_errors.clear();
  m_pendingSteps.clear();
  m_cancelled = false;
  m_scope = scope;
  m_targetDirOverride = targetDirOverride;

  QString normalizedUrl;
  QString validationErr;
  if (!validateGitHubUrl(repoUrl, &normalizedUrl, &m_repoSlug, &validationErr)) {
    finishWithFailure(validationErr);
    return false;
  }
  if (!validateGitRef(gitRef, &validationErr)) {
    finishWithFailure(validationErr);
    return false;
  }
  if (scope == PluginScope::Workspace && m_targetDirOverride.trimmed().isEmpty() &&
      ConfigManager::instance().workspacePath().trimmed().isEmpty()) {
    finishWithFailure(QStringLiteral("No active workspace directory is open for workspace-scoped installation."));
    return false;
  }

  m_stagingDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/gridlock_plug_build_XXXXXX"));
  if (!m_stagingDir->isValid()) {
    finishWithFailure(QStringLiteral("Failed to create isolated temporary staging directory."));
    return false;
  }

  const QString cloneDest = QDir(m_stagingDir->path()).filePath(QStringLiteral("repo"));
  QStringList cloneArgs{QStringLiteral("clone"), QStringLiteral("--depth"), QStringLiteral("1")};
  if (!gitRef.isEmpty()) {
    cloneArgs << QStringLiteral("--branch") << gitRef;
  }
  cloneArgs << QStringLiteral("--") << normalizedUrl << cloneDest;

  m_activeSourceRoot = cloneDest;
  m_awaitingClone = true;
  m_running = true;
  m_pendingSteps.enqueue(BuildStep{QStringLiteral("git"), cloneArgs, m_stagingDir->path(),
                                   QStringLiteral("Cloning %1...").arg(normalizedUrl)});
  launchNextStep();
  return true;
}

bool PluginInstaller::startBuildFromSourceDirectory(const QString& sourceDir, PluginScope scope,
                                                    const QString& targetDirOverride) {
  if (m_running) return false;

  m_fullLog.clear();
  m_partialLine.clear();
  m_warnings.clear();
  m_errors.clear();
  m_pendingSteps.clear();
  m_cancelled = false;
  m_awaitingClone = false;
  m_scope = scope;
  m_targetDirOverride = targetDirOverride;

  const QFileInfo srcInfo(sourceDir);
  if (!srcInfo.exists() || !srcInfo.isDir()) {
    finishWithFailure(QStringLiteral("Source directory does not exist: %1").arg(sourceDir));
    return false;
  }

  m_stagingDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/gridlock_plug_local_XXXXXX"));
  if (!m_stagingDir->isValid()) {
    finishWithFailure(QStringLiteral("Failed to create temporary build directory."));
    return false;
  }

  m_activeSourceRoot = srcInfo.absoluteFilePath();
  m_repoSlug = srcInfo.fileName().isEmpty() ? QStringLiteral("custom_plugin") : srcInfo.fileName();
  QString detectErr;
  if (!enqueueDetectedBuildSteps(m_activeSourceRoot, m_repoSlug, &detectErr)) {
    finishWithFailure(detectErr);
    return false;
  }

  m_running = true;
  launchNextStep();
  return true;
}

bool PluginInstaller::enqueueDetectedBuildSteps(const QString& rootDir, const QString& fallbackStem,
                                                QString* errorOut) {
  QString buildRoot = rootDir;
  if (!hasBuildManifestOrSources(buildRoot)) {
    const QString subPlugin = QDir(rootDir).filePath(QStringLiteral("plugin"));
    if (hasBuildManifestOrSources(subPlugin)) buildRoot = subPlugin;
  }
  m_activeSourceRoot = buildRoot;

  const QDir dir(buildRoot);
  const QString incDir = resolveGridLockIncludeDir();
  const QString outBuildDir = m_stagingDir ? QDir(m_stagingDir->path()).filePath(QStringLiteral("build"))
                                           : dir.filePath(QStringLiteral("build"));

  if (dir.exists(QStringLiteral("meson.build"))) {
    m_pendingSteps.enqueue(BuildStep{
        QStringLiteral("meson"),
        {QStringLiteral("setup"), outBuildDir, QStringLiteral("-Dgridlock_include_dir=%1").arg(incDir)},
        buildRoot,
        QStringLiteral("Configuring Meson project...")});
    m_pendingSteps.enqueue(BuildStep{QStringLiteral("ninja"), {QStringLiteral("-C"), outBuildDir},
                                     buildRoot, QStringLiteral("Compiling with Ninja...")});
    return true;
  }

  if (dir.exists(QStringLiteral("CMakeLists.txt"))) {
    m_pendingSteps.enqueue(BuildStep{
        QStringLiteral("cmake"),
        {QStringLiteral("-B"), outBuildDir, QStringLiteral("-DGRIDLOCK_INCLUDE_DIR=%1").arg(incDir)},
        buildRoot,
        QStringLiteral("Configuring CMake project...")});
    m_pendingSteps.enqueue(BuildStep{QStringLiteral("cmake"), {QStringLiteral("--build"), outBuildDir},
                                     buildRoot, QStringLiteral("Compiling with CMake...")});
    return true;
  }

  if (dir.exists(QStringLiteral("Makefile")) || dir.exists(QStringLiteral("GNUmakefile"))) {
    m_pendingSteps.enqueue(BuildStep{QStringLiteral("make"),
                                     {QStringLiteral("GRIDLOCK_INCLUDE_DIR=%1").arg(incDir)},
                                     buildRoot, QStringLiteral("Compiling with Make...")});
    return true;
  }

  const QStringList cxxSrcs = dir.entryList(
      {QStringLiteral("*.cpp"), QStringLiteral("*.cc"), QStringLiteral("*.cxx")}, QDir::Files | QDir::NoSymLinks);
  const QStringList cSrcs = dir.entryList({QStringLiteral("*.c")}, QDir::Files | QDir::NoSymLinks);
  if (cxxSrcs.isEmpty() && cSrcs.isEmpty()) {
    if (errorOut) {
      *errorOut = QStringLiteral(
          "No supported build file (meson.build, CMakeLists.txt, Makefile) or C/C++ plugin source found.");
    }
    return false;
  }

  QDir().mkpath(outBuildDir);
  const QString cleanStem = fallbackStem.isEmpty() ? QStringLiteral("plugin") : fallbackStem;
  const QString outLib = QDir(outBuildDir).filePath(QStringLiteral("lib%1%2").arg(cleanStem, hostSharedLibSuffix()));

  if (!cxxSrcs.isEmpty()) {
    QStringList args{QStringLiteral("-std=c++23"), QStringLiteral("-Wall"), QStringLiteral("-Wextra"),
                     QStringLiteral("-fvisibility=hidden"), QStringLiteral("-fPIC"), QStringLiteral("-shared"),
                     QStringLiteral("-I%1").arg(incDir)};
    for (const QString& s : cxxSrcs) args.append(dir.filePath(s));
    args << QStringLiteral("-o") << outLib;
    const QString cxxBin = QStandardPaths::findExecutable(QStringLiteral("c++")).isEmpty()
                               ? QStringLiteral("g++")
                               : QStringLiteral("c++");
    m_pendingSteps.enqueue(BuildStep{cxxBin, args, buildRoot, QStringLiteral("Compiling C++23 plugin source...")});
    return true;
  }

  QStringList args{QStringLiteral("-std=c99"), QStringLiteral("-Wall"), QStringLiteral("-Wextra"),
                   QStringLiteral("-fvisibility=hidden"), QStringLiteral("-fPIC"), QStringLiteral("-shared"),
                   QStringLiteral("-I%1").arg(incDir)};
  for (const QString& s : cSrcs) args.append(dir.filePath(s));
  args << QStringLiteral("-o") << outLib;
  const QString ccBin = QStandardPaths::findExecutable(QStringLiteral("cc")).isEmpty()
                            ? QStringLiteral("gcc")
                            : QStringLiteral("cc");
  m_pendingSteps.enqueue(BuildStep{ccBin, args, buildRoot, QStringLiteral("Compiling C99 plugin source...")});
  return true;
}

void PluginInstaller::launchNextStep() {
  if (m_cancelled) return;
  if (m_pendingSteps.isEmpty()) {
    if (m_awaitingClone) {
      m_awaitingClone = false;
      QString detectErr;
      if (!enqueueDetectedBuildSteps(m_activeSourceRoot, m_repoSlug, &detectErr)) {
        finishWithFailure(detectErr);
        return;
      }
    } else {
      finalizeInstalledArtifacts();
      return;
    }
  }

  const BuildStep step = m_pendingSteps.dequeue();
  emit stepStarted(step.description);
  const QString banner = QStringLiteral("==> %1 (%2 %3)\n").arg(step.description, step.program, step.args.join(QLatin1Char(' ')));
  m_fullLog.append(banner);
  emit logOutputReceived(banner);

  if (m_process != nullptr) {
    m_process->disconnect(this);
    m_process->deleteLater();
  }
  m_process = new QProcess(this);
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
  env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
  m_process->setProcessEnvironment(env);
  m_process->setWorkingDirectory(step.workingDir);

  connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
    if (m_process) handleProcessOutput(m_process->readAllStandardOutput());
  });
  connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
    if (m_process) handleProcessOutput(m_process->readAllStandardError());
  });
  connect(m_process, &QProcess::errorOccurred, this, [this, prog = step.program](QProcess::ProcessError err) {
    if (m_cancelled || !m_running) return;
    if (err == QProcess::FailedToStart) {
      finishWithFailure(QStringLiteral("Failed to launch required tool '%1' (not found or not executable).").arg(prog));
    }
  });
  connect(m_process, &QProcess::finished, this, [this, prog = step.program](int exitCode, QProcess::ExitStatus status) {
    if (m_cancelled || !m_running) return;
    if (m_process) {
      handleProcessOutput(m_process->readAllStandardOutput());
      handleProcessOutput(m_process->readAllStandardError());
    }
    if (!m_partialLine.isEmpty()) {
      classifyDiagnosticLine(m_partialLine);
      m_partialLine.clear();
    }
    if (status != QProcess::NormalExit || exitCode != 0) {
      finishWithFailure(QStringLiteral("Step '%1' exited with status %2.").arg(prog).arg(exitCode));
      return;
    }
    launchNextStep();
  });

  m_process->start(step.program, step.args);
}

void PluginInstaller::handleProcessOutput(const QByteArray& rawBytes) {
  if (rawBytes.isEmpty()) return;
  const QString chunk = QString::fromUtf8(rawBytes);
  m_fullLog.append(chunk);
  emit logOutputReceived(chunk);

  m_partialLine.append(chunk);
  int nl = m_partialLine.indexOf(QLatin1Char('\n'));
  while (nl >= 0) {
    const QString line = m_partialLine.left(nl).trimmed();
    m_partialLine.remove(0, nl + 1);
    if (!line.isEmpty()) classifyDiagnosticLine(line);
    nl = m_partialLine.indexOf(QLatin1Char('\n'));
  }
}

void PluginInstaller::classifyDiagnosticLine(const QString& line) {
  static const QRegularExpression kWarnRe(QStringLiteral("(?:^|\\s|:)warning:\\s"), QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression kErrRe(
      QStringLiteral("(?:(?:^|\\s|:)(?:fatal )?error:\\s|undefined reference to\\b|CMake Error\\b|meson\\.build:\\d+:\\d+:\\s*ERROR)"),
      QRegularExpression::CaseInsensitiveOption);
  if (kErrRe.match(line).hasMatch()) {
    if (!m_errors.contains(line)) m_errors.append(line);
  } else if (kWarnRe.match(line).hasMatch()) {
    if (!m_warnings.contains(line)) m_warnings.append(line);
  }
}

void PluginInstaller::finalizeInstalledArtifacts() {
  QStringList scanRoots;
  if (m_stagingDir && m_stagingDir->isValid()) scanRoots.append(m_stagingDir->path());
  if (!m_activeSourceRoot.isEmpty() && !scanRoots.contains(m_activeSourceRoot)) scanRoots.append(m_activeSourceRoot);

  QStringList builtLibs;
  for (const QString& root : scanRoots) {
    QDirIterator it(root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      it.next();
      const QFileInfo fi = it.fileInfo();
      if (isCandidateBuiltPluginArtifact(fi) && !builtLibs.contains(fi.absoluteFilePath())) {
        builtLibs.append(fi.absoluteFilePath());
      }
    }
  }

  if (builtLibs.isEmpty()) {
    finishWithFailure(QStringLiteral("Build completed, but no shared library (%1) artifact was produced.")
                          .arg(hostSharedLibSuffix()));
    return;
  }

  const QString destDir = resolveTargetInstallDir();
  if (!QDir().mkpath(destDir)) {
    finishWithFailure(QStringLiteral("Failed to create target plugin directory: %1").arg(destDir));
    return;
  }

  QString primaryInstalled;
  for (const QString& srcLib : builtLibs) {
    const QFileInfo srcFi(srcLib);
    const QString finalPath = QDir(destDir).filePath(srcFi.fileName());
    const QString tmpPath = QDir(destDir).filePath(
        QStringLiteral(".%1.tmp.%2").arg(srcFi.fileName()).arg(QCoreApplication::applicationPid()));
    QFile::remove(tmpPath);
    if (!QFile::copy(srcLib, tmpPath)) {
      finishWithFailure(QStringLiteral("Failed to stage built library into %1.").arg(destDir));
      return;
    }
    QFile::setPermissions(tmpPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                       QFile::ReadGroup | QFile::ExeGroup);
    if (::rename(QFile::encodeName(tmpPath).constData(), QFile::encodeName(finalPath).constData()) != 0) {
      QFile::remove(finalPath);
      if (!QFile::rename(tmpPath, finalPath)) {
        QFile::remove(tmpPath);
        finishWithFailure(QStringLiteral("Atomic rename failed for %1.").arg(finalPath));
        return;
      }
    }

    const QFileInfo destFi(finalPath);
    const QString canonDest = !destFi.canonicalFilePath().isEmpty() ? destFi.canonicalFilePath() : destFi.absoluteFilePath();
    if (m_scope == PluginScope::Workspace) {
      const QString sha256 = computeSha256Hex(canonDest);
      if (!sha256.isEmpty()) ConfigManager::instance().trustWorkspacePlugin(canonDest, sha256);
    }
    if (primaryInstalled.isEmpty()) primaryInstalled = canonDest;
    const QString msg = QStringLiteral("==> Installed %1\n").arg(canonDest);
    m_fullLog.append(msg);
    emit logOutputReceived(msg);
  }

  m_running = false;
  cleanupStaging();
  emit installFinished(PluginBuildResult{true, primaryInstalled, m_warnings, m_errors, m_fullLog});
}

void PluginInstaller::finishWithFailure(const QString& headlineError) {
  if (!headlineError.isEmpty()) {
    if (!m_errors.contains(headlineError)) m_errors.append(headlineError);
    const QString line = QStringLiteral("error: %1\n").arg(headlineError);
    m_fullLog.append(line);
    emit logOutputReceived(line);
  }
  m_running = false;
  m_awaitingClone = false;
  m_pendingSteps.clear();
  cleanupStaging();
  emit installFinished(PluginBuildResult{false, QString(), m_warnings, m_errors, m_fullLog});
}

void PluginInstaller::cancelInstall() {
  if (!m_running && m_process == nullptr) {
    cleanupStaging();
    return;
  }
  const bool wasRunning = m_running;
  m_cancelled = true;
  m_running = false;
  m_awaitingClone = false;
  m_pendingSteps.clear();
  if (m_process != nullptr) {
    m_process->disconnect(this);
    if (m_process->state() != QProcess::NotRunning) {
      m_process->terminate();
      if (!m_process->waitForFinished(500)) {
        m_process->kill();
        m_process->waitForFinished(500);
      }
    }
    m_process->deleteLater();
    m_process = nullptr;
  }
  cleanupStaging();
  if (wasRunning) {
    m_errors.append(QStringLiteral("Build cancelled by user."));
    m_fullLog.append(QStringLiteral("==> Build cancelled by user.\n"));
    emit installFinished(PluginBuildResult{false, QString(), m_warnings, m_errors, m_fullLog});
  }
}

void PluginInstaller::cleanupStaging() {
  m_stagingDir.reset();
}

}  // namespace gridlock::core::plugins

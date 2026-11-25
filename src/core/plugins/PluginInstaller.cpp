/**
 * @file PluginInstaller.cpp
 * @brief Shell-free multi-forge Git repository clone, build-system detection, and `plugins.toml` manifest sync.
 */

#include "PluginInstaller.hpp"

#include "PluginManifest.hpp"
#include "PluginVerifier.hpp"

#include <cstdio>

#include <QCoreApplication>
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
    if (c.isSpace() || c == QChar('\0') || c.category() == QChar::Other_Control ||
        c.category() == QChar::Other_Format) {
      return true;
    }
  }
  return false;
}

bool isHexCommitShaRef(const QString& s) {
  static const QRegularExpression kShaRe(QStringLiteral("^[0-9a-fA-F]{7,40}$"));
  return kShaRe.match(s).hasMatch();
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

PluginInstaller::~PluginInstaller() { cancelInstall(); }

bool PluginInstaller::normalizeAndValidateUrl(const QString& input, GitForgeHost defaultForge,
                                              QString* normalizedOut, QString* repoSlugOut, QString* errorOut) {
  return PluginManifest::normalizeGitUrl(input, defaultForge, normalizedOut, repoSlugOut, errorOut);
}

bool PluginInstaller::normalizeAndValidateUrl(const QString& input, QString* normalizedOut,
                                              QString* repoSlugOut, QString* errorOut) {
  return PluginManifest::normalizeGitUrl(input, GitForgeHost::GitHub, normalizedOut, repoSlugOut, errorOut);
}

bool PluginInstaller::validateGitHubUrl(const QString& rawUrl, QString* normalizedOut,
                                        QString* repoSlugOut, QString* errorOut) {
  return PluginManifest::normalizeGitUrl(rawUrl, GitForgeHost::GitHub, normalizedOut, repoSlugOut, errorOut);
}

bool PluginInstaller::validateGitRef(const QString& rawRef, QString* errorOut) {
  if (rawRef.isEmpty()) return true;
  if (rawRef.size() > 128) {
    if (errorOut) *errorOut = QStringLiteral("Branch, tag, or commit SHA exceeds 128 characters.");
    return false;
  }
  if (rawRef.startsWith(QLatin1Char('-')) || rawRef.startsWith(QLatin1Char('/')) ||
      rawRef.endsWith(QLatin1Char('/')) || rawRef.endsWith(QStringLiteral(".lock")) ||
      rawRef.contains(QStringLiteral("..")) || rawRef.contains(QStringLiteral("@{")) ||
      containsControlOrSpace(rawRef)) {
    if (errorOut) *errorOut = QStringLiteral("Invalid or unsafe git branch, tag, or commit reference.");
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

QString PluginInstaller::resolveScopeInstallDir(PluginScope scope, const QString& manifestPathHint) {
  if (scope == PluginScope::Workspace) {
    const QString ws = ConfigManager::instance().workspacePath().trimmed();
    if (!ws.isEmpty()) return QDir(ws).filePath(QStringLiteral(".gridlock/plugins"));
    if (!manifestPathHint.trimmed().isEmpty()) {
      return QDir(QFileInfo(manifestPathHint).absolutePath()).filePath(QStringLiteral("plugins"));
    }
  }
  return QDir(ConfigManager::configDirectory()).filePath(QStringLiteral("plugins"));
}

QString PluginInstaller::resolveTargetInstallDir() const {
  return !m_targetDirOverride.trimmed().isEmpty() ? m_targetDirOverride.trimmed() : resolveScopeInstallDir(m_scope);
}

void PluginInstaller::resetRunState(bool batchSync) {
  m_fullLog.clear(); m_partialLine.clear();
  m_warnings.clear(); m_errors.clear();
  m_pendingSteps.clear(); m_syncQueue.clear();
  m_currentTask = SyncTask{};
  m_clonedRepoDir.clear(); m_lastInstalledPath.clear(); m_lastCommitSha.clear();
  m_installedCount = 0;
  m_cancelled = false; m_awaitingClone = false;
  m_isBatchSync = batchSync; m_anySyncError = false; m_anyBinaryChanged = false;
}

bool PluginInstaller::startInstallFromGit(const QString& repoSpec, const QString& gitRef,
                                          PluginScope scope, GitForgeHost defaultForge,
                                          const QString& targetDirOverride, bool recordInManifest,
                                          const QString& buildMode) {
  if (m_running) return false;
  resetRunState(false);
  m_scope = scope;
  m_targetDirOverride = targetDirOverride;

  SyncTask task;
  const QString trimmedSpec = repoSpec.trimmed();
  task.rawRepo = PluginManifest::formatManifestRepoSpec(trimmedSpec, defaultForge);
  task.gitRef = gitRef.trimmed();
  task.scope = scope;
  task.targetDirOverride = targetDirOverride;

  QString validationErr;
  if (!normalizeAndValidateUrl(trimmedSpec, defaultForge, &task.normalizedUrl, &task.repoSlug, &validationErr) ||
      !validateGitRef(task.gitRef, &validationErr) ||
      !PluginManifest::validateBuildMode(buildMode, &task.buildMode, &validationErr)) {
    handleTaskFailure(validationErr);
    return false;
  }
  if (scope == PluginScope::Workspace && targetDirOverride.trimmed().isEmpty() &&
      ConfigManager::instance().workspacePath().trimmed().isEmpty()) {
    handleTaskFailure(QStringLiteral("No active workspace directory is open for workspace-scoped installation."));
    return false;
  }
  if (recordInManifest) {
    task.manifestPathToRecord =
        (scope == PluginScope::Workspace) ? PluginManifest::workspaceManifestPath() : PluginManifest::userManifestPath();
  }

  m_syncQueue.enqueue(task);
  m_running = true;
  launchNextSyncTask();
  return true;
}

bool PluginInstaller::startInstallFromGitHub(const QString& repoUrl, const QString& gitRef,
                                             PluginScope scope, const QString& targetDirOverride,
                                             bool recordInManifest, const QString& buildMode) {
  return startInstallFromGit(repoUrl, gitRef, scope, GitForgeHost::GitHub, targetDirOverride,
                             recordInManifest, buildMode);
}

bool PluginInstaller::syncFromManifests(const QString& userManifestPath,
                                        const QString& workspaceManifestPath,
                                        PluginScope scope) {
  if (m_running) return false;
  resetRunState(true);

  if (scope == PluginScope::Workspace && workspaceManifestPath.trimmed().isEmpty() &&
      ConfigManager::instance().workspacePath().trimmed().isEmpty()) {
    handleTaskFailure(QStringLiteral("No active workspace directory is open for workspace manifest sync."));
    return false;
  }

  QVector<PluginLockEntry> lockEntries;
  (void)PluginManifest::loadLockFile(PluginManifest::userLockFilePath(), &lockEntries, nullptr);

  auto enqueueManifestForScope = [&](const QString& manifestPath, PluginScope targetScope) -> bool {
    if (manifestPath.trimmed().isEmpty()) return true;
    QVector<PluginManifestEntry> entries;
    QString parseErr;
    if (!PluginManifest::loadManifestFile(manifestPath, &entries, &parseErr)) {
      handleTaskFailure(parseErr);
      return false;
    }
    const QString installDir = resolveScopeInstallDir(targetScope, manifestPath);
    for (const PluginManifestEntry& entry : entries) {
      if (!entry.enabled) {
        const QString msg = QStringLiteral("==> Skipping disabled plugin %1\n").arg(entry.repo);
        m_fullLog.append(msg);
        emit logOutputReceived(msg);
        continue;
      }
      QString canonBin, binSha;
      if (PluginManifest::isManifestEntryUpToDate(entry, targetScope, installDir, lockEntries, &canonBin, &binSha)) {
        if (targetScope == PluginScope::Workspace && !canonBin.isEmpty() && !binSha.isEmpty()) {
          ConfigManager::instance().trustWorkspacePlugin(canonBin, binSha);
        }
        const QString msg = QStringLiteral("==> Up to date: %1\n").arg(entry.repo);
        m_fullLog.append(msg);
        emit logOutputReceived(msg);
        continue;
      }
      m_syncQueue.enqueue(SyncTask{entry.repo, entry.normalizedUrl, entry.repoSlug, entry.ref,
                                   entry.build, targetScope, installDir, QString(), QString(), true});
    }
    return true;
  };

  if (!userManifestPath.trimmed().isEmpty() &&
      (scope == PluginScope::UserGlobal || QFileInfo::exists(userManifestPath.trimmed()))) {
    if (!enqueueManifestForScope(userManifestPath, PluginScope::UserGlobal)) return false;
  }
  if (scope == PluginScope::Workspace && !workspaceManifestPath.trimmed().isEmpty()) {
    if (!enqueueManifestForScope(workspaceManifestPath, PluginScope::Workspace)) return false;
  }

  if (m_syncQueue.isEmpty()) {
    const QString doneMsg = QStringLiteral("==> Manifest sync complete: all declared plugins are up to date.\n");
    m_fullLog.append(doneMsg);
    emit logOutputReceived(doneMsg);
    emit installFinished(PluginBuildResult{true, QString(), m_warnings, m_errors, m_fullLog, QString(), 0, false});
    return true;
  }

  m_running = true;
  launchNextSyncTask();
  return true;
}

bool PluginInstaller::startUpdatePlugins(const QVector<PluginUpdateTarget>& targets) {
  if (m_running || targets.isEmpty()) return false;
  resetRunState(targets.size() > 1);

  for (const PluginUpdateTarget& target : targets) {
    SyncTask task;
    task.rawRepo = target.gitRepo.trimmed();
    task.gitRef = target.gitRef.trimmed();
    task.scope = target.scope;
    task.targetDirOverride = target.installDir.trimmed();
    task.existingBinaryName = target.existingBinaryName.trimmed();
    task.preserveWorkspaceTrust = target.preserveWorkspaceTrust;

    QString validationErr;
    if (!normalizeAndValidateUrl(task.rawRepo, GitForgeHost::GitHub, &task.normalizedUrl, &task.repoSlug,
                                 &validationErr) ||
        !validateGitRef(task.gitRef, &validationErr) ||
        !PluginManifest::validateBuildMode(target.buildSystem, &task.buildMode, &validationErr)) {
      handleTaskFailure(validationErr);
      return false;
    }
    m_syncQueue.enqueue(task);
  }

  m_running = true;
  launchNextSyncTask();
  return true;
}

void PluginInstaller::launchNextSyncTask() {
  if (m_cancelled || m_syncQueue.isEmpty()) return;
  m_currentTask = m_syncQueue.dequeue();
  m_pendingSteps.clear();
  m_scope = m_currentTask.scope;
  m_targetDirOverride = m_currentTask.targetDirOverride;
  m_repoSlug = m_currentTask.repoSlug;
  m_buildMode = m_currentTask.buildMode;

  m_stagingDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/gridlock_plug_build_XXXXXX"));
  if (!m_stagingDir->isValid()) {
    handleTaskFailure(QStringLiteral("Failed to create isolated temporary staging directory."));
    return;
  }

  m_clonedRepoDir = QDir(m_stagingDir->path()).filePath(QStringLiteral("repo"));
  m_activeSourceRoot = m_clonedRepoDir;
  m_awaitingClone = true;

  QStringList baseGitArgs;
  if (m_currentTask.normalizedUrl.startsWith(QStringLiteral("file://"))) {
    baseGitArgs << QStringLiteral("-c") << QStringLiteral("protocol.file.allow=always");
  }

  if (isHexCommitShaRef(m_currentTask.gitRef)) {
    QStringList cloneArgs = baseGitArgs;
    cloneArgs << QStringLiteral("clone") << QStringLiteral("--no-checkout") << QStringLiteral("--")
              << m_currentTask.normalizedUrl << m_clonedRepoDir;
    m_pendingSteps.enqueue(BuildStep{QStringLiteral("git"), cloneArgs, m_stagingDir->path(),
                                     QStringLiteral("Cloning %1...").arg(m_currentTask.normalizedUrl)});
    m_pendingSteps.enqueue(BuildStep{QStringLiteral("git"), {QStringLiteral("checkout"), m_currentTask.gitRef},
                                     m_clonedRepoDir,
                                     QStringLiteral("Checking out pinned commit %1...").arg(m_currentTask.gitRef.left(12))});
  } else {
    QStringList cloneArgs = baseGitArgs;
    cloneArgs << QStringLiteral("clone") << QStringLiteral("--depth") << QStringLiteral("1");
    if (!m_currentTask.gitRef.isEmpty()) cloneArgs << QStringLiteral("--branch") << m_currentTask.gitRef;
    cloneArgs << QStringLiteral("--") << m_currentTask.normalizedUrl << m_clonedRepoDir;
    m_pendingSteps.enqueue(BuildStep{QStringLiteral("git"), cloneArgs, m_stagingDir->path(),
                                     QStringLiteral("Cloning %1...").arg(m_currentTask.normalizedUrl)});
  }

  launchNextStep();
}

bool PluginInstaller::startBuildFromSourceDirectory(const QString& sourceDir, PluginScope scope,
                                                    const QString& targetDirOverride) {
  if (m_running) return false;
  resetRunState(false);
  m_scope = scope;
  m_targetDirOverride = targetDirOverride;
  m_buildMode = QStringLiteral("auto");

  const QFileInfo srcInfo(sourceDir);
  if (!srcInfo.exists() || !srcInfo.isDir()) {
    handleTaskFailure(QStringLiteral("Source directory does not exist: %1").arg(sourceDir));
    return false;
  }

  m_stagingDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/gridlock_plug_local_XXXXXX"));
  if (!m_stagingDir->isValid()) {
    handleTaskFailure(QStringLiteral("Failed to create temporary build directory."));
    return false;
  }

  m_activeSourceRoot = srcInfo.absoluteFilePath();
  m_repoSlug = srcInfo.fileName().isEmpty() ? QStringLiteral("custom_plugin") : srcInfo.fileName();
  QString detectErr;
  if (!enqueueDetectedBuildSteps(m_activeSourceRoot, m_repoSlug, m_buildMode, &detectErr)) {
    handleTaskFailure(detectErr);
    return false;
  }

  m_running = true;
  launchNextStep();
  return true;
}

bool PluginInstaller::enqueueDetectedBuildSteps(const QString& rootDir, const QString& fallbackStem,
                                                const QString& buildMode, QString* errorOut) {
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
  const QString mode = buildMode.trimmed().isEmpty() ? QStringLiteral("auto") : buildMode.trimmed().toLower();

  if (mode == QLatin1String("meson") || (mode == QLatin1String("auto") && dir.exists(QStringLiteral("meson.build")))) {
    if (!dir.exists(QStringLiteral("meson.build"))) {
      if (errorOut) *errorOut = QStringLiteral("Requested Meson build, but meson.build was not found.");
      return false;
    }
    m_pendingSteps.enqueue(BuildStep{
        QStringLiteral("meson"),
        {QStringLiteral("setup"), outBuildDir, QStringLiteral("-Dgridlock_include_dir=%1").arg(incDir)},
        buildRoot, QStringLiteral("Configuring Meson project...")});
    m_pendingSteps.enqueue(BuildStep{QStringLiteral("ninja"), {QStringLiteral("-C"), outBuildDir},
                                     buildRoot, QStringLiteral("Compiling with Ninja...")});
    return true;
  }

  if (mode == QLatin1String("cmake") || (mode == QLatin1String("auto") && dir.exists(QStringLiteral("CMakeLists.txt")))) {
    if (!dir.exists(QStringLiteral("CMakeLists.txt"))) {
      if (errorOut) *errorOut = QStringLiteral("Requested CMake build, but CMakeLists.txt was not found.");
      return false;
    }
    m_pendingSteps.enqueue(BuildStep{
        QStringLiteral("cmake"),
        {QStringLiteral("-B"), outBuildDir, QStringLiteral("-DGRIDLOCK_INCLUDE_DIR=%1").arg(incDir)},
        buildRoot, QStringLiteral("Configuring CMake project...")});
    m_pendingSteps.enqueue(BuildStep{QStringLiteral("cmake"), {QStringLiteral("--build"), outBuildDir},
                                     buildRoot, QStringLiteral("Compiling with CMake...")});
    return true;
  }

  const bool hasMake = dir.exists(QStringLiteral("Makefile")) || dir.exists(QStringLiteral("GNUmakefile"));
  if (mode == QLatin1String("make") || (mode == QLatin1String("auto") && hasMake)) {
    if (!hasMake) {
      if (errorOut) *errorOut = QStringLiteral("Requested Make build, but Makefile was not found.");
      return false;
    }
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
      if (!enqueueDetectedBuildSteps(m_activeSourceRoot, m_repoSlug, m_buildMode, &detectErr)) {
        handleTaskFailure(detectErr);
        return;
      }
    } else {
      finalizeInstalledArtifacts();
      return;
    }
  }

  const BuildStep step = m_pendingSteps.dequeue();
  emit stepStarted(step.description);
  const QString banner =
      QStringLiteral("==> %1 (%2 %3)\n").arg(step.description, step.program, step.args.join(QLatin1Char(' ')));
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
      handleTaskFailure(QStringLiteral("Failed to launch required tool '%1' (not found or not executable).").arg(prog));
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
      handleTaskFailure(QStringLiteral("Step '%1' exited with status %2.").arg(prog).arg(exitCode));
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
  static const QRegularExpression kWarnRe(QStringLiteral("(?:^|\\s|:)warning:\\s"),
                                          QRegularExpression::CaseInsensitiveOption);
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
    handleTaskFailure(QStringLiteral("Build completed, but no shared library (%1) artifact was produced.")
                          .arg(hostSharedLibSuffix()));
    return;
  }

  const QString destDir = resolveTargetInstallDir();
  if (!QDir().mkpath(destDir)) {
    handleTaskFailure(QStringLiteral("Failed to create target plugin directory: %1").arg(destDir));
    return;
  }

  const QString commitSha = m_clonedRepoDir.isEmpty() ? QString() : PluginManifest::queryLocalHeadSha(m_clonedRepoDir);
  QString primaryInstalled, primarySha256;

  for (const QString& srcLib : builtLibs) {
    const QFileInfo srcFi(srcLib);
    const QString keepName = m_currentTask.existingBinaryName;
    const bool useExistingName =
        (builtLibs.size() == 1 && !keepName.isEmpty() && !keepName.contains(QLatin1Char('/')) &&
         !keepName.contains(QLatin1Char('\\')) && keepName != QLatin1String(".."));
    const QString outFileName = useExistingName ? keepName : srcFi.fileName();
    const QString finalPath = QDir(destDir).filePath(outFileName);
    const QString prevSha256 = QFileInfo::exists(finalPath) ? PluginManifest::computeFileSha256Hex(finalPath) : QString();
    const QString tmpPath = QDir(destDir).filePath(
        QStringLiteral(".%1.tmp.%2").arg(outFileName).arg(QCoreApplication::applicationPid()));
    QFile::remove(tmpPath);
    if (!QFile::copy(srcLib, tmpPath)) {
      handleTaskFailure(QStringLiteral("Failed to stage built library into %1.").arg(destDir));
      return;
    }
    QFile::setPermissions(tmpPath, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                       QFile::ReadGroup | QFile::ExeGroup);
    if (::rename(QFile::encodeName(tmpPath).constData(), QFile::encodeName(finalPath).constData()) != 0) {
      QFile::remove(finalPath);
      if (!QFile::rename(tmpPath, finalPath)) {
        QFile::remove(tmpPath);
        handleTaskFailure(QStringLiteral("Atomic rename failed for %1.").arg(finalPath));
        return;
      }
    }

    const QFileInfo destFi(finalPath);
    const QString canonDest = !destFi.canonicalFilePath().isEmpty() ? destFi.canonicalFilePath() : destFi.absoluteFilePath();

    PluginVerifyReport verifyRep;
    const bool verifiedOk = PluginVerifier::verifyBinary(canonDest, &verifyRep);
    for (const QString& w : verifyRep.warnings) {
      const QString wLine = QStringLiteral("warning: [PluginVerifier] %1").arg(w);
      if (!m_warnings.contains(wLine)) m_warnings.append(wLine);
      m_fullLog.append(wLine + QLatin1Char('\n'));
      emit logOutputReceived(wLine + QLatin1Char('\n'));
    }
    if (!verifiedOk) {
      QFile::remove(canonDest);
      for (const QString& e : verifyRep.errors) {
        const QString eLine = QStringLiteral("error: [PluginVerifier] %1").arg(e);
        if (!m_errors.contains(eLine)) m_errors.append(eLine);
        m_fullLog.append(eLine + QLatin1Char('\n'));
        emit logOutputReceived(eLine + QLatin1Char('\n'));
      }
      handleTaskFailure(verifyRep.errors.isEmpty() ? QStringLiteral("Sandboxed plugin conformance check failed.")
                                                   : verifyRep.errors.first());
      return;
    }

    const QString sha256 = PluginManifest::computeFileSha256Hex(canonDest);
    if (!sha256.isEmpty() && sha256 != prevSha256) m_anyBinaryChanged = true;
    if (m_scope == PluginScope::Workspace && m_currentTask.preserveWorkspaceTrust && !sha256.isEmpty()) {
      ConfigManager::instance().trustWorkspacePlugin(canonDest, sha256);
    }
    if (primaryInstalled.isEmpty()) {
      primaryInstalled = canonDest;
      primarySha256 = sha256;
    }
    ++m_installedCount;
    const QString msg = QStringLiteral("==> Verified & installed %1 (%2 %3)\n")
                            .arg(canonDest, verifyRep.pluginName, verifyRep.pluginVersion);
    m_fullLog.append(msg);
    emit logOutputReceived(msg);
  }

  if (!m_currentTask.normalizedUrl.isEmpty() && !primaryInstalled.isEmpty() && !primarySha256.isEmpty()) {
    (void)PluginManifest::upsertLockEntry(
        PluginManifest::userLockFilePath(),
        PluginLockEntry{m_currentTask.normalizedUrl, PluginManifest::scopeToString(m_scope), m_currentTask.gitRef,
                        commitSha, QFileInfo(primaryInstalled).fileName(), primarySha256},
        nullptr);
    if (!m_currentTask.manifestPathToRecord.isEmpty()) {
      (void)PluginManifest::upsertManifestEntry(
          m_currentTask.manifestPathToRecord,
          PluginManifestEntry{m_currentTask.rawRepo, m_currentTask.normalizedUrl, m_currentTask.repoSlug,
                              m_currentTask.gitRef, m_currentTask.buildMode, true},
          nullptr);
    }
  }

  m_lastInstalledPath = primaryInstalled;
  if (!commitSha.isEmpty()) m_lastCommitSha = commitSha;
  m_clonedRepoDir.clear();
  cleanupStaging();

  if (!m_syncQueue.isEmpty()) {
    launchNextSyncTask();
    return;
  }

  m_running = false;
  emit installFinished(PluginBuildResult{!m_anySyncError, m_lastInstalledPath, m_warnings, m_errors,
                                         m_fullLog, m_lastCommitSha, m_installedCount, m_anyBinaryChanged});
}

void PluginInstaller::handleTaskFailure(const QString& headlineError) {
  if (!headlineError.isEmpty()) {
    if (!m_errors.contains(headlineError)) m_errors.append(headlineError);
    const QString line = QStringLiteral("error: %1\n").arg(headlineError);
    m_fullLog.append(line);
    emit logOutputReceived(line);
  }
  m_anySyncError = true;
  m_awaitingClone = false;
  m_pendingSteps.clear();
  m_clonedRepoDir.clear();
  cleanupStaging();

  if (m_running && m_isBatchSync && !m_syncQueue.isEmpty()) {
    launchNextSyncTask();
    return;
  }

  m_running = false;
  m_syncQueue.clear();
  emit installFinished(PluginBuildResult{false, m_lastInstalledPath, m_warnings, m_errors,
                                         m_fullLog, m_lastCommitSha, m_installedCount, m_anyBinaryChanged});
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
  m_syncQueue.clear();
  m_clonedRepoDir.clear();
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
    emit installFinished(PluginBuildResult{false, QString(), m_warnings, m_errors, m_fullLog, QString(), 0, false});
  }
}

void PluginInstaller::cleanupStaging() { m_stagingDir.reset(); }

}  // namespace gridlock::core::plugins

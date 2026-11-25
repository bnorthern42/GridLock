/**
 * @file PluginInstaller.hpp
 * @brief Asynchronous GitHub repository clone, manifest sync, build-system detection, and atomic plugin installer.
 *
 * Accepts compact `owner/repo` shorthand as well as `https://github.com/<owner>/<repo>[.git]` and
 * `git@github.com:<owner>/<repo>.git` URLs, clones into isolated temporary staging directories without
 * shell interpolation, builds via Meson, CMake, Make, or direct C99/C++23 compilation, updates
 * `plugins.toml` and user-global `plugins-lock.toml`, and supports sequential declarative manifest sync.
 */

#pragma once

#include "PluginManager.hpp"

#include <memory>

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

namespace gridlock::core::plugins {

/**
 * @struct PluginBuildResult
 * @brief Final outcome and parsed compiler diagnostics for an asynchronous plugin build or manifest sync run.
 */
struct PluginBuildResult {
  bool success = false;      ///< True when all requested clone, build, and install operations succeeded.
  QString installedPath;     ///< Canonical or absolute path to the most recently installed shared library.
  QStringList warnings;      ///< Aggregated compiler diagnostic lines matching `warning:`.
  QStringList errors;        ///< Aggregated compiler, linker, git, or validation error lines.
  QString fullCompilerLog;   ///< Complete combined stdout/stderr build transcript.
  QString commitSha;         ///< Resolved 40-character HEAD commit SHA for the latest git build, if any.
  int installedCount = 0;    ///< Number of plugin shared libraries compiled and installed in this run.
};

/**
 * @class PluginInstaller
 * @brief Drives shell-free `QProcess` steps to clone, compile, lock, and install native C99 ABI plugins.
 */
class PluginInstaller : public QObject {
  Q_OBJECT

 public:
  /**
   * @brief Constructs an idle `PluginInstaller`.
   * @param parent Optional owning `QObject`.
   */
  explicit PluginInstaller(QObject* parent = nullptr);

  /** @brief Terminates any running build subprocess and removes temporary staging files. */
  ~PluginInstaller() override;

  /**
   * @brief Validates and normalizes `owner/repo` shorthand or GitHub HTTPS/SSH URLs, rejecting `-` flags and `..`.
   * @param rawUrl User-supplied `owner/repo`, `https://github.com/<owner>/<repo>[.git]`, or `git@github.com:<owner>/<repo>.git`.
   * @param normalizedOut Optional output receiving canonical `.git` clone URL (`https://github.com/<owner>/<repo>.git` for shorthand).
   * @param repoSlugOut Optional output receiving the repository basename without `.git`.
   * @param errorOut Optional output receiving a human-readable rejection reason.
   * @return True when @p rawUrl is safe and valid.
   */
  static bool normalizeAndValidateUrl(const QString& rawUrl,
                                      QString* normalizedOut = nullptr,
                                      QString* repoSlugOut = nullptr,
                                      QString* errorOut = nullptr);

  /**
   * @brief Alias for `normalizeAndValidateUrl()` accepting full GitHub URLs and `owner/repo` shorthand.
   */
  static bool validateGitHubUrl(const QString& rawUrl,
                                QString* normalizedOut = nullptr,
                                QString* repoSlugOut = nullptr,
                                QString* errorOut = nullptr);

  /**
   * @brief Validates an optional git branch, tag, or commit SHA and rejects leading `-` flag injection.
   * @param rawRef User-supplied branch/tag/SHA string (empty means default remote HEAD).
   * @param errorOut Optional output receiving a human-readable rejection reason.
   * @return True when @p rawRef is empty or a safe ref token.
   */
  static bool validateGitRef(const QString& rawRef, QString* errorOut = nullptr);

  /** @brief Resolves the local filesystem directory containing `gridlock/plugin_api.h`. */
  static QString resolveGridLockIncludeDir();

  /** @brief Returns true while a clone, build, or manifest sync pipeline is actively executing. */
  bool isRunning() const noexcept { return m_running; }

  /**
   * @brief Starts an asynchronous `git clone` + build + atomic install pipeline for @p repoUrl.
   * @param repoUrl Validated `owner/repo` shorthand or GitHub HTTPS/SSH URL.
   * @param gitRef Optional branch, tag, or commit SHA (empty for default branch).
   * @param scope Target installation scope (`PluginScope::UserGlobal` or `PluginScope::Workspace`).
   * @param targetDirOverride Optional explicit destination directory override (used in isolated tests).
   * @param recordInManifest When true, appends/updates the entry in the target scope's `plugins.toml`.
   * @param buildMode Build mode override (`"auto"`, `"meson"`, `"cmake"`, `"make"`, or `"single"`).
   * @return True if the pipeline started; false if already running or validation failed.
   */
  bool startInstallFromGitHub(const QString& repoUrl,
                              const QString& gitRef,
                              PluginScope scope,
                              const QString& targetDirOverride = QString(),
                              bool recordInManifest = true,
                              const QString& buildMode = QStringLiteral("auto"));

  /**
   * @brief Synchronizes declared `[[plugins]]` from `plugins.toml`, cloning/building missing or outdated entries.
   * @param userManifestPath Path to user-global `~/.config/gridlock/plugins.toml` (may be empty).
   * @param workspaceManifestPath Path to `<workspace>/.gridlock/plugins.toml` (synced only when @p scope is `Workspace`).
   * @param scope Scope selection (`UserGlobal` syncs global manifest only; `Workspace` syncs global if present plus workspace).
   * @return True if sync started or completed immediately; false if busy or manifest validation failed.
   */
  bool syncFromManifests(const QString& userManifestPath,
                         const QString& workspaceManifestPath,
                         PluginScope scope = PluginScope::UserGlobal);

  /**
   * @brief Runs compilation and atomic installation directly on an existing local source directory.
   * @param sourceDir Local directory containing `meson.build`, `CMakeLists.txt`, `Makefile`, or `.c`/`.cpp` sources.
   * @param scope Target installation scope (`PluginScope::UserGlobal` or `PluginScope::Workspace`).
   * @param targetDirOverride Optional explicit destination directory override.
   * @return True if the build pipeline started cleanly.
   */
  bool startBuildFromSourceDirectory(const QString& sourceDir,
                                     PluginScope scope,
                                     const QString& targetDirOverride = QString());

  /** @brief Cancels the active clone/build/sync subprocess and cleans up temporary staging state. */
  void cancelInstall();

 signals:
  /** @brief Emitted whenever a new pipeline stage begins (e.g. cloning, configuring, compiling). */
  void stepStarted(const QString& description);

  /** @brief Emitted incrementally as stdout/stderr text arrives from `git` or compiler tools. */
  void logOutputReceived(const QString& textChunk);

  /** @brief Emitted once when the single install or multi-plugin manifest sync completes, fails, or is cancelled. */
  void installFinished(const gridlock::core::plugins::PluginBuildResult& result);

 private:
  struct BuildStep {
    QString program;
    QStringList args;
    QString workingDir;
    QString description;
  };

  struct SyncTask {
    QString rawRepo;
    QString normalizedUrl;
    QString repoSlug;
    QString gitRef;
    QString buildMode = QStringLiteral("auto");
    PluginScope scope = PluginScope::UserGlobal;
    QString targetDirOverride;
    QString manifestPathToRecord;
  };

  void resetRunState(bool batchSync);
  void launchNextSyncTask();
  bool enqueueDetectedBuildSteps(const QString& rootDir,
                                 const QString& fallbackStem,
                                 const QString& buildMode,
                                 QString* errorOut);
  void launchNextStep();
  void handleProcessOutput(const QByteArray& rawBytes);
  void classifyDiagnosticLine(const QString& line);
  void finalizeInstalledArtifacts();
  void handleTaskFailure(const QString& headlineError);
  void cleanupStaging();
  QString resolveTargetInstallDir() const;
  static QString resolveScopeInstallDir(PluginScope scope, const QString& manifestPathHint = QString());

  QProcess* m_process = nullptr;
  std::unique_ptr<QTemporaryDir> m_stagingDir;
  QString m_clonedRepoDir;
  QString m_activeSourceRoot;
  QString m_repoSlug;
  QString m_buildMode = QStringLiteral("auto");
  QString m_targetDirOverride;
  PluginScope m_scope = PluginScope::UserGlobal;
  SyncTask m_currentTask;
  QQueue<SyncTask> m_syncQueue;
  QQueue<BuildStep> m_pendingSteps;
  QString m_fullLog;
  QString m_partialLine;
  QStringList m_warnings;
  QStringList m_errors;
  QString m_lastInstalledPath;
  QString m_lastCommitSha;
  int m_installedCount = 0;
  bool m_running = false;
  bool m_awaitingClone = false;
  bool m_cancelled = false;
  bool m_isBatchSync = false;
  bool m_anySyncError = false;
};

}  // namespace gridlock::core::plugins

Q_DECLARE_METATYPE(gridlock::core::plugins::PluginBuildResult)

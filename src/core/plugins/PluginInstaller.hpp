/**
 * @file PluginInstaller.hpp
 * @brief Asynchronous multi-forge Git repository clone, manifest sync, build detection, and atomic plugin installer.
 *
 * Supports GitHub, GitLab (including nested subgroups), Codeberg, Gitea, and custom self-hosted
 * `https://` or `git@` Git repositories (plus `file://` in unit tests), clones into isolated
 * temporary staging directories without shell interpolation, compiles via Meson, CMake, Make, or
 * direct C99/C++23 builds, updates `plugins.toml` and `plugins-lock.toml`, and runs manifest sync.
 */

#pragma once

#include "PluginManager.hpp"

#include <cstdint>
#include <memory>

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

namespace gridlock::core::plugins {

/**
 * @enum GitForgeHost
 * @brief Target Git forge preset used when resolving unprefixed `owner/repo` shorthand strings.
 */
enum class GitForgeHost : uint8_t {
  GitHub = 0,     ///< Resolves `<owner>/<repo>` to `https://github.com/<owner>/<repo>.git`.
  GitLab = 1,     ///< Resolves `<group>[/<sub...>]/<repo>` to `https://gitlab.com/<path>.git`.
  Codeberg = 2,   ///< Resolves `<owner>/<repo>` to `https://codeberg.org/<owner>/<repo>.git`.
  CustomUrl = 3   ///< Requires an explicit `https://`, `git@`, `file://`, or forge-prefixed URL.
};

/**
 * @struct PluginBuildResult
 * @brief Final outcome and parsed compiler diagnostics for an asynchronous plugin build or manifest sync run.
 */
struct PluginBuildResult {
  bool success = false;          ///< True when all requested clone, build, and install operations succeeded.
  QString installedPath;         ///< Canonical or absolute path to the most recently installed shared library.
  QStringList warnings;          ///< Aggregated compiler diagnostic lines matching `warning:`.
  QStringList errors;            ///< Aggregated compiler, linker, git, or validation error lines.
  QString fullCompilerLog;       ///< Complete combined stdout/stderr build transcript.
  QString commitSha;             ///< Resolved 40-character HEAD commit SHA for the latest git build, if any.
  int installedCount = 0;        ///< Number of plugin shared libraries compiled and installed in this run.
  bool anyBinaryChanged = false; ///< True when at least one installed binary's SHA-256 changed on disk.
};

/**
 * @struct PluginUpdateTarget
 * @brief Target specification for re-cloning and rebuilding an installed Git-backed plugin in place.
 */
struct PluginUpdateTarget {
  QString pluginId;                              ///< Discovered plugin ID (`LoadedPluginInfo::id`).
  QString gitRepo;                               ///< Canonical or forge-prefixed Git repository source.
  QString gitRef;                                ///< Optional branch, tag, or commit SHA (empty = default HEAD).
  QString buildSystem = QStringLiteral("auto");  ///< Build mode (`"auto"`, `"meson"`, `"cmake"`, `"make"`, `"single"`).
  PluginScope scope = PluginScope::UserGlobal;   ///< Existing scope (`UserGlobal` or `Workspace`).
  QString installDir;                            ///< Existing destination directory containing the binary.
  QString existingBinaryName;                    ///< Existing `.so`/`.dylib`/`.dll` filename to replace atomically.
  bool preserveWorkspaceTrust = true;            ///< Refresh workspace SHA-256 trust when previously approved.
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
   * @brief Validates and normalizes forge-prefixed (`github:`, `gitlab:`, `codeberg:`), unprefixed shorthand, or full `https://`/`git@` URLs.
   * @param input User-supplied shorthand or Git repository URL.
   * @param defaultForge Preset forge used when @p input is unprefixed `<owner>/<repo>`.
   * @param normalizedOut Optional output receiving the canonical clone URL.
   * @param repoSlugOut Optional output receiving the repository basename without `.git`.
   * @param errorOut Optional output receiving a human-readable rejection reason.
   * @return True when @p input passes strict argument-injection and path-traversal checks.
   */
  static bool normalizeAndValidateUrl(const QString& input,
                                      GitForgeHost defaultForge = GitForgeHost::GitHub,
                                      QString* normalizedOut = nullptr,
                                      QString* repoSlugOut = nullptr,
                                      QString* errorOut = nullptr);

  /**
   * @brief Convenience overload resolving unprefixed shorthand against `GitForgeHost::GitHub`.
   */
  static bool normalizeAndValidateUrl(const QString& input,
                                      QString* normalizedOut,
                                      QString* repoSlugOut = nullptr,
                                      QString* errorOut = nullptr);

  /** @brief Backward-compatible alias forwarding to `normalizeAndValidateUrl()`. */
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
   * @brief Starts an asynchronous `git clone` + build + atomic install pipeline for any supported Git forge or custom URL.
   * @param repoSpec Shorthand (`owner/repo`, `gitlab:group/sub/repo`, `codeberg:owner/repo`) or full `https://`/`git@` URL.
   * @param gitRef Optional branch, tag, or commit SHA (empty for default branch).
   * @param scope Target installation scope (`PluginScope::UserGlobal` or `PluginScope::Workspace`).
   * @param defaultForge Preset forge used when @p repoSpec is unprefixed shorthand.
   * @param targetDirOverride Optional explicit destination directory override (used in isolated tests).
   * @param recordInManifest When true, appends/updates the entry in the target scope's `plugins.toml`.
   * @param buildMode Build mode override (`"auto"`, `"meson"`, `"cmake"`, `"make"`, or `"single"`).
   * @return True if the pipeline started; false if already running or validation failed.
   */
  bool startInstallFromGit(const QString& repoSpec,
                           const QString& gitRef,
                           PluginScope scope,
                           GitForgeHost defaultForge = GitForgeHost::GitHub,
                           const QString& targetDirOverride = QString(),
                           bool recordInManifest = true,
                           const QString& buildMode = QStringLiteral("auto"));

  /** @brief Starts an install pipeline defaulting unprefixed shorthand to `GitForgeHost::GitHub`. */
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
   * @brief Sequentially re-clones and rebuilds one or more Git-installed plugins into their existing scopes.
   * @param targets Ordered list of installed Git-backed plugins to update in place.
   * @return True if the update queue started; false if busy, empty, or validation failed.
   */
  bool startUpdatePlugins(const QVector<PluginUpdateTarget>& targets);

  /**
   * @brief Runs compilation and atomic installation directly on an existing local source directory.
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
    QString existingBinaryName;
    bool preserveWorkspaceTrust = true;
  };

  void resetRunState(bool batchSync);
  void launchNextSyncTask();
  bool enqueueDetectedBuildSteps(const QString& rootDir, const QString& fallbackStem,
                                 const QString& buildMode, QString* errorOut);
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
  bool m_anyBinaryChanged = false;
};

}  // namespace gridlock::core::plugins

Q_DECLARE_METATYPE(gridlock::core::plugins::PluginBuildResult)

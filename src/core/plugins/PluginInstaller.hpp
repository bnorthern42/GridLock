/**
 * @file PluginInstaller.hpp
 * @brief Asynchronous GitHub repository clone, build-system auto-detection, and atomic plugin installer.
 *
 * Clones validated `https://github.com/<owner>/<repo>[.git]` or `git@github.com:<owner>/<repo>.git`
 * repositories into an isolated temporary staging directory without shell interpolation, auto-detects
 * Meson, CMake, Make, or single-file C99/C++23 sources, streams compiler diagnostics live, and
 * atomically installs compiled shared libraries into global or workspace plugin directories.
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
 * @brief Final outcome and parsed compiler diagnostics for an asynchronous plugin build/install run.
 */
struct PluginBuildResult {
  bool success = false;      ///< True when clone, compilation, and atomic shared-library installation succeeded.
  QString installedPath;     ///< Canonical or absolute path to the primary installed `.so`/`.dylib`/`.dll`.
  QStringList warnings;      ///< Compiler diagnostic lines matching `warning:`.
  QStringList errors;        ///< Compiler, linker, git, or validation error lines.
  QString fullCompilerLog;   ///< Complete combined stdout/stderr build transcript.
};

/**
 * @class PluginInstaller
 * @brief Drives shell-free `QProcess` steps to clone, compile, and install native C99 ABI plugins.
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
   * @brief Strictly validates a GitHub HTTPS or SSH repository URL and rejects leading `-` flags.
   * @param rawUrl User-supplied repository URL string.
   * @param normalizedOut Optional output receiving the validated clone URL.
   * @param repoSlugOut Optional output receiving the repository basename without `.git`.
   * @param errorOut Optional output receiving a human-readable rejection reason.
   * @return True when @p rawUrl is a safe GitHub repository URL.
   */
  static bool validateGitHubUrl(const QString& rawUrl,
                                QString* normalizedOut = nullptr,
                                QString* repoSlugOut = nullptr,
                                QString* errorOut = nullptr);

  /**
   * @brief Validates an optional git branch or tag name and rejects leading `-` flag injection.
   * @param rawRef User-supplied branch/tag string (empty means default remote HEAD).
   * @param errorOut Optional output receiving a human-readable rejection reason.
   * @return True when @p rawRef is empty or a safe ref token.
   */
  static bool validateGitRef(const QString& rawRef, QString* errorOut = nullptr);

  /** @brief Resolves the local filesystem directory containing `gridlock/plugin_api.h`. */
  static QString resolveGridLockIncludeDir();

  /** @brief Returns true while a clone or build step is actively executing. */
  bool isRunning() const noexcept { return m_running; }

  /**
   * @brief Starts an asynchronous `git clone` + build + atomic install pipeline for @p repoUrl.
   * @param repoUrl Validated `https://github.com/<owner>/<repo>[.git]` or `git@github.com:<owner>/<repo>.git`.
   * @param gitRef Optional branch or tag name (empty for default branch).
   * @param scope Target installation scope (`PluginScope::UserGlobal` or `PluginScope::Workspace`).
   * @param targetDirOverride Optional explicit destination directory override (used in isolated tests).
   * @return True if the pipeline started; false if already running or validation failed (emitting `installFinished`).
   */
  bool startInstallFromGitHub(const QString& repoUrl,
                              const QString& gitRef,
                              PluginScope scope,
                              const QString& targetDirOverride = QString());

  /**
   * @brief Runs auto-detected compilation and atomic installation directly on an existing local source directory.
   * @param sourceDir Local directory containing `meson.build`, `CMakeLists.txt`, `Makefile`, or `.c`/`.cpp` sources.
   * @param scope Target installation scope (`PluginScope::UserGlobal` or `PluginScope::Workspace`).
   * @param targetDirOverride Optional explicit destination directory override.
   * @return True if the build pipeline started cleanly.
   */
  bool startBuildFromSourceDirectory(const QString& sourceDir,
                                     PluginScope scope,
                                     const QString& targetDirOverride = QString());

  /** @brief Cancels the active clone/build subprocess and cleans up temporary staging state. */
  void cancelInstall();

 signals:
  /** @brief Emitted whenever a new pipeline stage begins (e.g. cloning, configuring, compiling). */
  void stepStarted(const QString& description);

  /** @brief Emitted incrementally as stdout/stderr text arrives from `git` or compiler tools. */
  void logOutputReceived(const QString& textChunk);

  /** @brief Emitted once when the pipeline completes, fails, or is cancelled. */
  void installFinished(const gridlock::core::plugins::PluginBuildResult& result);

 private:
  struct BuildStep {
    QString program;
    QStringList args;
    QString workingDir;
    QString description;
  };

  bool enqueueDetectedBuildSteps(const QString& rootDir, const QString& fallbackStem, QString* errorOut);
  void launchNextStep();
  void handleProcessOutput(const QByteArray& rawBytes);
  void classifyDiagnosticLine(const QString& line);
  void finalizeInstalledArtifacts();
  void finishWithFailure(const QString& headlineError);
  void cleanupStaging();
  QString resolveTargetInstallDir() const;

  QProcess* m_process = nullptr;
  std::unique_ptr<QTemporaryDir> m_stagingDir;
  QString m_activeSourceRoot;
  QString m_repoSlug;
  QString m_targetDirOverride;
  PluginScope m_scope = PluginScope::UserGlobal;
  QQueue<BuildStep> m_pendingSteps;
  QString m_fullLog;
  QString m_partialLine;
  QStringList m_warnings;
  QStringList m_errors;
  bool m_running = false;
  bool m_awaitingClone = false;
  bool m_cancelled = false;
};

}  // namespace gridlock::core::plugins

Q_DECLARE_METATYPE(gridlock::core::plugins::PluginBuildResult)

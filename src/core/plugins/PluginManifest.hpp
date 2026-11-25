/**
 * @file PluginManifest.hpp
 * @brief Declarative `plugins.toml` manifest parser, multi-forge URL validator, and `plugins-lock.toml` manager.
 *
 * Parses `[[plugins]]` declarations from `~/.config/gridlock/plugins.toml` and
 * `<workspace>/.gridlock/plugins.toml` across GitHub, GitLab, Codeberg, Gitea, and custom
 * self-hosted Git URLs, validates git refs and build modes, and maintains user-global SHA-256
 * and commit-SHA pins in `~/.config/gridlock/plugins-lock.toml`.
 */

#pragma once

#include "PluginInstaller.hpp"
#include "PluginManager.hpp"

#include <QString>
#include <QVector>

namespace gridlock::core::plugins {

/**
 * @struct PluginManifestEntry
 * @brief Single `[[plugins]]` declaration parsed from `plugins.toml`.
 */
struct PluginManifestEntry {
  QString repo;                              ///< Raw repository spec (`owner/repo`, `gitlab:...`, `codeberg:...`, or full URL).
  QString normalizedUrl;                     ///< Canonical validated Git clone URL.
  QString repoSlug;                          ///< Repository basename without `.git`.
  QString ref;                               ///< Optional branch, tag, or commit SHA (empty = remote default HEAD).
  QString build = QStringLiteral("auto");    ///< Build mode: `"auto"`, `"meson"`, `"cmake"`, `"make"`, or `"single"`.
  bool enabled = true;                       ///< Whether `syncFromManifests()` should install/update this entry.
};

/**
 * @struct PluginLockEntry
 * @brief Pinned build provenance stored in user-global `~/.config/gridlock/plugins-lock.toml`.
 */
struct PluginLockEntry {
  QString repo;          ///< Canonical normalized Git clone URL.
  QString scope;         ///< Installation scope token (`"global"` or `"workspace"`).
  QString ref;           ///< Declared branch, tag, or commit ref at install time.
  QString commitSha;     ///< Resolved 40-character lowercase Git commit SHA (`git rev-parse HEAD`).
  QString binaryName;    ///< Installed shared-library filename (e.g. `libmesh_probe.so`).
  QString binarySha256;  ///< Lowercase 64-character SHA-256 hex digest of the installed binary.
};

/**
 * @class PluginManifest
 * @brief Stateless helpers for multi-forge URL normalization, `plugins.toml` parsing, and `plugins-lock.toml` persistence.
 */
class PluginManifest {
 public:
  /** @brief Returns the user-global manifest path (`~/.config/gridlock/plugins.toml`). */
  static QString userManifestPath();

  /** @brief Returns the active workspace manifest path (`<workspace>/.gridlock/plugins.toml`), or empty if none. */
  static QString workspaceManifestPath();

  /** @brief Returns the user-global lockfile path (`~/.config/gridlock/plugins-lock.toml`). */
  static QString userLockFilePath();

  /** @brief Converts @p scope to `"global"` or `"workspace"`. */
  static QString scopeToString(PluginScope scope);

  /**
   * @brief Normalizes and validates forge-prefixed (`github:`, `gitlab:`, `codeberg:`), shorthand, `https://`, `git@`, and `file://` URLs.
   */
  static bool normalizeGitUrl(const QString& rawInput,
                              GitForgeHost defaultForge,
                              QString* normalizedOut = nullptr,
                              QString* repoSlugOut = nullptr,
                              QString* errorOut = nullptr);

  /** @brief Formats @p rawInput with an explicit `gitlab:` or `codeberg:` prefix when needed for `plugins.toml` persistence. */
  static QString formatManifestRepoSpec(const QString& rawInput, GitForgeHost defaultForge);

  /**
   * @brief Validates a `build` mode string (`"auto" | "meson" | "cmake" | "make" | "single"`).
   */
  static bool validateBuildMode(const QString& rawBuild,
                                QString* normalizedOut = nullptr,
                                QString* errorOut = nullptr);

  /**
   * @brief Parses `[[plugins]]` entries from in-memory TOML content.
   */
  static bool parseManifestToml(const QString& tomlContent,
                                QVector<PluginManifestEntry>* outEntries,
                                QString* errorOut = nullptr);

  /**
   * @brief Loads `[[plugins]]` entries from @p filePath (missing file yields empty list and returns true).
   */
  static bool loadManifestFile(const QString& filePath,
                               QVector<PluginManifestEntry>* outEntries,
                               QString* errorOut = nullptr);

  /** @brief Creates a commented starter `plugins.toml` at @p filePath if it does not already exist. */
  static bool ensureManifestTemplate(const QString& filePath, QString* errorOut = nullptr);

  /** @brief Inserts or updates @p entry (deduplicated by `normalizedUrl`) in @p filePath atomically. */
  static bool upsertManifestEntry(const QString& filePath,
                                  const PluginManifestEntry& entry,
                                  QString* errorOut = nullptr);

  /** @brief Loads pinned `PluginLockEntry` records from user-global @p lockFilePath. */
  static bool loadLockFile(const QString& lockFilePath,
                           QVector<PluginLockEntry>* outEntries,
                           QString* errorOut = nullptr);

  /** @brief Inserts or updates @p entry (keyed by `repo` + `scope`) in @p lockFilePath atomically. */
  static bool upsertLockEntry(const QString& lockFilePath,
                              const PluginLockEntry& entry,
                              QString* errorOut = nullptr);

  /** @brief Computes the lowercase SHA-256 hex digest of a regular file on disk. */
  static QString computeFileSha256Hex(const QString& filePath);

  /** @brief Queries `git rev-parse HEAD` inside @p repoDir via shell-free `QProcess`. */
  static QString queryLocalHeadSha(const QString& repoDir);

  /** @brief Queries `git ls-remote` for @p normalizedUrl and @p gitRef via shell-free `QProcess`. */
  static QString queryRemoteRefSha(const QString& normalizedUrl, const QString& gitRef);

  /** @brief Checks whether @p entry is already installed, SHA-256 verified, and commit-current. */
  static bool isManifestEntryUpToDate(const PluginManifestEntry& entry,
                                      PluginScope scope,
                                      const QString& installDir,
                                      const QVector<PluginLockEntry>& lockEntries,
                                      QString* outCanonBinaryPath = nullptr,
                                      QString* outBinarySha256 = nullptr);
};

}  // namespace gridlock::core::plugins

/**
 * @file PluginManifest.hpp
 * @brief Declarative `plugins.toml` manifest parser and user-global `plugins-lock.toml` lockfile manager.
 *
 * Parses `[[plugins]]` declarations from `~/.config/gridlock/plugins.toml` and
 * `<workspace>/.gridlock/plugins.toml`, validates repository URLs/shorthands, git refs,
 * and build-system overrides, and maintains user-global SHA-256 and commit-SHA pins in
 * `~/.config/gridlock/plugins-lock.toml`.
 */

#pragma once

#include "PluginManager.hpp"

#include <QString>
#include <QVector>

namespace gridlock::core::plugins {

/**
 * @struct PluginManifestEntry
 * @brief Single `[[plugins]]` declaration parsed from `plugins.toml`.
 */
struct PluginManifestEntry {
  QString repo;                              ///< Raw repository spec (`owner/repo` shorthand or full GitHub URL).
  QString normalizedUrl;                     ///< Canonical validated GitHub `.git` clone URL.
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
  QString repo;          ///< Canonical normalized GitHub `.git` URL.
  QString scope;         ///< Installation scope token (`"global"` or `"workspace"`).
  QString ref;           ///< Declared branch, tag, or commit ref at install time.
  QString commitSha;     ///< Resolved 40-character lowercase Git commit SHA (`git rev-parse HEAD`).
  QString binaryName;    ///< Installed shared-library filename (e.g. `libmesh_probe.so`).
  QString binarySha256;  ///< Lowercase 64-character SHA-256 hex digest of the installed binary.
};

/**
 * @class PluginManifest
 * @brief Stateless helpers for reading, validating, and atomically updating `plugins.toml` and `plugins-lock.toml`.
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
   * @brief Validates a `build` mode string (`"auto" | "meson" | "cmake" | "make" | "single"`).
   * @param rawBuild Input build mode string (empty defaults to `"auto"`).
   * @param normalizedOut Optional output receiving the lowercase canonical build token.
   * @param errorOut Optional output receiving a validation error message.
   * @return True if valid.
   */
  static bool validateBuildMode(const QString& rawBuild,
                                QString* normalizedOut = nullptr,
                                QString* errorOut = nullptr);

  /**
   * @brief Parses `[[plugins]]` entries from in-memory TOML content.
   * @param tomlContent Raw UTF-8 TOML document text.
   * @param outEntries Output vector populated with validated manifest entries.
   * @param errorOut Optional output receiving syntax or schema validation errors.
   * @return True when @p tomlContent is valid TOML and all `[[plugins]]` entries pass validation.
   */
  static bool parseManifestToml(const QString& tomlContent,
                                QVector<PluginManifestEntry>* outEntries,
                                QString* errorOut = nullptr);

  /**
   * @brief Loads `[[plugins]]` entries from @p filePath (missing file yields empty list and returns true).
   * @param filePath Path to `plugins.toml`.
   * @param outEntries Output vector populated with validated manifest entries.
   * @param errorOut Optional output receiving I/O or validation errors.
   * @return True if missing or parsed cleanly.
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

  /**
   * @brief Checks whether @p entry is already installed, SHA-256 verified, and commit-current.
   * @param entry Validated manifest entry.
   * @param scope Target installation scope.
   * @param installDir Target plugin directory containing built shared libraries.
   * @param lockEntries Loaded user-global lockfile entries.
   * @param outCanonBinaryPath Optional output receiving the canonical path to the verified binary.
   * @param outBinarySha256 Optional output receiving the verified lowercase SHA-256 digest.
   * @return True when no clone or rebuild is needed.
   */
  static bool isManifestEntryUpToDate(const PluginManifestEntry& entry,
                                      PluginScope scope,
                                      const QString& installDir,
                                      const QVector<PluginLockEntry>& lockEntries,
                                      QString* outCanonBinaryPath = nullptr,
                                      QString* outBinarySha256 = nullptr);
};

}  // namespace gridlock::core::plugins

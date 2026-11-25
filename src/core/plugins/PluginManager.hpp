/**
 * @file PluginManager.hpp
 * @brief Native shared-library discovery, ABI v1.0 validator, and plugin lifecycle manager.
 *
 * Scans user (`~/.config/gridlock/plugins/`) and workspace (`<workspace>/.gridlock/plugins/`)
 * directories for shared libraries (`.so`, `.dylib`, `.dll`), loads symbols with
 * `QLibrary::ResolveAllSymbolsHint`, validates the frozen C99 ABI v1.0 contract,
 * enforces `ConfigManager` `[plugins] disabled_ids`, and dispatches session/stop/shutdown
 * callbacks cleanly.
 */

#pragma once

#include "PluginHostBridge.hpp"
#include "gridlock/plugin_api.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <QHash>
#include <QLibrary>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gridlock {
class GdbRankCoordinator;
}  // namespace gridlock

namespace gridlock::core::plugins {

/**
 * @struct LoadedPluginInfo
 * @brief Diagnostic and status snapshot for a discovered or rejected native plugin.
 */
struct LoadedPluginInfo {
  QString id;              ///< Unique plugin ID (`desc.plugin_id_utf8`), or stem if rejected early.
  QString name;            ///< Human-readable title (`desc.plugin_name_utf8`).
  QString version;         ///< Semantic version string (`desc.plugin_version_utf8`).
  QString description;     ///< Short capability summary (`desc.description_utf8`).
  QString libraryPath;     ///< Canonical filesystem path to the `.so` / `.dylib` / `.dll`.
  uint32_t abiMajor = 0u;  ///< Reported `desc.abi_major` (0 if symbol resolution failed).
  uint32_t abiMinor = 0u;  ///< Reported `desc.abi_minor`.
  bool enabled = false;    ///< True when loaded, validated, and not disabled in `ConfigManager`.
  QString errorMessage;    ///< Non-empty human-readable diagnostic when load or ABI gate failed.
};

/**
 * @class PluginManager
 * @brief Manages native C99 plugin shared libraries, ABI validation, and debugger event routing.
 */
class PluginManager : public QObject {
  Q_OBJECT

 public:
  /**
   * @brief Constructs a `PluginManager` owning a `PluginHostBridge` for @p coordinator.
   * @param coordinator Optional `GdbRankCoordinator` bound to the host bridge.
   * @param parent Optional owning `QObject`.
   */
  explicit PluginManager(gridlock::GdbRankCoordinator* coordinator = nullptr,
                         QObject* parent = nullptr);

  /** @brief Cleanly shuts down and unloads all active plugins in reverse order. */
  ~PluginManager() override;

  /** @brief Returns the owned `PluginHostBridge` serving `GridLockHostApi` callbacks. */
  PluginHostBridge* hostBridge() noexcept { return &m_bridge; }

  /** @brief Returns the owned `PluginHostBridge` (const overload). */
  const PluginHostBridge* hostBridge() const noexcept { return &m_bridge; }

  /** @brief Rebinds the `GdbRankCoordinator` used for host callbacks and stop notifications. */
  void setCoordinator(gridlock::GdbRankCoordinator* coordinator);

  /** @brief Returns the ordered list of search directories (`~/.config/gridlock/plugins`, `<workspace>/.gridlock/plugins`). */
  QStringList searchDirectories() const;

  /**
   * @brief Unloads any existing plugins, scans `searchDirectories()`, and loads enabled plugins.
   * @return Number of plugins that passed ABI validation and remain enabled.
   */
  int discoverAndLoad();

  /**
   * @brief Loads and validates a single plugin shared library from @p libraryPath.
   * @param libraryPath Path to a `.so`, `.dylib`, or `.dll` plugin artifact.
   * @return `true` when the plugin initialized cleanly and is active; `false` if disabled or rejected.
   */
  bool loadPluginFromPath(const QString& libraryPath);

  /**
   * @brief Enables or disables @p pluginId, updating `[plugins] disabled_ids` and loading/unloading live.
   * @param pluginId Target plugin ID (`LoadedPluginInfo::id`).
   * @param enabled `false` to invoke `shutdown`, unregister actions, and `QLibrary::unload()`; `true` to reload.
   * @return `true` if the transition succeeded, `false` if not found or reload failed.
   */
  bool setPluginEnabled(const QString& pluginId, bool enabled);

  /** @brief Shuts down and unloads all currently active plugins and clears recorded entries. */
  void unloadAll();

  /** @brief Returns status records for all discovered plugins (both active and rejected/disabled). */
  const QVector<LoadedPluginInfo>& plugins() const noexcept { return m_pluginInfos; }

  /** @brief Dispatches `on_session_start` to every active plugin that defines the hook. */
  void notifySessionStarted(int32_t rankCount);

  /** @brief Dispatches `on_rank_stopped` to every active plugin that defines the hook. */
  void notifyRankStopped(const GridLockStopInfo& stopInfo);

  /** @brief Convenience overload building `GridLockStopInfo` and dispatching `on_rank_stopped`. */
  void notifyRankStopped(int32_t rank,
                         const QString& file,
                         int32_t line,
                         const QString& function = QString(),
                         uint64_t pcAddress = 0u,
                         const QString& reason = QStringLiteral("stopped"));

  /** @brief Dispatches `on_session_end` to every active plugin that defines the hook. */
  void notifySessionEnded();

 signals:
  /** @brief Emitted whenever the discovered/enabled plugin list changes. */
  void pluginsChanged();

 private:
  struct ActivePluginHandle {
    QString id;
    QString libraryPath;
    GridLockPluginDescriptor descriptor{};
    std::unique_ptr<QLibrary> library;
  };

  static bool hasSharedLibrarySuffix(const QString& fileName);
  ActivePluginHandle* findActiveHandle(const QString& pluginId);
  void shutdownAndUnloadHandle(ActivePluginHandle& handle);
  void cachePluginMetadata(const QString& canonicalPath, const LoadedPluginInfo& info);
  bool lookupCachedMetadata(const QString& canonicalPath, LoadedPluginInfo* outInfo) const;

  PluginHostBridge m_bridge;
  QPointer<gridlock::GdbRankCoordinator> m_coordinator;
  QMetaObject::Connection m_stopConn;
  QVector<LoadedPluginInfo> m_pluginInfos;
  std::vector<ActivePluginHandle> m_activePlugins;
  QHash<QString, LoadedPluginInfo> m_pathMetadataCache;
};

}  // namespace gridlock::core::plugins

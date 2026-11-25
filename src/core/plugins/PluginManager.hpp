/**
 * @file PluginManager.hpp
 * @brief Native shared-library discovery, ABI v1.0 validator, and plugin lifecycle manager.
 *
 * Scans user (`~/.config/gridlock/plugins/`) and workspace (`<workspace>/.gridlock/plugins/`)
 * directories for shared libraries (`.so`, `.dylib`, `.dll`), validates SHA-256 workspace
 * trust prior to `QLibrary::load()`, enforces the frozen C99 ABI v1.x contract, serializes
 * re-entrant `on_rank_stopped` events via a FIFO queue, and guards `dlclose` teardown.
 */

#pragma once

#include "PluginHostBridge.hpp"
#include "gridlock/plugin_api.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QHash>
#include <QLibrary>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QString>
#include <QStringList>
#include <QVector>

namespace gridlock {
class GdbRankCoordinator;
}  // namespace gridlock

namespace gridlock::core::plugins {

/**
 * @enum PluginScope
 * @brief Discovery trust origin for a native plugin shared library.
 */
enum class PluginScope : uint8_t {
  UserGlobal = 0,  ///< Installed in user-owned `~/.config/gridlock/plugins/`.
  Workspace = 1    ///< Discovered inside `<workspace>/.gridlock/plugins/` (requires SHA-256 trust).
};

/**
 * @struct LoadedPluginInfo
 * @brief Diagnostic, trust, and enablement snapshot for a discovered or rejected native plugin.
 */
struct LoadedPluginInfo {
  QString id;           ///< Unique plugin ID (`desc.plugin_id_utf8`), or stem if rejected early.
  QString name;         ///< Human-readable title (`desc.plugin_name_utf8`).
  QString version;      ///< Semantic version string (`desc.plugin_version_utf8`).
  QString description;  ///< Short capability summary (`desc.description_utf8`).
  QString libraryPath;  ///< Canonical filesystem path to the `.so` / `.dylib` / `.dll`.
  QString sha256Hex;    ///< Lowercase SHA-256 hex digest computed prior to `QLibrary::load()`.
  PluginScope scope = PluginScope::UserGlobal;  ///< Discovery scope (`UserGlobal` vs `Workspace`).
  uint32_t abiMajor = 0u;        ///< Reported `desc.abi_major` (0 if symbol resolution failed).
  uint32_t abiMinor = 0u;        ///< Reported `desc.abi_minor`.
  bool trusted = true;           ///< True for `UserGlobal` or SHA-256 approved `Workspace` binaries.
  bool enabled = false;          ///< True when loaded, ABI-valid, trusted, and effectively enabled.
  bool requiresRestart = false;  ///< True when enablement/trust changed pending IDE restart.
  QString errorMessage;          ///< Human-readable diagnostic when load, trust, or ABI gate failed.
};

/**
 * @class PluginManager
 * @brief Manages native C99 plugin shared libraries, SHA-256 trust gates, ABI validation, and event routing.
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
   * @brief Unloads any existing plugins, scans `searchDirectories()`, and loads trusted enabled plugins.
   * @return Number of plugins that passed trust/ABI validation and remain enabled (or `0` if deferred).
   */
  int discoverAndLoad();

  /**
   * @brief Loads and validates a single plugin shared library from @p libraryPath.
   * @param libraryPath Path to a host-native shared library plugin artifact.
   * @return `true` when the plugin initialized cleanly and is active; `false` if untrusted, disabled, or rejected.
   */
  bool loadPluginFromPath(const QString& libraryPath);

  /**
   * @brief Enables or disables @p pluginId, updating `[plugins] disabled_ids` and loading/unloading live.
   * @param pluginId Target plugin ID (`LoadedPluginInfo::id`).
   * @param enabled `false` to invoke `shutdown`, unregister actions, and `QLibrary::unload()`; `true` to reload.
   * @return `true` if the transition succeeded, `false` if not found, untrusted, or reload failed.
   */
  bool setPluginEnabled(const QString& pluginId, bool enabled);

  /** @brief Returns true when @p pluginId is enabled globally across workspaces. */
  bool isPluginGloballyEnabled(const QString& pluginId) const;

  /** @brief Persists global enablement for @p pluginId and marks `requiresRestart = true` without `dlclose`. */
  bool setPluginGloballyEnabled(const QString& pluginId, bool enabled);

  /** @brief Returns true when @p pluginId is enabled in the active workspace. */
  bool isPluginWorkspaceEnabled(const QString& pluginId) const;

  /** @brief Persists active-workspace enablement for @p pluginId and marks `requiresRestart = true`. */
  bool setPluginWorkspaceEnabled(const QString& pluginId, bool enabled);

  /** @brief Returns true when @p pluginId is globally enabled, workspace enabled, and trusted (if workspace-scoped). */
  bool effectivePluginEnabled(const QString& pluginId) const;

  /** @brief Approves a workspace plugin's current SHA-256 digest in `ConfigManager` and loads it if enabled. */
  bool trustWorkspacePlugin(const QString& pluginId);

  /** @brief Revokes SHA-256 trust for a workspace plugin in `ConfigManager` and marks `requiresRestart` if active. */
  bool revokeWorkspacePluginTrust(const QString& pluginId);

  /** @brief Shuts down and unloads all currently active plugins (deferring safely if inside a callback). */
  void unloadAll();

  /** @brief Returns status records for all discovered plugins (both active and rejected/disabled/untrusted). */
  const QVector<LoadedPluginInfo>& plugins() const noexcept { return m_pluginInfos; }

  /** @brief Dispatches `on_session_start` to every active plugin that defines the hook. */
  void notifySessionStarted(int32_t rankCount);

  /** @brief Dispatches `on_rank_stopped` serially via a FIFO queue to prevent nested `QEventLoop` recursion. */
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
  /** @brief Emitted whenever the discovered/enabled/trusted plugin list changes. */
  void pluginsChanged();

 private:
  struct ActivePluginHandle {
    QString id;
    QString libraryPath;
    QString sha256Hex;
    PluginScope scope = PluginScope::UserGlobal;
    GridLockPluginDescriptor descriptor{};
    std::unique_ptr<PluginBindingContext> bindingCtx;
    std::unique_ptr<GridLockHostApi> hostApi;
    std::unique_ptr<QLibrary> library;
  };

  struct PendingStopEvent {
    uint32_t reservedFlags = 0u;
    int32_t rank = 0;
    int32_t threadId = 0;
    int32_t line = -1;
    int32_t frameLevel = 0;
    uint64_t pcAddress = 0u;
    QByteArray reasonUtf8;
    QByteArray fileUtf8;
    QByteArray functionUtf8;
    bool hasReason = false;
    bool hasFile = false;
    bool hasFunction = false;
  };

  class ScopedCallbackDepth {
   public:
    explicit ScopedCallbackDepth(PluginManager* owner) noexcept : m_owner(owner) {
      ++m_owner->m_callbackDepth;
    }
    ~ScopedCallbackDepth() { m_owner->leaveCallbackScope(); }
    ScopedCallbackDepth(const ScopedCallbackDepth&) = delete;
    ScopedCallbackDepth& operator=(const ScopedCallbackDepth&) = delete;

   private:
    PluginManager* m_owner;
  };

  static bool hasSharedLibrarySuffix(const QString& fileName);
  static QString computeFileSha256Hex(const QString& filePath);
  PluginScope inferScopeForPath(const QString& canonicalPath) const;
  bool loadPluginInternal(const QString& libraryPath, PluginScope scope);
  ActivePluginHandle* findActiveHandle(const QString& pluginId);
  void shutdownAndUnloadHandle(ActivePluginHandle& handle);
  void leaveCallbackScope();
  void cachePluginMetadata(const QString& canonicalPath, const LoadedPluginInfo& info);
  void flushMetadataCache();
  bool lookupCachedMetadata(const QString& canonicalPath, LoadedPluginInfo* outInfo) const;

  PluginHostBridge m_bridge;
  QPointer<gridlock::GdbRankCoordinator> m_coordinator;
  QMetaObject::Connection m_stopConn;
  QVector<LoadedPluginInfo> m_pluginInfos;
  std::vector<ActivePluginHandle> m_activePlugins;
  QQueue<PendingStopEvent> m_pendingStops;
  QHash<QString, LoadedPluginInfo> m_pathMetadataCache;
  QHash<QString, LoadedPluginInfo> m_dirtyMetadataCache;
  int m_callbackDepth = 0;
  bool m_dispatchingStop = false;
  bool m_sessionActive = false;
  bool m_pendingUnloadAll = false;
  bool m_pendingDiscoverAndLoad = false;
  bool m_inDestructor = false;
  bool m_deferMetadataSync = false;
};

}  // namespace gridlock::core::plugins

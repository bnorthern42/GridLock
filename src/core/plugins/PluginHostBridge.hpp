/**
 * @file PluginHostBridge.hpp
 * @brief Thread-safe synchronous C99 `GridLockHostApi` adapter over `GdbRankCoordinator`.
 *
 * Implements every callback in `GridLockHostApi` (`alloc_bytes`, `free_bytes`,
 * `log_message`, `get_rank_count`, `get_active_rank`, `eval_expr`, `read_memory`,
 * `write_memory`, `set_breakpoint`, `remove_breakpoint`, `register_variable_action`,
 * `register_command_action`, and `emit_structured_event`). Each loaded plugin binds
 * its own `GridLockHostApi` table whose `host_ctx` points to a heap-owned
 * `PluginBindingContext`, eliminating shared mutable plugin-attribution state.
 */

#pragma once

#include "gridlock/plugin_api.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRegularExpression>
#include <QString>
#include <QVector>

namespace gridlock {
class GdbRankCoordinator;
}  // namespace gridlock

namespace gridlock::core::plugins {

class PluginHostBridge;

/**
 * @struct PluginBindingContext
 * @brief Heap-owned per-plugin identity and teardown state passed via `GridLockHostApi::host_ctx`.
 */
struct PluginBindingContext {
  PluginHostBridge* bridge = nullptr;         ///< Owning host bridge instance.
  QString pluginId;                           ///< Deterministic owning plugin identifier.
  std::atomic<bool> shuttingDown{false};      ///< Set prior to teardown to block late calls.
  const GridLockHostApi* boundApi = nullptr;  ///< Stable `GridLockHostApi` table bound to `this`.
};

/**
 * @struct RegisteredVariableAction
 * @brief Host-side record for a variable context action registered by a plugin.
 */
struct RegisteredVariableAction {
  QString pluginId;                                   ///< Owning plugin identifier.
  QString actionId;                                   ///< Unique action identifier.
  QString menuLabel;                                  ///< Display label for variable menus.
  QString typeRegex;                                  ///< Raw regular expression matching C/C++ types.
  QRegularExpression compiledRegex;                   ///< Pre-compiled type matcher.
  GridLockVariableActionCallback callback = nullptr;  ///< Plugin callback pointer.
  void* pluginCtx = nullptr;                          ///< Opaque plugin user-data pointer.
  const GridLockHostApi* hostApi = nullptr;           ///< Per-plugin host API table passed on invoke.
  const PluginBindingContext* bindingCtx = nullptr;   ///< Per-plugin binding context for teardown checks.
};

/**
 * @struct RegisteredCommandAction
 * @brief Host-side record for a global command action registered by a plugin.
 */
struct RegisteredCommandAction {
  QString pluginId;                                  ///< Owning plugin identifier.
  QString commandId;                                 ///< Unique command identifier.
  QString menuLabel;                                 ///< Display label for menus/command palette.
  QString shortcutHint;                              ///< Optional portable shortcut hint string.
  GridLockCommandActionCallback callback = nullptr;  ///< Plugin callback pointer.
  void* pluginCtx = nullptr;                         ///< Opaque plugin user-data pointer.
  const GridLockHostApi* hostApi = nullptr;          ///< Per-plugin host API table passed on invoke.
  const PluginBindingContext* bindingCtx = nullptr;  ///< Per-plugin binding context for teardown checks.
};

/**
 * @class PluginHostBridge
 * @brief Owns `GridLockHostApi` dispatch callbacks and per-plugin action/breakpoint registries.
 */
class PluginHostBridge : public QObject {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the bridge bound to an optional `GdbRankCoordinator`.
   * @param coordinator Target GDB/MI coordinator (may be attached or replaced later).
   * @param parent Optional owning `QObject`.
   */
  explicit PluginHostBridge(gridlock::GdbRankCoordinator* coordinator = nullptr,
                            QObject* parent = nullptr);

  /** @brief Destroys the bridge. */
  ~PluginHostBridge() override = default;

  /** @brief Returns the default `GridLockHostApi` v1.0 function table pointer. */
  const GridLockHostApi* hostApi() const noexcept { return &m_hostApi; }

  /** @brief Builds a `GridLockHostApi` v1.0 dispatch table bound to @p bindingCtx. */
  GridLockHostApi makeHostApi(PluginBindingContext* bindingCtx) const noexcept {
    return GridLockHostApi{
        static_cast<uint32_t>(sizeof(GridLockHostApi)),
        GRIDLOCK_PLUGIN_ABI_MAJOR,
        GRIDLOCK_PLUGIN_ABI_MINOR,
        0u,
        bindingCtx,
        &PluginHostBridge::cbAllocBytes,
        &PluginHostBridge::cbFreeBytes,
        &PluginHostBridge::cbLogMessage,
        &PluginHostBridge::cbGetRankCount,
        &PluginHostBridge::cbGetActiveRank,
        &PluginHostBridge::cbEvalExpr,
        &PluginHostBridge::cbReadMemory,
        &PluginHostBridge::cbWriteMemory,
        &PluginHostBridge::cbSetBreakpoint,
        &PluginHostBridge::cbRemoveBreakpoint,
        &PluginHostBridge::cbRegisterVariableAction,
        &PluginHostBridge::cbRegisterCommandAction,
        &PluginHostBridge::cbEmitStructuredEvent,
        {}};
  }

  /** @brief Binds or updates the active `GdbRankCoordinator` instance. */
  void setCoordinator(gridlock::GdbRankCoordinator* coordinator);

  /** @brief Returns the currently bound `GdbRankCoordinator`, or `nullptr`. */
  gridlock::GdbRankCoordinator* coordinator() const;

  /** @brief Installs an optional callback-depth observer invoked around action dispatch. */
  void setCallbackScopeHook(std::function<void(bool)> hook) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbackScopeHook = std::move(hook);
  }

  /** @brief Updates the focused zero-based MPI rank reported by `get_active_rank`. */
  void setActiveRank(int32_t rank) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeRank = rank;
  }

  /** @brief Returns the currently focused zero-based MPI rank. */
  int32_t activeRank() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeRank;
  }

  /** @brief Sets the bounded timeout (in milliseconds) for `eval_expr` and `read_memory`. */
  void setRequestTimeoutMs(int timeoutMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_timeoutMs = std::max(10, timeoutMs);
  }

  /** @brief Returns the active synchronous request timeout in milliseconds. */
  int requestTimeoutMs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_timeoutMs;
  }

  /** @brief Returns the current count of unfreed `alloc_bytes` blocks (for leak assertions). */
  int32_t liveAllocationCount() const noexcept {
    return m_liveAllocations.load(std::memory_order_relaxed);
  }

  /** @brief Begins staging registrations during `gridlock_plugin_init_v1` before ID validation. */
  void beginInitStaging() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stagingActive = true;
    m_stagedVariableActions.clear();
    m_stagedCommandActions.clear();
  }

  /** @brief Commits all staged registrations to `pluginId` (and optional `pluginHostApi`). */
  bool commitStagedActions(const QString& pluginId,
                           const GridLockHostApi* pluginHostApi = nullptr);

  /** @brief Discards all uncommitted staged registrations and breakpoints when init fails. */
  void discardStagedActions();

  /** @brief Removes all variable and command actions owned by `pluginId`. */
  void unregisterPluginActions(const QString& pluginId) {
    bool removed = false;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      const auto beforeVars = m_variableActions.size(), beforeCmds = m_commandActions.size();
      m_variableActions.removeIf([&](const RegisteredVariableAction& a) { return a.pluginId == pluginId; });
      m_commandActions.removeIf([&](const RegisteredCommandAction& c) { return c.pluginId == pluginId; });
      removed = (m_variableActions.size() != beforeVars) || (m_commandActions.size() != beforeCmds);
    }
    if (removed) emit registeredActionsChanged();
  }

  /** @brief Removes and clears all breakpoints installed by `pluginId`. */
  void unregisterPluginBreakpoints(const QString& pluginId);

  /** @brief Returns a snapshot of all registered variable actions across active plugins. */
  QVector<RegisteredVariableAction> variableActions() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_variableActions;
  }

  /** @brief Returns registered variable actions whose `typeRegex` matches `typeName`. */
  QVector<RegisteredVariableAction> matchingVariableActions(const QString& typeName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    QVector<RegisteredVariableAction> matches;
    for (const auto& act : m_variableActions) {
      if (act.compiledRegex.isValid() && act.compiledRegex.match(typeName).hasMatch()) matches.append(act);
    }
    return matches;
  }

  /** @brief Returns a snapshot of all registered command actions across active plugins. */
  QVector<RegisteredCommandAction> commandActions() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_commandActions;
  }

  /** @brief Invokes a registered variable action by `actionId` with `varCtx`. */
  GridLockStatus invokeVariableAction(const QString& actionId,
                                      const GridLockVariableContext& varCtx);

  /** @brief Invokes a registered command action by `commandId` with `cmdSpec`. */
  GridLockStatus invokeCommandAction(const QString& commandId,
                                     const GridLockCommandSpec& cmdSpec);

 signals:
  /** @brief Emitted when a plugin logs a diagnostic via `GridLockHostApi::log_message`. */
  void pluginLogReceived(const QString& pluginId, int level, const QString& message);

  /** @brief Emitted when a plugin publishes telemetry via `emit_structured_event`. */
  void structuredEventEmitted(const QString& channel, const QString& jsonPayload);

  /** @brief Emitted whenever variable or command action registrations change. */
  void registeredActionsChanged();

 private:
  struct PluginBreakpointRecord {
    QString pluginId;
    QString file;
    int32_t line = 0;
    QString condition;
    bool staged = false;
  };

  static void* GRIDLOCK_CALL cbAllocBytes(void* hostCtx, size_t numBytes) noexcept;
  static void GRIDLOCK_CALL cbFreeBytes(void* hostCtx, void* ptr) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbLogMessage(void* hostCtx,
                                                   GridLockLogLevel level,
                                                   const char* utf8Msg) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbGetRankCount(void* hostCtx, int32_t* outRanks) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbGetActiveRank(void* hostCtx, int32_t* outRank) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbEvalExpr(void* hostCtx, int32_t rank, const char* exprUtf8,
                                                 char* outUtf8, size_t cap, size_t* outLen) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbReadMemory(void* hostCtx, int32_t rank, uint64_t address,
                                                   void* outBytes, size_t numBytes, size_t* outRead) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbWriteMemory(void* hostCtx, int32_t rank, uint64_t address,
                                                    const void* inBytes, size_t numBytes, size_t* outWritten) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbSetBreakpoint(void* hostCtx, const char* fileUtf8, int32_t line,
                                                      const char* conditionUtf8, int32_t* outBpId) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbRemoveBreakpoint(void* hostCtx, int32_t bpId) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbRegisterVariableAction(
      void* hostCtx, const char* actionId, const char* menuLabel, const char* typeRegex,
      GridLockVariableActionCallback cb, void* pluginCtx) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbRegisterCommandAction(
      void* hostCtx, const char* commandId, const char* menuLabel, const char* shortcutHint,
      GridLockCommandActionCallback cb, void* pluginCtx) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbEmitStructuredEvent(
      void* hostCtx, const char* channelUtf8, const char* jsonPayloadUtf8) noexcept;

  GridLockStatus runOnObjectThread(
      const PluginBindingContext* ctx,
      const std::function<GridLockStatus(gridlock::GdbRankCoordinator*)>& fn,
      GridLockStatus missingCoordStatus = GRIDLOCK_ERR_NOT_STOPPED);
  GridLockStatus evalExprOnCoordinatorThread(const PluginBindingContext* ctx,
                                             gridlock::GdbRankCoordinator* coord,
                                             int32_t rank,
                                             const QString& expr,
                                             int timeoutMs,
                                             QByteArray* outPayload);
  GridLockStatus readMemoryOnCoordinatorThread(const PluginBindingContext* ctx,
                                               gridlock::GdbRankCoordinator* coord,
                                               int32_t rank,
                                               uint64_t address,
                                               size_t numBytes,
                                               int timeoutMs,
                                               QByteArray* outBytes);

  PluginBindingContext m_defaultContext{};
  GridLockHostApi m_hostApi{};
  mutable std::mutex m_mutex;
  QPointer<gridlock::GdbRankCoordinator> m_coordinator;
  std::function<void(bool)> m_callbackScopeHook;
  int32_t m_activeRank = 0;
  int m_timeoutMs = 2500;
  int32_t m_nextBreakpointId = 1;
  bool m_stagingActive = false;
  std::atomic<int32_t> m_liveAllocations{0};
  QVector<RegisteredVariableAction> m_variableActions;
  QVector<RegisteredCommandAction> m_commandActions;
  QVector<RegisteredVariableAction> m_stagedVariableActions;
  QVector<RegisteredCommandAction> m_stagedCommandActions;
  QHash<int32_t, PluginBreakpointRecord> m_breakpoints;
};

}  // namespace gridlock::core::plugins

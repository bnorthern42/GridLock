/**
 * @file PluginHostBridge.hpp
 * @brief Thread-safe synchronous C99 `GridLockHostApi` adapter over `GdbRankCoordinator`.
 *
 * Implements every callback in `GridLockHostApi` (`alloc_bytes`, `free_bytes`,
 * `log_message`, `get_rank_count`, `get_active_rank`, `eval_expr`, `read_memory`,
 * `write_memory`, `set_breakpoint`, `remove_breakpoint`, `register_variable_action`,
 * `register_command_action`, and `emit_structured_event`). Synchronous debugger
 * queries (`eval_expr`, `read_memory`) execute on the coordinator's thread via a
 * bounded `QEventLoop` + `QTimer` timeout without coupling plugins to Qt types.
 */

#pragma once

#include "gridlock/plugin_api.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

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

/**
 * @struct RegisteredVariableAction
 * @brief Host-side record for a variable context action registered by a plugin.
 */
struct RegisteredVariableAction {
  QString pluginId;                            ///< Owning plugin identifier.
  QString actionId;                            ///< Unique action identifier.
  QString menuLabel;                           ///< Display label for variable menus.
  QString typeRegex;                           ///< Raw regular expression matching C/C++ types.
  QRegularExpression compiledRegex;            ///< Pre-compiled type matcher.
  GridLockVariableActionCallback callback = nullptr; ///< Plugin callback pointer.
  void* pluginCtx = nullptr;                   ///< Opaque plugin user-data pointer.
};

/**
 * @struct RegisteredCommandAction
 * @brief Host-side record for a global command action registered by a plugin.
 */
struct RegisteredCommandAction {
  QString pluginId;                           ///< Owning plugin identifier.
  QString commandId;                          ///< Unique command identifier.
  QString menuLabel;                          ///< Display label for menus/command palette.
  QString shortcutHint;                       ///< Optional portable shortcut hint string.
  GridLockCommandActionCallback callback = nullptr; ///< Plugin callback pointer.
  void* pluginCtx = nullptr;                  ///< Opaque plugin user-data pointer.
};

/**
 * @class PluginHostBridge
 * @brief Owns the `GridLockHostApi` dispatch table and per-plugin action registries.
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

  /** @brief Returns the immutable `GridLockHostApi` v1.0 function table pointer. */
  const GridLockHostApi* hostApi() const noexcept { return &m_hostApi; }

  /** @brief Binds or updates the active `GdbRankCoordinator` instance. */
  void setCoordinator(gridlock::GdbRankCoordinator* coordinator);

  /** @brief Returns the currently bound `GdbRankCoordinator`, or `nullptr`. */
  gridlock::GdbRankCoordinator* coordinator() const;

  /** @brief Updates the focused zero-based MPI rank reported by `get_active_rank`. */
  void setActiveRank(int32_t rank);

  /** @brief Returns the currently focused zero-based MPI rank. */
  int32_t activeRank() const;

  /** @brief Sets the bounded timeout (in milliseconds) for `eval_expr` and `read_memory`. */
  void setRequestTimeoutMs(int timeoutMs);

  /** @brief Returns the active synchronous request timeout in milliseconds. */
  int requestTimeoutMs() const;

  /** @brief Returns the current count of unfreed `alloc_bytes` blocks (for leak assertions). */
  int32_t liveAllocationCount() const noexcept {
    return m_liveAllocations.load(std::memory_order_relaxed);
  }

  /** @brief Begins staging registrations during `gridlock_plugin_init_v1` before ID validation. */
  void beginInitStaging();

  /** @brief Assigns all staged registrations to `pluginId` once `gridlock_plugin_init_v1` succeeds. */
  bool commitStagedActions(const QString& pluginId);

  /** @brief Discards all uncommitted staged registrations when plugin initialization fails. */
  void discardStagedActions();

  /** @brief Sets the active plugin ID attributed to dynamic callback registrations. */
  void setCurrentPluginContextId(const QString& pluginId);

  /** @brief Removes all variable and command actions owned by `pluginId`. */
  void unregisterPluginActions(const QString& pluginId);

  /** @brief Returns a snapshot of all registered variable actions across active plugins. */
  QVector<RegisteredVariableAction> variableActions() const;

  /** @brief Returns registered variable actions whose `typeRegex` matches `typeName`. */
  QVector<RegisteredVariableAction> matchingVariableActions(const QString& typeName) const;

  /** @brief Returns a snapshot of all registered command actions across active plugins. */
  QVector<RegisteredCommandAction> commandActions() const;

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
    QString file;
    int32_t line = 0;
    QString condition;
  };

  static void* cbAllocBytes(void* hostCtx, size_t numBytes);
  static void cbFreeBytes(void* hostCtx, void* ptr);
  static GridLockStatus cbLogMessage(void* hostCtx, GridLockLogLevel level, const char* utf8Msg);
  static GridLockStatus cbGetRankCount(void* hostCtx, int32_t* outRanks);
  static GridLockStatus cbGetActiveRank(void* hostCtx, int32_t* outRank);
  static GridLockStatus cbEvalExpr(void* hostCtx,
                                   int32_t rank,
                                   const char* exprUtf8,
                                   char* outUtf8,
                                   size_t cap,
                                   size_t* outLen);
  static GridLockStatus cbReadMemory(void* hostCtx,
                                     int32_t rank,
                                     uint64_t address,
                                     void* outBytes,
                                     size_t numBytes,
                                     size_t* outRead);
  static GridLockStatus cbWriteMemory(void* hostCtx,
                                      int32_t rank,
                                      uint64_t address,
                                      const void* inBytes,
                                      size_t numBytes,
                                      size_t* outWritten);
  static GridLockStatus cbSetBreakpoint(void* hostCtx,
                                        const char* fileUtf8,
                                        int32_t line,
                                        const char* conditionUtf8,
                                        int32_t* outBpId);
  static GridLockStatus cbRemoveBreakpoint(void* hostCtx, int32_t bpId);
  static GridLockStatus cbRegisterVariableAction(void* hostCtx,
                                                 const char* actionId,
                                                 const char* menuLabel,
                                                 const char* typeRegex,
                                                 GridLockVariableActionCallback cb,
                                                 void* pluginCtx);
  static GridLockStatus cbRegisterCommandAction(void* hostCtx,
                                                const char* commandId,
                                                const char* menuLabel,
                                                const char* shortcutHint,
                                                GridLockCommandActionCallback cb,
                                                void* pluginCtx);
  static GridLockStatus cbEmitStructuredEvent(void* hostCtx,
                                              const char* channelUtf8,
                                              const char* jsonPayloadUtf8);

  GridLockStatus evalExprOnCoordinatorThread(gridlock::GdbRankCoordinator* coord,
                                             int32_t rank,
                                             const QString& expr,
                                             int timeoutMs,
                                             QByteArray* outPayload);
  GridLockStatus readMemoryOnCoordinatorThread(gridlock::GdbRankCoordinator* coord,
                                               int32_t rank,
                                               uint64_t address,
                                               size_t numBytes,
                                               int timeoutMs,
                                               QByteArray* outBytes);

  GridLockHostApi m_hostApi{};
  mutable std::mutex m_mutex;
  QPointer<gridlock::GdbRankCoordinator> m_coordinator;
  int32_t m_activeRank = 0;
  int m_timeoutMs = 2500;
  int32_t m_nextBreakpointId = 1;
  QString m_currentPluginId;
  bool m_stagingActive = false;
  std::atomic<int32_t> m_liveAllocations{0};
  QVector<RegisteredVariableAction> m_variableActions;
  QVector<RegisteredCommandAction> m_commandActions;
  QVector<RegisteredVariableAction> m_stagedVariableActions;
  QVector<RegisteredCommandAction> m_stagedCommandActions;
  QHash<int32_t, PluginBreakpointRecord> m_breakpoints;
};

}  // namespace gridlock::core::plugins

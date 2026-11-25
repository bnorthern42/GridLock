/**
 * @file PluginHostBridge.hpp
 * @brief Thread-safe synchronous C99 `GridLockHostApi` adapter over `GdbRankCoordinator`.
 */

#pragma once

#include "gridlock/plugin_api.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
  QString pluginId;
  QString actionId;
  QString menuLabel;
  QString typeRegex;
  QRegularExpression compiledRegex;
  GridLockVariableActionCallback callback = nullptr;
  void* pluginCtx = nullptr;
  const GridLockHostApi* hostApi = nullptr;
  const PluginBindingContext* bindingCtx = nullptr;
};

/**
 * @struct RegisteredCommandAction
 * @brief Host-side record for a global command action registered by a plugin.
 */
struct RegisteredCommandAction {
  QString pluginId;
  QString commandId;
  QString menuLabel;
  QString shortcutHint;
  GridLockCommandActionCallback callback = nullptr;
  void* pluginCtx = nullptr;
  const GridLockHostApi* hostApi = nullptr;
  const PluginBindingContext* bindingCtx = nullptr;
};

/**
 * @class PluginHostBridge
 * @brief Owns `GridLockHostApi` dispatch callbacks and per-plugin action/breakpoint registries.
 */
class PluginHostBridge : public QObject {
  Q_OBJECT

 public:
  explicit PluginHostBridge(gridlock::GdbRankCoordinator* coordinator = nullptr, QObject* parent = nullptr);
  ~PluginHostBridge() override = default;

  const GridLockHostApi* hostApi() const noexcept { return &m_hostApi; }

  GridLockHostApi makeHostApi(PluginBindingContext* bindingCtx) const noexcept {
    return GridLockHostApi{
        static_cast<uint32_t>(sizeof(GridLockHostApi)), GRIDLOCK_PLUGIN_ABI_MAJOR, GRIDLOCK_PLUGIN_ABI_MINOR, 0u,
        bindingCtx, &PluginHostBridge::cbAllocBytes, &PluginHostBridge::cbFreeBytes, &PluginHostBridge::cbLogMessage,
        &PluginHostBridge::cbGetRankCount, &PluginHostBridge::cbGetActiveRank, &PluginHostBridge::cbEvalExpr,
        &PluginHostBridge::cbReadMemory, &PluginHostBridge::cbWriteMemory, &PluginHostBridge::cbSetBreakpoint,
        &PluginHostBridge::cbRemoveBreakpoint, &PluginHostBridge::cbRegisterVariableAction,
        &PluginHostBridge::cbRegisterCommandAction, &PluginHostBridge::cbEmitStructuredEvent, {}};
  }

  void setCoordinator(gridlock::GdbRankCoordinator* coordinator);
  gridlock::GdbRankCoordinator* coordinator() const;

  void setCallbackScopeHook(std::function<void(bool)> hook) {
    std::lock_guard<std::mutex> lock(m_mutex); m_callbackScopeHook = std::move(hook);
  }
  void setActiveRank(int32_t rank) { std::lock_guard<std::mutex> lock(m_mutex); m_activeRank = rank; }
  int32_t activeRank() const { std::lock_guard<std::mutex> lock(m_mutex); return m_activeRank; }
  void setRequestTimeoutMs(int timeoutMs) { std::lock_guard<std::mutex> lock(m_mutex); m_timeoutMs = std::max(10, timeoutMs); }
  int requestTimeoutMs() const { std::lock_guard<std::mutex> lock(m_mutex); return m_timeoutMs; }
  int32_t liveAllocationCount() const noexcept { return m_liveAllocations.load(std::memory_order_relaxed); }

  void beginInitStaging() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_stagingActive = true; m_stagedVariableActions.clear(); m_stagedCommandActions.clear();
  }

  bool commitStagedActions(const QString& pluginId, const GridLockHostApi* pluginHostApi = nullptr) {
    bool changed = false;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      for (const auto& act : m_stagedVariableActions)
        if (std::any_of(m_variableActions.cbegin(), m_variableActions.cend(),
                        [&](const RegisteredVariableAction& a) { return a.actionId == act.actionId; })) return false;
      for (const auto& cmd : m_stagedCommandActions)
        if (std::any_of(m_commandActions.cbegin(), m_commandActions.cend(),
                        [&](const RegisteredCommandAction& c) { return c.commandId == cmd.commandId; })) return false;
      m_stagingActive = false;
      for (auto& act : m_stagedVariableActions) {
        act.pluginId = pluginId; if (pluginHostApi != nullptr) act.hostApi = pluginHostApi;
        m_variableActions.append(std::move(act)); changed = true;
      }
      for (auto& cmd : m_stagedCommandActions) {
        cmd.pluginId = pluginId; if (pluginHostApi != nullptr) cmd.hostApi = pluginHostApi;
        m_commandActions.append(std::move(cmd)); changed = true;
      }
      for (auto& bp : m_breakpoints) if (bp.staged) { bp.pluginId = pluginId; bp.staged = false; }
      m_stagedVariableActions.clear(); m_stagedCommandActions.clear();
    }
    if (changed) emit registeredActionsChanged();
    return true;
  }

  void discardStagedActions();

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

  void unregisterPluginBreakpoints(const QString& pluginId);

  QVector<RegisteredVariableAction> variableActions() const {
    std::lock_guard<std::mutex> lock(m_mutex); return m_variableActions;
  }
  QVector<RegisteredVariableAction> matchingVariableActions(const QString& typeName) const;
  QVector<RegisteredCommandAction> commandActions() const {
    std::lock_guard<std::mutex> lock(m_mutex); return m_commandActions;
  }

  GridLockStatus invokeVariableAction(const QString& actionId, const GridLockVariableContext& varCtx) {
    RegisteredVariableAction target;
    std::function<void(bool)> hook;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      auto it = std::find_if(m_variableActions.cbegin(), m_variableActions.cend(),
                             [&](const RegisteredVariableAction& a) { return a.actionId == actionId; });
      if (it == m_variableActions.cend() || it->callback == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
      target = *it; hook = m_callbackScopeHook;
    }
    if (target.bindingCtx != nullptr && target.bindingCtx->shuttingDown.load(std::memory_order_acquire))
      return GRIDLOCK_ERR_UNSUPPORTED;
    struct Guard { std::function<void(bool)> h; ~Guard() { if (h) h(false); } } guard{hook};
    if (guard.h) guard.h(true);
    try { return target.callback(target.pluginCtx, (target.hostApi != nullptr) ? target.hostApi : &m_hostApi, &varCtx); }
    catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
  }

  GridLockStatus invokeCommandAction(const QString& commandId, const GridLockCommandSpec& cmdSpec) {
    RegisteredCommandAction target;
    std::function<void(bool)> hook;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      auto it = std::find_if(m_commandActions.cbegin(), m_commandActions.cend(),
                             [&](const RegisteredCommandAction& c) { return c.commandId == commandId; });
      if (it == m_commandActions.cend() || it->callback == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
      target = *it; hook = m_callbackScopeHook;
    }
    if (target.bindingCtx != nullptr && target.bindingCtx->shuttingDown.load(std::memory_order_acquire))
      return GRIDLOCK_ERR_UNSUPPORTED;
    struct Guard { std::function<void(bool)> h; ~Guard() { if (h) h(false); } } guard{hook};
    if (guard.h) guard.h(true);
    try { return target.callback(target.pluginCtx, (target.hostApi != nullptr) ? target.hostApi : &m_hostApi, &cmdSpec); }
    catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
  }

 signals:
  void pluginLogReceived(const QString& pluginId, int level, const QString& message);
  void structuredEventEmitted(const QString& channel, const QString& jsonPayload);
  void registeredActionsChanged();

 private:
  struct PluginBreakpointRecord {
    QString pluginId;
    QString file;
    int32_t line = 0;
    QString condition;
    bool staged = false;
  };

  static void* GRIDLOCK_CALL cbAllocBytes(void* hostCtx, size_t numBytes) noexcept {
    try {
      auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
      if (ctx == nullptr || ctx->bridge == nullptr || numBytes == 0u) return nullptr;
      void* mem = std::malloc(numBytes);
      if (mem != nullptr) ctx->bridge->m_liveAllocations.fetch_add(1, std::memory_order_relaxed);
      return mem;
    } catch (...) { return nullptr; }
  }

  static void GRIDLOCK_CALL cbFreeBytes(void* hostCtx, void* ptr) noexcept {
    try {
      auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
      if (ctx != nullptr && ctx->bridge != nullptr && ptr != nullptr) {
        ctx->bridge->m_liveAllocations.fetch_sub(1, std::memory_order_relaxed);
        std::free(ptr);
      }
    } catch (...) {}
  }

  static GridLockStatus GRIDLOCK_CALL cbLogMessage(void* hostCtx, GridLockLogLevel level, const char* utf8Msg) noexcept;

  static GridLockStatus GRIDLOCK_CALL cbGetRankCount(void* hostCtx, int32_t* outRanks) noexcept;

  static GridLockStatus GRIDLOCK_CALL cbGetActiveRank(void* hostCtx, int32_t* outRank) noexcept {
    try {
      auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
      if (ctx == nullptr || ctx->bridge == nullptr || outRank == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
      *outRank = ctx->bridge->activeRank();
      return GRIDLOCK_OK;
    } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
  }

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
      GridLockCommandActionCallback cb, void* pluginCtx) noexcept {
    try {
      auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
      if (ctx == nullptr || ctx->bridge == nullptr || commandId == nullptr || commandId[0] == '\0' ||
          menuLabel == nullptr || menuLabel[0] == '\0' || cb == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
      if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;
      auto* self = ctx->bridge;
      const QString idStr = QString::fromUtf8(commandId);
      bool notify = false;
      {
        std::lock_guard<std::mutex> lock(self->m_mutex);
        const bool dupActive = !self->m_stagingActive && std::any_of(
            self->m_commandActions.cbegin(), self->m_commandActions.cend(),
            [&](const RegisteredCommandAction& c) { return c.commandId == idStr; });
        const bool dupStaged = std::any_of(
            self->m_stagedCommandActions.cbegin(), self->m_stagedCommandActions.cend(),
            [&](const RegisteredCommandAction& c) { return c.commandId == idStr; });
        if (dupActive || dupStaged) return GRIDLOCK_ERR_INVALID_ARG;
        RegisteredCommandAction entry{
            ctx->pluginId, idStr, QString::fromUtf8(menuLabel),
            (shortcutHint != nullptr) ? QString::fromUtf8(shortcutHint) : QString(), cb, pluginCtx,
            (ctx->boundApi != nullptr) ? ctx->boundApi : &self->m_hostApi, ctx};
        if (self->m_stagingActive) self->m_stagedCommandActions.append(std::move(entry));
        else { self->m_commandActions.append(std::move(entry)); notify = true; }
      }
      if (notify) emit self->registeredActionsChanged();
      return GRIDLOCK_OK;
    } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
  }

  static GridLockStatus GRIDLOCK_CALL cbEmitStructuredEvent(
      void* hostCtx, const char* channelUtf8, const char* jsonPayloadUtf8) noexcept;

  GridLockStatus runOnObjectThread(
      const PluginBindingContext* ctx,
      const std::function<GridLockStatus(gridlock::GdbRankCoordinator*)>& fn,
      GridLockStatus missingCoordStatus = GRIDLOCK_ERR_NOT_STOPPED);
  GridLockStatus evalExprOnCoordinatorThread(const PluginBindingContext* ctx, gridlock::GdbRankCoordinator* coord,
                                             int32_t rank, const QString& expr, int timeoutMs, QByteArray* outPayload);
  GridLockStatus readMemoryOnCoordinatorThread(const PluginBindingContext* ctx, gridlock::GdbRankCoordinator* coord,
                                               int32_t rank, uint64_t address, size_t numBytes, int timeoutMs, QByteArray* outBytes);
  GridLockStatus writeMemoryOnCoordinatorThread(const PluginBindingContext* ctx, gridlock::GdbRankCoordinator* coord,
                                                int32_t rank, uint64_t address, const uint8_t* inBytes,
                                                size_t numBytes, int timeoutMs, size_t* outWritten);

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

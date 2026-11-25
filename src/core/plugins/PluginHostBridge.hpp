/**
 * @file PluginHostBridge.hpp
 * @brief Thread-safe synchronous C99 `GridLockHostApi` adapter over `GdbRankCoordinator` and workstation UI signals.
 */

#pragma once

#include "PluginUiModels.hpp"
#include "gridlock/plugin_api.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <mutex>

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

namespace gridlock {
class GdbRankCoordinator;
}  // namespace gridlock

namespace gridlock::core::plugins {

/**
 * @class PluginHostBridge
 * @brief Owns `GridLockHostApi` dispatch callbacks, resource bounds, GUI-thread marshaling, and per-plugin registries.
 */
class PluginHostBridge : public QObject {
  Q_OBJECT

 public:
  explicit PluginHostBridge(gridlock::GdbRankCoordinator* coordinator = nullptr, QObject* parent = nullptr);
  ~PluginHostBridge() override = default;

  const GridLockHostApi* hostApi() const noexcept { return &m_hostApi; }

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
        &PluginHostBridge::cbShowMessageBox,
        &PluginHostBridge::cbPromptInput,
        &PluginHostBridge::cbShowToast,
        &PluginHostBridge::cbRegisterMenuAction,
        &PluginHostBridge::cbSetStatusItem,
        &PluginHostBridge::cbRemoveStatusItem,
        &PluginHostBridge::cbRegisterContextAction,
        &PluginHostBridge::cbNavigateToLocation,
        &PluginHostBridge::cbSetEditorAnnotation,
        &PluginHostBridge::cbClearEditorAnnotations,
        &PluginHostBridge::cbGetThemeInfo,
        &PluginHostBridge::cbPublishReportView,
        &PluginHostBridge::cbCloseReportView,
        {}};
  }

  void setCoordinator(gridlock::GdbRankCoordinator* coordinator);
  gridlock::GdbRankCoordinator* coordinator() const;

  void setCallbackScopeHook(std::function<void(bool)> hook) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callbackScopeHook = std::move(hook);
  }
  void setActiveRank(int32_t rank) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_activeRank = rank;
  }
  int32_t activeRank() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_activeRank;
  }
  void setRequestTimeoutMs(int timeoutMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_timeoutMs = std::max(10, timeoutMs);
  }
  int requestTimeoutMs() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_timeoutMs;
  }
  int32_t liveAllocationCount() const noexcept { return m_liveAllocations.load(std::memory_order_relaxed); }

  void beginInitStaging();
  bool commitStagedActions(const QString& pluginId, const GridLockHostApi* pluginHostApi = nullptr);
  void discardStagedActions();

  void unregisterPluginActions(const QString& pluginId);
  void unregisterPluginBreakpoints(const QString& pluginId);
  void unregisterPluginResources(const QString& pluginId);

  QVector<RegisteredVariableAction> variableActions() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_variableActions;
  }
  QVector<RegisteredVariableAction> matchingVariableActions(const QString& typeName) const;
  QVector<RegisteredCommandAction> commandActions() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_commandActions;
  }
  QVector<RegisteredMenuAction> menuActions() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_menuActions;
  }
  QVector<PluginStatusItem> statusItems() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_statusItems;
  }
  QVector<RegisteredContextAction> contextActions(GridLockContextSurface surface = 0) const;
  QVector<RegisteredContextAction> matchingContextActions(GridLockContextSurface surface,
                                                          const QString& filterTarget) const;
  QVector<PluginEditorAnnotation> editorAnnotations(const QString& filePath = QString()) const;
  QVector<PluginReportPanel> reportPanels() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_reportPanels;
  }

  GridLockStatus invokeVariableAction(const QString& actionId, const GridLockVariableContext& varCtx);
  GridLockStatus invokeCommandAction(const QString& commandId, const GridLockCommandSpec& cmdSpec);
  GridLockStatus invokeMenuAction(const QString& actionId, const GridLockCommandSpec& cmdSpec);
  GridLockStatus invokeStatusItem(const QString& itemId);
  GridLockStatus invokeContextAction(const QString& actionId, const GridLockSurfaceContext& surfaceCtx);

 signals:
  void pluginLogReceived(const QString& pluginId, int level, const QString& message);
  void structuredEventEmitted(const QString& channel, const QString& jsonPayload);
  void registeredActionsChanged();
  void messageBoxRequested(const QString& pluginId, int severity, int buttons,
                           const QString& title, const QString& body,
                           int32_t* outResult, bool* outHandled);
  void promptInputRequested(const QString& pluginId, const QString& title, const QString& label,
                            const QString& defaultValue, QString* outValue,
                            bool* outAccepted, bool* outHandled);
  void toastRequested(const QString& pluginId, int severity, const QString& title,
                      const QString& body, int durationMs);
  void menuActionsChanged();
  void statusItemUpdated(const gridlock::core::plugins::PluginStatusItem& item);
  void statusItemRemoved(const QString& pluginId, const QString& itemId);
  void contextActionsChanged();
  void navigateRequested(const QString& pluginId, const QString& filePath, int line, int column);
  void editorAnnotationUpdated(const gridlock::core::plugins::PluginEditorAnnotation& annotation);
  void editorAnnotationsCleared(const QString& pluginId, const QString& idPrefix);
  void themeInfoRequested(GridLockThemeInfo* outTheme, bool* outHandled);
  void reportViewPublished(const gridlock::core::plugins::PluginReportPanel& panel);
  void reportViewClosed(const QString& pluginId, const QString& panelId);

 private:
  struct PluginBreakpointRecord {
    QString pluginId;
    QString file;
    int32_t line = 0;
    QString condition;
    bool staged = false;
  };

  struct CachedPromptReply {
    QString pluginId;
    QString title;
    QString label;
    QByteArray utf8Value;
    bool valid = false;
  };

  static void* GRIDLOCK_CALL cbAllocBytes(void* hostCtx, size_t numBytes) noexcept {
    try {
      auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
      if (ctx == nullptr || ctx->bridge == nullptr || numBytes == 0u) return nullptr;
      void* mem = std::malloc(numBytes);
      if (mem != nullptr) ctx->bridge->m_liveAllocations.fetch_add(1, std::memory_order_relaxed);
      return mem;
    } catch (...) {
      return nullptr;
    }
  }

  static void GRIDLOCK_CALL cbFreeBytes(void* hostCtx, void* ptr) noexcept {
    try {
      auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
      if (ctx != nullptr && ctx->bridge != nullptr && ptr != nullptr) {
        ctx->bridge->m_liveAllocations.fetch_sub(1, std::memory_order_relaxed);
        std::free(ptr);
      }
    } catch (...) {
    }
  }

  static GridLockStatus GRIDLOCK_CALL cbLogMessage(void* hostCtx, GridLockLogLevel level, const char* utf8Msg) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbGetRankCount(void* hostCtx, int32_t* outRanks) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbGetActiveRank(void* hostCtx, int32_t* outRank) noexcept {
    try {
      auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
      if (ctx == nullptr || ctx->bridge == nullptr || outRank == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
      *outRank = ctx->bridge->activeRank();
      return GRIDLOCK_OK;
    } catch (...) {
      return GRIDLOCK_ERR_UNSUPPORTED;
    }
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
      GridLockCommandActionCallback cb, void* pluginCtx) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbEmitStructuredEvent(
      void* hostCtx, const char* channelUtf8, const char* jsonPayloadUtf8) noexcept;

  static GridLockStatus GRIDLOCK_CALL cbShowMessageBox(
      void* hostCtx, GridLockDialogSeverity severity, GridLockDialogButtons buttons,
      const char* titleUtf8, const char* bodyUtf8, int32_t* outResult) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbPromptInput(
      void* hostCtx, const char* titleUtf8, const char* labelUtf8, const char* defaultUtf8,
      char* outUtf8, size_t cap, size_t* outLen) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbShowToast(
      void* hostCtx, GridLockDialogSeverity severity, const char* titleUtf8,
      const char* bodyUtf8, int32_t durationMs) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbRegisterMenuAction(
      void* hostCtx, const char* actionId, const char* menuPathUtf8, const char* shortcutHint,
      GridLockCommandActionCallback cb, void* pluginCtx) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbSetStatusItem(
      void* hostCtx, const char* itemId, const char* textUtf8, const char* tooltipUtf8,
      const char* colorTokenUtf8, GridLockStatusItemCallback cb, void* pluginCtx) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbRemoveStatusItem(void* hostCtx, const char* itemId) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbRegisterContextAction(
      void* hostCtx, GridLockContextSurface surface, const char* actionId, const char* menuLabel,
      const char* filterRegex, GridLockContextActionFn cb, void* pluginCtx) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbNavigateToLocation(
      void* hostCtx, const char* fileUtf8, int32_t line, int32_t column) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbSetEditorAnnotation(
      void* hostCtx, const char* annotationId, const char* fileUtf8, int32_t line,
      const char* badgeUtf8, const char* detailUtf8, const char* colorTokenUtf8) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbClearEditorAnnotations(void* hostCtx, const char* idPrefixUtf8) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbGetThemeInfo(void* hostCtx, GridLockThemeInfo* outTheme) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbPublishReportView(
      void* hostCtx, const char* panelId, const char* titleUtf8,
      GridLockReportFormat format, const char* contentUtf8) noexcept;
  static GridLockStatus GRIDLOCK_CALL cbCloseReportView(void* hostCtx, const char* panelId) noexcept;

  GridLockStatus runOnGuiThread(const PluginBindingContext* ctx, const std::function<GridLockStatus()>& fn);
  GridLockStatus runOnObjectThread(
      const PluginBindingContext* ctx,
      const std::function<GridLockStatus(gridlock::GdbRankCoordinator*)>& fn,
      GridLockStatus missingCoordStatus = GRIDLOCK_ERR_NOT_STOPPED);
  GridLockStatus evalExprOnCoordinatorThread(const PluginBindingContext* ctx, gridlock::GdbRankCoordinator* coord,
                                             int32_t rank, const QString& expr, int timeoutMs, QByteArray* outPayload);
  GridLockStatus readMemoryOnCoordinatorThread(const PluginBindingContext* ctx, gridlock::GdbRankCoordinator* coord,
                                               int32_t rank, uint64_t address, size_t numBytes, int timeoutMs,
                                               QByteArray* outBytes);
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
  CachedPromptReply m_cachedPrompt;
  QVector<RegisteredVariableAction> m_variableActions;
  QVector<RegisteredCommandAction> m_commandActions;
  QVector<RegisteredMenuAction> m_menuActions;
  QVector<PluginStatusItem> m_statusItems;
  QVector<RegisteredContextAction> m_contextActions;
  QVector<PluginEditorAnnotation> m_editorAnnotations;
  QVector<PluginReportPanel> m_reportPanels;
  QVector<RegisteredVariableAction> m_stagedVariableActions;
  QVector<RegisteredCommandAction> m_stagedCommandActions;
  QVector<RegisteredMenuAction> m_stagedMenuActions;
  QVector<PluginStatusItem> m_stagedStatusItems;
  QVector<RegisteredContextAction> m_stagedContextActions;
  QVector<PluginEditorAnnotation> m_stagedEditorAnnotations;
  QVector<PluginReportPanel> m_stagedReportPanels;
  QHash<int32_t, PluginBreakpointRecord> m_breakpoints;
};

}  // namespace gridlock::core::plugins

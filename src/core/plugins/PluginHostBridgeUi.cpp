/**
 * @file PluginHostBridgeUi.cpp
 * @brief UI, nested-menu, status-item, multi-surface context, editor-annotation, theme, and report-panel host callbacks.
 *
 * Implements the workstation interaction half of `PluginHostBridge` with bounded UTF-8
 * decoding, control-character sanitization, PCRE2 resource limits, per-plugin item quotas,
 * and GUI-thread marshaling via `PluginHostBridge::runOnGuiThread`.
 */

#include "PluginHostBridge.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace gridlock::core::plugins {

namespace {

constexpr size_t kMaxTypeRegexBytes = 512u;
constexpr size_t kMaxTitleBytes = 256u;
constexpr size_t kMaxPayloadBytes = 1024u * 1024u;
constexpr size_t kMaxPromptDefaultBytes = 64u * 1024u;
constexpr size_t kMaxLogMessageBytes = 16u * 1024u;
constexpr int kMaxStatusItemsPerPlugin = 16;
constexpr int kMaxAnnotationsPerPlugin = 256;
constexpr int kMaxMenuPathDepth = 4;

bool hasControlCharsUi(const char* raw, const QString& decoded) noexcept {
  if (raw != nullptr) {
    for (const auto* p = reinterpret_cast<const unsigned char*>(raw); *p != 0u; ++p)
      if (*p < 0x20u || *p == 0x7Fu) return true;
  }
  return std::any_of(decoded.cbegin(), decoded.cend(),
                     [](QChar c) { return c.category() == QChar::Other_Control; });
}

bool decodeBoundedSingleLine(const char* raw, size_t maxBytes, bool allowEmpty, QString* out) {
  if (raw == nullptr) {
    if (!allowEmpty) return false;
    out->clear();
    return true;
  }
  const size_t n = ::strnlen(raw, maxBytes + 1u);
  if (n > maxBytes || (!allowEmpty && n == 0u)) return false;
  *out = QString::fromUtf8(raw, static_cast<qsizetype>(n));
  return !hasControlCharsUi(raw, *out);
}

bool decodeBoundedPayload(const char* raw, size_t maxBytes, bool allowEmpty, QString* out) {
  if (raw == nullptr) {
    if (!allowEmpty) return false;
    out->clear();
    return true;
  }
  const size_t n = ::strnlen(raw, maxBytes + 1u);
  if (n > maxBytes || (!allowEmpty && n == 0u)) return false;
  *out = QString::fromUtf8(raw, static_cast<qsizetype>(n));
  return true;
}

void seedDefaultThemeInfo(GridLockThemeInfo* outTheme) noexcept {
  outTheme->is_dark = 1;
  std::snprintf(outTheme->theme_id_utf8, sizeof(outTheme->theme_id_utf8), "%s", "gridlock_slate");
  std::snprintf(outTheme->bg_primary_hex, sizeof(outTheme->bg_primary_hex), "%s", "#14171f");
  std::snprintf(outTheme->bg_secondary_hex, sizeof(outTheme->bg_secondary_hex), "%s", "#1b1f2a");
  std::snprintf(outTheme->fg_primary_hex, sizeof(outTheme->fg_primary_hex), "%s", "#e5e9f0");
  std::snprintf(outTheme->fg_muted_hex, sizeof(outTheme->fg_muted_hex), "%s", "#8c96a8");
  std::snprintf(outTheme->accent_hex, sizeof(outTheme->accent_hex), "%s", "#38bdf8");
  std::snprintf(outTheme->selection_hex, sizeof(outTheme->selection_hex), "%s", "#26344a");
  std::snprintf(outTheme->warning_hex, sizeof(outTheme->warning_hex), "%s", "#f59e0b");
  std::snprintf(outTheme->error_hex, sizeof(outTheme->error_hex), "%s", "#ef4444");
}

}  // namespace

QVector<RegisteredContextAction> PluginHostBridge::contextActions(GridLockContextSurface surface) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (surface <= 0) return m_contextActions;
  QVector<RegisteredContextAction> out;
  for (const auto& a : m_contextActions) if (a.surface == surface) out.append(a);
  return out;
}

QVector<RegisteredContextAction> PluginHostBridge::matchingContextActions(
    GridLockContextSurface surface, const QString& filterTarget) const {
  QVector<RegisteredContextAction> snapshot;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& a : m_contextActions) if (a.surface == surface) snapshot.append(a);
  }
  const QString bounded = (filterTarget.size() > 2048) ? filterTarget.left(2048) : filterTarget;
  QVector<RegisteredContextAction> matches;
  for (const auto& act : snapshot) {
    if (!act.hasFilter || (act.compiledRegex.isValid() && act.compiledRegex.match(bounded).hasMatch())) {
      RegisteredContextAction lite;
      lite.pluginId = act.pluginId; lite.surface = act.surface;
      lite.actionId = act.actionId; lite.menuLabel = act.menuLabel; lite.filterRegex = act.filterRegex;
      matches.append(std::move(lite));
    }
  }
  return matches;
}

QVector<PluginEditorAnnotation> PluginHostBridge::editorAnnotations(const QString& filePath) const {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (filePath.isEmpty()) return m_editorAnnotations;
  QVector<PluginEditorAnnotation> out;
  for (const auto& ann : m_editorAnnotations) if (ann.filePath == filePath) out.append(ann);
  return out;
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbLogMessage(
    void* hostCtx, GridLockLogLevel level, const char* utf8Msg) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || utf8Msg == nullptr ||
        level < GRIDLOCK_LOG_TRACE || level > GRIDLOCK_LOG_ERROR) return GRIDLOCK_ERR_INVALID_ARG;
    const size_t msgLen = ::strnlen(utf8Msg, kMaxLogMessageBytes);
    emit ctx->bridge->pluginLogReceived(
        ctx->pluginId, static_cast<int>(level), QString::fromUtf8(utf8Msg, static_cast<qsizetype>(msgLen)));
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbRegisterVariableAction(
    void* hostCtx, const char* actionId, const char* menuLabel, const char* typeRegex,
    GridLockVariableActionCallback cb, void* pluginCtx) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString idStr, labelStr;
    if (ctx == nullptr || ctx->bridge == nullptr || cb == nullptr || typeRegex == nullptr || typeRegex[0] == '\0' ||
        !decodeBoundedSingleLine(actionId, kMaxTitleBytes, false, &idStr) ||
        !decodeBoundedSingleLine(menuLabel, kMaxTitleBytes, false, &labelStr)) return GRIDLOCK_ERR_INVALID_ARG;
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;
    const size_t regexLen = ::strnlen(typeRegex, kMaxTypeRegexBytes + 1u);
    if (regexLen > kMaxTypeRegexBytes) return GRIDLOCK_ERR_INVALID_ARG;
    const QString rawPattern = QString::fromUtf8(typeRegex, static_cast<qsizetype>(regexLen));
    const QRegularExpression compiled(QStringLiteral("(*LIMIT_MATCH=10000)(*LIMIT_RECURSION=250)") + rawPattern);
    if (!compiled.isValid()) return GRIDLOCK_ERR_INVALID_ARG;
    compiled.optimize();

    auto* self = ctx->bridge;
    bool notify = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      const bool dupActive = !self->m_stagingActive && std::any_of(
          self->m_variableActions.cbegin(), self->m_variableActions.cend(),
          [&](const auto& a) { return a.actionId == idStr; });
      const bool dupStaged = std::any_of(
          self->m_stagedVariableActions.cbegin(), self->m_stagedVariableActions.cend(),
          [&](const auto& a) { return a.actionId == idStr; });
      if (dupActive || dupStaged) return GRIDLOCK_ERR_INVALID_ARG;
      RegisteredVariableAction entry{
          ctx->pluginId, idStr, labelStr, rawPattern,
          compiled, cb, pluginCtx, (ctx->boundApi != nullptr) ? ctx->boundApi : &self->m_hostApi, ctx};
      if (self->m_stagingActive) self->m_stagedVariableActions.append(std::move(entry));
      else { self->m_variableActions.append(std::move(entry)); notify = true; }
    }
    if (notify) emit self->registeredActionsChanged();
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbRegisterCommandAction(
    void* hostCtx, const char* commandId, const char* menuLabel, const char* shortcutHint,
    GridLockCommandActionCallback cb, void* pluginCtx) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString idStr, labelStr, hintStr;
    if (ctx == nullptr || ctx->bridge == nullptr || cb == nullptr ||
        !decodeBoundedSingleLine(commandId, kMaxTitleBytes, false, &idStr) ||
        !decodeBoundedSingleLine(menuLabel, kMaxTitleBytes, false, &labelStr) ||
        !decodeBoundedSingleLine(shortcutHint, kMaxTitleBytes, true, &hintStr)) return GRIDLOCK_ERR_INVALID_ARG;
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;
    auto* self = ctx->bridge;
    bool notify = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      const bool dupActive = !self->m_stagingActive && std::any_of(
          self->m_commandActions.cbegin(), self->m_commandActions.cend(),
          [&](const auto& c) { return c.commandId == idStr; });
      const bool dupStaged = std::any_of(
          self->m_stagedCommandActions.cbegin(), self->m_stagedCommandActions.cend(),
          [&](const auto& c) { return c.commandId == idStr; });
      if (dupActive || dupStaged) return GRIDLOCK_ERR_INVALID_ARG;
      RegisteredCommandAction entry{
          ctx->pluginId, idStr, labelStr, hintStr, cb, pluginCtx,
          (ctx->boundApi != nullptr) ? ctx->boundApi : &self->m_hostApi, ctx};
      if (self->m_stagingActive) self->m_stagedCommandActions.append(std::move(entry));
      else { self->m_commandActions.append(std::move(entry)); notify = true; }
    }
    if (notify) emit self->registeredActionsChanged();
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbEmitStructuredEvent(
    void* hostCtx, const char* channelUtf8, const char* jsonPayloadUtf8) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString channel, payload;
    if (ctx == nullptr || ctx->bridge == nullptr || jsonPayloadUtf8 == nullptr ||
        !decodeBoundedSingleLine(channelUtf8, kMaxTitleBytes, false, &channel) ||
        !decodeBoundedPayload(jsonPayloadUtf8, kMaxPayloadBytes, true, &payload)) return GRIDLOCK_ERR_INVALID_ARG;
    emit ctx->bridge->structuredEventEmitted(channel, payload);
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbShowMessageBox(
    void* hostCtx, GridLockDialogSeverity severity, GridLockDialogButtons buttons,
    const char* titleUtf8, const char* bodyUtf8, int32_t* outResult) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString title, body;
    if (ctx == nullptr || ctx->bridge == nullptr || outResult == nullptr || bodyUtf8 == nullptr ||
        severity < GRIDLOCK_DIALOG_INFO || severity > GRIDLOCK_DIALOG_QUESTION ||
        buttons < GRIDLOCK_BUTTONS_OK || buttons > GRIDLOCK_BUTTONS_YES_NO ||
        !decodeBoundedSingleLine(titleUtf8, kMaxTitleBytes, false, &title) ||
        !decodeBoundedPayload(bodyUtf8, kMaxPayloadBytes, true, &body)) return GRIDLOCK_ERR_INVALID_ARG;
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;

    auto* self = ctx->bridge;
    return self->runOnGuiThread(ctx, [&]() {
      int32_t res = (buttons == GRIDLOCK_BUTTONS_YES_NO)
                        ? GRIDLOCK_RESULT_NO
                        : ((buttons == GRIDLOCK_BUTTONS_OK_CANCEL) ? GRIDLOCK_RESULT_CANCEL : GRIDLOCK_RESULT_OK);
      bool handled = false;
      emit self->messageBoxRequested(ctx->pluginId, static_cast<int>(severity), static_cast<int>(buttons),
                                     title, body, &res, &handled);
      if (!handled) return GRIDLOCK_ERR_UNSUPPORTED;
      *outResult = res;
      return GRIDLOCK_OK;
    });
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbPromptInput(
    void* hostCtx, const char* titleUtf8, const char* labelUtf8, const char* defaultUtf8,
    char* outUtf8, size_t cap, size_t* outLen) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString title, label, defaultVal;
    if (ctx == nullptr || ctx->bridge == nullptr || outLen == nullptr || (cap > 0u && outUtf8 == nullptr) ||
        !decodeBoundedSingleLine(titleUtf8, kMaxTitleBytes, false, &title) ||
        !decodeBoundedSingleLine(labelUtf8, kMaxTitleBytes, false, &label) ||
        !decodeBoundedPayload(defaultUtf8, kMaxPromptDefaultBytes, true, &defaultVal)) {
      return GRIDLOCK_ERR_INVALID_ARG;
    }
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;

    auto* self = ctx->bridge;
    QByteArray utf8Bytes;
    bool fromCache = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      if (self->m_cachedPrompt.valid && self->m_cachedPrompt.pluginId == ctx->pluginId &&
          self->m_cachedPrompt.title == title && self->m_cachedPrompt.label == label &&
          outUtf8 != nullptr && cap > static_cast<size_t>(self->m_cachedPrompt.utf8Value.size())) {
        utf8Bytes = std::move(self->m_cachedPrompt.utf8Value);
        self->m_cachedPrompt = CachedPromptReply{};
        fromCache = true;
      }
    }

    if (!fromCache) {
      const GridLockStatus guiSt = self->runOnGuiThread(ctx, [&]() {
        QString value;
        bool accepted = false, handled = false;
        emit self->promptInputRequested(ctx->pluginId, title, label, defaultVal, &value, &accepted, &handled);
        if (!handled) return GRIDLOCK_ERR_UNSUPPORTED;
        if (!accepted) return GRIDLOCK_ERR_CANCELLED;
        utf8Bytes = value.toUtf8();
        return GRIDLOCK_OK;
      });
      if (guiSt != GRIDLOCK_OK) return guiSt;
    }

    const size_t payloadLen = static_cast<size_t>(utf8Bytes.size());
    *outLen = payloadLen;
    if (outUtf8 == nullptr || cap <= payloadLen) {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      self->m_cachedPrompt = CachedPromptReply{ctx->pluginId, title, label, std::move(utf8Bytes), true};
      return GRIDLOCK_ERR_BUFFER_TOO_SMALL;
    }
    if (payloadLen > 0u) std::memcpy(outUtf8, utf8Bytes.constData(), payloadLen);
    outUtf8[payloadLen] = '\0';
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbShowToast(
    void* hostCtx, GridLockDialogSeverity severity, const char* titleUtf8,
    const char* bodyUtf8, int32_t durationMs) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString title, body;
    if (ctx == nullptr || ctx->bridge == nullptr || bodyUtf8 == nullptr ||
        severity < GRIDLOCK_DIALOG_INFO || severity > GRIDLOCK_DIALOG_QUESTION ||
        !decodeBoundedSingleLine(titleUtf8, kMaxTitleBytes, false, &title) ||
        !decodeBoundedPayload(bodyUtf8, kMaxPayloadBytes, true, &body)) return GRIDLOCK_ERR_INVALID_ARG;
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;
    const int clampedMs = std::clamp(static_cast<int>(durationMs <= 0 ? 4000 : durationMs), 500, 30000);
    emit ctx->bridge->toastRequested(ctx->pluginId, static_cast<int>(severity), title, body, clampedMs);
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbRegisterMenuAction(
    void* hostCtx, const char* actionId, const char* menuPathUtf8, const char* shortcutHint,
    GridLockCommandActionCallback cb, void* pluginCtx) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString idStr, rawPath, hintStr;
    if (ctx == nullptr || ctx->bridge == nullptr || cb == nullptr ||
        !decodeBoundedSingleLine(actionId, kMaxTitleBytes, false, &idStr) ||
        !decodeBoundedSingleLine(menuPathUtf8, kMaxTitleBytes, false, &rawPath) ||
        !decodeBoundedSingleLine(shortcutHint, kMaxTitleBytes, true, &hintStr)) return GRIDLOCK_ERR_INVALID_ARG;
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;

    const QStringList rawSegs = rawPath.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    if (rawSegs.isEmpty() || rawSegs.size() > kMaxMenuPathDepth) return GRIDLOCK_ERR_INVALID_ARG;
    QStringList cleanSegs;
    cleanSegs.reserve(rawSegs.size());
    for (const QString& seg : rawSegs) {
      const QString trimmed = seg.trimmed();
      if (trimmed.isEmpty()) return GRIDLOCK_ERR_INVALID_ARG;
      cleanSegs.append(trimmed);
    }

    auto* self = ctx->bridge;
    bool notify = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      const bool dupActive = !self->m_stagingActive && std::any_of(
          self->m_menuActions.cbegin(), self->m_menuActions.cend(), [&](const auto& m) { return m.actionId == idStr; });
      const bool dupStaged = std::any_of(
          self->m_stagedMenuActions.cbegin(), self->m_stagedMenuActions.cend(), [&](const auto& m) { return m.actionId == idStr; });
      if (dupActive || dupStaged) return GRIDLOCK_ERR_INVALID_ARG;
      RegisteredMenuAction entry{ctx->pluginId, idStr, cleanSegs.join(QLatin1Char('/')), cleanSegs, hintStr, cb,
                                 pluginCtx, (ctx->boundApi != nullptr) ? ctx->boundApi : &self->m_hostApi, ctx};
      if (self->m_stagingActive) self->m_stagedMenuActions.append(std::move(entry));
      else { self->m_menuActions.append(std::move(entry)); notify = true; }
    }
    if (notify) emit self->menuActionsChanged();
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbSetStatusItem(
    void* hostCtx, const char* itemId, const char* textUtf8, const char* tooltipUtf8,
    const char* colorTokenUtf8, GridLockStatusItemCallback cb, void* pluginCtx) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString idStr, textStr, tipStr, colorStr;
    if (ctx == nullptr || ctx->bridge == nullptr ||
        !decodeBoundedSingleLine(itemId, kMaxTitleBytes, false, &idStr) ||
        !decodeBoundedSingleLine(textUtf8, kMaxTitleBytes, false, &textStr) ||
        !decodeBoundedPayload(tooltipUtf8, 4096u, true, &tipStr) ||
        !decodeBoundedSingleLine(colorTokenUtf8, 64u, true, &colorStr)) return GRIDLOCK_ERR_INVALID_ARG;
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;

    auto* self = ctx->bridge;
    PluginStatusItem entry{ctx->pluginId, idStr, textStr, tipStr, colorStr, cb, pluginCtx,
                           (ctx->boundApi != nullptr) ? ctx->boundApi : &self->m_hostApi, ctx};
    bool notify = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      auto& vec = self->m_stagingActive ? self->m_stagedStatusItems : self->m_statusItems;
      auto it = std::find_if(vec.begin(), vec.end(), [&](const auto& s) { return s.itemId == idStr; });
      if (it != vec.end()) {
        if (it->pluginId != ctx->pluginId) return GRIDLOCK_ERR_INVALID_ARG;
        *it = entry;
      } else {
        const int ownedCount = static_cast<int>(
            std::count_if(vec.cbegin(), vec.cend(), [&](const auto& s) { return s.pluginId == ctx->pluginId; }));
        if (ownedCount >= kMaxStatusItemsPerPlugin) return GRIDLOCK_ERR_INVALID_ARG;
        vec.append(entry);
      }
      notify = !self->m_stagingActive;
    }
    if (notify) emit self->statusItemUpdated(entry);
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbRemoveStatusItem(void* hostCtx, const char* itemId) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString idStr;
    if (ctx == nullptr || ctx->bridge == nullptr ||
        !decodeBoundedSingleLine(itemId, kMaxTitleBytes, false, &idStr)) return GRIDLOCK_ERR_INVALID_ARG;
    auto* self = ctx->bridge;
    bool removed = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      auto& vec = self->m_stagingActive ? self->m_stagedStatusItems : self->m_statusItems;
      const auto before = vec.size();
      vec.removeIf([&](const auto& s) { return s.itemId == idStr && s.pluginId == ctx->pluginId; });
      removed = (vec.size() != before) && !self->m_stagingActive;
    }
    if (removed) emit self->statusItemRemoved(ctx->pluginId, idStr);
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbRegisterContextAction(
    void* hostCtx, GridLockContextSurface surface, const char* actionId, const char* menuLabel,
    const char* filterRegex, GridLockContextActionFn cb, void* pluginCtx) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString idStr, labelStr;
    if (ctx == nullptr || ctx->bridge == nullptr || cb == nullptr ||
        surface < GRIDLOCK_SURFACE_VARIABLES || surface > GRIDLOCK_SURFACE_RANK_LIST ||
        !decodeBoundedSingleLine(actionId, kMaxTitleBytes, false, &idStr) ||
        !decodeBoundedSingleLine(menuLabel, kMaxTitleBytes, false, &labelStr)) return GRIDLOCK_ERR_INVALID_ARG;
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;

    QString rawPattern;
    QRegularExpression compiled;
    bool hasFilter = false;
    if (filterRegex != nullptr && filterRegex[0] != '\0') {
      const size_t regexLen = ::strnlen(filterRegex, kMaxTypeRegexBytes + 1u);
      if (regexLen > kMaxTypeRegexBytes) return GRIDLOCK_ERR_INVALID_ARG;
      rawPattern = QString::fromUtf8(filterRegex, static_cast<qsizetype>(regexLen));
      compiled = QRegularExpression(QStringLiteral("(*LIMIT_MATCH=10000)(*LIMIT_RECURSION=250)") + rawPattern);
      if (!compiled.isValid()) return GRIDLOCK_ERR_INVALID_ARG;
      compiled.optimize();
      hasFilter = true;
    }

    auto* self = ctx->bridge;
    bool notify = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      const bool dupActive = !self->m_stagingActive && std::any_of(
          self->m_contextActions.cbegin(), self->m_contextActions.cend(), [&](const auto& a) { return a.actionId == idStr; });
      const bool dupStaged = std::any_of(
          self->m_stagedContextActions.cbegin(), self->m_stagedContextActions.cend(), [&](const auto& a) { return a.actionId == idStr; });
      if (dupActive || dupStaged) return GRIDLOCK_ERR_INVALID_ARG;
      RegisteredContextAction entry{ctx->pluginId, surface, idStr, labelStr, rawPattern, compiled, hasFilter,
                                    cb, pluginCtx, (ctx->boundApi != nullptr) ? ctx->boundApi : &self->m_hostApi, ctx};
      if (self->m_stagingActive) self->m_stagedContextActions.append(std::move(entry));
      else { self->m_contextActions.append(std::move(entry)); notify = true; }
    }
    if (notify) emit self->contextActionsChanged();
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbNavigateToLocation(
    void* hostCtx, const char* fileUtf8, int32_t line, int32_t column) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString fileStr;
    if (ctx == nullptr || ctx->bridge == nullptr || line <= 0 ||
        !decodeBoundedSingleLine(fileUtf8, 4096u, false, &fileStr)) return GRIDLOCK_ERR_INVALID_ARG;
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;
    emit ctx->bridge->navigateRequested(ctx->pluginId, fileStr, static_cast<int>(line),
                                        static_cast<int>(column <= 0 ? 1 : column));
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbSetEditorAnnotation(
    void* hostCtx, const char* annotationId, const char* fileUtf8, int32_t line,
    const char* badgeUtf8, const char* detailUtf8, const char* colorTokenUtf8) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString idStr, fileStr, badgeStr, detailStr, colorStr;
    if (ctx == nullptr || ctx->bridge == nullptr || line <= 0 ||
        !decodeBoundedSingleLine(annotationId, kMaxTitleBytes, false, &idStr) ||
        !decodeBoundedSingleLine(fileUtf8, 4096u, false, &fileStr) ||
        !decodeBoundedSingleLine(badgeUtf8, kMaxTitleBytes, false, &badgeStr) ||
        !decodeBoundedPayload(detailUtf8, 4096u, true, &detailStr) ||
        !decodeBoundedSingleLine(colorTokenUtf8, 64u, true, &colorStr)) return GRIDLOCK_ERR_INVALID_ARG;
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;

    auto* self = ctx->bridge;
    PluginEditorAnnotation entry{ctx->pluginId, idStr, fileStr, line, badgeStr, detailStr, colorStr};
    bool notify = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      auto& vec = self->m_stagingActive ? self->m_stagedEditorAnnotations : self->m_editorAnnotations;
      auto it = std::find_if(vec.begin(), vec.end(), [&](const auto& a) { return a.annotationId == idStr; });
      if (it != vec.end()) {
        if (it->pluginId != ctx->pluginId) return GRIDLOCK_ERR_INVALID_ARG;
        *it = entry;
      } else {
        const int ownedCount = static_cast<int>(
            std::count_if(vec.cbegin(), vec.cend(), [&](const auto& a) { return a.pluginId == ctx->pluginId; }));
        if (ownedCount >= kMaxAnnotationsPerPlugin) return GRIDLOCK_ERR_INVALID_ARG;
        vec.append(entry);
      }
      notify = !self->m_stagingActive;
    }
    if (notify) emit self->editorAnnotationUpdated(entry);
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbClearEditorAnnotations(
    void* hostCtx, const char* idPrefixUtf8) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString prefix;
    if (ctx == nullptr || ctx->bridge == nullptr ||
        !decodeBoundedSingleLine(idPrefixUtf8, kMaxTitleBytes, true, &prefix)) return GRIDLOCK_ERR_INVALID_ARG;
    auto* self = ctx->bridge;
    bool removed = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      auto& vec = self->m_stagingActive ? self->m_stagedEditorAnnotations : self->m_editorAnnotations;
      const auto before = vec.size();
      vec.removeIf([&](const auto& a) {
        return a.pluginId == ctx->pluginId && (prefix.isEmpty() || a.annotationId.startsWith(prefix));
      });
      removed = (vec.size() != before) && !self->m_stagingActive;
    }
    if (removed) emit self->editorAnnotationsCleared(ctx->pluginId, prefix);
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbGetThemeInfo(
    void* hostCtx, GridLockThemeInfo* outTheme) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    if (ctx == nullptr || ctx->bridge == nullptr || outTheme == nullptr) return GRIDLOCK_ERR_INVALID_ARG;
    if (outTheme->struct_size != 0u && outTheme->struct_size < sizeof(GridLockThemeInfo)) {
      return GRIDLOCK_ERR_ABI_MISMATCH;
    }
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;

    auto* self = ctx->bridge;
    return self->runOnGuiThread(ctx, [&]() {
      *outTheme = GridLockThemeInfo{};
      outTheme->struct_size = static_cast<uint32_t>(sizeof(GridLockThemeInfo));
      outTheme->abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
      outTheme->abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
      outTheme->reserved_flags = 0u;
      seedDefaultThemeInfo(outTheme);
      bool handled = false;
      emit self->themeInfoRequested(outTheme, &handled);
      return GRIDLOCK_OK;
    });
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbPublishReportView(
    void* hostCtx, const char* panelId, const char* titleUtf8,
    GridLockReportFormat format, const char* contentUtf8) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString idStr, titleStr, contentStr;
    if (ctx == nullptr || ctx->bridge == nullptr || contentUtf8 == nullptr ||
        format < GRIDLOCK_REPORT_MARKDOWN || format > GRIDLOCK_REPORT_JSON_TREE ||
        !decodeBoundedSingleLine(panelId, kMaxTitleBytes, false, &idStr) ||
        !decodeBoundedSingleLine(titleUtf8, kMaxTitleBytes, false, &titleStr) ||
        !decodeBoundedPayload(contentUtf8, kMaxPayloadBytes, true, &contentStr)) return GRIDLOCK_ERR_INVALID_ARG;
    if (ctx->shuttingDown.load(std::memory_order_acquire)) return GRIDLOCK_ERR_UNSUPPORTED;

    auto* self = ctx->bridge;
    PluginReportPanel panel{ctx->pluginId, idStr, titleStr, format, contentStr};
    bool notify = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      auto& vec = self->m_stagingActive ? self->m_stagedReportPanels : self->m_reportPanels;
      auto it = std::find_if(vec.begin(), vec.end(), [&](const auto& p) { return p.panelId == idStr; });
      if (it != vec.end()) {
        if (it->pluginId != ctx->pluginId) return GRIDLOCK_ERR_INVALID_ARG;
        *it = panel;
      } else {
        vec.append(panel);
      }
      notify = !self->m_stagingActive;
    }
    if (notify) emit self->reportViewPublished(panel);
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

GridLockStatus GRIDLOCK_CALL PluginHostBridge::cbCloseReportView(void* hostCtx, const char* panelId) noexcept {
  try {
    auto* ctx = static_cast<PluginBindingContext*>(hostCtx);
    QString idStr;
    if (ctx == nullptr || ctx->bridge == nullptr ||
        !decodeBoundedSingleLine(panelId, kMaxTitleBytes, false, &idStr)) return GRIDLOCK_ERR_INVALID_ARG;
    auto* self = ctx->bridge;
    bool removed = false;
    {
      std::lock_guard<std::mutex> lock(self->m_mutex);
      auto& vec = self->m_stagingActive ? self->m_stagedReportPanels : self->m_reportPanels;
      const auto before = vec.size();
      vec.removeIf([&](const auto& p) { return p.panelId == idStr && p.pluginId == ctx->pluginId; });
      removed = (vec.size() != before) && !self->m_stagingActive;
    }
    if (removed) emit self->reportViewClosed(ctx->pluginId, idStr);
    return GRIDLOCK_OK;
  } catch (...) { return GRIDLOCK_ERR_UNSUPPORTED; }
}

}  // namespace gridlock::core::plugins

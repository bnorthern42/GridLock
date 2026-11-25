/**
 * @file PluginUiModels.hpp
 * @brief Host-side binding contexts and UI/editor/report registration records for C99 plugins.
 *
 * Defines Qt-friendly value types owned by `PluginHostBridge` for per-plugin callback
 * attribution, variable/command/nested-menu actions, multi-surface context actions,
 * clickable status-bar items, inline editor annotations, and declarative report panels.
 */

#pragma once

#include "gridlock/plugin_api.h"

#include <atomic>
#include <cstdint>

#include <QMetaType>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

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
 * @brief Host-side record for a variable context action registered via `register_variable_action`.
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
 * @brief Host-side record for a flat command action registered via `register_command_action`.
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
 * @struct RegisteredMenuAction
 * @brief Host-side record for a slash-delimited nested menu action (`register_menu_action`).
 */
struct RegisteredMenuAction {
  QString pluginId;
  QString actionId;
  QString menuPath;
  QStringList pathSegments;
  QString shortcutHint;
  GridLockCommandActionCallback callback = nullptr;
  void* pluginCtx = nullptr;
  const GridLockHostApi* hostApi = nullptr;
  const PluginBindingContext* bindingCtx = nullptr;
};

/**
 * @struct PluginStatusItem
 * @brief Host-side record for a clickable status-bar entry (`set_status_item`).
 */
struct PluginStatusItem {
  QString pluginId;
  QString itemId;
  QString text;
  QString tooltip;
  QString colorToken;
  GridLockStatusItemCallback callback = nullptr;
  void* pluginCtx = nullptr;
  const GridLockHostApi* hostApi = nullptr;
  const PluginBindingContext* bindingCtx = nullptr;
};

/**
 * @struct RegisteredContextAction
 * @brief Host-side record for a multi-surface context-menu action (`register_context_action`).
 */
struct RegisteredContextAction {
  QString pluginId;
  GridLockContextSurface surface = GRIDLOCK_SURFACE_VARIABLES;
  QString actionId;
  QString menuLabel;
  QString filterRegex;
  QRegularExpression compiledRegex;
  bool hasFilter = false;
  GridLockContextActionFn callback = nullptr;
  void* pluginCtx = nullptr;
  const GridLockHostApi* hostApi = nullptr;
  const PluginBindingContext* bindingCtx = nullptr;
};

/**
 * @struct PluginEditorAnnotation
 * @brief Host-side record for an inline source-editor badge/detail annotation (`set_editor_annotation`).
 */
struct PluginEditorAnnotation {
  QString pluginId;
  QString annotationId;
  QString filePath;
  int32_t line = 0;
  QString badgeText;
  QString detailText;
  QString colorToken;
};

/**
 * @struct PluginReportPanel
 * @brief Host-side record for a declarative report view (`publish_report_view`).
 */
struct PluginReportPanel {
  QString pluginId;
  QString panelId;
  QString title;
  GridLockReportFormat format = GRIDLOCK_REPORT_MARKDOWN;
  QString content;
};

}  // namespace gridlock::core::plugins

Q_DECLARE_METATYPE(gridlock::core::plugins::PluginStatusItem)
Q_DECLARE_METATYPE(gridlock::core::plugins::PluginEditorAnnotation)
Q_DECLARE_METATYPE(gridlock::core::plugins::PluginReportPanel)

/**
 * @file MainWindowPlugins.cpp
 * @brief Native plugin UI integration: dialogs, toasts, nested menus, status badges, multi-surface context hooks, annotations, theme sync, and report dock.
 *
 * Owns `PluginManager` and `PluginReportDockWidget` inside `MainWindow`, binds `PluginHostBridge`
 * signals to modal dialogs (`QMessageBox`, `QInputDialog`), timed toast banners, hierarchical
 * **Tools -> Plugins** submenus with `ShortcutManager` collision checks, clickable status-bar
 * badges, source-editor navigation and inline annotations, multi-surface context menus, and
 * live `ThemeManager` synchronization.
 */

#include "MainWindow.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>

#include <QAction>
#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "../core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/ShortcutManager.hpp"
#include "../core/managers/SpackManager.hpp"
#include "../core/managers/ThemeManager.hpp"
#include "../core/plugins/PluginHostBridge.hpp"
#include "../core/plugins/PluginManager.hpp"
#include "EditorTabManager.hpp"
#include "dialogs/preferences/PreferencesDialog.hpp"
#include "docks/CallStackDockWidget.hpp"
#include "docks/PluginReportDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/ProjectExplorerWidget.hpp"

namespace gridlock::ui {

namespace {

constexpr qsizetype kMaxPluginConsolePreviewChars = 1024;
constexpr int kPluginConsoleBatchFlushMs = 50;
constexpr int kMaxPluginLinesPerWindow = 64;

QString truncatePluginConsoleLine(QString line) {
  line.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\r'), QLatin1Char(' '));
  if (line.size() > kMaxPluginConsolePreviewChars) {
    line.truncate(kMaxPluginConsolePreviewChars - 3);
    line.append(QStringLiteral("..."));
  }
  return line;
}

struct PendingPluginConsoleBatch {
  QStringList lines;
  bool hasError = false;
  int droppedCount = 0;

  void clear() {
    lines.clear();
    hasError = false;
    droppedCount = 0;
  }
};

QString canonicalOrAbsolutePath(const QString& rawPath) {
  if (rawPath.isEmpty()) return {};
  const QFileInfo fi(rawPath);
  return (!fi.canonicalFilePath().isEmpty()) ? fi.canonicalFilePath() : fi.absoluteFilePath();
}

QColor resolvePluginColorToken(const QString& colorToken,
                               const gridlock::core::ThemeManager::ThemePalette& pal) {
  const QString tok = colorToken.trimmed().toLower();
  if (tok == QStringLiteral("error")) return QColor(pal.isDark ? QStringLiteral("#ef4444") : QStringLiteral("#dc2626"));
  if (tok == QStringLiteral("warning")) return QColor(pal.isDark ? QStringLiteral("#f59e0b") : QStringLiteral("#d97706"));
  if (tok == QStringLiteral("accent")) return QColor(pal.accentHex);
  if (!colorToken.trimmed().isEmpty() && QColor::isValidColorName(colorToken.trimmed())) {
    return QColor(colorToken.trimmed());
  }
  return QColor(pal.textHex);
}

GridLockThemeInfo buildCurrentPluginThemeInfo() {
  const auto& tm = gridlock::core::ThemeManager::instance();
  const QString themeId = tm.activeThemeId().trimmed().isEmpty() ? QStringLiteral("gridlock_slate") : tm.activeThemeId().trimmed();
  const auto pal = tm.paletteForTheme(themeId);
  GridLockThemeInfo info{static_cast<uint32_t>(sizeof(GridLockThemeInfo)),
                         GRIDLOCK_PLUGIN_ABI_MAJOR, GRIDLOCK_PLUGIN_ABI_MINOR, 0u,
                         pal.isDark ? 1 : 0, {}, {}, {}, {}, {}, {}, {}, {}, {}};
  std::snprintf(info.theme_id_utf8, sizeof(info.theme_id_utf8), "%s", themeId.toUtf8().constData());
  std::snprintf(info.bg_primary_hex, sizeof(info.bg_primary_hex), "%s", pal.canvasHex.toUtf8().constData());
  std::snprintf(info.bg_secondary_hex, sizeof(info.bg_secondary_hex), "%s", pal.headerHex.toUtf8().constData());
  std::snprintf(info.fg_primary_hex, sizeof(info.fg_primary_hex), "%s", pal.textHex.toUtf8().constData());
  std::snprintf(info.fg_muted_hex, sizeof(info.fg_muted_hex), "%s", pal.isDark ? "#8c96a8" : "#57606a");
  std::snprintf(info.accent_hex, sizeof(info.accent_hex), "%s", pal.accentHex.toUtf8().constData());
  std::snprintf(info.selection_hex, sizeof(info.selection_hex), "%s", pal.isDark ? "#26344a" : "#ddf4ff");
  std::snprintf(info.warning_hex, sizeof(info.warning_hex), "%s", pal.isDark ? "#f59e0b" : "#d97706");
  std::snprintf(info.error_hex, sizeof(info.error_hex), "%s", pal.isDark ? "#ef4444" : "#dc2626");
  return info;
}

bool hasNonPluginShortcutCollision(const QKeySequence& seq) {
  if (seq.isEmpty()) return false;
  for (const auto& b : gridlock::core::ShortcutManager::instance().registeredBindings()) {
    if (b.id.startsWith(QStringLiteral("pluginCmd_")) || b.id.startsWith(QStringLiteral("pluginMenuAct_"))) continue;
    if (!b.currentShortcut.isEmpty() && b.currentShortcut == seq) return true;
  }
  return false;
}

void applyPluginShortcutChecked(QAction* act, const QString& shortcutHint) {
  if (!act || shortcutHint.trimmed().isEmpty()) return;
  const QKeySequence seq = QKeySequence::fromString(shortcutHint.trimmed(), QKeySequence::PortableText);
  if (seq.isEmpty()) return;
  if (hasNonPluginShortcutCollision(seq)) {
    act->setToolTip(QStringLiteral("%1 (shortcut '%2' already bound)").arg(act->text(), shortcutHint.trimmed()));
  } else {
    act->setShortcut(seq);
  }
}

struct PluginUiRuntimeState {
  QMap<QString, QPointer<QToolButton>> statusButtons;
  QMap<QString, QString> statusColorTokens;
  QPointer<QFrame> toastBanner;
  QPointer<QLabel> toastLabel;
  QPointer<QTimer> toastTimer;
  QString lastBroadcastThemeId;
  int lastBroadcastIsDark = -1;
};

void styleStatusButton(QToolButton* btn, const QString& colorToken) {
  if (!btn) return;
  const auto& tm = gridlock::core::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  const QColor fg = resolvePluginColorToken(colorToken, pal);
  QColor tint = fg;
  tint.setAlpha(pal.isDark ? 36 : 28);
  btn->setStyleSheet(QStringLiteral(
      "QToolButton { background-color: rgba(%1,%2,%3,%4); color: %5; border: 1px solid %6; "
      "border-radius: 3px; padding: 0px 6px; font-size: 11px; font-weight: 600; min-height: 18px; max-height: 20px; } "
      "QToolButton:hover { border-color: %5; }")
      .arg(tint.red()).arg(tint.green()).arg(tint.blue()).arg(tint.alpha())
      .arg(fg.name(), colorToken.trimmed().isEmpty() ? pal.borderHex : fg.name()));
}

}  // namespace

bool MainWindow::openFileAtLine(const QString& filePath, int lineOneBased, int columnOneBased) {
  if (filePath.trimmed().isEmpty() || !m_editorTabManager) return false;
  const QFileInfo rawFi(filePath.trimmed());
  const QString resolved = rawFi.exists() ? rawFi.absoluteFilePath() : resolveWorkspacePath(filePath.trimmed());
  if (resolved.isEmpty() || !QFileInfo::exists(resolved)) return false;

  SourceCodeView* view = m_editorTabManager->navigateToLocation(
      resolved, std::max(1, lineOneBased), std::max(0, columnOneBased - 1));
  if (!view) return false;
  m_currentFile = view->currentFile().isEmpty() ? resolved : view->currentFile();
  syncTabBreakpointMetadata(view);
  view->setActiveFocusRank(m_focusedRank);
  return true;
}

void MainWindow::initializePluginSubsystem() {
  if (m_pluginManager != nullptr) return;

  auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator);
  m_pluginManager = new gridlock::core::plugins::PluginManager(gdbCoord, this);
  auto* bridge = m_pluginManager->hostBridge();
  bridge->setActiveRank(m_focusedRank);
  auto uiState = std::make_shared<PluginUiRuntimeState>();
  const GridLockThemeInfo initialTheme = buildCurrentPluginThemeInfo();
  uiState->lastBroadcastThemeId = QString::fromUtf8(initialTheme.theme_id_utf8);
  uiState->lastBroadcastIsDark = initialTheme.is_dark;

  if (m_variablesDockWidget != nullptr) {
    m_variablesDockWidget->setPluginHostBridge(bridge);
    connect(m_variablesDockWidget, &gridlock::VariablesDockWidget::coordinatorBound,
            this, &MainWindow::bindPluginCoordinatorSignals, Qt::UniqueConnection);
  }
  if (m_callStackDockWidget != nullptr) {
    m_callStackDockWidget->setPluginHostBridge(bridge);
    m_callStackDockWidget->setFocusedRank(m_focusedRank);
  }
  if (m_serverRackView != nullptr) {
    m_serverRackView->setPluginHostBridge(bridge);
  }

  // Instantiate host-owned PluginReportDockWidget tabified in the right dock area (hidden until populated).
  m_pluginReportDock = new PluginReportDockWidget(this);
  m_pluginReportDock->installEventFilter(this);
  setupDockTitleBar(m_pluginReportDock, QStringLiteral("PLUGIN REPORTS"));
  addDockWidget(Qt::RightDockWidgetArea, m_pluginReportDock);
  if (auto* watchDock = findChild<QDockWidget*>(QStringLiteral("WatchExpressionsDock"))) {
    tabifyDockWidget(watchDock, m_pluginReportDock);
  }
  m_dockHomeSlots.insert(m_pluginReportDock, {nullptr, -1, Qt::RightDockWidgetArea});
  m_pluginReportDock->hide();

  connect(this, &MainWindow::focusedRankChanged, this, [this, bridge](int rank) {
    bridge->setActiveRank(rank);
    if (m_callStackDockWidget != nullptr) m_callStackDockWidget->setFocusedRank(rank);
  });

  // Wire multi-surface context menus (Code Editor + Breakpoints) and annotation sync on EditorTabManager.
  auto syncAnnotationsForView = [bridge](SourceCodeView* view) {
    if (!view || !bridge) return;
    view->clearPluginAnnotations();
    const QString viewCanon = canonicalOrAbsolutePath(view->currentFile());
    if (viewCanon.isEmpty()) return;
    for (const auto& ann : bridge->editorAnnotations()) {
      if (canonicalOrAbsolutePath(ann.filePath) == viewCanon) {
        view->upsertPluginAnnotation({ann.pluginId, ann.annotationId, ann.line,
                                      ann.badgeText, ann.detailText, ann.colorToken});
      }
    }
  };

  if (m_editorTabManager != nullptr) {
    auto editorHook = [this, bridge](QMenu* menu, const QString& filePath, int lineOneBased,
                                     int colOneBased, const QString& symbol, const QString& selectedText) {
      if (!menu || !bridge) return;
      const QString filterTarget = !selectedText.isEmpty() ? selectedText : (!symbol.isEmpty() ? symbol : filePath);
      const auto matches = bridge->matchingContextActions(GRIDLOCK_SURFACE_CODE_EDITOR, filterTarget.left(2048));
      if (matches.isEmpty()) return;
      menu->addSeparator();
      QMenu* plugSub = menu->addMenu(QStringLiteral("Plugin Actions"));
      plugSub->setObjectName(QStringLiteral("codeEditorPluginActionsMenu"));
      for (const auto& act : matches) {
        const QString actId = act.actionId;
        QAction* pAct = plugSub->addAction(act.menuLabel);
        pAct->setObjectName(QStringLiteral("pluginCtxAct_") + actId);
        connect(pAct, &QAction::triggered, this,
                [this, bridge, actId, filePath, lineOneBased, colOneBased, symbol, selectedText]() {
                  const int frameLvl = m_callStackDockWidget ? std::max(0, m_callStackDockWidget->activeFrameLevel()) : 0;
                  const QByteArray symUtf8 = symbol.toUtf8(), fileUtf8 = filePath.toUtf8(), selUtf8 = selectedText.toUtf8();
                  GridLockSurfaceContext ctx{static_cast<uint32_t>(sizeof(GridLockSurfaceContext)),
                                             GRIDLOCK_PLUGIN_ABI_MAJOR, GRIDLOCK_PLUGIN_ABI_MINOR, 0u,
                                             GRIDLOCK_SURFACE_CODE_EDITOR, m_focusedRank, frameLvl,
                                             lineOneBased, colOneBased, -1, sourceLineToAddress(lineOneBased),
                                             symUtf8.isEmpty() ? nullptr : symUtf8.constData(), nullptr,
                                             fileUtf8.isEmpty() ? nullptr : fileUtf8.constData(),
                                             selUtf8.isEmpty() ? nullptr : selUtf8.constData()};
                  (void)bridge->invokeContextAction(actId, ctx);
                });
      }
    };

    auto breakpointHook = [this, bridge](QMenu* menu, const QString& filePath, int lineOneBased, int bpId) {
      if (!menu || !bridge) return;
      const QString locTarget = QStringLiteral("%1:%2").arg(filePath).arg(lineOneBased);
      const auto matches = bridge->matchingContextActions(GRIDLOCK_SURFACE_BREAKPOINTS, locTarget.left(2048));
      if (matches.isEmpty()) return;
      menu->addSeparator();
      QMenu* plugSub = menu->addMenu(QStringLiteral("Plugin Actions"));
      plugSub->setObjectName(QStringLiteral("breakpointPluginActionsMenu"));
      for (const auto& act : matches) {
        const QString actId = act.actionId;
        QAction* pAct = plugSub->addAction(act.menuLabel);
        pAct->setObjectName(QStringLiteral("pluginCtxAct_") + actId);
        connect(pAct, &QAction::triggered, this, [this, bridge, actId, filePath, lineOneBased, bpId]() {
          const int frameLvl = m_callStackDockWidget ? std::max(0, m_callStackDockWidget->activeFrameLevel()) : 0;
          const QByteArray fileUtf8 = filePath.toUtf8();
          GridLockSurfaceContext ctx{static_cast<uint32_t>(sizeof(GridLockSurfaceContext)),
                                     GRIDLOCK_PLUGIN_ABI_MAJOR, GRIDLOCK_PLUGIN_ABI_MINOR, 0u,
                                     GRIDLOCK_SURFACE_BREAKPOINTS, m_focusedRank, frameLvl,
                                     lineOneBased, -1, bpId, sourceLineToAddress(lineOneBased),
                                     nullptr, nullptr,
                                     fileUtf8.isEmpty() ? nullptr : fileUtf8.constData(), nullptr};
          (void)bridge->invokeContextAction(actId, ctx);
        });
      }
    };

    m_editorTabManager->setPluginMenuHooks(editorHook, breakpointHook);
    connect(m_editorTabManager, &EditorTabManager::sourceViewCreated, this, syncAnnotationsForView);
  }

  // Coalesce console preview updates on a 50 ms single-shot timer so tight stop loops never stall UI layout.
  auto consoleBatch = std::make_shared<PendingPluginConsoleBatch>();
  auto* consoleFlushTimer = new QTimer(this);
  consoleFlushTimer->setSingleShot(true);
  consoleFlushTimer->setInterval(kPluginConsoleBatchFlushMs);
  connect(consoleFlushTimer, &QTimer::timeout, this, [this, consoleBatch]() {
    if (m_isShuttingDown || m_isClosing || m_spackManager == nullptr) {
      consoleBatch->clear();
      return;
    }
    if (consoleBatch->lines.isEmpty() && consoleBatch->droppedCount == 0) return;
    QStringList out = std::move(consoleBatch->lines);
    if (consoleBatch->droppedCount > 0) {
      out.append(QStringLiteral("[Plugin] ... (%1 additional messages coalesced)").arg(consoleBatch->droppedCount));
    }
    const bool isErr = consoleBatch->hasError;
    consoleBatch->clear();
    m_spackManager->appendMessage(out.join(QLatin1Char('\n')), isErr);
  });

  auto enqueueConsoleLine = [this, consoleBatch, consoleFlushTimer](const QString& rawLine, bool isError) {
    if (m_isShuttingDown || m_isClosing || m_spackManager == nullptr) return;
    if (consoleBatch->lines.size() < kMaxPluginLinesPerWindow) {
      consoleBatch->lines.append(truncatePluginConsoleLine(rawLine));
    } else {
      ++consoleBatch->droppedCount;
    }
    if (isError) consoleBatch->hasError = true;
    if (!consoleFlushTimer->isActive()) consoleFlushTimer->start();
  };

  connect(bridge, &gridlock::core::plugins::PluginHostBridge::structuredEventEmitted, this,
          [this, enqueueConsoleLine](const QString& channel, const QString& jsonPayload) {
            if (m_isShuttingDown || m_isClosing) return;
            enqueueConsoleLine(QStringLiteral("[Plugin:%1] %2").arg(channel, jsonPayload), false);
            emit pluginStructuredEventEmitted(channel, jsonPayload);
          });

  connect(bridge, &gridlock::core::plugins::PluginHostBridge::pluginLogReceived, this,
          [this, enqueueConsoleLine](const QString& pluginId, int level, const QString& message) {
            if (m_isShuttingDown || m_isClosing) return;
            if (level <= GRIDLOCK_LOG_DEBUG && qEnvironmentVariableIntValue("GRIDLOCK_PLUGIN_VERBOSE_LOGS") != 1) {
              return;
            }
            const QString tag = pluginId.isEmpty() ? QStringLiteral("plugin") : pluginId;
            enqueueConsoleLine(QStringLiteral("[PluginLog:%1] %2").arg(tag, message), level >= GRIDLOCK_LOG_ERROR);
          });

  // Modal dialogs, input prompts, and timed non-modal toast banners.
  connect(bridge, &gridlock::core::plugins::PluginHostBridge::messageBoxRequested, this,
          [this](const QString&, int severity, int buttons, const QString& title,
                 const QString& body, int32_t* outResult, bool* outHandled) {
            if (m_isShuttingDown || m_isClosing || !outResult || !outHandled) return;
            setProperty("gl_last_plugin_msgbox_title", title);
            setProperty("gl_last_plugin_msgbox_body", body);
            if (property("gl_plugin_msgbox_auto_result").isValid()) {
              *outResult = property("gl_plugin_msgbox_auto_result").toInt();
              *outHandled = true;
              return;
            }
            QMessageBox box(this);
            box.setWindowTitle(title); box.setText(body);
            box.setIcon(severity == GRIDLOCK_DIALOG_ERROR ? QMessageBox::Critical
                        : severity == GRIDLOCK_DIALOG_WARNING ? QMessageBox::Warning
                        : severity == GRIDLOCK_DIALOG_QUESTION ? QMessageBox::Question : QMessageBox::Information);
            box.setStandardButtons(buttons == GRIDLOCK_BUTTONS_YES_NO ? (QMessageBox::Yes | QMessageBox::No)
                                   : (buttons == GRIDLOCK_BUTTONS_OK_CANCEL ? (QMessageBox::Ok | QMessageBox::Cancel) : QMessageBox::Ok));
            const int clicked = box.exec();
            *outResult = (clicked == QMessageBox::Ok) ? GRIDLOCK_RESULT_OK
                         : (clicked == QMessageBox::Yes) ? GRIDLOCK_RESULT_YES
                         : (clicked == QMessageBox::No) ? GRIDLOCK_RESULT_NO : GRIDLOCK_RESULT_CANCEL;
            *outHandled = true;
          });

  connect(bridge, &gridlock::core::plugins::PluginHostBridge::promptInputRequested, this,
          [this](const QString&, const QString& title, const QString& label,
                 const QString& defaultValue, QString* outValue, bool* outAccepted, bool* outHandled) {
            if (m_isShuttingDown || m_isClosing || !outValue || !outAccepted || !outHandled) return;
            setProperty("gl_last_plugin_prompt_title", title);
            setProperty("gl_last_plugin_prompt_label", label);
            if (property("gl_plugin_prompt_auto_accept").isValid()) {
              *outAccepted = property("gl_plugin_prompt_auto_accept").toBool();
              if (*outAccepted) *outValue = property("gl_plugin_prompt_auto_value").toString();
              *outHandled = true;
              return;
            }
            QInputDialog dlg(this);
            dlg.setInputMode(QInputDialog::TextInput); dlg.setWindowTitle(title);
            dlg.setLabelText(label); dlg.setTextValue(defaultValue);
            *outAccepted = (dlg.exec() == QDialog::Accepted);
            if (*outAccepted) *outValue = dlg.textValue();
            *outHandled = true;
          });

  connect(bridge, &gridlock::core::plugins::PluginHostBridge::toastRequested, this,
          [this, uiState](const QString&, int severity, const QString& title, const QString& body, int durationMs) {
            if (m_isShuttingDown || m_isClosing) return;
            if (statusBar()) statusBar()->showMessage(QStringLiteral("[%1] %2").arg(title, body), durationMs);
            if (!uiState->toastBanner) {
              auto* banner = new QFrame(this); banner->setObjectName(QStringLiteral("pluginToastBanner"));
              auto* hl = new QHBoxLayout(banner); hl->setContentsMargins(10, 6, 8, 6); hl->setSpacing(8);
              auto* lbl = new QLabel(banner); lbl->setObjectName(QStringLiteral("pluginToastLabel")); lbl->setWordWrap(true);
              auto* closeBtn = new QToolButton(banner); closeBtn->setObjectName(QStringLiteral("pluginToastDismissBtn"));
              closeBtn->setText(QStringLiteral("✕")); closeBtn->setFixedSize(18, 18);
              connect(closeBtn, &QToolButton::clicked, banner, &QWidget::hide);
              hl->addWidget(lbl, 1); hl->addWidget(closeBtn);
              auto* timer = new QTimer(banner); timer->setSingleShot(true);
              connect(timer, &QTimer::timeout, banner, &QWidget::hide);
              uiState->toastBanner = banner; uiState->toastLabel = lbl; uiState->toastTimer = timer;
            }
            const auto& tm = gridlock::core::ThemeManager::instance();
            const auto pal = tm.paletteForTheme(tm.activeThemeId());
            const QString borderHex = (severity == GRIDLOCK_DIALOG_ERROR)     ? QStringLiteral("#ef4444")
                                      : (severity == GRIDLOCK_DIALOG_WARNING) ? QStringLiteral("#f59e0b")
                                                                              : pal.accentHex;
            uiState->toastBanner->setStyleSheet(QStringLiteral(
                "QFrame#pluginToastBanner { background-color: %1; color: %2; border: 1px solid %3; "
                "border-left: 4px solid %4; border-radius: 4px; } "
                "QLabel#pluginToastLabel { color: %2; font-size: 12px; background: transparent; border: none; }")
                .arg(pal.headerHex, pal.textHex, pal.borderHex, borderHex));
            uiState->toastLabel->setText(QStringLiteral("<b>%1</b>: %2").arg(title.toHtmlEscaped(), body.toHtmlEscaped()));
            const int bannerW = std::clamp(width() / 3, 280, 440);
            uiState->toastBanner->setFixedWidth(bannerW);
            uiState->toastBanner->adjustSize();
            const int sbH = statusBar() ? statusBar()->height() : 24;
            uiState->toastBanner->move(std::max(12, width() - bannerW - 16),
                                       std::max(36, height() - uiState->toastBanner->height() - sbH - 12));
            uiState->toastBanner->show();
            uiState->toastBanner->raise();
            uiState->toastTimer->start(std::clamp(durationMs, 500, 30000));
          });

  // Status-bar items, editor navigation/annotations, theme queries, and declarative reports.
  auto mountOrUpdateStatusItem = [this, bridge, uiState](const gridlock::core::plugins::PluginStatusItem& item) {
    if (m_isShuttingDown || m_isClosing || !statusBar()) return;
    QToolButton* btn = uiState->statusButtons.value(item.itemId).data();
    if (!btn) {
      btn = new QToolButton(statusBar());
      btn->setObjectName(QStringLiteral("pluginStatusItem_") + item.itemId);
      btn->setFixedHeight(20);
      statusBar()->addPermanentWidget(btn);
      uiState->statusButtons.insert(item.itemId, btn);
    }
    uiState->statusColorTokens.insert(item.itemId, item.colorToken);
    btn->setText(item.text);
    btn->setToolTip(item.tooltip.isEmpty() ? item.text : item.tooltip);
    btn->setCursor(item.callback != nullptr ? Qt::PointingHandCursor : Qt::ArrowCursor);
    styleStatusButton(btn, item.colorToken);
    btn->disconnect(this);
    if (item.callback != nullptr) {
      const QString itemId = item.itemId;
      connect(btn, &QToolButton::clicked, this, [bridge, itemId]() {
        if (bridge) (void)bridge->invokeStatusItem(itemId);
      });
    }
    btn->show();
  };

  connect(bridge, &gridlock::core::plugins::PluginHostBridge::statusItemUpdated, this, mountOrUpdateStatusItem);
  connect(bridge, &gridlock::core::plugins::PluginHostBridge::statusItemRemoved, this,
          [this, uiState](const QString&, const QString& itemId) {
            uiState->statusColorTokens.remove(itemId);
            if (QToolButton* btn = uiState->statusButtons.take(itemId).data()) {
              if (statusBar()) statusBar()->removeWidget(btn);
              btn->deleteLater();
            }
          });

  connect(bridge, &gridlock::core::plugins::PluginHostBridge::navigateRequested, this,
          [this](const QString&, const QString& filePath, int line, int column) {
            if (!m_isShuttingDown && !m_isClosing) openFileAtLine(filePath, line, column);
          });

  connect(bridge, &gridlock::core::plugins::PluginHostBridge::editorAnnotationUpdated, this,
          [this](const gridlock::core::plugins::PluginEditorAnnotation& ann) {
            if (m_isShuttingDown || m_isClosing || !m_editorTabManager) return;
            const QString targetCanon = canonicalOrAbsolutePath(ann.filePath);
            for (SourceCodeView* v : m_editorTabManager->openViews()) {
              if (v && canonicalOrAbsolutePath(v->currentFile()) == targetCanon) {
                v->upsertPluginAnnotation({ann.pluginId, ann.annotationId, ann.line,
                                           ann.badgeText, ann.detailText, ann.colorToken});
              }
            }
          });

  connect(bridge, &gridlock::core::plugins::PluginHostBridge::editorAnnotationsCleared, this,
          [this](const QString& pluginId, const QString& idPrefix) {
            if (!m_editorTabManager) return;
            for (SourceCodeView* v : m_editorTabManager->openViews()) {
              if (v) v->clearPluginAnnotations(pluginId, idPrefix);
            }
          });

  connect(bridge, &gridlock::core::plugins::PluginHostBridge::themeInfoRequested, this,
          [](GridLockThemeInfo* outTheme, bool* outHandled) {
            if (!outTheme || !outHandled) return;
            *outTheme = buildCurrentPluginThemeInfo();
            *outHandled = true;
          });

  connect(&gridlock::core::ThemeManager::instance(), &gridlock::core::ThemeManager::themeModeChanged,
          this, [this, uiState](bool) {
            if (m_isShuttingDown || m_isClosing || !m_pluginManager) return;
            for (auto it = uiState->statusButtons.cbegin(); it != uiState->statusButtons.cend(); ++it) {
              if (it.value()) styleStatusButton(it.value(), uiState->statusColorTokens.value(it.key()));
            }
            if (m_pluginReportDock) m_pluginReportDock->reloadStyle();
            const GridLockThemeInfo theme = buildCurrentPluginThemeInfo();
            const QString newId = QString::fromUtf8(theme.theme_id_utf8);
            if (newId != uiState->lastBroadcastThemeId || theme.is_dark != uiState->lastBroadcastIsDark) {
              uiState->lastBroadcastThemeId = newId;
              uiState->lastBroadcastIsDark = theme.is_dark;
              m_pluginManager->notifyThemeChanged(theme);
            }
          });

  connect(bridge, &gridlock::core::plugins::PluginHostBridge::reportViewPublished, this,
          [this](const gridlock::core::plugins::PluginReportPanel& panel) {
            if (!m_isShuttingDown && !m_isClosing && m_pluginReportDock) m_pluginReportDock->upsertPanel(panel);
          });
  connect(bridge, &gridlock::core::plugins::PluginHostBridge::reportViewClosed, this,
          [this](const QString& pluginId, const QString& panelId) {
            if (m_pluginReportDock) m_pluginReportDock->removePanel(pluginId, panelId);
          });

  // Status-bar notification banner with "Restart GridLock Now" action when plugin configuration requires restart.
  QFrame* restartNotice = nullptr;
  if (QStatusBar* bar = statusBar()) {
    restartNotice = new QFrame(bar);
    restartNotice->setObjectName(QStringLiteral("mainWindowPluginRestartBanner"));
    restartNotice->setStyleSheet(QStringLiteral(
        "QFrame#mainWindowPluginRestartBanner { background-color: rgba(217, 119, 6, 0.20); "
        "border: 1px solid #d97706; border-radius: 4px; }"));
    auto* noticeLay = new QHBoxLayout(restartNotice);
    noticeLay->setContentsMargins(8, 1, 6, 1);
    noticeLay->setSpacing(8);
    auto* noticeLbl = new QLabel(tr("Plugin changes require restart"), restartNotice);
    noticeLbl->setObjectName(QStringLiteral("mainWindowPluginRestartLabel"));
    noticeLay->addWidget(noticeLbl);

    auto* restartBtn = new QPushButton(tr("Restart GridLock Now"), restartNotice);
    restartBtn->setObjectName(QStringLiteral("mainWindowRestartGridLockBtn"));
    restartBtn->setFixedHeight(20);
    noticeLay->addWidget(restartBtn);
    restartNotice->setVisible(false);
    bar->addPermanentWidget(restartNotice);

    connect(restartBtn, &QPushButton::clicked, this, [this]() {
      if (!m_activeWorkspaceDir.isEmpty()) saveSession(false);
      if (m_debugSessionActive) stopTargetWorkflow();
      gridlock::core::ConfigManager::instance().saveGlobalConfig();
      QSettings(QStringLiteral("gridlock"), QStringLiteral("debugger")).sync();
      if (qEnvironmentVariableIntValue("GRIDLOCK_DRY_RUN_SELF_RESTART") == 1) {
        if (qApp != nullptr) qApp->setProperty("gridlock_restart_triggered", true);
        return;
      }
      const QString appPath = QCoreApplication::applicationFilePath();
      const QStringList appArgs = QCoreApplication::arguments().mid(1);
      if (!appPath.isEmpty()) QProcess::startDetached(appPath, appArgs);
      QCoreApplication::quit();
    });
  }

  if (menuBar() != nullptr) {
    for (QAction* topAct : menuBar()->actions()) {
      QMenu* m = topAct ? topAct->menu() : nullptr;
      if (m != nullptr && topAct->text().contains(QStringLiteral("Tools"), Qt::CaseInsensitive)) {
        m->addSeparator();
        m_toolsPluginsMenu = m->addMenu(tr("Plugins"));
        m_toolsPluginsMenu->setObjectName(QStringLiteral("menuToolsPlugins"));
        connect(m_toolsPluginsMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildToolsPluginsMenu);
        break;
      }
    }
  }

  auto reconcilePluginUiState = [this, bridge, uiState, mountOrUpdateStatusItem, syncAnnotationsForView]() {
    if (m_isShuttingDown || m_isClosing || !bridge) return;
    QSet<QString> liveStatusIds;
    for (const auto& item : bridge->statusItems()) {
      liveStatusIds.insert(item.itemId);
      mountOrUpdateStatusItem(item);
    }
    for (const QString& existingId : uiState->statusButtons.keys()) {
      if (!liveStatusIds.contains(existingId)) {
        uiState->statusColorTokens.remove(existingId);
        if (QToolButton* btn = uiState->statusButtons.take(existingId).data()) {
          if (statusBar()) statusBar()->removeWidget(btn);
          btn->deleteLater();
        }
      }
    }
    if (m_pluginReportDock) {
      QSet<QString> livePanelIds;
      for (const auto& p : bridge->reportPanels()) {
        livePanelIds.insert(p.panelId);
        m_pluginReportDock->upsertPanel(p);
      }
      if (livePanelIds.isEmpty() && m_pluginReportDock->panelCount() > 0) {
        m_pluginReportDock->removePanelsForPlugin(QString());
      }
    }
    if (m_editorTabManager) {
      for (SourceCodeView* v : m_editorTabManager->openViews()) syncAnnotationsForView(v);
    }
  };

  connect(bridge, &gridlock::core::plugins::PluginHostBridge::registeredActionsChanged,
           this, &MainWindow::rebuildToolsPluginsMenu);
  connect(bridge, &gridlock::core::plugins::PluginHostBridge::menuActionsChanged,
           this, &MainWindow::rebuildToolsPluginsMenu);
  connect(m_pluginManager, &gridlock::core::plugins::PluginManager::pluginsChanged, this,
          [this, restartNotice, enqueueConsoleLine, reconcilePluginUiState]() {
            rebuildToolsPluginsMenu();
            reconcilePluginUiState();
            if (m_isShuttingDown || m_isClosing || m_pluginManager == nullptr) return;
            const auto& list = m_pluginManager->plugins();
            const bool needRestart =
                m_pluginManager->property("gl_restart_required").toBool() ||
                std::any_of(list.cbegin(), list.cend(),
                            [](const gridlock::core::plugins::LoadedPluginInfo& i) { return i.requiresRestart; });
            if (restartNotice != nullptr) {
              const bool wasHidden = restartNotice->isHidden();
              restartNotice->setVisible(needRestart);
              if (needRestart && wasHidden) {
                if (statusBar() != nullptr) {
                  statusBar()->showMessage(
                      tr("Plugin settings updated: click 'Restart GridLock Now' to apply all changes."), 10000);
                }
                enqueueConsoleLine(
                    QStringLiteral("[Plugins] Restart GridLock to apply updated plugin enablement/trust."), false);
              }
            }
          });

  if (m_runAction != nullptr) {
    connect(m_runAction, &QAction::changed, this, [this]() {
      if (m_isShuttingDown || m_isClosing || m_pluginManager == nullptr) return;
      const bool active =
          m_debugSessionActive || (m_coordinator != nullptr && m_coordinator->getProcessCount() > 0);
      if (active && !m_pluginSessionActive) {
        const int ranks = (m_coordinator != nullptr && m_coordinator->getProcessCount() > 0)
                              ? m_coordinator->getProcessCount()
                              : configuredRankCount();
        notifySessionStart(ranks, activeTargetBinaryPath());
      } else if (!active && m_pluginSessionActive) {
        notifySessionEnd();
      }
    });
  }
  if (m_stopAction != nullptr) {
    connect(m_stopAction, &QAction::triggered, this, [this]() {
      if (m_pluginSessionActive) notifySessionEnd();
    });
  }

  bindPluginCoordinatorSignals(m_coordinator);
  m_pluginManager->discoverAndLoad();
  reconcilePluginUiState();
  rebuildToolsPluginsMenu();
}

void MainWindow::bindPluginCoordinatorSignals(IBackendCoordinator* coord) {
  if (m_pluginManager == nullptr) return;
  for (const auto& c : m_pluginCoordConns) QObject::disconnect(c);
  m_pluginCoordConns.clear();

  auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(coord);
  m_pluginManager->setCoordinator(gdbCoord);
  if (gdbCoord == nullptr) {
    if (m_pluginSessionActive) notifySessionEnd();
    return;
  }

  const QPointer<MainWindow> self(this);
  const QPointer<gridlock::GdbRankCoordinator> gdbGuard(gdbCoord);
  m_pluginCoordConns.append(connect(
      gdbCoord, &gridlock::GdbRankCoordinator::rankStateChanged, this,
      [self, gdbGuard](int, const RankState& st) {
        if (!self || !gdbGuard || self->m_isShuttingDown || self->m_isClosing) return;
        if (!self->m_pluginSessionActive && gdbGuard->getProcessCount() > 0 &&
            (st.currentState == QLatin1String("running") || st.currentState == QLatin1String("stopped"))) {
          self->notifySessionStart(gdbGuard->getProcessCount(), gdbGuard->getCurrentBinaryPath());
        }
      }));
}

void MainWindow::notifySessionStart(int numRanks, const QString& targetPath) {
  Q_UNUSED(targetPath);
  if (m_pluginManager == nullptr) return;
  m_pluginSessionActive = true;
  m_pluginManager->notifySessionStarted(static_cast<int32_t>(numRanks));
}

void MainWindow::notifyRankStopped(int rank, const QString& file, int line, const QString& reason) {
  if (m_pluginManager == nullptr) return;
  uint64_t pc = 0u;
  QString func;
  if (const auto it = m_latestStates.find(rank); it != m_latestStates.end()) {
    func = it->second.currentFunction;
    const QString pcStr = it->second.registers.value(16);
    bool ok = false;
    if (pcStr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) pc = pcStr.mid(2).toULongLong(&ok, 16);
  }
  m_pluginManager->notifyRankStopped(rank, file, line, func, pc, reason);
}

void MainWindow::notifySessionEnd() {
  if (m_pluginManager == nullptr || !m_pluginSessionActive) return;
  m_pluginSessionActive = false;
  m_pluginManager->notifySessionEnded();
}

void MainWindow::rebuildToolsPluginsMenu() {
  if (m_toolsPluginsMenu == nullptr || m_pluginManager == nullptr) return;
  m_toolsPluginsMenu->clear();

  auto* bridge = m_pluginManager->hostBridge();
  const auto commands = bridge->commandActions();
  const auto nestedMenus = bridge->menuActions();

  if (commands.isEmpty() && nestedMenus.isEmpty()) {
    QAction* emptyAct = m_toolsPluginsMenu->addAction(tr("(No plugin commands registered)"));
    emptyAct->setEnabled(false);
    connect(emptyAct, &QAction::triggered, this, []() {});
  } else {
    auto bindCommandTrigger = [this](QAction* act, const QString& id, const QString& label,
                                     const QString& hint, bool isMenuPath) {
      applyPluginShortcutChecked(act, hint);
      connect(act, &QAction::triggered, this, [this, id, label, hint, isMenuPath]() {
        if (m_pluginManager == nullptr) return;
        const QByteArray idUtf8 = id.toUtf8(), lblUtf8 = label.toUtf8(), hintUtf8 = hint.toUtf8();
        GridLockCommandSpec spec{static_cast<uint32_t>(sizeof(GridLockCommandSpec)),
                                 GRIDLOCK_PLUGIN_ABI_MAJOR, GRIDLOCK_PLUGIN_ABI_MINOR, 0u,
                                 m_focusedRank, (m_coordinator != nullptr) ? m_coordinator->getProcessCount() : 0,
                                 idUtf8.constData(), lblUtf8.constData(),
                                 hintUtf8.isEmpty() ? nullptr : hintUtf8.constData(), nullptr};
        auto* hb = m_pluginManager->hostBridge();
        (void)(isMenuPath ? hb->invokeMenuAction(id, spec) : hb->invokeCommandAction(id, spec));
      });
    };

    for (const auto& cmd : commands) {
      QAction* act = m_toolsPluginsMenu->addAction(cmd.menuLabel);
      act->setObjectName(QStringLiteral("pluginCmd_") + cmd.commandId);
      bindCommandTrigger(act, cmd.commandId, cmd.menuLabel, cmd.shortcutHint, false);
    }

    QMap<QString, QMenu*> submenuByPrefix;
    for (const auto& item : nestedMenus) {
      if (item.pathSegments.isEmpty()) continue;
      QMenu* parentMenu = m_toolsPluginsMenu;
      QString prefix;
      for (int i = 0; i + 1 < item.pathSegments.size(); ++i) {
        const QString& seg = item.pathSegments.at(i);
        prefix = prefix.isEmpty() ? seg : (prefix + QLatin1Char('/') + seg);
        QMenu* sub = submenuByPrefix.value(prefix, nullptr);
        if (!sub) {
          sub = parentMenu->addMenu(seg);
          sub->setObjectName(QStringLiteral("pluginSubMenu_") + prefix);
          submenuByPrefix.insert(prefix, sub);
        }
        parentMenu = sub;
      }
      QAction* act = parentMenu->addAction(item.pathSegments.last());
      act->setObjectName(QStringLiteral("pluginMenuAct_") + item.actionId);
      bindCommandTrigger(act, item.actionId, item.menuPath, item.shortcutHint, true);
    }
  }

  m_toolsPluginsMenu->addSeparator();
  QAction* manageAct = m_toolsPluginsMenu->addAction(tr("Manage Plugins..."));
  manageAct->setObjectName(QStringLiteral("actionManagePlugins"));
  connect(manageAct, &QAction::triggered, this, [this]() {
    auto* dlg = new PreferencesDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    if (QTreeWidget* tree = dlg->categoryTree()) {
      for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(0) == tr("Plugins")) {
          tree->setCurrentItem(tree->topLevelItem(i));
          break;
        }
      }
    }
    dlg->exec();
  });
}

}  // namespace gridlock::ui

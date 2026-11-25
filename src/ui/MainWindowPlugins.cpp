/**
 * @file MainWindowPlugins.cpp
 * @brief Native plugin lifecycle wiring, throttled console telemetry routing, Tools -> Plugins menu, and self-restart notice.
 *
 * Owns `PluginManager` inside `MainWindow`, synchronizes its `GdbRankCoordinator`
 * binding across engine switches, forwards session start/end events, coalesces
 * plugin log/event console previews on a `50 ms` timer, surfaces a status-bar
 * restart notification banner with **Restart GridLock Now**, and populates **Tools -> Plugins**.
 */

#include "MainWindow.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include <QAction>
#include <QByteArray>
#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "../core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/SpackManager.hpp"
#include "../core/plugins/PluginHostBridge.hpp"
#include "../core/plugins/PluginManager.hpp"
#include "dialogs/preferences/PreferencesDialog.hpp"
#include "docks/VariablesDockWidget.hpp"
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

}  // namespace

void MainWindow::initializePluginSubsystem() {
  if (m_pluginManager != nullptr) return;

  auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator);
  m_pluginManager = new gridlock::core::plugins::PluginManager(gdbCoord, this);
  auto* bridge = m_pluginManager->hostBridge();
  bridge->setActiveRank(m_focusedRank);

  if (m_variablesDockWidget != nullptr) {
    m_variablesDockWidget->setPluginHostBridge(bridge);
    connect(m_variablesDockWidget, &gridlock::VariablesDockWidget::coordinatorBound,
            this, &MainWindow::bindPluginCoordinatorSignals, Qt::UniqueConnection);
  }

  connect(this, &MainWindow::focusedRankChanged, bridge,
          &gridlock::core::plugins::PluginHostBridge::setActiveRank, Qt::UniqueConnection);

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

  connect(bridge, &gridlock::core::plugins::PluginHostBridge::registeredActionsChanged,
          this, &MainWindow::rebuildToolsPluginsMenu);
  connect(m_pluginManager, &gridlock::core::plugins::PluginManager::pluginsChanged, this,
          [this, restartNotice, enqueueConsoleLine]() {
            rebuildToolsPluginsMenu();
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
                      tr("Plugin settings updated — click 'Restart GridLock Now' to apply all changes."), 10000);
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
  if (commands.isEmpty()) {
    QAction* emptyAct = m_toolsPluginsMenu->addAction(tr("(No plugin commands registered)"));
    emptyAct->setEnabled(false);
    connect(emptyAct, &QAction::triggered, this, []() {});
  } else {
    for (const auto& cmd : commands) {
      QAction* act = m_toolsPluginsMenu->addAction(cmd.menuLabel);
      act->setObjectName(QStringLiteral("pluginCmd_") + cmd.commandId);
      if (!cmd.shortcutHint.isEmpty()) {
        act->setShortcut(QKeySequence::fromString(cmd.shortcutHint, QKeySequence::PortableText));
      }
      const QString cmdId = cmd.commandId, label = cmd.menuLabel, hint = cmd.shortcutHint;
      connect(act, &QAction::triggered, this, [this, cmdId, label, hint]() {
        if (m_pluginManager == nullptr) return;
        const QByteArray idUtf8 = cmdId.toUtf8(), lblUtf8 = label.toUtf8(), hintUtf8 = hint.toUtf8();
        GridLockCommandSpec spec{};
        spec.struct_size = static_cast<uint32_t>(sizeof(GridLockCommandSpec));
        spec.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
        spec.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
        spec.active_rank = m_focusedRank;
        spec.rank_count = (m_coordinator != nullptr) ? m_coordinator->getProcessCount() : 0;
        spec.command_id_utf8 = idUtf8.constData();
        spec.menu_label_utf8 = lblUtf8.constData();
        spec.shortcut_hint_utf8 = hintUtf8.isEmpty() ? nullptr : hintUtf8.constData();
        spec.args_utf8 = nullptr;
        (void)m_pluginManager->hostBridge()->invokeCommandAction(cmdId, spec);
      });
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

/**
 * @file MainWindowPlugins.cpp
 * @brief Native plugin lifecycle wiring, structured telemetry routing, and Tools -> Plugins menu.
 *
 * Owns `PluginManager` inside `MainWindow`, synchronizes its `GdbRankCoordinator`
 * binding across engine switches, forwards session start/stop/rank-stopped events,
 * routes `emit_structured_event` payloads to `HpcConsoleWidget` and `MainWindow`
 * signals, and populates **Tools -> Plugins** command actions dynamically.
 */

#include "MainWindow.hpp"

#include <QAction>
#include <QByteArray>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
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

/**
 * @brief Instantiates `PluginManager`, wires telemetry/menu/dock bridges, and discovers plugins.
 */
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

  // Forward plugin structured events to HpcConsoleWidget (m_spackManager) and external listeners.
  connect(bridge, &gridlock::core::plugins::PluginHostBridge::structuredEventEmitted, this,
          [this](const QString& channel, const QString& jsonPayload) {
            if (m_isShuttingDown || m_isClosing) return;
            if (m_spackManager != nullptr) {
              m_spackManager->appendMessage(
                  QStringLiteral("[Plugin:%1] %2").arg(channel, jsonPayload), false);
            }
            emit pluginStructuredEventEmitted(channel, jsonPayload);
          });

  connect(bridge, &gridlock::core::plugins::PluginHostBridge::pluginLogReceived, this,
          [this](const QString& pluginId, int level, const QString& message) {
            if (m_isShuttingDown || m_isClosing || m_spackManager == nullptr) return;
            const QString tag = pluginId.isEmpty() ? QStringLiteral("plugin") : pluginId;
            m_spackManager->appendMessage(
                QStringLiteral("[PluginLog:%1] %2").arg(tag, message),
                level >= GRIDLOCK_LOG_ERROR);
          });

  // Mount Tools -> Plugins submenu and keep command actions live-synced.
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
  connect(m_pluginManager, &gridlock::core::plugins::PluginManager::pluginsChanged,
          this, &MainWindow::rebuildToolsPluginsMenu);

  // Track toolbar Run latch transitions to dispatch session start and session end hooks.
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

/**
 * @brief Synchronizes `PluginManager` with @p coord and hooks non-source-line stops and exits.
 */
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
      [self, gdbGuard](int rankId, const RankState& st) {
        if (!self || !gdbGuard || self->m_isShuttingDown || self->m_isClosing) return;
        if (!self->m_pluginSessionActive && gdbGuard->getProcessCount() > 0 &&
            (st.currentState == QLatin1String("running") ||
             st.currentState == QLatin1String("stopped"))) {
          self->notifySessionStart(gdbGuard->getProcessCount(), gdbGuard->getCurrentBinaryPath());
        }
        // PluginManager handles stops with line > 0 via locationChanged; cover line <= 0 here.
        if (st.currentState == QLatin1String("stopped") && st.currentLine <= 0) {
          self->notifyRankStopped(rankId, st.currentFile, st.currentLine, QStringLiteral("stopped"));
        }
      }));
}

void MainWindow::notifySessionStart(int numRanks, const QString& targetPath) {
  Q_UNUSED(targetPath);
  if (m_pluginManager == nullptr) return;
  m_pluginSessionActive = true;
  m_pluginManager->notifySessionStarted(static_cast<int32_t>(numRanks));
}

void MainWindow::notifyRankStopped(int rank,
                                   const QString& file,
                                   int line,
                                   const QString& reason) {
  if (m_pluginManager == nullptr) return;
  uint64_t pc = 0u;
  QString func;
  if (const auto it = m_latestStates.find(rank); it != m_latestStates.end()) {
    func = it->second.currentFunction;
    const QString pcStr = it->second.registers.value(16);
    bool ok = false;
    if (pcStr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
      pc = pcStr.mid(2).toULongLong(&ok, 16);
    }
  }
  m_pluginManager->notifyRankStopped(rank, file, line, func, pc, reason);
}

void MainWindow::notifySessionEnd() {
  if (m_pluginManager == nullptr || !m_pluginSessionActive) return;
  m_pluginSessionActive = false;
  m_pluginManager->notifySessionEnded();
}

/**
 * @brief Populates **Tools -> Plugins** from active `register_command_action` entries.
 */
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
      const QString cmdId = cmd.commandId;
      const QString label = cmd.menuLabel;
      const QString hint = cmd.shortcutHint;
      connect(act, &QAction::triggered, this, [this, cmdId, label, hint]() {
        if (m_pluginManager == nullptr) return;
        const QByteArray idUtf8 = cmdId.toUtf8();
        const QByteArray lblUtf8 = label.toUtf8();
        const QByteArray hintUtf8 = hint.toUtf8();
        GridLockCommandSpec spec{};
        spec.struct_size = static_cast<uint32_t>(sizeof(GridLockCommandSpec));
        spec.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
        spec.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
        spec.reserved_flags = 0u;
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

/**
 * @file MainWindow.cpp
 * @brief Implementation of the central workstation window for GridLock.
 *
 * Implements window initialization, docking management, MPI execution control,
 * cross-pane address synchronization, breakpoint propagation, and session I/O.
 */

#include "MainWindow.hpp"
#include "../core/hpc/DapCoordinator.hpp"
#include "../core/hpc/GdbRankCoordinator.hpp"
#include "../core/hpc/LspCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "dialogs/PreferencesDialog.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/ReferenceManualWidget.hpp"

#include "../core/commands/DebugCommands.hpp"
#include "../core/Version.hpp"
#include "../core/hpc/DeadlockAnalyzer.hpp"
#include "../core/hpc/MemoryDiffer.hpp"
#include "../core/hpc/MockHpcBackend.hpp"
#include "../core/managers/SessionManager.hpp"
#include "../core/managers/ShortcutManager.hpp"
#include "../core/managers/SpackManager.hpp"
#include "../core/managers/ThemeManager.hpp"
#include "dialogs/ProjectSettingsDialog.hpp"
#include "dialogs/ProjectWizard.hpp"
#include "tutorial/TutorialDialog.hpp"
#include "widgets/MpiNetworkLogWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"
#include <QFileDialog>
#include <QPointer>
#include <QtConcurrent/QtConcurrent>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QLabel>
#include <QScreen>
#include <QGuiApplication>
#include <QShortcut>
#include <QStatusBar>
#include <QTabBar>
#include <QTextCursor>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace gridlock {
class PlainJaneTooltip : public QLabel {
public:
    PlainJaneTooltip(QWidget* parent = nullptr) : QLabel(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
        setStyleSheet("padding: 4px;");
        setTextFormat(Qt::RichText);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }
    
    void showTooltip(const QPoint& globalPos, const QString& text) {
        setText(text);
        adjustSize();
        QPoint pos = globalPos + QPoint(10, 10);
        if (QScreen* screen = QGuiApplication::screenAt(globalPos)) {
            QRect avail = screen->availableGeometry();
            if (pos.x() + width() > avail.right()) pos.setX(globalPos.x() - width() - 10);
            if (pos.y() + height() > avail.bottom()) pos.setY(globalPos.y() - height() - 10);
            pos.setX(qMax(pos.x(), avail.left()));
            pos.setY(qMax(pos.y(), avail.top()));
        }
        move(pos);
        show();
    }
    
    void hideTooltip() {
        hide();
    }
};
}  // namespace gridlock

namespace gridlock::ui {

/**
 * @brief Dismisses any visible tooltip popover displayed by PlainJaneTooltip.
 */
void MainWindow::hideHoverTooltipPopup() {
    if (m_plainJaneTooltip) {
        m_plainJaneTooltip->hideTooltip();
    }
}

/**
 * @brief Returns the active network log widget embedded inside MPI diagnostics.
 * @return MpiNetworkLogWidget pointer, or nullptr if diagnostics widget unavailable.
 */
MpiNetworkLogWidget* MainWindow::networkLog() const {
    if (m_mpiDiagnosticsWidget) {
        return m_mpiDiagnosticsWidget->networkLogWidget();
    }
    return nullptr;
}

/**
 * @brief Retrieves the active SourceCodeView tab in the editor manager.
 * @return Currently displayed SourceCodeView or nullptr.
 */
SourceCodeView *MainWindow::sourceCodeView() const {
  return m_editorTabManager ? m_editorTabManager->currentSourceCodeView()
                            : nullptr;
}

/**
 * @brief Backward-compatible accessor for active SourceCodeView.
 * @return Active SourceCodeView pointer or nullptr.
 */
SourceCodeView *MainWindow::getSourceCodeView() const {
  return sourceCodeView();
}

/**
 * @brief Constructs the main application window and initializes docking architecture.
 * @param parent Optional parent widget context.
 *
 * Instantiates the mock HPC backend, sets up menus, toolbars, docks, and status
 * bar telemetry. Restores persistent window geometry, dock layouts, and splitters.
 */
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  // Construct mock HPC backend provider for local emulation.
  m_hpcBackend = new gridlock::core::MockHpcBackend(this);
  setupUi();
  setupMenu();
  setupDocks();
  setupStatusBar();

  // Initialize global shortcuts and install top-level event filter.
  gridlock::core::ShortcutManager::instance().initialize(this);
  qApp->installEventFilter(this);

  // Eagerly load default source file so UI is populated before running.
  m_lspCoordinator = new gridlock::core::LspCoordinator(this);
  m_plainJaneTooltip = new gridlock::PlainJaneTooltip(this);
  if (QFileInfo::exists("tutorial/inspection_demo.cpp")) {
      loadSourceFile("tutorial/inspection_demo.cpp");
  }

  // Load offline persistence TOML breakpoints from user preferences.
  m_persistentBreakpoints =
      gridlock::core::ConfigManager::instance().getBreakpoints();

  // Restore saved geometry and dock configuration.
  QSettings settings("gridlock", "debugger");
  if (settings.contains("MainWindow/Geometry_v8")) {
      restoreGeometry(settings.value("MainWindow/Geometry_v8").toByteArray());
  }
  // Enforce minimum usable geometry to prevent microscopic window mapping.
  if (width() < 800 || height() < 600) {
      resize(1440, 900);
  }
  if (settings.contains("MainWindow/State_v8")) {
      restoreState(settings.value("MainWindow/State_v8").toByteArray());
  }
  
  // Restore individual splitter sash positions.
  for (auto* splitter : findChildren<QSplitter*>()) {
      if (!splitter->objectName().isEmpty()) {
          auto state = settings.value("MainWindow/Splitter_v8_" + splitter->objectName()).toByteArray();
          if (!state.isEmpty()) {
              splitter->restoreState(state);
          }
      }
  }

  // Constrain side dock widths to prevent collapsing below 150px.
  for (auto* dock : findChildren<QDockWidget*>()) {
      const auto area = dockWidgetArea(dock);
      if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
          dock->setMinimumWidth(150);
          dock->setMaximumWidth(QWIDGETSIZE_MAX);
          if (dock->widget()) {
              dock->widget()->setMinimumWidth(150);
              dock->widget()->setMaximumWidth(QWIDGETSIZE_MAX);
          }
      }
  }

  // Hide empty tab bars and configure tab scrolling.
  for (auto* tabBar : findChildren<QTabBar*>()) {
      if (tabBar->count() == 0) {
          tabBar->hide();
      }
      tabBar->setUsesScrollButtons(true);
      tabBar->setElideMode(Qt::ElideNone);
  }

  // Route window to preferred monitor if specified in configuration.
  auto& cfg = gridlock::core::ConfigManager::instance();
  applyPreferredScreenRouting(cfg.preferredMonitor(), cfg.openUnfocused());
  loadPreviewFixture();
}

/**
 * @brief Generates synthetic telemetry fixture data for headless snapshots and testing.
 *
 * Populates 8 sample MPI ranks with stopped/running execution states and simulated
 * program counter registers across the .text segment.
 */
void MainWindow::loadPreviewFixture() {
  QList<RankState> fixtureRanks;
  fixtureRanks.reserve(8);
  for (int i = 0; i < 8; ++i) {
    RankState st;
    st.rankId = i;
    st.currentState = (i == 0) ? QStringLiteral("stopped") : QStringLiteral("running");
    if (i == 0) {
      st.registers[16] = QStringLiteral("0x00401120");
    } else if (i == 1) {
      st.registers[16] = QStringLiteral("0x00401155");
    } else {
      st.registers[16] = QStringLiteral("0x00401157");
    }
    st.totalRuntimeMs = 12 + i * 3;
    fixtureRanks.append(st);
    m_latestStates[i] = st;
  }
  // Propagate fixture ranks to ServerRackView or DisassemblyView.
  if (m_serverRackView) {
    m_serverRackView->updateRanks(fixtureRanks);
  } else if (m_disassemblyView) {
    QMap<int, uint64_t> pcs;
    pcs.insert(0, 0x00401120ULL);
    pcs.insert(1, 0x00401155ULL);
    for (int i = 2; i < 8; ++i) {
      pcs.insert(i, 0x00401157ULL);
    }
    m_disassemblyView->setRankAddresses(pcs);
  }
}

/**
 * @brief Dispatches a polymorphic debugger command object.
 * @param cmd The command instance to execute.
 */
void MainWindow::executeCommand(
    std::unique_ptr<gridlock::core::commands::IDebugCommand> cmd) {
  if (cmd) {
    cmd->execute();
  }
}

/**
 * @brief Attaches a backend coordinator (GDB or DAP) and establishes signal routes.
 * @param coord Pointer to active coordinator instance.
 */
void MainWindow::setCoordinator(IBackendCoordinator *coord) {
  m_coordinator = coord;

  if (auto *dapCoord = dynamic_cast<DapCoordinator *>(m_coordinator)) {
    connect(dapCoord, &DapCoordinator::stateChanged, this,
            [this, dapCoord](SessionState state) {
              if (m_runAction) {
                if (state == SessionState::Disconnected) {
                  m_runAction->setEnabled(true);
                  m_runAction->setText("▶ Run Target");
                  updateStatusTelemetry(QString(), QStringLiteral("IDLE"));
                } else {
                  m_runAction->setEnabled(false);
                  if (state == SessionState::Launching) {
                    m_runAction->setText("Launching...");
                    updateStatusTelemetry(QString(), QStringLiteral("LAUNCHING"));
                  } else if (state == SessionState::Queued) {
                    m_runAction->setText("Queued in SLURM...");
                    updateStatusTelemetry(QString(), QStringLiteral("QUEUED"));
                  } else if (state == SessionState::Running) {
                    m_runAction->setText("Running");
                    updateStatusTelemetry(QString(), QStringLiteral("RUNNING"));
                    for (int i = 0; i < dapCoord->getProcessCount(); ++i) {
                        RankState rs;
                        if (m_latestStates.contains(i)) {
                            rs = m_latestStates[i];
                        }
                        rs.currentState = "running";
                        rs.executionTimer.start();
                        onRankStateChanged(i, rs);
                    }
                  } else if (state == SessionState::Paused) {
                    m_runAction->setText("Paused");
                    updateStatusTelemetry(QString(), QStringLiteral("PAUSED"));
                  }
                }
              }
            });

    connect(dapCoord, &DapCoordinator::executionStopped, this,
            [this](int rankId, const QString& /*reason*/) {
                RankState rs;
                if (m_latestStates.contains(rankId)) {
                    rs = m_latestStates[rankId];
                }
                rs.currentState = "stopped";
                rs.totalRuntimeMs += rs.executionTimer.isValid() ? rs.executionTimer.elapsed() : 0;
                onRankStateChanged(rankId, rs);
            });
  }

  // Connect variable inspection dock and deadlock analyzer.
  if (m_variablesDockWidget) {
    m_variablesDockWidget->setCoordinator(m_coordinator);
    if (auto *gdbCoord =
            dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {

      // Instantiate deadlock analyzer daemon monitoring lock hierarchies.
      if (!m_deadlockAnalyzer) {
        m_deadlockAnalyzer =
            new gridlock::core::DeadlockAnalyzer(gdbCoord, this);
        connect(gdbCoord, &gridlock::GdbRankCoordinator::rankStateChanged,
                this, &MainWindow::onRankStateChanged);
        if (m_mpiDiagnosticsWidget) {
          // Connect deadlock notifications to diagnostic widget banners.
          connect(m_deadlockAnalyzer,
                  &gridlock::core::DeadlockAnalyzer::deadlockDetected,
                  m_mpiDiagnosticsWidget,
                  &MpiDiagnosticsWidget::onDeadlockDetected);
          connect(m_deadlockAnalyzer,
                  &gridlock::core::DeadlockAnalyzer::rankCleared,
                  m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::onRankCleared);
        }
      }
    }
  }

  // Bind coordinator execution, hover, and memory events to UI views.
  if (m_coordinator) {
    // Route execution location updates (file:line) to the focused rank's editor.
    connect(m_coordinator, &IBackendCoordinator::locationChanged, this,
            [this](int rankId, const QString &file, int line) {
              if (rankId == m_focusedRank) {
                if (!file.isEmpty() && m_editorTabManager) {
                  if (auto view = m_editorTabManager->openFile(file)) {
                    view->highlightCurrentLine(line);
                  }
                } else if (getSourceCodeView()) {
                  getSourceCodeView()->highlightCurrentLine(line);
                }
              }
            });

    // Wire GDB-specific signals if coordinator is a GdbRankCoordinator.
    if (auto *gdbCoord =
            dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
      // Connect variable hover evaluation popover to display rich tooltip.
      connect(gdbCoord, &gridlock::GdbRankCoordinator::hoverEvaluationComplete,
              this, [this](QString varName, QString result, QPoint globalPos) {
                if (getSourceCodeView() && m_plainJaneTooltip) {
                  m_plainJaneTooltip->showTooltip(
                      globalPos,
                      QString("<b>%1</b>: %2").arg(varName).arg(result));
                }
              });
      // Stream expression evaluator widget evaluations.
      if (m_expressionEvaluatorWidget) {
        connect(gdbCoord, &gridlock::GdbRankCoordinator::expressionEvaluated,
                m_expressionEvaluatorWidget,
                &ExpressionEvaluatorWidget::appendResult);
      }
      // Stream GDB console traffic (stdout and command inputs).
      if (m_gdbConsoleWidget) {
        connect(gdbCoord, &gridlock::GdbRankCoordinator::gdbOutputReceived,
                m_gdbConsoleWidget, &GdbConsoleWidget::appendGdbOutput);
        connect(gdbCoord, &gridlock::GdbRankCoordinator::commandSentToGdb,
                m_gdbConsoleWidget, &GdbConsoleWidget::appendGdbInput);
        connect(m_gdbConsoleWidget, &GdbConsoleWidget::commandEntered, gdbCoord,
                &gridlock::GdbRankCoordinator::sendCommand);
      }
      // Trap floating-point exceptions (SIGFPE) across MPI ranks.
      connect(gdbCoord, &gridlock::GdbRankCoordinator::gdbOutputReceived, this,
              [this](int rankId, const QString &output) {
                if (output.contains("reason=\"signal-received\"") &&
                    output.contains("signal-name=\"SIGFPE\"")) {
                  onRankSelected(rankId);
                  if (networkLog()) {
                    networkLog()->appendError(
                        QString("SIGFPE (Floating-Point Exception) detected on "
                                "Rank %1!\n")
                            .arg(rankId));
                  }
                }
              });
      // Stream memory hex dumps directly to the hex inspection view.
      connect(
          gdbCoord, &gridlock::GdbRankCoordinator::memoryDataReady, this,
          [this](int rankId, qint64 beginAddress, const QString &hexContents) {
            if (rankId == m_focusedRank && m_memView) {
              m_memView->setMemoryData(beginAddress, hexContents);
            }
          });
    }

    // Connect generic process stdout/stderr to network console log.
    if (networkLog()) {
      connect(m_coordinator, &IBackendCoordinator::targetOutputReceived,
              this,
              [this](const QString &category, const QString &output) {
                if (networkLog()) networkLog()->appendText(category, output);
              });
    }
    // Update memory dump on raw byte buffer reception.
    connect(m_coordinator, &IBackendCoordinator::memoryRead, this,
            [this](int rankId, const QString &address, const QByteArray &data) {
              if (rankId == m_focusedRank && m_memView) {
                m_memView->setMemoryData(address, data);
              }
            });
    // Forward JSON register values to register inspection panel.
    connect(m_coordinator, &IBackendCoordinator::registersUpdated, this,
            [this](int rankId, const QJsonArray &registers) {
              if (rankId == m_focusedRank && m_registerView) {
                m_registerView->updateRegisters(registers);
              }
            });
  }
}

/**
 * @brief Constructs application menu bar actions, shortcuts, and dialog triggers.
 *
 * Populates File, Edit, View, Tools, and Help menus with project wizards,
 * session serialization, SLURM batch submissions, and build triggers.
 */
void MainWindow::setupMenu() {
  QMenuBar *menuBar = new QMenuBar(this);
  setMenuBar(menuBar);

  // Construct File menu with workspace, project wizard, session, and SLURM triggers.
  QMenu *fileMenu = menuBar->addMenu("&File");

  // New Project Action: Launches the multi-step project wizard allowing configuration
  // of target executable, MPI rank counts, hostfiles, and initial watch expressions.
  QAction *newAction = fileMenu->addAction("New Project...");
  connect(newAction, &QAction::triggered, this, [this]() {
    gridlock::ui::dialogs::ProjectWizard dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
      QString projectRoot = dialog.getProjectRoot();
      if (projectRoot.isEmpty())
        return;

      // Reset existing differential watches and editor tabs when loading a new project.
      if (m_differentialGrid)
        m_differentialGrid->clearWatches();
      if (m_editorTabManager)
        m_editorTabManager->clearAllTabs();

      // Persist chosen workspace root and project configurations.
      gridlock::core::ConfigManager::instance().setWorkspace(projectRoot);

      auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
      ps.targetBinary = dialog.getTargetBinary().toStdString();
      ps.workingDirectory = dialog.getProjectRoot().toStdString();
      gridlock::core::ConfigManager::instance().saveProjectSettings(ps);

      // Configure default MPI launcher settings based on wizard selections.
      auto dbgSettings =
          gridlock::core::ConfigManager::instance().getDebuggerSettings();
      dbgSettings.mpiExecutable = "mpirun";
      dbgSettings.defaultRanks = dialog.getRanks();
      // Using the generated mpirun string for demonstration
      dbgSettings.mpiArgs =
          dialog.getMpiLaunchCommand()
              .remove("mpirun ")
              .remove("-np " + QString::number(dialog.getRanks()) + " ")
              .remove(dialog.getTargetBinary())
              .trimmed();
      gridlock::core::ConfigManager::instance().saveDebuggerSettings(
          dbgSettings);

      // Point project explorer tree to new project root directory.
      if (m_projectExplorerWidget) {
        m_projectExplorerWidget->setRootPath(projectRoot);
      }

      // Immediately launch debugging session if coordinator is available.
      if (m_coordinator) {
        startDebuggingSession(dialog.getTargetBinary(), dialog.getRanks());
      }
    }
  });

  // Open Workspace Action: Opens existing directory, restores persistent breakpoints
  // and project-level watch expressions into the differential matrix.
  QAction *openProjAction = fileMenu->addAction("Open Workspace...");
  connect(openProjAction, &QAction::triggered, this, [this]() {
    QString dir = QFileDialog::getExistingDirectory(this, "Open Workspace",
                                                    QDir::currentPath());
    if (!dir.isEmpty()) {
      if (m_differentialGrid)
        m_differentialGrid->clearWatches();
      if (m_editorTabManager)
        m_editorTabManager->clearAllTabs();

      gridlock::core::ConfigManager::instance().setWorkspace(dir);
      m_persistentBreakpoints =
          gridlock::core::ConfigManager::instance().getBreakpoints();

      auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
      for (const auto &w : ps.watchExpressions) {
        if (m_differentialGrid)
          m_differentialGrid->addVariableColumn(QString::fromStdString(w));
      }
    }
  });

  // Open Source File Action: Direct file picker bypassing project hierarchy.
  QAction *openAction = fileMenu->addAction("Open Source File");
  connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

  fileMenu->addSeparator();

  // Session Serialization Actions: Export and restore active workspace layout,
  // breakpoint configurations, variable watches, and rank allocations.
  QAction *saveSessionAction = fileMenu->addAction("Save Session As...");
  connect(saveSessionAction, &QAction::triggered, this,
          &MainWindow::saveSessionAs);

  QAction *loadSessionAction = fileMenu->addAction("Load Session...");
  connect(loadSessionAction, &QAction::triggered, this,
          &MainWindow::loadSession);

  // Recent Sessions Submenu: Dynamically populated with most-recently-used session files.
  QMenu *recentMenu = fileMenu->addMenu("Recent Sessions");
  connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu]() {
    recentMenu->clear();
    QStringList mru =
        gridlock::core::managers::SessionManager::instance().getMruSessions();
    for (const QString &session : mru) {
      QAction *act = recentMenu->addAction(session);
      connect(act, &QAction::triggered, this, [this, session]() {
        gridlock::core::managers::SessionManager::instance().loadSession(
            session);
      });
    }
    if (mru.isEmpty()) {
      QAction *empty = recentMenu->addAction("No Recent Sessions");
      empty->setEnabled(false);
    }
  });

  fileMenu->addSeparator();

  // Project Settings Dialog Trigger: Edits compiler flags, include paths, and runtime environments.
  QAction *projSetAction = fileMenu->addAction("Project Settings");
  connect(projSetAction, &QAction::triggered, this, [this]() {
    ProjectSettingsDialog dlg(this);
    dlg.exec();
  });

  // SLURM Job Submission Action: Interpolates active target into template and queues via HPC backend.
  QAction *slurmAction = fileMenu->addAction("Submit SLURM Job");
  connect(slurmAction, &QAction::triggered, this, [this]() {
    const auto slurm =
        gridlock::core::ConfigManager::instance().getSlurmSettings();
    QString script = slurm.scriptTemplate;
    script.replace("{FILE}", m_currentFile);
    if (m_spackManager) {
      m_spackManager->appendMessage("Submitting SLURM batch job...");
      m_bottomTabs->setCurrentWidget(m_spackManager);
    }
    m_hpcBackend->submitSlurmJob(script);
  });

  fileMenu->addSeparator();
  fileMenu->addAction("Exit", this, &MainWindow::close);

  // Construct Edit menu with focus-aware editor operations and Preferences dialog trigger.
  // Supports Undo/Redo, Clipboard (Cut/Copy/Paste/Select All), Search/Navigation
  // (Find in View, Go to Line, Toggle Line Comment), and global Preferences.
  QMenu *editMenu = menuBar->addMenu("&Edit");
  const QList<QPair<QString, QKeySequence>> editActions = {
      {QStringLiteral("Undo"), QKeySequence::Undo},
      {QStringLiteral("Redo"), QKeySequence::Redo},
      {QStringLiteral("Cut"), QKeySequence::Cut},
      {QStringLiteral("Copy"), QKeySequence::Copy},
      {QStringLiteral("Paste"), QKeySequence::Paste},
      {QStringLiteral("Select All"), QKeySequence::SelectAll},
      {QStringLiteral("Find in View..."), QKeySequence::Find},
      {QStringLiteral("Go to Line..."), QKeySequence(Qt::CTRL | Qt::Key_L)},
      {QStringLiteral("Toggle Comment"), QKeySequence(Qt::CTRL | Qt::Key_Slash)}
  };
  // Register each action on the Edit menu and route its signal to triggerEditMenuAction().
  for (int i = 0; i < editActions.size(); ++i) {
    // Insert structural dividers between History, Clipboard, and Navigation groups.
    if (i == 2 || i == 6) editMenu->addSeparator();
    QAction *act = editMenu->addAction(editActions[i].first);
    act->setShortcut(editActions[i].second);
    const QString actLabel = editActions[i].first;
    // Dispatch action by label to the currently focused editor or disassembly view.
    connect(act, &QAction::triggered, this,
            [this, actLabel]() { triggerEditMenuAction(actLabel); });
  }
  editMenu->addSeparator();
  // Preferences Action: Launches the multi-page PreferencesDialog (Ctrl+,).
  QAction *prefAction = editMenu->addAction("Preferences");
  prefAction->setShortcut(QKeySequence::Preferences);
  connect(prefAction, &QAction::triggered, this, &MainWindow::openPreferences);
  menuBar->addMenu("&View");

  // Construct Tools menu for smart build system compilation, cleaning, rebuilding, and unit tests.
  QMenu *toolsMenu = menuBar->addMenu("&Tools");
  QAction *buildAction = toolsMenu->addAction("Build Target");
  buildAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
  connect(buildAction, &QAction::triggered, this, &MainWindow::buildTargetWorkflow);

  QAction *cleanAction = toolsMenu->addAction("Clean Target");
  connect(cleanAction, &QAction::triggered, this, &MainWindow::cleanTargetWorkflow);

  QAction *rebuildAction = toolsMenu->addAction("Rebuild Target");
  connect(rebuildAction, &QAction::triggered, this, &MainWindow::rebuildTargetWorkflow);

  toolsMenu->addSeparator();
  QAction *runTestsAction = toolsMenu->addAction("Run Tests");
  connect(runTestsAction, &QAction::triggered, this,
          &MainWindow::runTargetRequested);

  // Construct Help menu with comprehensive About dialog and attribution metadata.
  QMenu *helpMenu = menuBar->addMenu("&Help");
  QAction *aboutAction = helpMenu->addAction("About GridLock");
  connect(aboutAction, &QAction::triggered, this, [this]() {
    // Asynchronously query remote Git tags to display latest release version.
    QProcess gitProc;
    gitProc.start("git", QStringList()
                             << "ls-remote" << "--tags" << "--sort=v:refname"
                             << "https://github.com/bnorthern42/GridLock.git");
    gitProc.waitForFinished(3000);
    // Fall back to built-in compile-time version string if network check fails.
    QString latestTag = "v" + gridlock::core::Version::getString();
    if (gitProc.exitStatus() == QProcess::NormalExit &&
        gitProc.exitCode() == 0) {
      // Parse output stream to extract highest lexicographical tag name.
      QString output =
          QString::fromUtf8(gitProc.readAllStandardOutput()).trimmed();
      if (!output.isEmpty()) {
        QStringList lines = output.split('\n');
        QString lastLine = lines.last();
        QString tag = lastLine.split('\t').last().replace("refs/tags/", "");
        if (!tag.isEmpty()) {
          latestTag = tag;
        }
      }
    }

    // Construct modal message box with rich HTML formatting and project logo.
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("About GridLock");
    msgBox.setIconPixmap(
        QPixmap(":/icon.png")
            .scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(
        QString("<b>GridLock IDE %1</b><br><br>"
                "A high-performance graphical debugger for MPI "
                "applications.<br><br>"
                "<a href=\"https://github.com/bnorthern42/GridLock\">GridLock "
                "on GitHub</a><br><br>"
                "<b>Acknowledgments & Attributions:</b><ul>"
                "<li><a href=\"https://www.gnu.org/software/ddd/\">GNU "
                "DDD</a>: For pioneering visual data display in debugging.</li>"
                "<li><a href=\"https://www.kdbg.org/\">KDbg</a>: For early "
                "inspiration on KDE/Qt-based GDB frontend wrappers.</li>"
                "<li><a href=\"https://www.gdbgui.com/\">gdbgui</a>: For "
                "demonstrating the power of browser-based debug visualization, "
                "which inspired our native DifferentialGrid.</li>"
                "<li><a href=\"https://zealdocs.org/\">Zeal</a>: For the "
                "open-source .docset standard and offline documentation "
                "workflows that power our Reference Manual.</li>"
                "</ul><br>"
                "Special thanks to the <b>Spack</b>, <b>SLURM</b>, and "
                "<b>OpenMPI</b> communities for the tools that power our HPC "
                "environments.")
            .arg(latestTag));
    // Enable interactive hyperlinks in the about box so documentation and project links open in user browser.
    msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
    // Display dialog modally to ensure user acknowledges application provenance and acknowledgments.
    msgBox.exec();
  });
}

/**
 * @brief Updates workspace views upon receiving state changes for an MPI rank.
 * @param rankId Zero-based rank index.
 * @param state Snapshot containing current execution state, PC, and registers.
 */
void MainWindow::onRankStateChanged(int rankId, const RankState &state) {
  // Cache the newest rank state in local memory map for fast lookup.
  m_latestStates[rankId] = state;
  // Reflect state changes on the cluster-wide Server Rack overview widget.
  m_serverRackView->updateRankState(rankId, state);

  // If the stopped or updated rank is currently focused, update detail inspection views.
  if (rankId == m_focusedRank) {
    // Refresh disassembly text if updated assembly instructions are present.
    if (!state.disassemblyText.isEmpty()) {
      m_disassemblyView->updateDisassembly(state.disassemblyText);
    }
    // Update top status bar with new PC register address and operational state.
    const QString pcAddr = state.registers.value(16, m_currentSeekOrPc);
    updateStatusTelemetry(pcAddr, state.currentState.isEmpty() ? m_currentTargetState : state.currentState.toUpper());
    // If execution has suspended, reposition editor tabs to the active source file and line.
    if (state.currentState == "stopped") {
      if (state.currentLine > 0) {
        // Attempt to open the source file in a tab; fall back to the central source code view.
        if (!state.currentFile.isEmpty() && m_editorTabManager) {
          if (auto view = m_editorTabManager->openFile(state.currentFile)) {
            // Apply line highlight styling to the stopped execution line.
            view->highlightCurrentLine(state.currentLine);
          }
        } else if (getSourceCodeView()) {
          // Highlight line on default source code viewer widget when tabs are uninitialized.
          getSourceCodeView()->highlightCurrentLine(state.currentLine);
        }
      }
    }
  }

  // Synchronize watched variable column matrix across all ranks in the differential grid.
  m_differentialGrid->setVariableData(rankId, state.variableWatches);
  // Forward register values to register view only if the changed rank is focused.
  if (rankId == m_focusedRank && m_registerView) {
    m_registerView->updateRegisters(state);
  }
}

/**
 * @brief Focuses inspection views (Disassembly, Registers, Memory) on a selected rank.
 * @param rankId The target rank index selected by user.
 */
void MainWindow::onRankSelected(int rankId) {
  // Switch internal focus pointer to the newly selected rank ID.
  m_focusedRank = rankId;
  if (m_latestStates.count(rankId)) {
    const auto &state = m_latestStates[rankId];
    const QString pcAddr = state.registers.value(16, m_currentSeekOrPc);
    // Refresh header telemetry breadcrumb with selected rank's status.
    updateStatusTelemetry(pcAddr, state.currentState.isEmpty() ? m_currentTargetState : state.currentState.toUpper());
    m_disassemblyView->updateDisassembly(state.disassemblyText);

    // If disassembly is empty, trigger an asynchronous disassembly request.
    if (state.disassemblyText.isEmpty()) {
      if (auto *gdbCoord =
              dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
        gdbCoord->requestDisassemblyFallback(rankId);
      }
    }

    // Scroll source code editor to the current execution line for stopped ranks.
    if (state.currentState == "stopped") {
      if (state.currentLine > 0) {
        if (!state.currentFile.isEmpty() && m_editorTabManager) {
          if (auto view = m_editorTabManager->openFile(state.currentFile)) {
            view->highlightCurrentLine(state.currentLine);
          }
        } else if (getSourceCodeView()) {
          getSourceCodeView()->highlightCurrentLine(state.currentLine);
        }
      }
    }

    // Force update DifferentialGrid table matrix rows
    m_differentialGrid->setVariableData(rankId, state.variableWatches);
    if (m_registerView) {
      m_registerView->updateRegisters(state);
    }
  } else {
    // Fallback status telemetry when state has not yet been cached.
    updateStatusTelemetry();
    if (auto *gdbCoord =
            dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
      gdbCoord->requestDisassemblyFallback(rankId);
    }
  }
}

/**
 * @brief Handles application termination, cleans up sessions, and persists layout.
 * @param event Close event payload.
 */
void MainWindow::closeEvent(QCloseEvent *event) {
  // Gracefully terminate active debug sessions across all spawned ranks and DAP processes.
  if (m_coordinator) {
    m_coordinator->terminateSession();
  }
  // Terminate language server daemon to avoid dangling background LSP processes.
  if (m_lspCoordinator) {
    m_lspCoordinator->stop();
  }

  // Persist project settings watch expressions so user watches survive restarts.
  if (m_differentialGrid) {
    auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
    ps.watchExpressions = m_differentialGrid->getWatchExpressions();
    gridlock::core::ConfigManager::instance().saveProjectSettings(ps);
  }

  // Serialize window geometry, dock layouts, and splitter proportions to QSettings.
  QSettings settings("gridlock", "debugger");
  settings.setValue("MainWindow/Geometry_v8", saveGeometry());
  settings.setValue("MainWindow/State_v8", saveState());

  // Store individual named splitter positions to restore precise panel distributions.
  for (auto* splitter : findChildren<QSplitter*>()) {
      if (!splitter->objectName().isEmpty()) {
          settings.setValue("MainWindow/Splitter_v8_" + splitter->objectName(), splitter->saveState());
      }
  }

  // Accept window close event allowing Qt to proceed with widget teardown.
  event->accept();
}

/**
 * @brief Global event filter routing keyboard shortcuts through ShortcutManager.
 * @param watched Target QObject.
 * @param event Event payload.
 * @return True if event was handled, false otherwise.
 */
bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  // Intercept key press events and query the application-wide ShortcutManager.
  if (event->type() == QEvent::KeyPress) {
    auto *keyEvent = static_cast<QKeyEvent *>(event);
    // If a shortcut matches active key sequence, consume event immediately.
    if (gridlock::core::ShortcutManager::instance().handleKeyPress(watched,
                                                                   keyEvent)) {
      return true;
    }
  }
  // Delegate unhandled events to base QMainWindow implementation for default focus/tab behavior.
  return QMainWindow::eventFilter(watched, event);
}

/**
 * @brief Launches a multi-rank MPI debugging session.
 * @param binaryPath Executable path to target binary.
 * @param ranks Number of parallel MPI ranks to allocate.
 */
void MainWindow::startDebuggingSession(const QString &binaryPath, int ranks) {
  // Verify that an active coordinator exists before triggering launch.
  if (!m_coordinator)
    return;

  // Launch session through attached coordinator with specified binary and rank count.
  m_coordinator->launchParallelSession(binaryPath, ranks);

  // Reset console and server rack view ranks to reflect new process topology.
  if (m_gdbConsoleWidget) {
    // Rebuild per-rank tab bars or prompt indicators in the GDB console widget.
    m_gdbConsoleWidget->resetRanks(ranks);
  }
  if (m_serverRackView) {
    // Reinitialize server rack slots to match newly spawned MPI worker count.
    m_serverRackView->resetRanks(ranks);
  }

  // Register differential grid watch variables with GDB for automated evaluation.
  if (m_differentialGrid) {
    auto watches = m_differentialGrid->getWatchExpressions();
    if (auto *gdbCoord =
            dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
      // Loop over active watch strings and send subscription queries to GDB backend.
      for (const auto &w : watches) {
        gdbCoord->registerWatchVariable(QString::fromStdString(w));
      }
    }
  }
}

/**
 * @brief No-op Vulkan instance configuration placeholder.
 * @param inst Active Vulkan instance pointer.
 *
 * Provides a hook for hardware-accelerated rendering surfaces when Vulkan
 * presentation windows are enabled in advanced high-performance profiles.
 */
void MainWindow::setVulkanInstance([[maybe_unused]] QVulkanInstance *inst) {}

}  // namespace gridlock::ui
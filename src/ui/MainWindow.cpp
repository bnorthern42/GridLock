/**
 * @file MainWindow.cpp
 * @brief Implementation of the central workstation window for GridLock.
 *
 * Implements window initialization, docking management, MPI execution control,
 * cross-pane address synchronization, breakpoint propagation, and session I/O.
 */

#include "MainWindow.hpp"
#include "../core/Version.hpp"
#include "../core/hpc/DapCoordinator.hpp"
#include "../core/hpc/DeadlockAnalyzer.hpp"
#include "../core/hpc/GdbRankCoordinator.hpp"
#include "../core/hpc/LspCoordinator.hpp"
#include "../core/hpc/MockHpcBackend.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/SessionManager.hpp"
#include "../core/commands/DebugCommands.hpp"
#include "../core/managers/ShortcutManager.hpp"
#include "../core/managers/SpackManager.hpp"
#include "../core/managers/ThemeManager.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "dialogs/PreferencesDialog.hpp"
#include "dialogs/ProjectSettingsDialog.hpp"
#include "dialogs/ProjectWizard.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/MpiNetworkLogWidget.hpp"
#include "MainWindowExecution.cpp"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDockWidget>
#include <QDynamicPropertyChangeEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QJsonArray>
#include <QJsonObject>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
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
        setText(text); adjustSize();
        QPoint pos = globalPos + QPoint(10, 10);
        if (QScreen* screen = QGuiApplication::screenAt(globalPos)) {
            QRect avail = screen->availableGeometry();
            if (pos.x() + width() > avail.right()) pos.setX(globalPos.x() - width() - 10);
            if (pos.y() + height() > avail.bottom()) pos.setY(globalPos.y() - height() - 10);
            pos.setX(qMax(pos.x(), avail.left()));
            pos.setY(qMax(pos.y(), avail.top()));
        }
        move(pos); show();
    }

    void hideTooltip() { hide(); }
};
}  // namespace gridlock

namespace gridlock::ui {

/**
 * @brief Dismisses any visible tooltip popover displayed by PlainJaneTooltip.
 */
void MainWindow::hideHoverTooltipPopup() {
    if (m_plainJaneTooltip) m_plainJaneTooltip->hideTooltip();
}

/**
 * @brief Returns the active network log widget embedded inside MPI diagnostics.
 * @return MpiNetworkLogWidget pointer, or nullptr if diagnostics widget unavailable.
 */
MpiNetworkLogWidget* MainWindow::networkLog() const {
    return m_mpiDiagnosticsWidget ? m_mpiDiagnosticsWidget->networkLogWidget() : nullptr;
}

/**
 * @brief Retrieves the active SourceCodeView tab in the editor manager.
 * @return Currently displayed SourceCodeView or nullptr.
 */
SourceCodeView *MainWindow::sourceCodeView() const {
  return m_editorTabManager ? m_editorTabManager->currentSourceCodeView() : nullptr;
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
  setupCallStackDock();
  setupStatusBar();

  // Keep active file tracking synchronized whenever F12 Go-to-Definition switches editor tabs.
  if (m_editorTabManager) {
    connect(m_editorTabManager, &EditorTabManager::definitionJumpRequested, this,
            [this](const QString &filePath, int oneBasedLine, int /*zeroBasedColumn*/) {
              m_currentFile = filePath;
              if (networkLog()) {
                networkLog()->appendText(
                    QStringLiteral("Navigated to definition: %1:%2\n").arg(filePath).arg(oneBasedLine));
              }
            });
  }

  // Initialize global shortcuts and install top-level event filter.
  gridlock::core::ShortcutManager::instance().initialize(this);
  qApp->installEventFilter(this);

  // Eagerly load default source file so UI is populated before running.
  m_lspCoordinator = new gridlock::core::LspCoordinator(this);
  m_plainJaneTooltip = new gridlock::PlainJaneTooltip(this);
  if (m_targetSelector) {
    // Register the full 7-program MPI showcase suite ahead of the Open ELF / Binary... item.
    // Each target is mapped to its relative tutorial source code path.
    const QList<QPair<QString, QString>> showcaseTargets = {
        {QStringLiteral("inspection_demo (MPI)"), QStringLiteral("tutorial/inspection_demo.cpp")},
        {QStringLiteral("deadlock_ring (MPI)"), QStringLiteral("tutorial/deadlock_ring.cpp")},
        {QStringLiteral("collective_mismatch (MPI)"), QStringLiteral("tutorial/collective_mismatch.cpp")},
        {QStringLiteral("ghost_cell_drift (MPI)"), QStringLiteral("tutorial/ghost_cell_drift.cpp")},
        {QStringLiteral("heap_imbalance (MPI)"), QStringLiteral("tutorial/heap_imbalance.cpp")},
        {QStringLiteral("tag_mismatch_stall (MPI)"), QStringLiteral("tutorial/tag_mismatch_stall.cpp")},
        {QStringLiteral("simd_struct_watch (MPI)"), QStringLiteral("tutorial/simd_struct_watch.cpp")}};
    for (const auto& entry : showcaseTargets) {
      // Determine if tutorial source file exists relative to working tree or parent builddir.
      const bool localExists = QFileInfo::exists(entry.second) || QFileInfo::exists(QStringLiteral("../") + entry.second);
      // Fall back to read-only AppImage share path ($APPDIR/usr/share/gridlock/tutorial/) if external.
      const QString targetData = localExists ? entry.second : resolveTutorialTarget(entry.second);
      // Update existing default items (inspection_demo, deadlock_ring) or insert new tutorial targets.
      const int existingIdx = m_targetSelector->findText(entry.first);
      if (existingIdx >= 0) {
        // Synchronize item data with resolved file path so toolbar selection activates proper source.
        m_targetSelector->setItemData(existingIdx, targetData);
      } else {
        // Position new tutorial entries directly before the open binary file chooser item.
        const int openIdx = m_targetSelector->findData(QStringLiteral("__open_binary__"));
        if (openIdx >= 0) m_targetSelector->insertItem(openIdx, entry.first, targetData);
        else m_targetSelector->addItem(entry.first, targetData);
      }
    }
  }
  // Resolve initial demonstration source file across local workspace and AppImage paths.
  const QString initialSource = resolveTutorialTarget(QStringLiteral("tutorial/inspection_demo.cpp"));
  // Eagerly load resolved demo source file into editor canvas if present on disk.
  if (QFileInfo::exists(initialSource)) loadSourceFile(initialSource);

  // Load offline persistence TOML breakpoints from user preferences.
  m_persistentBreakpoints = gridlock::core::ConfigManager::instance().getBreakpoints();

  // Restore saved desktop geometry and dock configuration when running with an interactive display server.
  if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
    QSettings settings("gridlock", "debugger");
    if (settings.contains("MainWindow/Geometry_v9")) restoreGeometry(settings.value("MainWindow/Geometry_v9").toByteArray());
    if (settings.contains("MainWindow/State_v9")) restoreState(settings.value("MainWindow/State_v9").toByteArray());
    // Restore individual splitter sash positions.
    for (auto* splitter : findChildren<QSplitter*>()) {
      if (!splitter->objectName().isEmpty()) {
        auto st = settings.value("MainWindow/Splitter_v9_" + splitter->objectName()).toByteArray();
        if (!st.isEmpty()) splitter->restoreState(st);
      }
    }
  }
  if (width() < 800 || height() < 600) resize(1440, 900);
  // Constrain side dock widths to prevent collapsing below 150px.
  for (auto* dock : findChildren<QDockWidget*>()) {
    const auto area = dockWidgetArea(dock);
    if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
      dock->setMinimumWidth(150); dock->setMaximumWidth(QWIDGETSIZE_MAX);
      if (dock->widget()) { dock->widget()->setMinimumWidth(150); dock->widget()->setMaximumWidth(QWIDGETSIZE_MAX); }
    }
  }
  // Hide empty tab bars, disable scroll buttons, and prevent horizontal tab expansion.
  for (auto* tabBar : findChildren<QTabBar*>()) {
    if (tabBar->count() == 0) tabBar->hide();
    tabBar->setUsesScrollButtons(false); tabBar->setElideMode(Qt::ElideNone); tabBar->setExpanding(false);
  }

  // Route window to preferred monitor if specified in configuration.
  auto& cfg = gridlock::core::ConfigManager::instance();
  applyPreferredScreenRouting(cfg.preferredMonitor(), cfg.openUnfocused());
  // Instantiate live debugger coordinator (`gdb` or `lldb-dap`) before seeding preview UI rows.
  syncCoordinatorWithConfig();
  bindLiveMemViewHandlers();
  loadPreviewFixture();
  m_previewFixtureOnly = false;
  // Listen for global theme mode change events from ThemeManager singleton.
  connect(&gridlock::core::ThemeManager::instance(), &gridlock::core::ThemeManager::themeModeChanged,
          this, [this]() { propagateThemeToAllWidgets(); });
  // Restore persisted theme choice and synchronize toolbar CTA, ribbons, and dock headers.
  gridlock::core::ThemeManager::instance().loadPersistedTheme();
  propagateThemeToAllWidgets();
  // Balance right-sidebar zones: 380px wide, 55% top tabbed inspector, 45% bottom Server Rack.
  if (auto* varsDock = findChild<QDockWidget*>(QStringLiteral("VariablesDock"))) {
    resizeDocks({varsDock}, {380}, Qt::Horizontal);
    if (auto* rackDock = findChild<QDockWidget*>(QStringLiteral("ServerRackDock"))) resizeDocks({varsDock, rackDock}, {520, 380}, Qt::Vertical);
  }
  // Populate initial status bar Stack/Heap/RAM/CPU telemetry and start 1500ms refresh timer.
  refreshStatusBarMemoryTelemetry();
  auto* telemetryTimer = new QTimer(this);
  connect(telemetryTimer, &QTimer::timeout, this, &MainWindow::refreshStatusBarMemoryTelemetry);
  telemetryTimer->start(1500);
}

/**
 * @brief Generates synthetic telemetry fixture data for headless snapshots and testing.
 *
 * Populates 8 sample MPI ranks with stopped/running execution states and simulated
 * program counter registers across the .text segment.
 */
void MainWindow::loadPreviewFixture() {
  m_previewFixtureOnly = true;
  m_useRealBinaryTarget = false;
  // Pre-allocate storage capacity for eight initial process rank descriptors.
  QList<RankState> fixtureRanks; fixtureRanks.reserve(8);
  for (int i = 0; i < 8; ++i) {
    RankState st; st.rankId = i;
    // Rank 0 is stopped at coordinator breakpoint; worker ranks begin in running state.
    st.currentState = (i == 0) ? QStringLiteral("stopped") : QStringLiteral("running");
    // Assign realistic .text RIP offsets for coordinator (0x401120) and workers (0x401155/0x401157).
    st.registers[16] = (i == 0) ? QStringLiteral("0x00401120") : (i == 1 ? QStringLiteral("0x00401155") : QStringLiteral("0x00401157"));
    st.totalRuntimeMs = 12 + i * 3;
    fixtureRanks.append(st); m_latestStates[i] = st;
  }
  // Propagate fixture ranks to ServerRackView or fallback DisassemblyView PC badges.
  if (m_serverRackView) {
    m_serverRackView->updateRanks(fixtureRanks);
  } else if (m_disassemblyView) {
    QMap<int, uint64_t> pcs{{0, 0x00401120ULL}, {1, 0x00401155ULL}};
    for (int i = 2; i < 8; ++i) pcs.insert(i, 0x00401157ULL);
    m_disassemblyView->setRankAddresses(pcs);
  }
  // Seed stack-frame local variables into VariableTreeModel when empty so Variables tab displays 10+ rows.
  if (m_variablesDockWidget) {
    if (auto* varModel = m_variablesDockWidget->findChild<gridlock::VariableTreeModel*>()) {
      if (varModel->rowCount() == 0) {
        // Construct DAP-compatible variable descriptors for rank 0's active sym.main stack frame.
        auto mkVar = [](const char* n, const char* v, const char* t) {
          return QJsonObject{{QStringLiteral("name"), QLatin1String(n)}, {QStringLiteral("value"), QLatin1String(v)}, {QStringLiteral("type"), QLatin1String(t)}};
        };
        // Populate 11 representative MPI ring and struct fields for unclipped table inspection.
        varModel->onLocalsUpdated(0, 0, QJsonArray{
            mkVar("mpi_rank", "0", "int"), mkVar("world_size", "8", "int"), mkVar("argc", "1", "int"),
            mkVar("argv", "0x7fffffffe5a8", "char **"), mkVar("data[0].id", "0", "int"),
            mkVar("data[0].coords.x", "0.0f", "float"), mkVar("data[0].coords.y", "1.25f", "float"),
            mkVar("send_buf", "0x00404040", "double *"), mkVar("recv_buf", "0x00404140", "double *"),
            mkVar("tag", "42", "int"), mkVar("comm", "MPI_COMM_WORLD", "MPI_Comm")});
      }
    }
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
  if (m_deadlockAnalyzer) {
    delete m_deadlockAnalyzer;
    m_deadlockAnalyzer = nullptr;
  }
  m_coordinator = coord;

  if (auto *dapCoord = dynamic_cast<DapCoordinator *>(m_coordinator)) {
    connect(dapCoord, &DapCoordinator::stateChanged, this,
            [this, dapCoord](SessionState state) {
              if (state == SessionState::Disconnected) {
                m_debugSessionActive = false;
                updateDebugToolbarState();
                updateStatusTelemetry(QString(), QStringLiteral("IDLE"));
              } else if (state == SessionState::Launching) {
                updateStatusTelemetry(QString(), QStringLiteral("LAUNCHING"));
              } else if (state == SessionState::Queued) {
                updateStatusTelemetry(QString(), QStringLiteral("QUEUED"));
              } else if (state == SessionState::Running) {
                updateStatusTelemetry(QString(), QStringLiteral("RUNNING"));
                for (int i = 0; i < dapCoord->getProcessCount(); ++i) {
                  RankState rs = m_latestStates.contains(i) ? m_latestStates[i] : RankState{};
                  rs.rankId = i;
                  rs.currentState = "running";
                  rs.executionTimer.start();
                  onRankStateChanged(i, rs);
                }
              } else if (state == SessionState::Paused) {
                updateStatusTelemetry(QString(), QStringLiteral("PAUSED"));
              }
            });

    connect(dapCoord, &DapCoordinator::executionStopped, this,
            [this](int rankId, const QString& /*reason*/) {
              RankState rs = m_latestStates.contains(rankId) ? m_latestStates[rankId] : RankState{};
              rs.rankId = rankId;
              rs.currentState = "stopped";
              rs.totalRuntimeMs += rs.executionTimer.isValid() ? rs.executionTimer.elapsed() : 0;
              onRankStateChanged(rankId, rs);
            });
  }

  // Connect variable inspection dock and polymorphic live DeadlockAnalyzer across GDB and DAP.
  if (m_variablesDockWidget) m_variablesDockWidget->setCoordinator(m_coordinator);
  if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
    connect(gdbCoord, &gridlock::GdbRankCoordinator::rankStateChanged, this, &MainWindow::onRankStateChanged);
    connect(gdbCoord, &gridlock::GdbRankCoordinator::localsUpdated, this,
            [this](int rankId, const QJsonArray &locals) {
              if (rankId == m_focusedRank && m_variablesDockWidget) {
                if (auto *vm = m_variablesDockWidget->findChild<gridlock::VariableTreeModel *>()) {
                  vm->onLocalsUpdated(rankId, 0, locals);
                }
              }
            });
  }

  // Bind coordinator execution, call-stack, console REPL, watchpoints, and deadlock routes.
  if (m_coordinator) {
    bindCallStackCoordinatorRoutes();
    bindConsoleAndWatchpointRoutes();
    // Instantiate live DeadlockAnalyzer bound to m_coordinator's per-rank call stacks and state changes.
    m_deadlockAnalyzer = new gridlock::core::DeadlockAnalyzer(m_coordinator, this);
    if (m_mpiDiagnosticsWidget) {
      // Forward detected wait-for cycles and collective divergences directly to MPI Diagnostics panel.
      connect(m_deadlockAnalyzer, &gridlock::core::DeadlockAnalyzer::deadlockDetected,
              m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::onDeadlockDetected);
      connect(m_deadlockAnalyzer, &gridlock::core::DeadlockAnalyzer::rankCleared,
              m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::onRankCleared);
    }
    // Reflect detected wait-for cycles and collective divergences on ServerRackView and DisassemblyView.
    connect(m_deadlockAnalyzer, &gridlock::core::DeadlockAnalyzer::deadlockDetected, this,
            [this](const gridlock::core::DeadlockInfo &info) {
              // Mark rank card badge as DEADLOCK (`#f44747`) while preserving register/watch snapshots.
              RankState rs = m_latestStates.contains(info.rankId) ? m_latestStates[info.rankId] : RankState{};
              rs.rankId = info.rankId;
              rs.currentState = QStringLiteral("deadlock");
              rs.currentFunction = info.blockedFunction;
              m_latestStates[info.rankId] = rs;
              if (m_serverRackView) m_serverRackView->updateRankState(info.rankId, rs);
            });
    // Forward live register array updates for the currently focused MPI rank to RegisterView.
    connect(m_coordinator, &IBackendCoordinator::registersUpdated, this,
            [this](int rankId, const QJsonArray &regs) {
              if (rankId == m_focusedRank && m_registerView) m_registerView->updateRegisters(regs);
            });
    // Route execution location updates (file:line) to the focused rank's editor and gutter PC marker.
    connect(m_coordinator, &IBackendCoordinator::locationChanged, this,
            [this](int rankId, const QString &file, int line) {
              m_latestStates[rankId].currentFile = file;
              m_latestStates[rankId].currentLine = line;
              if (rankId == m_focusedRank && line > 0) {
                SourceCodeView *view = (!file.isEmpty() && QFileInfo::exists(file) && m_editorTabManager)
                                           ? m_editorTabManager->openFile(file)
                                           : getSourceCodeView();
                if (view) {
                  view->highlightCurrentLine(line);
                  view->highlightExecutionLine(line);
                }
              }
            });

    // Wire GDB-specific hover, evaluator, raw MI stream, and SIGFPE handlers when using GDB.
    if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
      // Display floating inline variable preview popup when editor hover evaluation finishes.
      connect(gdbCoord, &gridlock::GdbRankCoordinator::hoverEvaluationComplete,
              this, [this](const QString &varName, const QString &result, QPoint globalPos) {
                if (getSourceCodeView() && m_plainJaneTooltip) {
                  m_plainJaneTooltip->showTooltip(globalPos, QString("<b>%1</b>: %2").arg(varName).arg(result));
                }
              });
      // Stream interactive expression evaluations to ExpressionEvaluatorWidget history.
      if (m_expressionEvaluatorWidget) {
        connect(gdbCoord, &gridlock::GdbRankCoordinator::expressionEvaluated,
                m_expressionEvaluatorWidget, &ExpressionEvaluatorWidget::appendResult);
      }
      // Stream raw GDB/MI stdout and dispatched MI commands to GdbConsoleWidget log cache.
      if (m_gdbConsoleWidget) {
        connect(gdbCoord, &gridlock::GdbRankCoordinator::gdbOutputReceived, m_gdbConsoleWidget, &GdbConsoleWidget::appendGdbOutput);
        connect(gdbCoord, &gridlock::GdbRankCoordinator::commandSentToGdb, m_gdbConsoleWidget, &GdbConsoleWidget::appendGdbInput);
      }
      // Focus offending rank and log floating-point trap details on SIGFPE signal stop.
      connect(gdbCoord, &gridlock::GdbRankCoordinator::gdbOutputReceived, this,
              [this](int rankId, const QString &output) {
                if (output.contains("reason=\"signal-received\"") && output.contains("signal-name=\"SIGFPE\"")) {
                  onRankSelected(rankId);
                  if (networkLog()) networkLog()->appendError(QString("SIGFPE (Floating-Point Exception) detected on Rank %1!\n").arg(rankId));
                }
              });
      // Forward hexadecimal GDB memory dumps to MemView unless synchronous live fetch is active.
      connect(gdbCoord, &gridlock::GdbRankCoordinator::memoryDataReady, this,
              [this](int rankId, qint64 beginAddress, const QString &hexContents) {
                if (!m_suppressMemViewBroadcast && rankId == m_focusedRank && m_memView) m_memView->setMemoryData(beginAddress, hexContents);
              });
    }

    // Forward target inferior stdout/stderr streams into the bottom Network / Target Output log.
    if (networkLog()) {
      connect(m_coordinator, &IBackendCoordinator::targetOutputReceived, this,
              [this](const QString &category, const QString &output) {
                if (networkLog()) networkLog()->appendText(category, output);
              });
    }
    // Forward raw byte-array memory reads to MemView for the currently focused MPI rank.
    connect(m_coordinator, &IBackendCoordinator::memoryRead, this,
            [this](int rankId, const QString &address, const QByteArray &data) {
              if (!m_suppressMemViewBroadcast && rankId == m_focusedRank && m_memView) m_memView->setMemoryData(address, data);
            });
    // Forward JSON register values to RegisterView and keep DisassemblyView PC cursor synchronized.
    connect(m_coordinator, &IBackendCoordinator::registersUpdated, this,
            [this](int rankId, const QJsonArray &registers) {
              for (const QJsonValue &rv : registers) {
                const QJsonObject ro = rv.toObject();
                const QString rn = ro.value(QStringLiteral("name")).toString().toLower();
                const QString rval = ro.value(QStringLiteral("value")).toString();
                if (rn == QStringLiteral("rip") || rn == QStringLiteral("pc")) m_latestStates[rankId].registers[16] = rval;
                else if (rn == QStringLiteral("rsp") || rn == QStringLiteral("sp")) m_latestStates[rankId].registers[7] = rval;
              }
              if (rankId == m_focusedRank) {
                if (m_registerView) m_registerView->updateRegisters(registers);
                const QString pcToken = m_latestStates[rankId].registers.value(16).section(QLatin1Char(' '), 0, 0);
                bool ok = false;
                const uint64_t pcVal = pcToken.mid(pcToken.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 2 : 0).toULongLong(&ok, 16);
                if (ok && pcVal != 0ULL && m_disassemblyView) {
                  m_disassemblyView->setCurrentAddress(pcVal);
                  m_disassemblyView->setActiveSteppingRank(rankId, pcVal);
                }
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

      // Atomically switch workspace directory, FILES tree, and target discovery before saving wizard overrides.
      switchWorkspaceDirectory(projectRoot);

      auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
      ps.targetBinary = dialog.getTargetBinary().toStdString();
      ps.workingDirectory = dialog.getProjectRoot().toStdString();
      gridlock::core::ConfigManager::instance().saveProjectSettings(ps);

      // Configure default MPI launcher settings based on wizard selections.
      auto dbgSettings = gridlock::core::ConfigManager::instance().getDebuggerSettings();
      dbgSettings.mpiExecutable = "mpirun";
      dbgSettings.defaultRanks = dialog.getRanks();
      // Strip launcher prefix and target binary path to persist pure MPI flags.
      dbgSettings.mpiArgs = dialog.getMpiLaunchCommand()
                               .remove("mpirun ")
                               .remove("-np " + QString::number(dialog.getRanks()) + " ")
                               .remove(dialog.getTargetBinary())
                               .trimmed();
      gridlock::core::ConfigManager::instance().saveDebuggerSettings(dbgSettings);

      // Immediately launch debugging session if coordinator is available.
      if (m_coordinator) startDebuggingSession(dialog.getTargetBinary(), dialog.getRanks());
    }
  });

  // Open Workspace Action: Atomically switches FILES tree, TARGET selector, LSP, and persisted watches/breakpoints.
  QAction *openProjAction = fileMenu->addAction("Open Workspace...");
  connect(openProjAction, &QAction::triggered, this, [this]() {
    const QString startDir = !m_activeWorkspaceDir.isEmpty() ? m_activeWorkspaceDir : QDir::currentPath();
    const QString dir = QFileDialog::getExistingDirectory(this, "Open Workspace", startDir);
    if (!dir.isEmpty()) switchWorkspaceDirectory(dir);
  });

  // Open Source File Action: Direct file picker bypassing project hierarchy.
  QAction *openAction = fileMenu->addAction("Open Source File");
  connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

  // Save Source File Action: Saves active editor buffer directly to disk.
  QAction *saveAction = fileMenu->addAction("Save Source File");
  saveAction->setShortcut(QKeySequence::Save);
  connect(saveAction, &QAction::triggered, this, [this]() {
    // Write modified buffer contents to disk and clear the tab dirty indicator.
    if (auto* v = getSourceCodeView(); v && v->document()->isModified()) v->saveToFile();
  });

  // Save All Action (Ctrl+Shift+S): Saves all dirty open editor buffers and clears dirty tab indicators.
  QAction *saveAllAction = fileMenu->addAction("Save All");
  saveAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
  connect(saveAllAction, &QAction::triggered, this, [this]() {
    // Flush every modified split/tab buffer managed by EditorTabManager.
    if (m_editorTabManager) m_editorTabManager->saveAllModifiedTabs();
  });

  // Switch Header/Source Action (F10): Toggles between corresponding .cpp/.c and .hpp/.h files.
  QAction *switchHdrAct = fileMenu->addAction("Switch Header/Source");
  switchHdrAct->setShortcut(QKeySequence(Qt::Key_F10));
  connect(switchHdrAct, &QAction::triggered, this, [this]() {
    // Resolve companion declaration/implementation file in the active tab group.
    if (m_editorTabManager) m_editorTabManager->switchHeaderSource();
  });
  fileMenu->addSeparator();

  // Session Serialization Actions: Export and restore active workspace layout,
  // breakpoint configurations, variable watches, and rank allocations.
  QAction *saveSessionAction = fileMenu->addAction("Save Session As...");
  connect(saveSessionAction, &QAction::triggered, this, &MainWindow::saveSessionAs);
  QAction *loadSessionAction = fileMenu->addAction("Load Session...");
  connect(loadSessionAction, &QAction::triggered, this, &MainWindow::loadSession);

  // Recent Sessions Submenu: Dynamically populated with most-recently-used session files.
  QMenu *recentMenu = fileMenu->addMenu("Recent Sessions");
  connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu]() {
    // Rebuild MRU menu entries lazily each time the submenu opens.
    recentMenu->clear();
    QStringList mru = gridlock::core::managers::SessionManager::instance().getMruSessions();
    for (const QString &s : mru) {
      // Trigger asynchronous session state restoration when an MRU item is selected.
      connect(recentMenu->addAction(s), &QAction::triggered, this, [s]() {
        gridlock::core::managers::SessionManager::instance().loadSession(s);
      });
    }
    // Provide a disabled placeholder item when no prior session snapshots exist.
    if (mru.isEmpty()) recentMenu->addAction("No Recent Sessions")->setEnabled(false);
  });

  fileMenu->addSeparator();

  // Project Settings Dialog Trigger: Edits compiler flags, include paths, and runtime environments.
  QAction *projSetAction = fileMenu->addAction("Project Settings");
  connect(projSetAction, &QAction::triggered, this, [this]() {
    // Present modal per-project build/MPI configuration dialog.
    ProjectSettingsDialog dlg(this);
    dlg.exec();
  });

  // Attach to Process Action: Enumerates live `/proc` processes and attaches active coordinator.
  QAction *attachProcAction = fileMenu->addAction("Attach to Process...");
  connect(attachProcAction, &QAction::triggered, this, [this]() { attachToProcessWorkflow(); });

  // SLURM Job Submission Action: Interpolates active target into template and queues via HPC backend.
  QAction *slurmAction = fileMenu->addAction("Submit SLURM Job");
  connect(slurmAction, &QAction::triggered, this, [this]() {
    // Load SLURM batch script template and substitute the active source/target path.
    const auto slurm =
        gridlock::core::ConfigManager::instance().getSlurmSettings();
    QString script = slurm.scriptTemplate;
    script.replace("{FILE}", m_currentFile);
    // Focus the Spack/HPC console tab so job allocation output is immediately visible.
    if (m_spackManager) {
      m_spackManager->appendMessage("Submitting SLURM batch job...");
      m_bottomTabs->setCurrentWidget(m_spackManager);
    }
    // Dispatch rendered sbatch script to the configured HPC backend.
    m_hpcBackend->submitSlurmJob(script);
  });

  fileMenu->addSeparator();
  // Exit Action: Closes MainWindow and triggers graceful session teardown in closeEvent().
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
  // Go to Definition Action: Resolves symbol declaration via clangd LSP (F12 inside SourceCodeView).
  QAction *gotoDefAction = editMenu->addAction(QStringLiteral("Go to Definition"));
  connect(gotoDefAction, &QAction::triggered, this, &MainWindow::goToDefinitionAtCursor);
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
    gitProc.start("git", {"ls-remote", "--tags", "--sort=v:refname", "https://github.com/bnorthern42/GridLock.git"});
    gitProc.waitForFinished(3000);
    // Fall back to built-in compile-time version string if network check fails.
    QString latestTag = "v" + gridlock::core::Version::getString();
    if (gitProc.exitStatus() == QProcess::NormalExit && gitProc.exitCode() == 0) {
      // Parse output stream to extract highest lexicographical tag name.
      const QString output = QString::fromUtf8(gitProc.readAllStandardOutput()).trimmed();
      if (!output.isEmpty()) {
        const QString tag = output.split('\n').last().split('\t').last().replace("refs/tags/", "");
        if (!tag.isEmpty()) latestTag = tag;
      }
    }

    // Construct modal message box with rich HTML formatting and project logo.
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("About GridLock");
    msgBox.setIconPixmap(QPixmap(":/icon.png").scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setText(QString(
        "<b>GridLock IDE %1</b><br><br>A high-performance graphical debugger for MPI applications.<br><br>"
        "<a href=\"https://github.com/bnorthern42/GridLock\">GridLock on GitHub</a><br><br>"
        "<b>Acknowledgments & Attributions:</b><ul>"
        "<li><a href=\"https://www.gnu.org/software/ddd/\">GNU DDD</a>: For pioneering visual data display in debugging.</li>"
        "<li><a href=\"https://www.kdbg.org/\">KDbg</a>: For early inspiration on KDE/Qt-based GDB frontend wrappers.</li>"
        "<li><a href=\"https://www.gdbgui.com/\">gdbgui</a>: For demonstrating the power of browser-based debug visualization, which inspired our native DifferentialGrid.</li>"
        "<li><a href=\"https://zealdocs.org/\">Zeal</a>: For the open-source .docset standard and offline documentation workflows that power our Reference Manual.</li>"
        "</ul><br>Special thanks to the <b>Spack</b>, <b>SLURM</b>, and <b>OpenMPI</b> communities for the tools that power our HPC environments.").arg(latestTag));
    // Enable interactive hyperlinks in the about box so documentation and project links open in user browser.
    msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
    // Display dialog modally to ensure user acknowledges application provenance and acknowledgments.
    msgBox.exec();
  });
}

/**
 * @brief Appends live per-rank Stack/Heap and host RAM/CPU telemetry to the bottom status bar label.
 */
void MainWindow::refreshStatusBarMemoryTelemetry() {
  if (!m_statusTelemetryLabel) return;
  // Query live inferior OS PID for the focused rank when a debug session is active (`0` when idle).
  const qint64 rankPid = (m_debugSessionActive && m_coordinator)
                             ? m_coordinator->getPidForRank(m_focusedRank)
                             : 0;
  quint64 currentRsp = 0, stackTop = 0;
  if (rankPid > 0 && m_latestStates.count(m_focusedRank)) {
    const QString rspStr = m_latestStates[m_focusedRank].registers.value(7).section(QLatin1Char(' '), 0, 0);
    bool ok = false;
    currentRsp = rspStr.trimmed().mid(rspStr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 2 : 0).toULongLong(&ok, 16);
    if (ok && currentRsp > 0) {
      stackTop = gridlock::core::ElfBinaryInspector::queryStackTopAddress(rankPid);
      if (stackTop == 0) stackTop = (currentRsp & ~0xfffULL) + 0x1000ULL;
    }
  }
  // Sample `/proc/<rankPid>/smaps` (or host fallback when `rankPid == 0`) for Stack/Heap/RAM metrics.
  const auto mem = gridlock::core::ElfBinaryInspector::queryProcessMemory(rankPid, currentRsp, stackTop);
  const QString summary = gridlock::core::ElfBinaryInspector::formatStatusBarSummary(
      mem, gridlock::core::ConfigManager::instance().telemetryDisplayFormat());
  // Replace any trailing telemetry suffix in-place without disturbing the active PC/status prefix.
  QString base = m_statusTelemetryLabel->text();
  const int stackIdx = base.indexOf(QStringLiteral(" | STACK:"));
  if (stackIdx >= 0) base = base.left(stackIdx);
  m_statusTelemetryLabel->setText(base + QStringLiteral(" | ") + summary);
}

/**
 * @brief Handles application termination, cleans up sessions, and persists layout.
 * @param event Close event payload.
 */
void MainWindow::closeEvent(QCloseEvent *event) {
  const bool hadModified = m_editorTabManager && m_editorTabManager->hasModifiedTabs();
  if (m_editorTabManager && !m_editorTabManager->confirmCloseAllModifiedTabs()) {
    if (event) event->ignore();
    return;
  }
  const bool interactive = QGuiApplication::platformName() != QStringLiteral("offscreen");
  if (!hadModified && interactive && QSettings("gridlock", "debugger").value("behavior/confirm_quit", true).toBool()) {
    if (QMessageBox::question(this, QStringLiteral("Quit GridLock"), QStringLiteral("Exit GridLock?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) != QMessageBox::Yes) {
      if (event) event->ignore();
      return;
    }
  }
  // Gracefully terminate active debug sessions across all spawned ranks and DAP processes.
  if (m_coordinator) m_coordinator->terminateSession();
  // Shut down background clangd LSP worker before destroying editor buffer widgets.
  if (m_lspCoordinator) m_lspCoordinator->stop();

  // Persist user-configured cross-rank watch expressions into `.gridlock/workspace.toml`.
  if (m_differentialGrid) {
    auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
    ps.watchExpressions = m_differentialGrid->getWatchExpressions();
    gridlock::core::ConfigManager::instance().saveProjectSettings(ps);
  }

  // Save active color theme and desktop dock/splitter geometry when running interactively.
  gridlock::core::ThemeManager::instance().savePersistedTheme();
  if (interactive) {
    QSettings settings("gridlock", "debugger");
    settings.setValue("MainWindow/Geometry_v9", saveGeometry());
    settings.setValue("MainWindow/State_v9", saveState());
    // Record individual QSplitter ratios so side-by-side source/CFG splits restore cleanly.
    for (auto* splitter : findChildren<QSplitter*>()) {
      if (!splitter->objectName().isEmpty()) settings.setValue("MainWindow/Splitter_v9_" + splitter->objectName(), splitter->saveState());
    }
  }
  if (event) event->accept();
}

/**
 * @brief Global event filter routing keyboard shortcuts and dynamic debugger-engine updates.
 * @param watched Target QObject.
 * @param event Event payload.
 * @return True if event was handled, false otherwise.
 */
bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  // Hot-swap between GdbRankCoordinator and DapCoordinator when ConfigManager::setDebuggerEngine fires.
  if (watched == qApp && event->type() == QEvent::DynamicPropertyChange) {
    const auto *dp = static_cast<QDynamicPropertyChangeEvent *>(event);
    if (dp->propertyName() == QByteArrayLiteral("gridlock_debugger_engine")) syncCoordinatorWithConfig();
    else if (dp->propertyName() == QByteArrayLiteral("gridlock_editor_highlight_rev") && m_editorTabManager)
      m_editorTabManager->rehighlightAllTabs();
  }
  // Connect PreferencesDialog::preferencesChanged on demand whenever Preferences opens.
  if (event->type() == QEvent::Show) {
    if (auto *prefs = qobject_cast<PreferencesDialog *>(watched)) {
      connect(prefs, &PreferencesDialog::preferencesChanged, this, &MainWindow::syncCoordinatorWithConfig, Qt::UniqueConnection);
      if (m_editorTabManager) {
        connect(prefs, &PreferencesDialog::preferencesChanged, m_editorTabManager, &EditorTabManager::rehighlightAllTabs, Qt::UniqueConnection);
      }
    }
  }
  // Ensure status-bar Stack/Heap telemetry populates immediately on initial status bar paint.
  if (watched == m_statusTelemetryLabel && (event->type() == QEvent::Paint || event->type() == QEvent::Show)) {
    if (m_statusTelemetryLabel && !m_statusTelemetryLabel->text().contains(QStringLiteral("STACK:"))) refreshStatusBarMemoryTelemetry();
  }
  // Delegate custom dock title-bar drag, pop-out, and double-click events first.
  if (handleDockHeaderEvent(watched, event)) return true;

  // Keep tabified sibling QDockWidgets synchronized to the active dock's dimensions.
  if (event->type() == QEvent::Resize) {
    if (auto *dock = qobject_cast<QDockWidget *>(watched)) {
      if (dock->parentWidget() == this && !dock->isFloating() && dock->width() > 155 && dock->height() > 155) {
        for (QDockWidget *sib : tabifiedDockWidgets(dock)) {
          if (sib && !sib->isFloating() && sib->size() != dock->size()) sib->resize(dock->size());
        }
      }
    }
  }
  // Route global customizable IDE keybindings through ShortcutManager before default widget handling.
  if (event->type() == QEvent::KeyPress) {
    if (gridlock::core::ShortcutManager::instance().handleKeyPress(watched, static_cast<QKeyEvent *>(event))) return true;
  }
  return QMainWindow::eventFilter(watched, event);
}

}  // namespace gridlock::ui
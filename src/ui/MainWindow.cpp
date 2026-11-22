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
#include "dialogs/CommandPaletteDialog.hpp"
#include "dialogs/PreferencesDialog.hpp"
#include "dialogs/ProgrammerCalculatorDialog.hpp"
#include "dialogs/ProjectSettingsDialog.hpp"
#include "dialogs/ProjectWizard.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "utils/FileDialogHelper.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/MpiNetworkLogWidget.hpp"
#include "dialogs/ArrayPlotDialog.hpp"
#include "dialogs/PointerGraphDialog.hpp"

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
#include <QThreadPool>
#include <QTimer>
#include <QToolBar>
#include <algorithm>
#include "core/utils/ClangdLspClient.hpp"

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
            [this](const QString &filePath, int oneBasedLine, int) {
              m_currentFile = filePath;
              if (networkLog()) networkLog()->appendText(QStringLiteral("Navigated to definition: %1:%2\n").arg(filePath).arg(oneBasedLine));
            });
  }

  // Initialize global shortcuts and install top-level event filter.
  gridlock::core::ShortcutManager::instance().initialize(this);
  qApp->installEventFilter(this);

  // Eagerly load default source file so UI is populated before running.
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
      int idx = m_targetSelector->findText(entry.first);
      if (idx >= 0) {
        m_targetSelector->setItemData(idx, targetData);
      } else {
        const int openIdx = m_targetSelector->findData(QStringLiteral("__open_binary__"));
        idx = (openIdx >= 0) ? openIdx : m_targetSelector->count();
        m_targetSelector->insertItem(idx, entry.first, targetData);
      }
      m_targetSelector->setItemData(idx, QStringLiteral("MPI Showcase Target (%1)").arg(entry.second), Qt::ToolTipRole);
    }
  }
  for (QComboBox* cb : findChildren<QComboBox*>()) {
    for (int i = 0; i < cb->count(); ++i) {
      if (cb->itemData(i, Qt::ToolTipRole).toString().isEmpty()) cb->setItemData(i, cb->itemText(i), Qt::ToolTipRole);
    }
  }
  // Resolve initial demonstration source file across local workspace and AppImage paths.
  const QString initialSource = resolveTutorialTarget(QStringLiteral("tutorial/inspection_demo.cpp"));
  // Eagerly load resolved demo source file into editor canvas if present on disk.
  if (QFileInfo::exists(initialSource)) loadSourceFile(initialSource);
  bindSourceViewGraphSignals(this, sourceCodeView());

  // Load offline persistence TOML breakpoints and connect per-rank breakpoint metadata sync.
  m_persistentBreakpoints = gridlock::core::ConfigManager::instance().getBreakpoints();
  if (m_editorTabManager) connect(m_editorTabManager, &EditorTabManager::rankBreakpointChanged, this, &MainWindow::onRankBreakpointChanged);

  // Restore saved desktop geometry and dock configuration when running with an interactive display server.
  if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
    QSettings settings("gridlock", "debugger");
    if (settings.contains("MainWindow/Geometry_v9")) restoreGeometry(settings.value("MainWindow/Geometry_v9").toByteArray());
    if (settings.contains("MainWindow/State_v9")) restoreState(settings.value("MainWindow/State_v9").toByteArray());
    // Restore individual splitter sash positions.
    for (auto* splitter : findChildren<QSplitter*>())
      if (!splitter->objectName().isEmpty()) {
        auto st = settings.value("MainWindow/Splitter_v9_" + splitter->objectName()).toByteArray();
        if (!st.isEmpty()) splitter->restoreState(st);
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
  // Instantiate live debugger coordinator (`gdb` or `lldb-dap`) before finishing dock initialization.
  syncCoordinatorWithConfig();
  bindLiveMemViewHandlers();
  if (m_variablesDockWidget) connect(m_variablesDockWidget, &gridlock::VariablesDockWidget::sendToCalculatorRequested, this, &MainWindow::openProgrammerCalculatorWithText, Qt::UniqueConnection);
  if (m_registerView) connect(m_registerView, &RegisterView::sendToCalculatorRequested, this, &MainWindow::openProgrammerCalculatorWithText, Qt::UniqueConnection);
  if (m_memView) connect(m_memView, &MemView::sendToCalculatorRequested, this, &MainWindow::openProgrammerCalculatorWithText, Qt::UniqueConnection);
  if (m_disassemblyView) connect(m_disassemblyView, &DisassemblyView::sendToCalculatorRequested, this, &MainWindow::openProgrammerCalculatorWithText, Qt::UniqueConnection);
  if (m_editorTabManager) {
    for (SourceCodeView* sv : m_editorTabManager->openViews()) connect(sv, &SourceCodeView::sendToCalculatorRequested, this, &MainWindow::openProgrammerCalculatorWithText, Qt::UniqueConnection);
    connect(m_editorTabManager, &EditorTabManager::currentChanged, this, [this](int) {
      if (auto* sv = getSourceCodeView()) connect(sv, &SourceCodeView::sendToCalculatorRequested, this, &MainWindow::openProgrammerCalculatorWithText, Qt::UniqueConnection);
    });
  }
  m_previewFixtureOnly = false;
  if (m_serverRackView) m_serverRackView->setSessionIdle(m_rankCountSpin ? m_rankCountSpin->value() : 8);
  // Listen for global theme mode change events from ThemeManager singleton.
  connect(&gridlock::core::ThemeManager::instance(), &gridlock::core::ThemeManager::themeModeChanged,
          this, [this]() { if (!m_isShuttingDown && !m_isClosing) propagateThemeToAllWidgets(); });
  // Only load persisted theme if ThemeManager::initialize() has not already populated the global QSS.
  if (gridlock::core::ThemeManager::instance().currentStyleSheet().isEmpty()) {
    gridlock::core::ThemeManager::instance().loadPersistedTheme();
  }
  propagateThemeToAllWidgets();
  setWorkspacePerspective(QStringLiteral("Source"));
  syncExecutionScopeUi();
  if (m_variablesDockWidget) connect(m_variablesDockWidget, &gridlock::VariablesDockWidget::rankSelectionChanged, this, &MainWindow::setFocusedRank, Qt::UniqueConnection);
  if (m_memView) connect(m_memView, &MemView::rankSelected, this, &MainWindow::setFocusedRank, Qt::UniqueConnection);
  if (m_expressionEvaluatorWidget) {
    disconnect(m_expressionEvaluatorWidget, &ExpressionEvaluatorWidget::evaluateRequested, this, nullptr);
    connect(m_expressionEvaluatorWidget, &ExpressionEvaluatorWidget::evaluateRequested, this, &MainWindow::evaluateExpressionFromWidget);
  }
  if (m_pauseAllAction && menuBar()) {
    for (QAction *ma : menuBar()->actions()) {
      if (QMenu *dm = ma ? ma->menu() : nullptr; dm && dm->title().remove(QLatin1Char('&')) == QLatin1String("Debug") && !dm->actions().contains(m_pauseAllAction)) {
        const QList<QAction *> acts = dm->actions();
        const int pIdx = m_pauseAction ? acts.indexOf(m_pauseAction) : -1;
        dm->insertAction((pIdx >= 0 && pIdx + 1 < acts.size()) ? acts.at(pIdx + 1) : nullptr, m_pauseAllAction);
      }
    }
  }
  refreshStatusBarMemoryTelemetry();
  m_telemetryTimer = new QTimer(this);
  connect(m_telemetryTimer, &QTimer::timeout, this, &MainWindow::refreshStatusBarMemoryTelemetry);
  m_telemetryTimer->start(1500);
}

/**
 * @brief Generates synthetic telemetry fixture data for headless snapshots and testing.
 *
 * Populates 8 sample MPI ranks with stopped/running execution states and simulated
 * program counter registers across the .text segment.
 */
void MainWindow::loadPreviewFixture() {
  m_previewFixtureOnly = true; m_useRealBinaryTarget = false;
  if (m_workspacePerspective == QStringLiteral("Source")) setWorkspacePerspective(QStringLiteral("Code"));
  syncRankCountChrome(8);
  if (m_projectExplorerWidget) { m_projectExplorerWidget->seedDefaultSymbols(); m_projectExplorerWidget->seedDefaultStrings(); }
  if (m_disassemblyView) m_disassemblyView->loadSampleDisassembly();
  if (m_registerView) m_registerView->seedPreviewRegisters();
  if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->populateDefaultRankTelemetry();
  if (m_memView) { if (const QByteArray pm = synthesizeMemoryWindow(0x00401120ULL, 256); !pm.isEmpty()) m_memView->setMemoryData(QStringLiteral("0x00401120"), pm); }
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

/** @brief Dispatches a polymorphic debugger command object. */
void MainWindow::executeCommand(std::unique_ptr<gridlock::core::commands::IDebugCommand> cmd) {
  if (cmd) cmd->execute();
}

/** @brief Attaches a backend coordinator (GDB or DAP) and establishes signal routes. */
void MainWindow::setCoordinator(IBackendCoordinator *coord) {
  if (m_deadlockAnalyzer) { delete m_deadlockAnalyzer; m_deadlockAnalyzer = nullptr; }
  m_coordinator = coord;
  if (!m_coordinator) m_debugSessionActive = false;
  else if (m_coordinator->getProcessCount() > 0) m_debugSessionActive = true;
  if (!m_isShuttingDown && !m_isClosing) { updateDebugToolbarState(); syncExecutionScopeUi(); }
  const QPointer<MainWindow> self(this);

  if (auto *dapCoord = dynamic_cast<DapCoordinator *>(m_coordinator)) {
    const QPointer<DapCoordinator> dapGuard(dapCoord);
    connect(dapCoord, &DapCoordinator::stateChanged, this, [self, dapGuard](SessionState state) {
      if (!self || self->m_isShuttingDown || self->m_isClosing || !dapGuard) return;
      if (state == SessionState::Disconnected) {
        self->m_debugSessionActive = false; self->updateDebugToolbarState(); self->updateStatusTelemetry(QString(), QStringLiteral("IDLE"));
      } else if (state == SessionState::Launching) { self->updateStatusTelemetry(QString(), QStringLiteral("LAUNCHING"));
      } else if (state == SessionState::Queued) { self->updateStatusTelemetry(QString(), QStringLiteral("QUEUED"));
      } else if (state == SessionState::Running) {
        self->updateStatusTelemetry(QString(), QStringLiteral("RUNNING"));
        for (int i = 0; i < dapGuard->getProcessCount(); ++i) {
          RankState rs = self->m_latestStates.contains(i) ? self->m_latestStates[i] : RankState{};
          rs.rankId = i; rs.currentState = "running"; rs.executionTimer.start(); self->onRankStateChanged(i, rs);
        }
      } else if (state == SessionState::Paused) { self->updateStatusTelemetry(QString(), QStringLiteral("PAUSED")); }
    });
    connect(dapCoord, &DapCoordinator::executionStopped, this, [self](int rankId, const QString&) {
      if (!self || self->m_isShuttingDown || self->m_isClosing) return;
      RankState rs = self->m_latestStates.contains(rankId) ? self->m_latestStates[rankId] : RankState{};
      rs.rankId = rankId; rs.currentState = "stopped";
      rs.totalRuntimeMs += rs.executionTimer.isValid() ? rs.executionTimer.elapsed() : 0;
      self->onRankStateChanged(rankId, rs);
    });
  }

  // Connect variable inspection dock and polymorphic live DeadlockAnalyzer across GDB and DAP.
  if (m_variablesDockWidget) m_variablesDockWidget->setCoordinator(m_coordinator);
  if (m_expressionEvaluatorWidget && m_coordinator && m_coordinator->getProcessCount() > 0)
    m_expressionEvaluatorWidget->setAvailableRankCount(m_coordinator->getProcessCount());
  if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
    connect(gdbCoord, &gridlock::GdbRankCoordinator::rankStateChanged, this, [self](int rankId, const RankState &st) {
      if (self && !self->m_isShuttingDown && !self->m_isClosing) self->onRankStateChanged(rankId, st);
    });
    connect(gdbCoord, &gridlock::GdbRankCoordinator::localsUpdated, this, [self](int rankId, const QJsonArray &locals) {
      if (!self || self->m_isShuttingDown || self->m_isClosing) return;
      if (rankId == self->m_focusedRank && self->m_variablesDockWidget) self->m_variablesDockWidget->applyLocalsForRank(rankId, locals);
    });
  }

  // Bind coordinator execution, call-stack, console REPL, watchpoints, and deadlock routes.
  if (m_coordinator) {
    if (m_expressionEvaluatorWidget) connect(m_coordinator, &IBackendCoordinator::expressionEvaluated, m_expressionEvaluatorWidget, &ExpressionEvaluatorWidget::appendResult, Qt::UniqueConnection);
    connect(m_coordinator, &IBackendCoordinator::sessionErrorOccurred, this, [self](const QString &errMsg) {
      if (!self || self->m_isShuttingDown || self->m_isClosing) return;
      if (self->m_debugSessionActive) self->stopTargetWorkflow();
      self->appendProgramOutput(-1, QStringLiteral("[Session Error] %1").arg(errMsg), true); self->flushProgramOutputBatch();
      self->updateStatusTelemetry(QStringLiteral("0x00000000"), QStringLiteral("ATTACH / LAUNCH FAILED"));
    });
    bindCallStackCoordinatorRoutes();
    bindConsoleAndWatchpointRoutes();
    // Instantiate live DeadlockAnalyzer bound to m_coordinator's per-rank call stacks and state changes.
    m_deadlockAnalyzer = new gridlock::core::DeadlockAnalyzer(m_coordinator, this);
    if (m_mpiDiagnosticsWidget) {
      connect(m_deadlockAnalyzer, &gridlock::core::DeadlockAnalyzer::deadlockDetected, m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::onDeadlockDetected);
      connect(m_deadlockAnalyzer, &gridlock::core::DeadlockAnalyzer::rankCleared, m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::onRankCleared);
    }
    // Reflect detected wait-for cycles and collective divergences on ServerRackView and DisassemblyView.
    connect(m_deadlockAnalyzer, &gridlock::core::DeadlockAnalyzer::deadlockDetected, this, [self](const gridlock::core::DeadlockInfo &info) {
      if (!self || self->m_isShuttingDown || self->m_isClosing) return;
      RankState rs = self->m_latestStates.contains(info.rankId) ? self->m_latestStates[info.rankId] : RankState{};
      rs.rankId = info.rankId; rs.currentState = QStringLiteral("deadlock"); rs.currentFunction = info.blockedFunction;
      self->m_latestStates[info.rankId] = rs;
      if (self->m_serverRackView) self->m_serverRackView->updateRankState(info.rankId, rs);
    });
    // Route execution location updates (file:line) without hijacking user-focused secondary tabs.
    connect(m_coordinator, &IBackendCoordinator::locationChanged, this, [self](int rankId, const QString &file, int line) {
      if (!self || self->m_isShuttingDown || self->m_isClosing) return;
      const QString prevFile = self->m_latestStates.contains(rankId) ? self->m_latestStates[rankId].currentFile : QString();
      const int prevLine = self->m_latestStates.contains(rankId) ? self->m_latestStates[rankId].currentLine : -1;
      const bool sameLoc = (prevFile == file && prevLine == line && self->m_latestStates[rankId].currentState == QLatin1String("stopped"));
      self->m_latestStates[rankId].currentFile = file; self->m_latestStates[rankId].currentLine = line;
      if (rankId != self->m_focusedRank || line <= 0) return;
      if (self->m_suppressExecutionTabSwitch || sameLoc) {
        if (SourceCodeView *sv = (self->m_editorTabManager && !file.isEmpty()) ? self->m_editorTabManager->getSourceCodeViewForFile(file) : nullptr) sv->setExecutionLineOnly(line);
        return;
      }
      SourceCodeView *view = (!file.isEmpty() && QFileInfo::exists(file) && self->m_editorTabManager) ? self->m_editorTabManager->openFile(file) : self->getSourceCodeView();
      if (view) { self->syncTabBreakpointMetadata(view); view->highlightExecutionLine(line); }
    });

    // Wire GDB-specific hover, raw MI stream, and SIGFPE handlers when using GDB.
    if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
      connect(gdbCoord, &gridlock::GdbRankCoordinator::hoverEvaluationComplete, this, [self](const QString &varName, const QString &result, QPoint globalPos) {
        if (self && !self->m_isShuttingDown && !self->m_isClosing && self->getSourceCodeView() && self->m_plainJaneTooltip)
          self->m_plainJaneTooltip->showTooltip(globalPos, QString("<b>%1</b>: %2").arg(varName).arg(result));
      });
      if (m_gdbConsoleWidget) {
        connect(gdbCoord, &gridlock::GdbRankCoordinator::gdbOutputReceived, m_gdbConsoleWidget, &GdbConsoleWidget::appendGdbOutput, Qt::UniqueConnection);
        connect(gdbCoord, &gridlock::GdbRankCoordinator::commandSentToGdb, m_gdbConsoleWidget, &GdbConsoleWidget::appendGdbInput, Qt::UniqueConnection);
        connect(m_gdbConsoleWidget, &GdbConsoleWidget::completionRequested, gdbCoord, &gridlock::GdbRankCoordinator::requestConsoleCompletions, Qt::UniqueConnection);
        connect(gdbCoord, &IBackendCoordinator::consoleCompletionsReady, m_gdbConsoleWidget, &GdbConsoleWidget::onConsoleCompletionsReady, Qt::UniqueConnection);
      }
      connect(gdbCoord, &gridlock::GdbRankCoordinator::gdbOutputReceived, this, [self](int rankId, const QString &output) {
        if (!self || self->m_isShuttingDown || self->m_isClosing) return;
        if (output.contains("reason=\"signal-received\"") && output.contains("signal-name=\"SIGFPE\"")) {
          self->setFocusedRank(rankId);
          if (self->networkLog()) self->networkLog()->appendError(QString("SIGFPE (Floating-Point Exception) detected on Rank %1!\n").arg(rankId));
        }
      });
      connect(gdbCoord, &gridlock::GdbRankCoordinator::memoryDataReady, this, [self](int rankId, qint64 beginAddress, const QString &hexContents) {
        if (!self || self->m_isShuttingDown || self->m_isClosing) return;
        if (!self->m_suppressMemViewBroadcast && self->m_memView && rankId == self->m_memView->focusedRank()) {
          self->m_memView->setMemoryData(beginAddress, hexContents);
          self->m_memView->setSourceModeLabel(QStringLiteral("LIVE RANK %1 (PID %2)").arg(rankId).arg(self->m_coordinator ? self->m_coordinator->getPidForRank(rankId) : 0LL));
        }
      });
    }
    connect(m_coordinator, &IBackendCoordinator::programOutputReceived, this, [self](int rank, const QString &text, bool isErr) {
      if (self && !self->m_isShuttingDown && !self->m_isClosing) self->appendProgramOutput(rank, text, isErr);
    });
    if (networkLog()) {
      connect(m_coordinator, &IBackendCoordinator::targetOutputReceived, this, [self](const QString &category, const QString &output) {
        if (self && !self->m_isShuttingDown && !self->m_isClosing && self->networkLog()) self->networkLog()->appendText(category, output);
      });
    }
    connect(m_coordinator, &IBackendCoordinator::memoryRead, this, [self](int rankId, const QString &address, const QByteArray &data) {
      if (!self || self->m_isShuttingDown || self->m_isClosing) return;
      if (!self->m_suppressMemViewBroadcast && self->m_memView && rankId == self->m_memView->focusedRank()) {
        self->m_memView->setMemoryData(address, data);
        self->m_memView->setSourceModeLabel(QStringLiteral("LIVE RANK %1 (PID %2)").arg(rankId).arg(self->m_coordinator ? self->m_coordinator->getPidForRank(rankId) : 0LL));
      }
    });
    connect(m_coordinator, &IBackendCoordinator::registersUpdated, this, [self](int rankId, const QJsonArray &registers) {
      if (!self || self->m_isShuttingDown || self->m_isClosing) return;
      for (const QJsonValue &rv : registers) {
        const QJsonObject ro = rv.toObject();
        const QString rn = ro.value(QStringLiteral("name")).toString().toLower(), rval = ro.value(QStringLiteral("value")).toString();
        if (rn == QStringLiteral("rip") || rn == QStringLiteral("pc")) self->m_latestStates[rankId].registers[16] = rval;
        else if (rn == QStringLiteral("rsp") || rn == QStringLiteral("sp")) self->m_latestStates[rankId].registers[7] = rval;
      }
      if (rankId == self->m_focusedRank) {
        if (self->m_registerView) self->m_registerView->updateRegisters(registers);
        const QString pcToken = self->m_latestStates[rankId].registers.value(16).section(QLatin1Char(' '), 0, 0);
        bool ok = false;
        const uint64_t pcVal = pcToken.mid(pcToken.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 2 : 0).toULongLong(&ok, 16);
        if (ok && pcVal != 0ULL && self->m_disassemblyView) {
          self->m_disassemblyView->setCurrentAddress(pcVal); self->m_disassemblyView->setActiveSteppingRank(rankId, pcVal);
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
      if (projectRoot.isEmpty()) return;

      // Atomically switch workspace directory, FILES tree, and target discovery before saving wizard overrides.
      switchWorkspaceDirectory(projectRoot);
      auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
      ps.targetBinary = dialog.getTargetBinary().trimmed().toStdString();
      ps.workingDirectory = projectRoot.trimmed().toStdString();
      ps.targetArguments = dialog.getExecutableArguments().trimmed();
      ps.programArguments = ps.targetArguments.toStdString();
      ps.mpiRanks = dialog.getRanks();
      QString pureMpiArgs = dialog.getMpiLaunchCommand();
      pureMpiArgs.remove(QStringLiteral("mpirun ")).remove(QStringLiteral("-np %1").arg(dialog.getRanks())).remove(dialog.getTargetBinary());
      ps.mpiArgs = pureMpiArgs.simplified();
      gridlock::core::ConfigManager::instance().saveProjectSettings(ps);

      // Configure default MPI launcher settings based on wizard selections (without inferior execArgs).
      auto dbgSettings = gridlock::core::ConfigManager::instance().getDebuggerSettings();
      dbgSettings.mpiExecutable = "mpirun"; dbgSettings.defaultRanks = dialog.getRanks();
      dbgSettings.mpiArgs = ps.mpiArgs;
      gridlock::core::ConfigManager::instance().saveDebuggerSettings(dbgSettings);

      // Immediately launch debugging session through unified startTargetWorkflow() if coordinator is available.
      if (m_coordinator) startDebuggingSession(dialog.getTargetBinary(), dialog.getRanks());
    }
  });

  // Open Workspace Action: Atomically switches FILES tree, TARGET selector, LSP, and persisted watches/breakpoints.
  QAction *openProjAction = fileMenu->addAction("Open Workspace...");
  connect(openProjAction, &QAction::triggered, this, [this]() {
    const QString dir = FileDialogHelper::getExistingDirectory(this, QStringLiteral("Open Workspace"), m_activeWorkspaceDir);
    if (!dir.isEmpty()) switchWorkspaceDirectory(dir);
  });

  // Open Source File Action: Direct file picker bypassing project hierarchy.
  QAction *openAction = fileMenu->addAction("Open Source File");
  connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

  // Quick Open File Action (Ctrl+P): Launches the unified fuzzy Command Palette in file/all mode.
  QAction *quickOpenAction = fileMenu->addAction(QStringLiteral("Quick Open File..."));
  quickOpenAction->setObjectName(QStringLiteral("actionQuickOpen"));
  quickOpenAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
  connect(quickOpenAction, &QAction::triggered, this, [this]() { openCommandPalette(QString()); });

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

  // Switch Header/Source Action (F4): Toggles between corresponding .cpp/.c and .hpp/.h files.
  QAction *switchHdrAct = fileMenu->addAction("Switch Header/Source");
  switchHdrAct->setObjectName(QStringLiteral("file.switch_header_source"));
  switchHdrAct->setShortcut(QKeySequence(Qt::Key_F4));
  connect(switchHdrAct, &QAction::triggered, this, [this]() {
    // Resolve companion declaration/implementation file in the active tab group.
    if (m_editorTabManager) m_editorTabManager->switchHeaderSource();
  });
  fileMenu->addSeparator();

  // Session Serialization Actions: Save active workspace session or export/load TOML snapshots.
  QAction *saveNowAct = fileMenu->addAction("Save Session"); saveNowAct->setObjectName(QStringLiteral("actionSaveSession"));
  connect(saveNowAct, &QAction::triggered, this, [this]() { saveSession(true); });
  QAction *saveSessionAction = fileMenu->addAction("Save Session As..."); connect(saveSessionAction, &QAction::triggered, this, &MainWindow::saveSessionAs);
  QAction *loadSessionAction = fileMenu->addAction("Load Session..."); connect(loadSessionAction, &QAction::triggered, this, &MainWindow::loadSession);

  // Recent Sessions Submenu: Dynamically populated with persisted workspaces and TOML sessions.
  m_recentSessionsMenu = fileMenu->addMenu("Recent Sessions"); m_recentSessionsMenu->setObjectName(QStringLiteral("menuRecentSessions"));
  connect(m_recentSessionsMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentSessionsMenu);
  rebuildRecentSessionsMenu();
  fileMenu->addSeparator();
  // Project Settings Dialog Trigger: Edits compiler flags, include paths, and runtime environments.
  QAction *projSetAction = fileMenu->addAction("Project Settings");
  connect(projSetAction, &QAction::triggered, this, [this]() {
    // Present modal per-project target binary, CLI arguments, cwd, and MPI settings dialog.
    ProjectSettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) { syncCoordinatorWithConfig(); if (m_terminalDock) m_terminalDock->setWorkingDirectory(QString::fromStdString(gridlock::core::ConfigManager::instance().projectSettings().workingDirectory)); }
  });
  // Attach to Process Action: Enumerates live `/proc` processes and attaches active coordinator.
  QAction *attachProcAction = fileMenu->addAction("Attach to Process...");
  attachProcAction->setObjectName(QStringLiteral("actionAttachProcess"));
  attachProcAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P));
  connect(attachProcAction, &QAction::triggered, this, [this]() { attachToProcessWorkflow(); });
  // SLURM Job Submission Action: Interpolates active target into template and queues via HPC backend.
  QAction *slurmAction = fileMenu->addAction("Submit SLURM Job");
  connect(slurmAction, &QAction::triggered, this, [this]() {
    QString script = gridlock::core::ConfigManager::instance().getSlurmSettings().scriptTemplate; script.replace("{FILE}", m_currentFile);
    if (m_spackManager) { m_spackManager->appendMessage("Submitting SLURM batch job..."); m_bottomTabs->setCurrentWidget(m_spackManager); }
    m_hpcBackend->submitSlurmJob(script);
  });
  fileMenu->addSeparator();
  // Exit Action: Closes MainWindow and triggers graceful session teardown in closeEvent().
  fileMenu->addAction("Exit", this, &MainWindow::close);
  // Construct Edit menu with focus-aware editor operations and Preferences dialog trigger.
  QMenu *editMenu = menuBar->addMenu("&Edit");
  const QList<QPair<QString, QKeySequence>> editActions = {
      {QStringLiteral("Undo"), QKeySequence::Undo}, {QStringLiteral("Redo"), QKeySequence::Redo}, {QStringLiteral("Cut"), QKeySequence::Cut},
      {QStringLiteral("Copy"), QKeySequence::Copy}, {QStringLiteral("Paste"), QKeySequence::Paste}, {QStringLiteral("Select All"), QKeySequence::SelectAll},
      {QStringLiteral("Find in View..."), QKeySequence(Qt::CTRL | Qt::Key_F)}, {QStringLiteral("Go to Line..."), QKeySequence(Qt::CTRL | Qt::Key_L)},
      {QStringLiteral("Toggle Comment"), QKeySequence(Qt::CTRL | Qt::Key_Slash)}};
  // Register each action on the Edit menu and route its signal to triggerEditMenuAction().
  for (int i = 0; i < editActions.size(); ++i) {
    // Insert structural dividers between History, Clipboard, and Navigation groups.
    if (i == 2 || i == 6) editMenu->addSeparator();
    QAction *act = editMenu->addAction(editActions[i].first); act->setShortcut(editActions[i].second);
    if (editActions[i].first.startsWith(QStringLiteral("Find"))) act->setObjectName(QStringLiteral("actionFindInView"));
    const QString actLabel = editActions[i].first;
    connect(act, &QAction::triggered, this, [this, actLabel]() { triggerEditMenuAction(actLabel); });
  }
  // Go to Definition Action: Resolves symbol declaration via clangd LSP (F12 inside SourceCodeView).
  QAction *gotoDefAction = editMenu->addAction(QStringLiteral("Go to Definition"));
  connect(gotoDefAction, &QAction::triggered, this, &MainWindow::goToDefinitionAtCursor);
  editMenu->addSeparator();
  // Preferences Action: Launches the multi-page PreferencesDialog (Ctrl+,).
  QAction *prefAction = editMenu->addAction("Preferences");
  prefAction->setShortcut(QKeySequence::Preferences);
  connect(prefAction, &QAction::triggered, this, &MainWindow::openPreferences);
  QMenu *viewMenu = menuBar->addMenu("&View");
  QAction *cmdPaletteAction = viewMenu->addAction(QStringLiteral("Command Palette..."));
  cmdPaletteAction->setObjectName(QStringLiteral("actionCommandPalette"));
  cmdPaletteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
  connect(cmdPaletteAction, &QAction::triggered, this, [this]() { openCommandPalette(QStringLiteral(">")); });

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
  connect(runTestsAction, &QAction::triggered, this, &MainWindow::runTargetRequested);
  QAction *ptrGraphAction = toolsMenu->addAction("Pointer & Struct Graph Visualizer...");
  connect(ptrGraphAction, &QAction::triggered, this, [this]() { openPointerGraphVisualizer(); });
  QAction *progCalcAction = toolsMenu->addAction(QStringLiteral("Programmer's Calculator..."));
  progCalcAction->setObjectName(QStringLiteral("actionProgrammerCalculator"));
  progCalcAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
  connect(progCalcAction, &QAction::triggered, this, &MainWindow::openProgrammerCalculator);

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
  if (m_isShuttingDown || m_isClosing || !m_statusTelemetryLabel) return;
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
  int cutIdx = base.indexOf(QStringLiteral(" | STACK:"));
  const int idleIdx = base.indexOf(QStringLiteral(" | TARGET: IDLE"));
  if (idleIdx >= 0 && (cutIdx < 0 || idleIdx < cutIdx)) cutIdx = idleIdx;
  if (cutIdx >= 0) base = base.left(cutIdx);
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
  // Begin guarded teardown: set shutdown flags immediately, remove event filter, and stop all timers.
  m_isShuttingDown = true;
  m_isClosing = true;
  if (qApp) qApp->removeEventFilter(this);
  if (m_telemetryTimer) m_telemetryTimer->stop();
  for (auto* t : findChildren<QTimer*>()) if (t) t->stop();
  // Re-dock any floating MemView window so no top-level window outlives MainWindow.
  if (m_memView && m_memView->isPoppedOut()) m_memView->setPoppedOut(false);
  // Save active session and watch expressions before disconnecting coordinator/LSP signals.
  if (!m_activeWorkspaceDir.isEmpty()) {
    saveSession(false);
    if (m_differentialGrid) {
      auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
      ps.watchExpressions = m_differentialGrid->getWatchExpressions();
      gridlock::core::ConfigManager::instance().saveProjectSettings(ps);
    }
  }
  // Persist active theme and interactive dock/splitter geometry.
  gridlock::core::ThemeManager::instance().savePersistedTheme();
  if (interactive) {
    QSettings settings("gridlock", "debugger");
    settings.setValue("MainWindow/Geometry_v9", saveGeometry()); settings.setValue("MainWindow/State_v9", saveState());
    for (auto* splitter : findChildren<QSplitter*>()) if (!splitter->objectName().isEmpty()) settings.setValue("MainWindow/Splitter_v9_" + splitter->objectName(), splitter->saveState());
  }
  // Synchronously disconnect and reap active debug/MPI sessions and embedded terminal PTY before QMainWindow tears down docks/widgets.
  shutdownActiveSessionSync();
  if (m_terminalDock) m_terminalDock->shutdownTerminalSession();
  if (m_lspCoordinator) { QObject::disconnect(m_lspCoordinator, nullptr, this, nullptr); m_lspCoordinator->stop(); }
  if (m_editorTabManager) QObject::disconnect(m_editorTabManager, nullptr, this, nullptr);
  QObject::disconnect(&gridlock::core::ClangdLspClient::instance(), nullptr, this, nullptr);
  gridlock::core::ClangdLspClient::instance().shutdown();
  QThreadPool::globalInstance()->waitForDone(500);
  if (event) QMainWindow::closeEvent(event);
}

/** @brief Synchronizes DisassemblyView & decompiler visibility across Source, Debug, Binary, and MPI perspectives. */
void MainWindow::syncDisassemblyDecompilerVisibility(bool applyDefaultMode) {
  if (!m_disassemblyView) return;
  const bool isBin = (m_workspacePerspective.compare(QStringLiteral("Binary"), Qt::CaseInsensitive) == 0 ||
                      m_workspacePerspective.compare(QStringLiteral("Reverse"), Qt::CaseInsensitive) == 0);
  const bool sourceVisible = m_editorTabManager && !m_editorTabManager->isHidden() && !isBin;
  m_disassemblyView->setSourceEditorActive(sourceVisible);
  if (!sourceVisible) { m_disassemblyView->setVisible(true); if (!m_disassemblyView->isDecompilerVisible()) m_disassemblyView->setSplitGraphDecompilerMode(true); return; }
  const auto& cfg = gridlock::core::ConfigManager::instance();
  if (m_workspacePerspective == QStringLiteral("Debug") && cfg.showDecompilerWithSource()) m_disassemblyView->setVisible(true);
  if (!cfg.showDecompilerWithSource()) { m_showingDecompilerFallback = false; m_forceDecompilerFallback = false; m_disassemblyView->setDecompilerPaneVisible(false, applyDefaultMode); }
  else if (applyDefaultMode || !m_disassemblyView->isDecompilerVisible()) m_disassemblyView->setDecompilerPaneVisible(true, true);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  // Ignore global events once window teardown has started.
  if (m_isShuttingDown || m_isClosing) return QMainWindow::eventFilter(watched, event);
  // Hot-swap between GdbRankCoordinator and DapCoordinator when ConfigManager::setDebuggerEngine fires.
  if (watched == qApp && event->type() == QEvent::DynamicPropertyChange) {
    const auto *dp = static_cast<QDynamicPropertyChangeEvent *>(event);
    if (dp->propertyName() == QByteArrayLiteral("gridlock_debugger_engine")) syncCoordinatorWithConfig();
    else if (dp->propertyName() == QByteArrayLiteral("gridlock_disasm_prefs_rev")) syncDisassemblyDecompilerVisibility(true);
    else if (dp->propertyName() == QByteArrayLiteral("gridlock_editor_highlight_rev") && m_editorTabManager)
      m_editorTabManager->rehighlightAllTabs();
  }
  // Connect PreferencesDialog, dialog completers, SourceCodeView graph signals, and QFileDialogs on Show/Polish.
  if (event->type() == QEvent::Show || event->type() == QEvent::Polish) {
    if (auto *sv = qobject_cast<SourceCodeView *>(watched)) bindSourceViewGraphSignals(this, sv);
    if (auto *fd = qobject_cast<QFileDialog *>(watched))
      FileDialogHelper::configureFileDialog(fd, !m_activeWorkspaceDir.isEmpty() ? m_activeWorkspaceDir : (m_projectExplorerWidget ? m_projectExplorerWidget->rootPath() : QString()));
    if (auto *psPre = qobject_cast<ProjectSettingsDialog *>(watched)) {
      if (m_terminalDock) m_terminalDock->ensureShellStarted();
      if (!psPre->property("_glWd").toBool()) { psPre->setProperty("_glWd", true); connect(psPre, &QDialog::accepted, this, [this]() { if (m_terminalDock) m_terminalDock->setWorkingDirectory(QString::fromStdString(gridlock::core::ConfigManager::instance().projectSettings().workingDirectory)); if (!m_activeWorkspaceDir.isEmpty()) saveSession(false); }); }
    }
    if (event->type() == QEvent::Show) {
      if (m_pauseAllAction && menuBar()) {
        for (QAction *ma : menuBar()->actions()) {
          if (QMenu *dm = ma ? ma->menu() : nullptr; dm && dm->title().remove(QLatin1Char('&')) == QLatin1String("Debug") && !dm->actions().contains(m_pauseAllAction)) {
            const QList<QAction *> acts = dm->actions();
            const int pIdx = m_pauseAction ? acts.indexOf(m_pauseAction) : -1;
            dm->insertAction((pIdx >= 0 && pIdx + 1 < acts.size()) ? acts.at(pIdx + 1) : nullptr, m_pauseAllAction);
          }
        }
      }
      if (qobject_cast<PointerGraphDialog *>(watched) || qobject_cast<ArrayPlotDialog *>(watched)) refreshDialogCompleters(this);
      if (auto *prefs = qobject_cast<PreferencesDialog *>(watched)) {
        gridlock::core::ShortcutManager::instance().syncMenuActionsFromMainWindow();
        if (prefs->keybindingsPage()) prefs->keybindingsPage()->refreshBindings();
        connect(prefs, &PreferencesDialog::preferencesChanged, this, &MainWindow::syncCoordinatorWithConfig, Qt::UniqueConnection);
        connect(prefs, &PreferencesDialog::preferencesChanged, this, qOverload<>(&MainWindow::syncDisassemblyDecompilerVisibility), Qt::UniqueConnection);
        if (m_editorTabManager) connect(prefs, &PreferencesDialog::preferencesChanged, m_editorTabManager, &EditorTabManager::rehighlightAllTabs, Qt::UniqueConnection);
      }
    }
  }
  // Ensure status-bar Host/Target telemetry populates immediately on initial status bar paint.
  if (watched == m_statusTelemetryLabel && (event->type() == QEvent::Paint || event->type() == QEvent::Show) && m_statusTelemetryLabel && !m_statusTelemetryLabel->text().contains(QStringLiteral("CPU:"))) refreshStatusBarMemoryTelemetry();
  // Delegate custom dock title-bar drag, pop-out, and double-click events first.
  if (handleDockHeaderEvent(watched, event)) return true;

  // Keep tabified sibling QDockWidgets synchronized to the active dock's dimensions.
  if (event->type() == QEvent::Resize) {
    if (auto *dock = qobject_cast<QDockWidget *>(watched); dock && dock->parentWidget() == this && !dock->isFloating() && dock->width() > 155 && dock->height() > 155) {
      for (QDockWidget *sib : tabifiedDockWidgets(dock)) if (sib && !sib->isFloating() && sib->size() != dock->size()) sib->resize(dock->size());
    }
  }
  // Route global customizable IDE keybindings through live QAction shortcuts and ShortcutManager.
  if (event->type() == QEvent::KeyPress) {
    auto *ke = static_cast<QKeyEvent *>(event);
    const auto mods = ke->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    const QKeySequence pressed(ke->key() | mods);
    auto matchesAct = [this, &pressed](const QString &objName, const QKeySequence &fallback) {
      if (pressed.isEmpty()) return false;
      if (auto *a = findChild<QAction *>(objName)) return !a->shortcut().isEmpty() && a->shortcut() == pressed;
      return !fallback.isEmpty() && fallback == pressed;
    };
    if (matchesAct(QStringLiteral("actionPauseAllRanks"), QKeySequence(Qt::Key_F6))) { pauseAllRanksWorkflow(); return true; }
    if (matchesAct(QStringLiteral("actionCommandPalette"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P))) { openCommandPalette(QStringLiteral(">")); return true; }
    if (matchesAct(QStringLiteral("actionAttachProcess"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P))) { attachToProcessWorkflow(); return true; }
    if (matchesAct(QStringLiteral("actionProgrammerCalculator"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C))) { openProgrammerCalculator(); return true; }
    if (matchesAct(QStringLiteral("actionSignalsAndCatchpoints"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S))) { openSignalsAndCatchpoints(); return true; }
    if (matchesAct(QStringLiteral("actionQuickOpen"), QKeySequence(Qt::CTRL | Qt::Key_P)) && (!m_commandPaletteDialog || !m_commandPaletteDialog->isVisible())) { openCommandPalette(QString()); return true; }
    if (watched && !watched->inherits("QKeySequenceEdit") && (!watched->parent() || !watched->parent()->inherits("QKeySequenceEdit")) && matchesAct(QStringLiteral("actionFindInView"), QKeySequence(Qt::CTRL | Qt::Key_F)) && triggerEditMenuAction(QStringLiteral("Find in View"))) return true;
    if (gridlock::core::ShortcutManager::instance().handleKeyPress(watched, ke)) return true;
  }
  return QMainWindow::eventFilter(watched, event);
}
/** @brief Opens the unified fuzzy Command Palette overlay pre-populated with @p initialPrefix. */
void MainWindow::openCommandPalette(const QString &initialPrefix) {
  if (!m_commandPaletteDialog) m_commandPaletteDialog = new CommandPaletteDialog(this);
  m_commandPaletteDialog->populateFromMainWindow(this); m_commandPaletteDialog->openWithPrefix(initialPrefix);
}
/** @brief Opens or raises the floating Programmer's Calculator tool window. */
void MainWindow::openProgrammerCalculator() {
  if (!m_programmerCalculatorDialog) m_programmerCalculatorDialog = new ProgrammerCalculatorDialog(this);
  m_programmerCalculatorDialog->reloadStyle();
  m_programmerCalculatorDialog->show(); m_programmerCalculatorDialog->raise(); m_programmerCalculatorDialog->activateWindow();
}
/** @brief Opens or raises the floating Programmer's Calculator pre-populated with @p text. */
void MainWindow::openProgrammerCalculatorWithText(const QString &text) {
  openProgrammerCalculator();
  if (m_programmerCalculatorDialog) m_programmerCalculatorDialog->setInitialTextOrValue(text);
}
/** @brief Records @p path in SessionManager MRU history and refreshes File -> Recent Sessions. */
void MainWindow::recordRecentWorkspace(const QString &path) {
  // Persist normalized path into debugger.conf MRU before rebuilding submenu items.
  if (!path.isEmpty()) gridlock::core::managers::SessionManager::instance().addRecentSession(path);
  rebuildRecentSessionsMenu();
}
/** @brief Dynamically populates #menuRecentSessions from persisted MRU entries in debugger.conf. */
void MainWindow::rebuildRecentSessionsMenu() {
  if (!m_recentSessionsMenu) return;
  m_recentSessionsMenu->clear();
  // Query live normalized MRU workspaces/TOML paths (most recent first, capped at 10).
  const QStringList entries = gridlock::core::managers::SessionManager::instance().recentSessions();
  for (const QString &path : entries) {
    // Format human-readable workspace title without underscores and store canonical path in data & tooltip.
    const QFileInfo fi(path); QString label = fi.isDir() ? QDir(path).dirName() : fi.completeBaseName();
    label.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (label.trimmed().isEmpty()) label = QStringLiteral("Workspace Session");
    QAction *act = m_recentSessionsMenu->addAction(label); act->setData(path); act->setToolTip(path);
    connect(act, &QAction::triggered, this, [this, path]() {
      // Restore workspace TOML state if present; otherwise switch directly to the workspace directory.
      const QString toml = QFileInfo(path).isDir() ? QDir(path).filePath(QStringLiteral(".gridlock/workspace.toml")) : path;
      const auto opt = QFileInfo::exists(toml) ? gridlock::core::managers::SessionManager::instance().loadSession(toml) : std::nullopt;
      if (!opt.has_value()) { switchWorkspaceDirectory(path); return; }
      // Repopulate watched variables and persistent breakpoints from the loaded session snapshot.
      if (m_differentialGrid) { m_differentialGrid->clearWatches(); for (const auto &w : opt->watchedVariables) m_differentialGrid->addVariableColumn(w); }
      m_persistentBreakpoints.clear();
      for (const auto &bp : opt->activeBreakpoints) {
        const auto p = bp.split(':'); if (p.size() != 2) continue;
        m_persistentBreakpoints[p[0]].insert(p[1].toInt());
        if (m_editorTabManager) if (auto *v = m_editorTabManager->getSourceCodeViewForFile(p[0])) v->setBreakpoints(m_persistentBreakpoints[p[0]]);
      }
    });
  }
  // Show a disabled placeholder when empty; otherwise append a Clear Recent Sessions action.
  if (entries.isEmpty()) { m_recentSessionsMenu->addAction(QStringLiteral("(No Recent Sessions)"))->setEnabled(false); return; }
  m_recentSessionsMenu->addSeparator();
  connect(m_recentSessionsMenu->addAction(QStringLiteral("Clear Recent Sessions")), &QAction::triggered, this, [this]() { gridlock::core::managers::SessionManager::instance().clearRecentSessions(); rebuildRecentSessionsMenu(); });
}

}  // namespace gridlock::ui
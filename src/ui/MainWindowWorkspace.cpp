/**
 * @file MainWindowWorkspace.cpp
 * @brief Implements the four-zone docking architecture and workspace layout choreography.
 *
 * Constructs and arranges the left symbol/project explorer dock, central source and
 * disassembly/CFG splitters, right-hand CPU state inspection docks (Variables, Registers,
 * Watch Expressions, Server Rack), and bottom console/memory tabs.
 */

#include "MainWindow.hpp"
#include "../core/commands/DebugCommands.hpp"
#include "../core/hpc/DapCoordinator.hpp"
#include "../core/hpc/GdbRankCoordinator.hpp"
#include "../core/hpc/MemoryDiffer.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/SessionManager.hpp"
#include "../core/managers/SpackManager.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"
#include "widgets/ReferenceManualWidget.hpp"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QGuiApplication>
#include <QScreen>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextCursor>
#include <QToolBar>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWindow>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace gridlock::ui {

QWidget* createAddressSpaceNavBar(MainWindow* window, QWidget* parent);

/**
 * @brief Initializes main window geometry, Cutter-style dock layout, and toolbars.
 *
 * Configures the default 1440x900 desktop viewport, establishes dock nesting
 * rules across the four peripheral zones, and binds top-level action controls.
 */
void MainWindow::setupUi() {
  setWindowTitle("GridLock - MPI Graphical Debugger");
  setWindowIcon(QIcon());
  resize(1440, 900);
  applyCutterWorkspaceLayout();
  setupToolbar();
}

/**
 * @brief Configures modern reverse-engineering workstation docking rules.
 *
 * Enables nested docks, corner sharing favoring side panels over top/bottom,
 * document tabs for tabified docks, and north-oriented tab placement.
 */
void MainWindow::applyCutterWorkspaceLayout() {
  setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
  setDockNestingEnabled(true);
  setDocumentMode(true);
  setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
  // Favor vertical side docks over horizontal top/bottom docks at corners.
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
}

/**
 * @brief Constructs status bar displaying real-time MPI rank, PC, and execution state.
 */
void MainWindow::setupStatusBar() {
  QStatusBar *bar = statusBar();
  bar->setObjectName("cutterStatusBar");
  bar->setSizeGripEnabled(false);
  bar->setContentsMargins(6, 0, 6, 0);

  // Initialize instruction pointer to main entry point if not previously set.
  if (m_currentSeekOrPc == QStringLiteral("0x00000000") ||
      m_currentSeekOrPc.isEmpty()) {
    m_currentSeekOrPc = QStringLiteral("0x00401120");
  }

  // Construct permanent telemetry label spanning status bar width.
  m_statusTelemetryLabel = new QLabel(bar);
  m_statusTelemetryLabel->setObjectName("cutterStatusTelemetry");
  m_statusTelemetryLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
  bar->addPermanentWidget(m_statusTelemetryLabel, 1);
  updateStatusTelemetry(m_currentSeekOrPc, m_currentTargetState);
}

/**
 * @brief Updates the status bar telemetry banner with active rank, PC, and status.
 * @param pcOrSeek Hexadecimal address string or symbol.
 * @param targetState Execution state string (e.g. "RUNNING", "PAUSED", "IDLE").
 */
void MainWindow::updateStatusTelemetry(const QString &pcOrSeek,
                                       const QString &targetState) {
  // Update internal address cache if parameter is non-empty.
  if (!pcOrSeek.isEmpty()) {
    m_currentSeekOrPc = pcOrSeek;
  }
  // Update operational state cache if parameter is non-empty.
  if (!targetState.isEmpty()) {
    m_currentTargetState = targetState;
  }
  // Format standardized breadcrumb displaying architecture, rank ID, address, and state.
  if (m_statusTelemetryLabel) {
    m_statusTelemetryLabel->setText(
        QStringLiteral("ARCH: x86_64 | MPI RANK: %1 | PC/SEEK: %2 | STATUS: %3")
            .arg(m_focusedRank)
            .arg(m_currentSeekOrPc)
            .arg(m_currentTargetState));
  }
}

/**
 * @brief Constructs primary debugger control toolbar and secondary address space ribbon.
 *
 * Configures actions for Run, Continue, Step, Pause, Stop, and Terminate, along
 * with history back/forward buttons, global seek bar, and address space visualizer.
 */
void MainWindow::setupToolbar() {
  // Construct main debugger control toolbar.
  QToolBar *toolbar = addToolBar("Main Toolbar");
  toolbar->setObjectName("MainToolbar");
  toolbar->setMovable(false);
  toolbar->setFixedHeight(28);
  toolbar->setContentsMargins(4, 1, 4, 1);
  toolbar->setStyleSheet(
      "QToolBar#MainToolbar { background: #1e2228; border-bottom: 1px solid #2c313a; "
      "spacing: 4px; padding: 1px 4px; height: 26px; min-height: 26px; max-height: 28px; }"
      "QToolBar#MainToolbar QToolButton { background: #252932; color: #d7dae0; "
      "border: 1px solid #323844; border-radius: 2px; padding: 1px 8px; margin: 0px; "
      "height: 20px; min-height: 20px; max-height: 22px; font-size: 11px; font-weight: 600; }"
      "QToolBar#MainToolbar QToolButton:hover { background: #2c323d; border: 1px solid #3e4452; color: #ffffff; }"
      "QToolBar#MainToolbar QToolButton:pressed { background: #2c3444; border: 1px solid #528bff; }");

  // Run Action: Starts target execution across all configured MPI ranks.
  m_runAction = new QAction("▶ Run Target", this);
  connect(m_runAction, &QAction::triggered, this, [this]() {
    // Resolve target binary path from active coordinator or fallback project settings.
    auto settings =
        gridlock::core::ConfigManager::instance().loadProjectSettings();
    QString binary =
        m_coordinator && !m_coordinator->getCurrentBinaryPath().isEmpty()
            ? m_coordinator->getCurrentBinaryPath()
            : QString::fromStdString(settings.targetBinary);
    // Instantiate polymorphic launch command targeting default MPI rank allocation.
    auto cmd = std::make_unique<gridlock::core::commands::LaunchCommand>(
        this, binary,
        gridlock::core::ConfigManager::instance().getDefaultRanks());
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(m_runAction);

  // Continue Action: Resumes suspended execution on active rank or process group.
  QAction *continueAction = new QAction("⏩ Continue", this);
  connect(continueAction, &QAction::triggered, this, [this]() {
    // GDB coordinator commands use 1-based rank IDs internally (focusedRank + 1).
    auto cmd = std::make_unique<gridlock::core::commands::ContinueCommand>(
        m_coordinator, m_focusedRank + 1);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(continueAction);

  // Step Instruction Action: Executes single machine instruction on active rank.
  QAction *stepAction = new QAction("↷ Step Inst", this);
  connect(stepAction, &QAction::triggered, this, [this]() {
    // Dispatch single instruction step without stepping over subroutines.
    auto cmd = std::make_unique<gridlock::core::commands::StepCommand>(
        m_coordinator, m_focusedRank + 1, false);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(stepAction);

  // Pause Action: Interrupts running execution on active rank.
  QAction *pauseAction = new QAction("Pause Rank", this);
  connect(pauseAction, &QAction::triggered, this, [this]() {
    // Deliver asynchronous interrupt signal (SIGINT) to pause target rank.
    auto cmd = std::make_unique<gridlock::core::commands::PauseCommand>(
        m_coordinator, m_focusedRank + 1);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(pauseAction);

  // Stop Action: Sends termination request to active coordinator.
  QAction *stopAction = new QAction("⏹ Stop", this);
  connect(stopAction, &QAction::triggered, this, [this]() {
    // Instruct coordinator to terminate all spawned worker processes.
    if (m_coordinator) {
      m_coordinator->terminateSession();
    }
  });
  toolbar->addAction(stopAction);

  // Terminate Action: Closes debugging session and clears active state.
  QAction *exitAction = new QAction("Terminate Session", this);
  connect(exitAction, &QAction::triggered, this, [this]() {
    // Execute top-level termination command to reset GUI and release resources.
    auto cmd =
        std::make_unique<gridlock::core::commands::TerminateCommand>(this);
    executeCommand(std::move(cmd));
  });
  toolbar->addAction(exitAction);

  // Strip tooltips to maintain minimalist visual chrome.
  for (QAction *action : toolbar->actions()) {
    action->setToolTip(QString());
  }

  // Expanding spacer separating debugger actions from navigation controls.
  QWidget *spacer = new QWidget(toolbar);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  toolbar->addWidget(spacer);

  // Navigation button stylesheet matching dark workstation theme.
  const QString navBtnStyle = QStringLiteral(
      "QPushButton, QPushButton:disabled { background: #252932; color: #d7dae0; border: 1px solid #323844; "
      "border-radius: 2px; padding: 0px; margin: 0px; font-family: 'JetBrains Mono', monospace; "
      "font-size: 11px; font-weight: 700; width: 20px; min-width: 20px; max-width: 22px; "
      "height: 20px; min-height: 20px; max-height: 22px; }"
      "QPushButton:hover:enabled { background: #2c323d; border-color: #528bff; color: #ffffff; }"
      "QPushButton:disabled { background: #181a1f; color: #5c6370; border: 1px solid #242830; }");

  // History back navigation button (<).
  m_navBackBtn = new QPushButton(QStringLiteral("<"), toolbar);
  m_navBackBtn->setObjectName("navBackButton");
  m_navBackBtn->setFixedSize(22, 22);
  m_navBackBtn->setStyleSheet(navBtnStyle);
  m_navBackBtn->setEnabled(false);
  // Connect back button click to address stack pop operation.
  connect(m_navBackBtn, &QPushButton::clicked, this, &MainWindow::navigateBack);
  toolbar->addWidget(m_navBackBtn);

  // History forward navigation button (>).
  m_navForwardBtn = new QPushButton(QStringLiteral(">"), toolbar);
  m_navForwardBtn->setObjectName("navForwardButton");
  m_navForwardBtn->setFixedSize(22, 22);
  m_navForwardBtn->setStyleSheet(navBtnStyle);
  m_navForwardBtn->setEnabled(false);
  // Connect forward button click to address stack forward traversal.
  connect(m_navForwardBtn, &QPushButton::clicked, this,
          &MainWindow::navigateForward);
  toolbar->addWidget(m_navForwardBtn);

  // Global Alt+Left and Alt+Right keyboard shortcuts for navigation history.
  QShortcut *navBackShortcut =
      new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
  connect(navBackShortcut, &QShortcut::activated, this,
          &MainWindow::navigateBack);
  QShortcut *navFwdShortcut =
      new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
  connect(navFwdShortcut, &QShortcut::activated, this,
          &MainWindow::navigateForward);

  // Universal seek bar allowing direct hexadecimal address or symbol navigation.
  m_seekBar = new QLineEdit(toolbar);
  m_seekBar->setObjectName("cutterSeekBar");
  m_seekBar->setPlaceholderText("Go to address / symbol (Ctrl+G)...");
  m_seekBar->setFixedHeight(22);
  m_seekBar->setMinimumWidth(300);
  m_seekBar->setFixedWidth(320);
  m_seekBar->setMaximumWidth(320);
  // Apply monospace JetBrains Mono font for legible hex address formatting.
  QFont seekFont("JetBrains Mono");
  seekFont.setStyleHint(QFont::Monospace);
  seekFont.setPointSize(9);
  m_seekBar->setFont(seekFont);
  toolbar->addWidget(m_seekBar);

  // Synchronize inspection views whenever user presses Return in seek bar.
  connect(m_seekBar, &QLineEdit::returnPressed, this, [this]() {
    seekToAddressOrExpression(m_seekBar->text());
  });

  // Ctrl+G shortcut to focus and select all text in the seek bar for rapid jumping.
  QShortcut *seekShortcut =
      new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_G), this);
  connect(seekShortcut, &QShortcut::activated, this, [this]() {
    if (m_seekBar) {
      m_seekBar->setFocus(Qt::ShortcutFocusReason);
      m_seekBar->selectAll();
    }
  });

  // Secondary toolbar break for address space navigation ribbon.
  addToolBarBreak(Qt::TopToolBarArea);
  QToolBar *navToolbar = addToolBar("Address Space Navigation");
  navToolbar->setObjectName("AddressNavToolbar");
  navToolbar->setMovable(false);
  navToolbar->setFloatable(false);
  navToolbar->setFixedHeight(22);
  navToolbar->setContentsMargins(0, 0, 0, 0);

  // Instantiate interactive address space navigation bar ribbon.
  m_addressNavBar = createAddressSpaceNavBar(this, navToolbar);
  m_addressNavBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  navToolbar->addWidget(m_addressNavBar);
}

/**
 * @brief Constructs and docks all secondary workspace inspection panels and editors.
 *
 * Assembles the left-hand project/symbol explorer, central editor and disassembly
 * splitters, right-hand CPU/rank inspection tabs, and bottom console tabs.
 */
void MainWindow::setupDocks() {
  // Left Zone: Project Explorer / Symbols & Functions list
  m_projectExplorerWidget = new ProjectExplorerWidget(this);
  m_projectExplorerWidget->setObjectName("ProjectExplorerDock");
  addDockWidget(Qt::LeftDockWidgetArea, m_projectExplorerWidget);

  // Synchronize project explorer root directory when a saved session is restored.
  connect(&gridlock::core::managers::SessionManager::instance(),
          &gridlock::core::managers::SessionManager::sessionLoaded,
          m_projectExplorerWidget, &ProjectExplorerWidget::setRootPath);

  // Route file double-clicks from project explorer directly into editor tabs.
  connect(m_projectExplorerWidget, &ProjectExplorerWidget::fileDoubleClicked,
          this, [this](const QString &filePath) {
            if (m_editorTabManager) {
              loadSourceFile(filePath);
            }
          });
  // Route symbol clicks to synchronous multi-viewport address seek.
  connect(m_projectExplorerWidget,
          &ProjectExplorerWidget::symbolAddressSelected, this,
          [this](uint64_t address, const QString & /*symbolName*/) {
            seekToAddressOrExpression(
                QStringLiteral("0x%1").arg(address, 8, 16, QChar('0')));
          });

  // Center & Bottom Zone Splitter: Divides central code/disassembly from bottom tabs.
  QSplitter *mainVerticalSplitter = new QSplitter(Qt::Vertical, this);
  mainVerticalSplitter->setObjectName("MainVerticalSplitter");
  mainVerticalSplitter->setMinimumWidth(240);
  mainVerticalSplitter->setContentsMargins(0, 0, 0, 0);
  mainVerticalSplitter->setHandleWidth(4);
  // Prevent child widgets from being collapsed completely via splitter drag.
  mainVerticalSplitter->setChildrenCollapsible(false);

  // Center Zone: Source Code (Left) & Disassembly View (Right) side-by-side
  QSplitter *masterHorizontalSplitter =
      new QSplitter(Qt::Horizontal, mainVerticalSplitter);
  masterHorizontalSplitter->setObjectName("MasterHorizontalSplitter");
  masterHorizontalSplitter->setContentsMargins(0, 0, 0, 0);
  masterHorizontalSplitter->setHandleWidth(4);
  // Prevent source editor or disassembly panes from being zeroed out.
  masterHorizontalSplitter->setChildrenCollapsible(false);

  // Instantiate multi-tab source code editor manager.
  m_editorTabManager = new EditorTabManager(masterHorizontalSplitter);
  m_editorTabManager->setMinimumWidth(120);
  m_editorTabManager->setMaximumWidth(QWIDGETSIZE_MAX);
  m_editorTabManager->setSizePolicy(QSizePolicy::Expanding,
                                    QSizePolicy::Expanding);
  // Enable document mode for compact tab headers without excessive padding.
  m_editorTabManager->setDocumentMode(true);
  m_editorTabManager->setContentsMargins(0, 0, 0, 0);
  if (m_editorTabManager->tabBar()) {
    // Enable horizontal tab scroll buttons when many source files are open.
    m_editorTabManager->tabBar()->setUsesScrollButtons(true);
    m_editorTabManager->tabBar()->setElideMode(Qt::ElideNone);
  }
  // Connect gutter breakpoint toggles to active debugger backend.
  connect(m_editorTabManager, &EditorTabManager::toggleBreakpointRequested,
          this, [this](const QString &loc) {
            // Forward location string to GDB backend coordinator.
            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->insertBreakpoint(loc);
            }
          });
  connect(m_editorTabManager, &EditorTabManager::breakpointToggled, this,
          [this](const QString &file, int line, bool isSet,
                 const QString &condition) {
            // Normalize path to absolute filesystem coordinates.
            QString absoluteFilePath = QFileInfo(file).absoluteFilePath();

            // Update persistent cache (only tracks line numbers currently)
            if (!isSet) {
              m_persistentBreakpoints[absoluteFilePath].remove(line);
            } else {
              m_persistentBreakpoints[absoluteFilePath].insert(line);
            }
            gridlock::core::ConfigManager::instance().saveBreakpoints(
                m_persistentBreakpoints);

            // Broadcast breakpoint toggle to all active ranks or DAP session.
            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->broadcastBreakpoint(absoluteFilePath, line, isSet,
                                            condition);
            } else if (auto *dapCoord =
                           dynamic_cast<DapCoordinator *>(m_coordinator)) {
              if (isSet) {
                dapCoord->toggleBreakpoint(absoluteFilePath, line);
              } else {
                dapCoord->toggleBreakpoint(absoluteFilePath, line);
              }
            }
          });

  // Evaluate variable under cursor on hover and display value popover.
  connect(
      m_editorTabManager, &EditorTabManager::hoverVariableRequested, this,
      [this](const QString &varName, const QPoint &globalPos) {
        if (auto *gdbCoord =
                dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) {
          gdbCoord->evaluateHoverVariable(m_focusedRank, varName, globalPos);
        }
      });

  connect(m_editorTabManager, &EditorTabManager::hideHoverTooltip, this,
          &MainWindow::hideHoverTooltipPopup);

  // Right Inspection Zone: Variables, Registers, Watch Expressions, and Server Rack
  m_variablesDockWidget = new VariablesDockWidget(this);
  m_variablesDockWidget->setObjectName("VariablesWidget");
  QDockWidget *variablesDock = new QDockWidget("Variables", this);
  variablesDock->setObjectName("VariablesDock");
  variablesDock->setWidget(m_variablesDockWidget);
  QWidget *emptyVarTitle = new QWidget(variablesDock);
  emptyVarTitle->setFixedSize(0, 0);
  variablesDock->setTitleBarWidget(emptyVarTitle);

  // CPU Registers View: Renders hardware registers (GPRs, segment, flags) with change diffs.
  m_registerView = new RegisterView(this);
  m_registerView->setObjectName("RegisterViewWidget");
  connect(m_registerView, &RegisterView::addressActivated, this,
          [this](uint64_t addr) {
            seekToAddressOrExpression(
                QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')));
          });
  QDockWidget *registersDock = new QDockWidget("Registers", this);
  registersDock->setObjectName("RegisterViewDock");
  registersDock->setWidget(m_registerView);
  QWidget *emptyRegTitle = new QWidget(registersDock);
  emptyRegTitle->setFixedSize(0, 0);
  registersDock->setTitleBarWidget(emptyRegTitle);

  // Differential Grid: Cross-rank tabular matrix comparing watched variable values.
  m_differentialGrid = new DifferentialGrid(this);
  m_differentialGrid->setObjectName("DifferentialGrid");
  // Set compact 20px row heights for dense tabular display.
  m_differentialGrid->verticalHeader()->setDefaultSectionSize(20);
  // Default column width 96px accommodating standard variable names.
  m_differentialGrid->horizontalHeader()->setDefaultSectionSize(96);
  // Connect column addition signal to backend GDB coordinator watch registration.
  connect(m_differentialGrid, &DifferentialGrid::watchVariableAdded, this,
          [this](const QString &name) {
            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->registerWatchVariable(name);
            }
          });

  // Restore project settings watch expressions into differential grid columns.
  auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
  for (const auto &w : ps.watchExpressions) {
    if (!w.empty()) {
      // Append each persisted expression as a comparative matrix column.
      m_differentialGrid->addVariableColumn(QString::fromStdString(w));
    }
  }
  // Populate initial synthetic fixture watch entries for baseline inspection.
  m_differentialGrid->updateVariableDisplay(0, QStringLiteral("data[0].id"),
                                            QStringLiteral("0 (int)"));
  m_differentialGrid->updateVariableDisplay(
      0, QStringLiteral("data[0].coords.x"), QStringLiteral("0.0f (float)"));
  m_differentialGrid->updateVariableDisplay(0, QStringLiteral("mpi_rank"),
                                            QStringLiteral("0 (int)"));
  m_differentialGrid->updateVariableDisplay(1, QStringLiteral("data[0].id"),
                                            QStringLiteral("1 (int)"));
  m_differentialGrid->updateVariableDisplay(
      1, QStringLiteral("data[0].coords.x"), QStringLiteral("1.5f (float)"));
  m_differentialGrid->updateVariableDisplay(1, QStringLiteral("mpi_rank"),
                                            QStringLiteral("1 (int)"));
  // Hide row/column header indices to maximize data cell reading density.
  m_differentialGrid->setColumnHidden(0, true);
  m_differentialGrid->setRowHidden(0, true);
  // Configure column sizing to fit variable names with trailing column stretch.
  m_differentialGrid->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  m_differentialGrid->horizontalHeader()->setStretchLastSection(true);

  // Watch Expressions container with custom inline expression add bar.
  QWidget *watchContainer = new QWidget(this);
  QVBoxLayout *watchLayout = new QVBoxLayout(watchContainer);
  watchLayout->setContentsMargins(0, 0, 0, 0);
  watchLayout->setSpacing(0);

  // Compact header toolbar hosting watch input text field and submit button.
  QWidget *watchToolbar = new QWidget(watchContainer);
  watchToolbar->setObjectName("watchToolbar");
  QHBoxLayout *watchBarLayout = new QHBoxLayout(watchToolbar);
  watchBarLayout->setContentsMargins(3, 2, 3, 2);
  watchBarLayout->setSpacing(3);

  // Expression input box allowing arbitrary C/C++ expressions (e.g. data[i]->val).
  QLineEdit *watchInput = new QLineEdit(watchToolbar);
  watchInput->setObjectName("watchExpressionInput");
  watchInput->setPlaceholderText("New watch expression...");
  watchInput->setFixedHeight(22);

  // Submit button adding expression to active differential inspection grid.
  QPushButton *addWatchBtn = new QPushButton("+ Watch", watchToolbar);
  addWatchBtn->setObjectName("addWatchBtn");
  addWatchBtn->setFixedHeight(22);

  // Helper lambda submitting new watch variable from text input to grid.
  auto submitWatch = [this, watchInput]() {
    const QString expr = watchInput->text().trimmed();
    // Validate expression is non-empty before adding matrix column.
    if (!expr.isEmpty() && m_differentialGrid) {
      m_differentialGrid->addVariableColumn(expr);
      // Emit signal notifying backend coordinator to evaluate expression on stopped ranks.
      emit m_differentialGrid->watchVariableAdded(expr);
      watchInput->clear();
    }
  };
  connect(addWatchBtn, &QPushButton::clicked, this, submitWatch);
  connect(watchInput, &QLineEdit::returnPressed, this, submitWatch);

  watchBarLayout->addWidget(watchInput, 1);
  watchBarLayout->addWidget(addWatchBtn);
  watchLayout->addWidget(watchToolbar);
  watchLayout->addWidget(m_differentialGrid, 1);

  // Encapsulate watch container in a standard dockable QDockWidget.
  QDockWidget *watchDock = new QDockWidget("Watch Expressions", this);
  watchDock->setObjectName("WatchExpressionsDock");
  watchDock->setWidget(watchContainer);

  // Server Rack Overview: Visualizes cluster node/rank topology and operational status.
  m_serverRackView = new ServerRackView(this);
  m_serverRackView->setObjectName("ServerRackViewWidget");
  // Propagate updated rank addresses to disassembly view for divergence highlighting.
  connect(m_serverRackView, &ServerRackView::ranksUpdated, this,
          [this](const QMap<int, uint64_t> &rankPcMap) {
            if (m_disassemblyView) {
              m_disassemblyView->setRankAddresses(rankPcMap);
            }
          });
  // Focus workspace inspection views on whichever rank node is clicked in rack.
  connect(m_serverRackView, &ServerRackView::rankSelected, this,
          [this](int rankId, uint64_t pcAddr) {
            onRankSelected(rankId);
            if (m_disassemblyView) {
              m_disassemblyView->setSelectedRank(rankId);
            }
            // Navigate viewports to rank's current program counter if valid.
            if (pcAddr != 0ULL) {
              seekToAddressOrExpression(
                  QStringLiteral("0x%1").arg(pcAddr, 8, 16, QChar('0')));
            } else {
              updateStatusTelemetry();
            }
          });
  QDockWidget *serverRackDock = new QDockWidget("Server Rack", this);
  serverRackDock->setObjectName("ServerRackDock");
  serverRackDock->setWidget(m_serverRackView);

  // Dock right inspection widgets into RightDockWidgetArea with tabification.
  addDockWidget(Qt::RightDockWidgetArea, variablesDock);
  addDockWidget(Qt::RightDockWidgetArea, registersDock);
  tabifyDockWidget(variablesDock, registersDock);
  addDockWidget(Qt::RightDockWidgetArea, watchDock);
  addDockWidget(Qt::RightDockWidgetArea, serverRackDock);
  registersDock->raise();

  // Pin variable context menu trigger from editor tabs into watch expressions.
  connect(m_editorTabManager, &EditorTabManager::pinVariableRequested, this,
          [this, watchDock](const QString &varName) {
            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->registerWatchVariable(varName);
            }
            if (m_differentialGrid) {
              m_differentialGrid->addVariableColumn(varName);
            }
            if (watchDock) {
              watchDock->show();
              watchDock->raise();
            }
          });

  // Disassembly & CFG Graph View: Dual-mode linear assembly and basic-block graph renderer.
  m_disassemblyView = new DisassemblyView(masterHorizontalSplitter);
  m_disassemblyView->setMinimumWidth(120);
  m_disassemblyView->setMaximumWidth(QWIDGETSIZE_MAX);
  m_disassemblyView->setSizePolicy(QSizePolicy::Expanding,
                                   QSizePolicy::Expanding);
  m_disassemblyView->setContentsMargins(0, 0, 0, 0);
  connect(m_disassemblyView, &DisassemblyView::addressSelected, this,
          [this](uint64_t addr) {
            seekToAddressOrExpression(
                QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')));
          });

  masterHorizontalSplitter->addWidget(m_editorTabManager);
  masterHorizontalSplitter->addWidget(m_disassemblyView);

  // Allocate 45% width to source editor and 55% width to disassembly / CFG graph.
  masterHorizontalSplitter->setStretchFactor(0, 45);
  masterHorizontalSplitter->setStretchFactor(1, 55);
  masterHorizontalSplitter->setSizes({430, 520});

  mainVerticalSplitter->addWidget(masterHorizontalSplitter);

  // Bottom Zone: Reduced to 7 core I/O & memory inspection tabs
  m_bottomTabs = new QTabWidget(mainVerticalSplitter);
  m_bottomTabs->setObjectName("BottomConsoleTabs");
  m_bottomTabs->setMinimumWidth(200);
  m_bottomTabs->setDocumentMode(true);
  m_bottomTabs->setContentsMargins(0, 0, 0, 0);
  if (m_bottomTabs->tabBar()) {
    m_bottomTabs->tabBar()->setUsesScrollButtons(true);
    m_bottomTabs->tabBar()->setElideMode(Qt::ElideNone);
  }

  // Embedded Interactive PTY Terminal: Spawns bash or user shell for command execution.
  m_terminalDock = new TerminalDockWidget("Terminal", m_bottomTabs);
  m_terminalDock->setObjectName("TerminalDock");

  // Dynamic Expression Evaluator: Supports ad-hoc C/C++ expression evaluation via GDB.
  m_expressionEvaluatorWidget = new ExpressionEvaluatorWidget(m_bottomTabs);
  m_expressionEvaluatorWidget->setObjectName("ExpressionEvaluatorDock");
  QWidget *emptyEvalTitle = new QWidget(m_expressionEvaluatorWidget);
  emptyEvalTitle->setFixedSize(0, 0);
  m_expressionEvaluatorWidget->setTitleBarWidget(emptyEvalTitle);
  connect(m_expressionEvaluatorWidget,
          &ExpressionEvaluatorWidget::evaluateRequested, this,
          [this](const QString &expr) {
            if (m_coordinator) {
              m_coordinator->evaluateExpression(m_focusedRank, expr);
            }
          });

  // Offline Reference Manual: Zeal/Dash compatible offline API and MPI manual viewer.
  m_referenceManualWidget = new ReferenceManualWidget(m_bottomTabs);
  m_referenceManualWidget->setObjectName("ReferenceManualWidget");

  // Raw GDB Machine Interface Console: Displays low-level MI traffic and CLI input.
  m_gdbConsoleWidget = new GdbConsoleWidget(m_bottomTabs);
  m_gdbConsoleWidget->setObjectName("GdbConsoleWidget");

  // Hexadecimal Memory Inspector: Renders binary byte arrays and ASCII representations.
  m_memView = new MemView(m_bottomTabs);
  m_memView->setObjectName("MemViewDock");

  // Spack Environment & Package Manager: Visualizes active software stacks.
  m_spackManager = new SpackManager(m_hpcBackend, m_bottomTabs);
  m_spackManager->setObjectName("HpcConsoleDock");

  // Connect memory inspection view read requests to coordinator backend.
  connect(m_memView, &MemView::requestMemory, this,
          [this](const QString &address, int length) {
            if (m_coordinator) {
              m_coordinator->readMemory(m_focusedRank, address, length);
            }
          });

  // Asynchronous Cross-Rank Memory Differ: Compares byte blocks between two MPI rank processes.
  connect(
      m_memView, &MemView::requestMemoryDiff, this,
      [this](int baseRank, int targetRank, const QString &addrStr, int length) {
        if (!m_coordinator)
          return;
        pid_t basePid = m_coordinator->getPidForRank(baseRank);
        pid_t targetPid = m_coordinator->getPidForRank(targetRank);

        bool ok;
        quint64 addrInt = addrStr.toULongLong(&ok, 16);
        if (!ok)
          return;
        void *remoteAddr = reinterpret_cast<void *>(addrInt);

        // Run cross-process memory comparison in worker thread via QtConcurrent.
        QPointer<MainWindow> weakThis(this);
        (void)QtConcurrent::run([weakThis, basePid, targetPid, remoteAddr,
                                 length]() {
          auto result = gridlock::core::hpc::MemoryDiffer::compareMemory(
              basePid, targetPid, remoteAddr, length);
          // Dispatch comparison result back to GUI thread for visualization.
          QMetaObject::invokeMethod(weakThis, [weakThis, result, remoteAddr]() {
            if (weakThis && weakThis->m_memView) {
              weakThis->m_memView->displayMemoryDiff(result, remoteAddr);
            }
          });
        });
      });

  // MPI Diagnostics & Deadlock Visualizer: Displays wait-for graphs and lock cycles.
  m_mpiDiagnosticsWidget = new MpiDiagnosticsWidget(m_bottomTabs);
  connect(m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::jumpToFrameRequested,
          this, &MainWindow::onRankSelected);

  // Add all inspection and terminal widgets to bottom tabbed container.
  m_bottomTabs->addTab(m_terminalDock, "Terminal");
  m_bottomTabs->addTab(m_gdbConsoleWidget, "GDB Console");
  m_bottomTabs->addTab(m_memView, "Memory View");
  m_bottomTabs->addTab(m_mpiDiagnosticsWidget, "MPI Diagnostics");
  m_bottomTabs->addTab(m_spackManager, "HPC Console");
  m_bottomTabs->addTab(m_expressionEvaluatorWidget, "Evaluator");
  m_bottomTabs->addTab(m_referenceManualWidget, "Reference Manual");
  m_bottomTabs->setCurrentWidget(m_memView);

  // Mount bottom tabbed container into central vertical splitter.
  mainVerticalSplitter->addWidget(m_bottomTabs);
  m_bottomTabs->setMinimumHeight(180);

  // Allocate 68% height to code/disassembly and 32% to bottom console tabs.
  mainVerticalSplitter->setStretchFactor(0, 68);
  mainVerticalSplitter->setStretchFactor(1, 32);
  mainVerticalSplitter->setSizes({600, 260});

  // Designate vertical splitter as the central QMainWindow widget.
  // The central widget occupies remaining interior space between all four dock areas.
  setCentralWidget(mainVerticalSplitter);

  // Enforce consistent minimum width constraints across all peripheral docks.
  const QList<QDockWidget *> sideDocks = {
      m_projectExplorerWidget, variablesDock, registersDock, watchDock,
      serverRackDock};
  for (QDockWidget *dock : sideDocks) {
    if (!dock) {
      continue;
    }
    // Prevent side panels from collapsing below 150px during splitter adjustments.
    dock->setMinimumWidth(150);
    dock->setMaximumWidth(QWIDGETSIZE_MAX);
    dock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // Apply matching size limits to internal child widgets to prevent layout clipping.
    if (QWidget *inner = dock->widget()) {
      inner->setMinimumWidth(150);
      inner->setMaximumWidth(QWIDGETSIZE_MAX);
      inner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
  }

  // Apply default pixel distributions for side explorer and inspection docks.
  // 215px allocated to left explorer, 255px to right inspection zone.
  resizeDocks({m_projectExplorerWidget, registersDock}, {215, 255},
              Qt::Horizontal);
  // Stack right-hand docks vertically: 320px registers, 210px watches, 290px server rack.
  resizeDocks({registersDock, watchDock, serverRackDock}, {320, 210, 290},
              Qt::Vertical);

  // Locate or initialize View menu to register dock toggle actions.
  QMenu *viewMenu = nullptr;
  // Traverse menu bar actions searching for top-level View menu container.
  for (QAction *action : menuBar()->actions()) {
    if (action->menu() && action->text() == "&View") {
      viewMenu = action->menu();
      break;
    }
  }

  // Register toggle actions and configure window flags for all child docks.
  for (auto *dock : findChildren<QDockWidget *>()) {
    // Expose dynamic toggle checkboxes in View menu for showing/hiding each panel.
    if (viewMenu) {
      viewMenu->addAction(dock->toggleViewAction());
    }
    // Allow panels to be closed, undocked into floating windows, or tabified.
    dock->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    // Eliminate outer dock margins for seamless workstation borders.
    dock->setContentsMargins(0, 0, 0, 0);
    if (QWidget *inner = dock->widget()) {
      inner->setContentsMargins(0, 0, 0, 0);
      if (inner->layout()) {
        inner->layout()->setContentsMargins(0, 0, 0, 0);
        inner->layout()->setSpacing(0);
      }
    }

    // Ensure floating top-level docks restore standard OS window chrome and shadows.
    connect(dock, &QDockWidget::topLevelChanged, dock, [dock](bool topLevel) {
      if (topLevel) {
        // Strip custom frameless hints when detaching from main window.
        dock->setWindowFlags(dock->windowFlags() & ~Qt::FramelessWindowHint);
        dock->show();
      }
    });
  }

  // Purge legacy geometry keys to guarantee fresh default workstation layout proportions.
  // This clears stale v8 serialization entries that might distort default panel ratios.
  QSettings settings("gridlock", "debugger");
  settings.remove("MainWindow/Geometry_v8");
  settings.remove("MainWindow/State_v8");
  settings.remove("MainWindow/Splitter_v8_MasterHorizontalSplitter");
  settings.remove("MainWindow/Splitter_v8_MainVerticalSplitter");
}

}  // namespace gridlock::ui

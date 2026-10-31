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
#include "widgets/HpcConsoleWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"
#include "widgets/ReferenceManualWidget.hpp"

#include <QAction>
#include <QApplication>
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
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <algorithm>

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
  // Allow docking panes to nest horizontally/vertically and tabify when dragged together.
  setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
  setDockNestingEnabled(true);
  // Configure seamless document mode styling without redundant frame borders.
  setDocumentMode(true);
  // Position dock tab bars along the north edge for consistent Cutter ergonomics.
  setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
  // Favor vertical side docks over horizontal top/bottom docks at corners.
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
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
      "QToolBar#MainToolbar QLabel { background: transparent; color: #abb2bf; "
      "font-size: 10px; font-weight: 700; font-style: normal; padding: 0px 2px; border: none; }"
      "QToolBar#MainToolbar QComboBox, QToolBar#MainToolbar QSpinBox, #toolbarTargetSelector { "
      "background: #141619; color: #d7dae0; border: 1px solid #323844; border-radius: 2px; "
      "padding: 0px 6px; height: 22px; min-height: 20px; max-height: 22px; "
      "font-size: 11px; font-weight: 500; font-style: normal; }"
      "QToolBar#MainToolbar QToolButton { background: #252932; color: #d7dae0; "
      "border: 1px solid #323844; border-radius: 2px; padding: 1px 8px; margin: 0px; "
      "height: 20px; min-height: 20px; max-height: 22px; font-size: 11px; font-weight: 600; font-style: normal; }"
      "QToolBar#MainToolbar QToolButton:hover:enabled { background: #2c323d; border: 1px solid #3e4452; color: #ffffff; }"
      "QToolBar#MainToolbar QToolButton:pressed:enabled { background: #2c3444; border: 1px solid #528bff; }"
      "QToolBar#MainToolbar QToolButton:checked { background: #19362b; color: #4ec9b0; border: 1px solid #2ea043; font-weight: 600; }"
      "QToolBar#MainToolbar QToolButton:disabled { background: #14161b; color: #454c59; border: 1px solid #1e2128; }");

  // Inline Target binary selector and MPI Rank (-np) spinbox ahead of Run Target.
  QLabel *targetLabel = new QLabel(QStringLiteral("TARGET:"), toolbar);
  toolbar->addWidget(targetLabel);

  // Inline Target Selector combo box allowing one-click switching between MPI targets or ELF binaries.
  m_targetSelector = new QComboBox(toolbar);
  m_targetSelector->setObjectName(QStringLiteral("toolbarTargetSelector"));
  m_targetSelector->setFixedHeight(22);
  m_targetSelector->setMinimumWidth(145);
  QFont selectorFont = m_targetSelector->font();
  selectorFont.setItalic(false);
  selectorFont.setWeight(QFont::Medium);
  m_targetSelector->setFont(selectorFont);
  // Populate default MPI demonstration targets and the Open ELF / Binary file dialog entry.
  m_targetSelector->addItem(QStringLiteral("inspection_demo (MPI)"),
                            QStringLiteral("tutorial/inspection_demo.cpp"));
  m_targetSelector->addItem(QStringLiteral("deadlock_ring (MPI)"),
                            QStringLiteral("tutorial/deadlock_ring.cpp"));
  m_targetSelector->addItem(QStringLiteral("Open ELF / Binary..."),
                            QStringLiteral("__open_binary__"));
  // Synchronize source editor, disassembly/CFG, and build paths when the user switches targets.
  connect(m_targetSelector, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (!m_targetSelector || index < 0) return;
    const QString data = m_targetSelector->itemData(index).toString();
    if (data == QStringLiteral("__open_binary__")) {
      m_targetSelector->blockSignals(true);
      m_targetSelector->setCurrentIndex(0);
      m_targetSelector->blockSignals(false);
      openFile();
    } else if (!data.isEmpty()) {
      selectTargetByName(data);
    }
  });
  toolbar->addWidget(m_targetSelector);

  // Inline MPI Rank Count (-np) label and spin box configuring spawned cluster ranks (1..64).
  QLabel *ranksLabel = new QLabel(QStringLiteral("RANKS (-np):"), toolbar);
  toolbar->addWidget(ranksLabel);

  // Spin box controlling the number of MPI ranks launched by startTargetWorkflow().
  m_rankCountSpin = new QSpinBox(toolbar);
  m_rankCountSpin->setObjectName(QStringLiteral("toolbarRankSpin"));
  m_rankCountSpin->setRange(1, 64);
  m_rankCountSpin->setValue(8);
  m_rankCountSpin->setFixedHeight(22);
  m_rankCountSpin->setFixedWidth(48);
  m_rankCountSpin->setFont(selectorFont);
  toolbar->addWidget(m_rankCountSpin);
  toolbar->addSeparator();

  // Build Action: Auto-detects Meson/CMake/Make/SingleFileMPI and compiles in the embedded Terminal tab.
  QAction *toolbarBuildAct = new QAction("⚙ Build", this);
  connect(toolbarBuildAct, &QAction::triggered, this, &MainWindow::buildTargetWorkflow);
  toolbar->addAction(toolbarBuildAct);

  // Clean Action: Auto-detects active build system and dispatches clean command to Terminal tab.
  QAction *toolbarCleanAct = new QAction("✕ Clean", this);
  connect(toolbarCleanAct, &QAction::triggered, this, &MainWindow::cleanTargetWorkflow);
  toolbar->addAction(toolbarCleanAct);
  toolbar->addSeparator();

  // Run Action: Compiles active target on-demand, latches Running state, and advances to first BP or deadlock.
  m_runAction = new QAction("▶ Run Target", this);
  m_runAction->setCheckable(true);
  connect(m_runAction, &QAction::triggered, this, [this]() {
    ensureTargetBinaryCompiled();
    startTargetWorkflow();
  });
  toolbar->addAction(m_runAction);

  // Continue Action: Resumes suspended execution until the next active breakpoint or procedure exit.
  m_continueAction = new QAction("⏭ Continue", this);
  // Route Continue clicks directly to breakpoint-aware continueExecutionWorkflow().
  connect(m_continueAction, &QAction::triggered, this,
          &MainWindow::continueExecutionWorkflow);
  toolbar->addAction(m_continueAction);

  // Step Instruction Action: Executes a single machine instruction on the active rank
  // and synchronizes DisassemblyView, RegisterView, ServerRackView, MemView, and SourceCodeView.
  m_stepAction = new QAction("↷ Step Inst", this);
  // Route single-instruction step clicks to rank-aware stepInstructionWorkflow().
  connect(m_stepAction, &QAction::triggered, this,
          &MainWindow::stepInstructionWorkflow);
  toolbar->addAction(m_stepAction);

  // Pause Action: Interrupts running execution on the focused rank and updates status telemetry.
  m_pauseAction = new QAction("⏸ Pause Rank", this);
  // Dispatch asynchronous PauseCommand to backend coordinator when active.
  connect(m_pauseAction, &QAction::triggered, this, [this]() {
    updateStatusTelemetry(QString(), QStringLiteral("PAUSED"));
    if (m_coordinator) {
      // Ranks are 1-indexed inside backend PauseCommand wire messages.
      auto cmd = std::make_unique<gridlock::core::commands::PauseCommand>(
          m_coordinator, m_focusedRank + 1);
      executeCommand(std::move(cmd));
    }
  });
  toolbar->addAction(m_pauseAction);

  // Stop Action: Sends termination request to active coordinator and resets session state to IDLE.
  m_stopAction = new QAction("■ Stop", this);
  // Unlatch Run Target button and clear active PC cursor when Stop is pressed.
  connect(m_stopAction, &QAction::triggered, this,
          &MainWindow::stopTargetWorkflow);
  toolbar->addAction(m_stopAction);

  // Terminate Action: Closes debugging session, clears execution line highlight, and resets toolbar state.
  m_terminateAction = new QAction("✕ Terminate Session", this);
  // Reuse stopTargetWorkflow() so Stop and Terminate share identical session teardown semantics.
  connect(m_terminateAction, &QAction::triggered, this,
          &MainWindow::stopTargetWorkflow);
  toolbar->addAction(m_terminateAction);

  // Initialize state-aware toolbar button enablement (Idle by default: only Run Target enabled).
  updateDebugToolbarState();

  // Strip tooltips to maintain minimalist visual chrome.
  // Prevents native OS hover popups from obscuring adjacent code/disassembly rows.
  for (QAction *action : toolbar->actions()) {
    action->setToolTip(QString());
  }

  // Expanding spacer separating debugger actions from navigation controls.
  // Right-aligns history arrows, universal seek bar, and perspective pills on wide monitors.
  QWidget *spacer = new QWidget(toolbar);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  spacer->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
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
  m_seekBar->setMinimumWidth(160);
  m_seekBar->setFixedWidth(180);
  m_seekBar->setMaximumWidth(200);
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
  setupDockTitleBar(m_projectExplorerWidget, QStringLiteral("PROJECT / SYMBOLS"));
  m_dockHomeSlots.insert(m_projectExplorerWidget, {nullptr, -1, Qt::LeftDockWidgetArea});

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

            // Synchronize corresponding instruction bookmark in DisassemblyView.
            if (m_disassemblyView) {
              const uint64_t bpAddr = sourceLineToAddress(line);
              if (bpAddr != 0ULL && m_disassemblyView->isBookmarked(bpAddr) != isSet) {
                m_disassemblyView->toggleBookmark(bpAddr);
              }
            }

            // Broadcast breakpoint toggle to all active ranks or DAP session.
            if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(
                    m_coordinator)) {
              gdbCoord->broadcastBreakpoint(absoluteFilePath, line, isSet,
                                            condition);
            } else if (auto *dapCoord =
                           dynamic_cast<DapCoordinator *>(m_coordinator)) {
              dapCoord->toggleBreakpoint(absoluteFilePath, line);
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
  setupDockTitleBar(variablesDock, QStringLiteral("VARIABLES"));
  m_dockHomeSlots.insert(variablesDock, {nullptr, -1, Qt::RightDockWidgetArea});

  // CPU Registers View: Renders hardware registers (GPRs, segment, flags) with change diffs.
  m_registerView = new RegisterView(this);
  m_registerView->setObjectName("RegisterViewWidget");
  connect(m_registerView, &RegisterView::addressActivated, this,
          [this](uint64_t addr) { seekToAddressOrExpression(QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0'))); });
  QDockWidget *registersDock = new QDockWidget("Registers", this);
  registersDock->setObjectName("RegisterViewDock");
  registersDock->setWidget(m_registerView);
  setupDockTitleBar(registersDock, QStringLiteral("REGISTERS"));
  m_dockHomeSlots.insert(registersDock, {nullptr, -1, Qt::RightDockWidgetArea});

  // Differential Grid: Cross-rank tabular matrix comparing watched variable values.
  m_differentialGrid = new DifferentialGrid(this);
  m_differentialGrid->setObjectName("DifferentialGrid");
  m_differentialGrid->verticalHeader()->setDefaultSectionSize(20);
  m_differentialGrid->horizontalHeader()->setDefaultSectionSize(96);
  connect(m_differentialGrid, &DifferentialGrid::watchVariableAdded, this, [this](const QString &name) {
    if (auto *gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator *>(m_coordinator)) gdbCoord->registerWatchVariable(name);
  });

  // Restore project settings watch expressions into differential grid columns.
  auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
  for (const auto &w : ps.watchExpressions) {
    if (!w.empty()) m_differentialGrid->addVariableColumn(QString::fromStdString(w));
  }
  // Populate initial synthetic fixture watch entries for baseline inspection.
  m_differentialGrid->updateVariableDisplay(0, QStringLiteral("data[0].id"), QStringLiteral("0 (int)"));
  m_differentialGrid->updateVariableDisplay(0, QStringLiteral("data[0].coords.x"), QStringLiteral("0.0f (float)"));
  m_differentialGrid->updateVariableDisplay(0, QStringLiteral("mpi_rank"), QStringLiteral("0 (int)"));
  m_differentialGrid->updateVariableDisplay(1, QStringLiteral("data[0].id"), QStringLiteral("1 (int)"));
  m_differentialGrid->updateVariableDisplay(1, QStringLiteral("data[0].coords.x"), QStringLiteral("1.5f (float)"));
  m_differentialGrid->updateVariableDisplay(1, QStringLiteral("mpi_rank"), QStringLiteral("1 (int)"));
  m_differentialGrid->setColumnHidden(0, true);
  m_differentialGrid->setRowHidden(0, true);
  m_differentialGrid->horizontalHeader()->setMinimumSectionSize(64);
  m_differentialGrid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_differentialGrid->horizontalHeader()->setStretchLastSection(true);

  // Watch Expressions container with custom inline expression add bar.
  QWidget *watchContainer = new QWidget(this);
  QVBoxLayout *watchLayout = new QVBoxLayout(watchContainer);
  watchLayout->setContentsMargins(0, 0, 0, 0);
  watchLayout->setSpacing(0);

  QWidget *watchToolbar = new QWidget(watchContainer);
  watchToolbar->setObjectName("watchToolbar");
  QHBoxLayout *watchBarLayout = new QHBoxLayout(watchToolbar);
  watchBarLayout->setContentsMargins(3, 2, 3, 2);
  watchBarLayout->setSpacing(3);

  QLineEdit *watchInput = new QLineEdit(watchToolbar);
  watchInput->setObjectName("watchExpressionInput");
  watchInput->setPlaceholderText("New watch expression...");
  watchInput->setFixedHeight(22);

  QPushButton *addWatchBtn = new QPushButton("+ Watch", watchToolbar);
  addWatchBtn->setObjectName("addWatchBtn");
  addWatchBtn->setFixedHeight(22);

  auto submitWatch = [this, watchInput]() {
    const QString expr = watchInput->text().trimmed();
    if (!expr.isEmpty() && m_differentialGrid) {
      m_differentialGrid->addVariableColumn(expr);
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
  setupDockTitleBar(watchDock, QStringLiteral("WATCH EXPRESSIONS"));
  m_dockHomeSlots.insert(watchDock, {nullptr, -1, Qt::RightDockWidgetArea});

  // Server Rack Overview: Visualizes cluster node/rank topology and operational status.
  m_serverRackView = new ServerRackView(this);
  m_serverRackView->setObjectName("ServerRackViewWidget");
  connect(m_serverRackView, &ServerRackView::ranksUpdated, this, [this](const QMap<int, uint64_t> &rankPcMap) {
    if (m_disassemblyView) m_disassemblyView->setRankAddresses(rankPcMap);
  });
  connect(m_serverRackView, &ServerRackView::rankSelected, this, [this](int rankId, uint64_t pcAddr) {
    onRankSelected(rankId);
    if (m_disassemblyView) m_disassemblyView->setSelectedRank(rankId);
    if (pcAddr != 0ULL) seekToAddressOrExpression(QStringLiteral("0x%1").arg(pcAddr, 8, 16, QChar('0')));
    else updateStatusTelemetry();
  });
  QDockWidget *serverRackDock = new QDockWidget("Server Rack", this);
  serverRackDock->setObjectName("ServerRackDock");
  serverRackDock->setWidget(m_serverRackView);
  setupDockTitleBar(serverRackDock, QStringLiteral("SERVER RACK"));
  m_dockHomeSlots.insert(serverRackDock, {nullptr, -1, Qt::RightDockWidgetArea});

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
  // Synchronize all workspace views and highlight matching C/C++ source line when an instruction is clicked.
  connect(m_disassemblyView, &DisassemblyView::addressSelected, this,
          [this](uint64_t addr) {
            seekToAddressOrExpression(
                QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')));
            if (SourceCodeView *srcView = sourceCodeView()) {
              srcView->highlightExecutionLine(addressToSourceLine(addr));
            }
          });
  // Write patched instruction bytes back to the in-memory ELF binary image when edited in DisassemblyView.
  connect(m_disassemblyView, &DisassemblyView::instructionPatched, this,
          [this](uint64_t addr, const QByteArray &bytes) {
            if (m_elfInspector && m_elfInspector->isValid()) {
              m_elfInspector->writeVirtualMemory(addr, bytes);
            }
          });

  // Mount Source Editor and Disassembly/CFG side-by-side in the master horizontal splitter.
  masterHorizontalSplitter->addWidget(m_editorTabManager);
  masterHorizontalSplitter->addWidget(m_disassemblyView);

  // Allocate 45% width to source editor and 55% width to disassembly / CFG graph.
  masterHorizontalSplitter->setStretchFactor(0, 45);
  masterHorizontalSplitter->setStretchFactor(1, 55);
  masterHorizontalSplitter->setSizes({430, 520});

  // Mount top horizontal splitter into the upper portion of the central vertical splitter.
  mainVerticalSplitter->addWidget(masterHorizontalSplitter);

  // Bottom Zone: Left 36px vertical tool-window icon rail paired with linked contextual sub-tabs.
  QWidget *bottomDockContainer = new QWidget(mainVerticalSplitter);
  bottomDockContainer->setObjectName(QStringLiteral("bottomDockContainer"));
  bottomDockContainer->setMinimumHeight(180);
  QHBoxLayout *bottomRowLayout = new QHBoxLayout(bottomDockContainer);
  bottomRowLayout->setContentsMargins(0, 0, 0, 0);
  bottomRowLayout->setSpacing(0);

  // Left 36px activity rail hosting Debug, MPI, Terminal, and Docs tool-group buttons.
  m_bottomToolRail = new QWidget(bottomDockContainer);
  m_bottomToolRail->setObjectName(QStringLiteral("bottomToolWindowRail"));
  m_bottomToolRail->setFixedWidth(36);
  m_bottomToolRail->setAttribute(Qt::WA_StyledBackground, true);

  // Expanding document-mode tab container hosting linked contextual tool views.
  m_bottomTabs = new QTabWidget(bottomDockContainer);
  m_bottomTabs->setObjectName("BottomConsoleTabs");
  m_bottomTabs->setMinimumWidth(200);
  m_bottomTabs->setMinimumHeight(180);
  m_bottomTabs->setDocumentMode(true);
  m_bottomTabs->setContentsMargins(0, 0, 0, 0);
  if (m_bottomTabs->tabBar()) {
    m_bottomTabs->tabBar()->setUsesScrollButtons(true);
    m_bottomTabs->tabBar()->setElideMode(Qt::ElideNone);
  }
  bottomRowLayout->addWidget(m_bottomToolRail);
  bottomRowLayout->addWidget(m_bottomTabs, 1);

  // Embedded Interactive PTY Terminal: Spawns bash or user shell for command execution.
  m_terminalDock = new TerminalDockWidget("Terminal", m_bottomTabs);
  m_terminalDock->setObjectName("TerminalDock");

  // Dynamic Expression Evaluator: Supports ad-hoc C/C++ expression evaluation via GDB.
  m_expressionEvaluatorWidget = new ExpressionEvaluatorWidget(m_bottomTabs);
  m_expressionEvaluatorWidget->setObjectName("ExpressionEvaluatorDock");
  QWidget *emptyEvalTitle = new QWidget(m_expressionEvaluatorWidget);
  emptyEvalTitle->setFixedSize(0, 0);
  m_expressionEvaluatorWidget->setTitleBarWidget(emptyEvalTitle);
  connect(m_expressionEvaluatorWidget, &ExpressionEvaluatorWidget::evaluateRequested, this,
          [this](const QString &expr) {
            if (m_coordinator) m_coordinator->evaluateExpression(m_focusedRank, expr);
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

  // Multi-Distro HPC & System Package Manager Workstation: Visualizes Spack and system packages.
  m_spackManager = new HpcConsoleWidget(m_hpcBackend, m_bottomTabs);
  m_spackManager->setObjectName("HpcConsoleDock");

  // Connect memory inspection view read and live-patch requests to coordinator and ELF image.
  connect(m_memView, &MemView::requestMemory, this, [this](const QString &address, int length) {
    seekToAddressOrExpression(address);
    if (m_coordinator) m_coordinator->readMemory(m_focusedRank, address, length);
  });
  connect(m_memView, &MemView::bytesPatched, this, [this](uint64_t addr, const QByteArray &bytes) {
    if (m_elfInspector && m_elfInspector->isValid()) m_elfInspector->writeVirtualMemory(addr, bytes);
  });

  // Asynchronous Cross-Rank Memory Differ: Compares byte blocks between two MPI rank processes.
  connect(m_memView, &MemView::requestMemoryDiff, this,
          [this](int baseRank, int targetRank, const QString &addrStr, int length) {
            if (!m_coordinator) return;
            const pid_t basePid = m_coordinator->getPidForRank(baseRank);
            const pid_t targetPid = m_coordinator->getPidForRank(targetRank);
            bool ok = false;
            const quint64 addrInt = addrStr.toULongLong(&ok, 16);
            if (!ok) return;
            void *remoteAddr = reinterpret_cast<void *>(addrInt);
            // Run cross-process memory comparison in worker thread via QtConcurrent.
            QPointer<MainWindow> weakThis(this);
            (void)QtConcurrent::run([weakThis, basePid, targetPid, remoteAddr, length]() {
              auto result = gridlock::core::hpc::MemoryDiffer::compareMemory(basePid, targetPid, remoteAddr, length);
              // Dispatch comparison result back to GUI thread for visualization.
              QMetaObject::invokeMethod(weakThis, [weakThis, result, remoteAddr]() {
                if (weakThis && weakThis->m_memView) weakThis->m_memView->displayMemoryDiff(result, remoteAddr);
              });
            });
          });

  // MPI Diagnostics & Deadlock Visualizer: Displays wait-for graphs and lock cycles.
  m_mpiDiagnosticsWidget = new MpiDiagnosticsWidget(m_bottomTabs);
  connect(m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::jumpToFrameRequested, this, &MainWindow::onRankSelected);

  // Add all inspection and terminal widgets to bottom tabbed container.
  // 1. Interactive bash/shell terminal tab.
  m_bottomTabs->addTab(m_terminalDock, "Terminal");
  // 2. Raw GDB machine interface console tab.
  m_bottomTabs->addTab(m_gdbConsoleWidget, "GDB Console");
  // 3. Hexadecimal memory inspector and live patching tab.
  m_bottomTabs->addTab(m_memView, "Memory View");
  // 4. MPI cluster wait-for cycle and deadlock analysis tab.
  m_bottomTabs->addTab(m_mpiDiagnosticsWidget, "MPI Diagnostics");
  // 5. Multi-distro HPC & system package manager tab.
  m_bottomTabs->addTab(m_spackManager, QStringLiteral("Package Manager"));
  // 6. Dynamic C/C++ expression evaluator tab.
  m_bottomTabs->addTab(m_expressionEvaluatorWidget, "Evaluator");
  // 7. Offline Dash/Zeal reference manual tab.
  m_bottomTabs->addTab(m_referenceManualWidget, "Reference Manual");
  m_bottomTabs->setCurrentWidget(m_memView);

  // Harmonize margins, control heights, borderless frames, tool-window rail, and F1 lookup across bottom tabs.
  harmonizeBottomConsoleTabs();

  // Mount bottom rail + contextual tab container into central vertical splitter.
  mainVerticalSplitter->addWidget(bottomDockContainer);

  // Allocate 68% height to code/disassembly and 32% to bottom console tabs.
  mainVerticalSplitter->setStretchFactor(0, 68);
  mainVerticalSplitter->setStretchFactor(1, 32);
  mainVerticalSplitter->setSizes({600, 260});

  // Designate vertical splitter as the central QMainWindow widget.
  // The central widget occupies remaining interior space between all four dock areas.
  setCentralWidget(mainVerticalSplitter);

  // Enforce consistent minimum width constraints across all peripheral docks.
  // Side panels retain full horizontal/vertical resizability while preventing accidental collapse.
  const QList<QDockWidget *> sideDocks = {
      m_projectExplorerWidget, variablesDock, registersDock, watchDock, serverRackDock};
  for (QDockWidget *dock : sideDocks) {
    if (!dock) continue;
    dock->setMinimumWidth(150);
    dock->setMaximumWidth(QWIDGETSIZE_MAX);
    dock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (QWidget *inner = dock->widget()) {
      inner->setMinimumWidth(150);
      inner->setMaximumWidth(QWIDGETSIZE_MAX);
      inner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
  }

  // Apply default pixel distributions for side explorer and inspection docks.
  // 215px allocated to left explorer, 255px to right inspection zone.
  resizeDocks({m_projectExplorerWidget, registersDock}, {215, 255}, Qt::Horizontal);
  // Stack right-hand docks vertically: 320px registers, 210px watches, 290px server rack.
  resizeDocks({registersDock, watchDock, serverRackDock}, {320, 210, 290}, Qt::Vertical);

  // Locate or initialize View menu to register dock toggle actions and redock command.
  QMenu *viewMenu = nullptr;
  for (QAction *action : menuBar()->actions()) {
    if (action->menu() && action->text() == "&View") { viewMenu = action->menu(); break; }
  }
  // Expose one-click batch recovery action in View menu to redock all floating panels.
  if (viewMenu) {
    viewMenu->addSeparator();
    auto* redockAct = viewMenu->addAction(QStringLiteral("Redock All Floating Panels"),
                                          this, &MainWindow::redockAllFloatingPanels);
    redockAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
  }

  // Register toggle actions and configure window flags for all child docks.
  for (auto *dock : findChildren<QDockWidget *>()) {
    // Add dynamic toggle checkbox to View menu.
    if (viewMenu) viewMenu->addAction(dock->toggleViewAction());
    // Allow panels to be closed, undocked into floating windows, or tabified.
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
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
  }

  // Purge legacy geometry keys to guarantee fresh default workstation layout proportions.
  QSettings settings("gridlock", "debugger");
  settings.remove("MainWindow/Geometry_v8");
  settings.remove("MainWindow/State_v8");
  settings.remove("MainWindow/Splitter_v8_MasterHorizontalSplitter");
  settings.remove("MainWindow/Splitter_v8_MainVerticalSplitter");
}



}  // namespace gridlock::ui

#include "widgets/HpcConsoleWidget.cpp"

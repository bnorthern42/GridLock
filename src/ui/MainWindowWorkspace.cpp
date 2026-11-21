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
#include "utils/FileDialogHelper.hpp"
#include "widgets/ProjectExplorerWidget.hpp"
#include "widgets/ReferenceManualWidget.hpp"

#include <QAbstractItemView>
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
#include <QStatusBar>
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
  // Construct consolidated main debugger toolbar targeting < 1,040 px width without overflow.
  QToolBar *toolbar = addToolBar("Main Toolbar");
  toolbar->setObjectName("MainToolbar");
  toolbar->setMovable(true);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
  toolbar->setFixedHeight(32);
  toolbar->setContentsMargins(2, 0, 2, 0);

  // Target selector capped at 190 px with middle text elision and full-path tooltip.
  m_targetSelector = new QComboBox(toolbar);
  m_targetSelector->setObjectName(QStringLiteral("toolbarTargetSelector"));
  m_targetSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_targetSelector->setMinimumContentsLength(8);
  m_targetSelector->setMinimumWidth(100);
  m_targetSelector->setMaximumWidth(190);
  m_targetSelector->setFixedHeight(26);
  if (m_targetSelector->view()) {
    m_targetSelector->view()->setTextElideMode(Qt::ElideMiddle);
    m_targetSelector->view()->setMinimumWidth(260);
  }
  QFont selectorFont = m_targetSelector->font();
  selectorFont.setItalic(false);
  selectorFont.setWeight(QFont::Medium);
  m_targetSelector->setFont(selectorFont);
  m_targetSelector->addItem(QStringLiteral("inspection_demo (MPI)"), QStringLiteral("tutorial/inspection_demo.cpp"));
  m_targetSelector->addItem(QStringLiteral("deadlock_ring (MPI)"), QStringLiteral("tutorial/deadlock_ring.cpp"));
  m_targetSelector->addItem(QStringLiteral("Open ELF / Binary..."), QStringLiteral("__open_binary__"));
  auto syncTargetTooltip = [this]() {
    if (!m_targetSelector || m_targetSelector->currentIndex() < 0) return;
    const int idx = m_targetSelector->currentIndex();
    const QString raw = m_targetSelector->itemData(idx).toString();
    const QString full = (raw.isEmpty() || raw == QStringLiteral("__open_binary__")) ? m_targetSelector->itemText(idx) : resolveWorkspacePath(raw);
    m_targetSelector->setToolTip(QStringLiteral("Active Target: %1\nPath: %2").arg(m_targetSelector->itemText(idx), full.isEmpty() ? raw : full));
  };
  syncTargetTooltip();
  connect(m_targetSelector, &QComboBox::currentIndexChanged, this, [this, syncTargetTooltip](int index) {
    if (!m_targetSelector || index < 0) return;
    const QString data = m_targetSelector->itemData(index).toString();
    if (data == QStringLiteral("__open_binary__")) {
      m_targetSelector->blockSignals(true);
      m_targetSelector->setCurrentIndex(0);
      m_targetSelector->blockSignals(false);
      syncTargetTooltip();
      openFile();
    } else if (!data.isEmpty()) {
      selectTargetByName(data);
      syncTargetTooltip();
    }
  });
  toolbar->addWidget(m_targetSelector);

  // Compact self-labeled MPI rank count spin box ("np:8").
  m_rankCountSpin = new QSpinBox(toolbar);
  m_rankCountSpin->setObjectName(QStringLiteral("toolbarRankSpin"));
  m_rankCountSpin->setPrefix(QStringLiteral("np:"));
  m_rankCountSpin->setRange(1, 256);
  m_rankCountSpin->setValue(8);
  m_rankCountSpin->setMinimumWidth(56);
  m_rankCountSpin->setMaximumWidth(68);
  m_rankCountSpin->setFixedHeight(26);
  m_rankCountSpin->setFont(selectorFont);
  m_rankCountSpin->setToolTip(QStringLiteral("MPI Rank Count (-np): number of parallel processes spawned when launching the target"));
  connect(m_rankCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
    if (!m_debugSessionActive && !m_previewFixtureOnly && m_serverRackView) m_serverRackView->setSessionIdle(val);
    syncRankCountChrome(val);
  });
  toolbar->addWidget(m_rankCountSpin);
  toolbar->addSeparator();

  // Compact Build / Clean / Rebuild split button (#toolbarBuildSplitBtn).
  auto *toolbarBuildAct = new QAction(QStringLiteral("Build Target"), this);
  toolbarBuildAct->setIconText(QStringLiteral("⚙ Build"));
  toolbarBuildAct->setToolTip(QStringLiteral("Build Active Target (Ctrl+B) — click arrow for Clean / Rebuild"));
  connect(toolbarBuildAct, &QAction::triggered, this, &MainWindow::buildTargetWorkflow);
  auto *buildSplitBtn = new QToolButton(toolbar);
  buildSplitBtn->setObjectName(QStringLiteral("toolbarBuildSplitBtn"));
  buildSplitBtn->setPopupMode(QToolButton::MenuButtonPopup);
  buildSplitBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
  buildSplitBtn->setDefaultAction(toolbarBuildAct);
  auto *buildMenu = new QMenu(buildSplitBtn);
  buildMenu->addAction(QStringLiteral("Build Target (Ctrl+B)"), this, &MainWindow::buildTargetWorkflow);
  buildMenu->addAction(QStringLiteral("Clean Target"), this, &MainWindow::cleanTargetWorkflow);
  buildMenu->addAction(QStringLiteral("Rebuild Target"), this, &MainWindow::rebuildTargetWorkflow);
  buildSplitBtn->setMenu(buildMenu);
  toolbar->addWidget(buildSplitBtn);

  // Run Action: Compiles active target on-demand, latches Running state, and advances to first BP.
  m_runAction = new QAction(QStringLiteral("▶ Run Target"), this);
  m_runAction->setObjectName(QStringLiteral("actionRunTarget"));
  m_runAction->setIconText(QStringLiteral("▶ Run"));
  m_runAction->setCheckable(true);
  connect(m_runAction, &QAction::triggered, this, [this]() {
    ensureTargetBinaryCompiled();
    startTargetWorkflow();
  });
  toolbar->addAction(m_runAction);
  if (QWidget *runBtn = toolbar->widgetForAction(m_runAction)) {
    runBtn->setObjectName(QStringLiteral("toolbarRunTargetBtn"));
  }
  toolbar->addSeparator();

  // Continue Action (F5).
  m_continueAction = new QAction(QStringLiteral("Continue"), this);
  m_continueAction->setObjectName(QStringLiteral("debug.continue"));
  m_continueAction->setIconText(QStringLiteral("⏭"));
  m_continueAction->setShortcut(QKeySequence(Qt::Key_F5));
  connect(m_continueAction, &QAction::triggered, this, &MainWindow::continueExecutionWorkflow);
  toolbar->addAction(m_continueAction);

  // Source-Level Stepping: Step Over (F10), Step Into (F11, with stepi/nexti dropdown), Step Out (Shift+F11).
  m_stepOverAction = new QAction(QStringLiteral("Step Over"), this);
  m_stepOverAction->setObjectName(QStringLiteral("debug.step_over"));
  m_stepOverAction->setIconText(QStringLiteral("↷ Over"));
  m_stepOverAction->setShortcut(QKeySequence(Qt::Key_F10));
  connect(m_stepOverAction, &QAction::triggered, this, &MainWindow::stepOverWorkflow);
  toolbar->addAction(m_stepOverAction);

  m_stepIntoAction = new QAction(QStringLiteral("Step Into"), this);
  m_stepIntoAction->setObjectName(QStringLiteral("debug.step_into"));
  m_stepIntoAction->setIconText(QStringLiteral("↳ Into"));
  m_stepIntoAction->setShortcut(QKeySequence(Qt::Key_F11));
  connect(m_stepIntoAction, &QAction::triggered, this, &MainWindow::stepIntoWorkflow);
  toolbar->addAction(m_stepIntoAction);

  m_stepAction = new QAction(QStringLiteral("Step Instruction (stepi)"), this);
  m_stepAction->setObjectName(QStringLiteral("actionStepInstruction"));
  connect(m_stepAction, &QAction::triggered, this, &MainWindow::stepInstructionWorkflow);
  if (auto *intoBtn = qobject_cast<QToolButton *>(toolbar->widgetForAction(m_stepIntoAction))) {
    intoBtn->setObjectName(QStringLiteral("toolbarStepIntoBtn"));
    intoBtn->setPopupMode(QToolButton::MenuButtonPopup);
    auto *stepMenu = new QMenu(intoBtn);
    stepMenu->addAction(m_stepAction);
    QAction *nextiAct = stepMenu->addAction(QStringLiteral("Step Over Instruction (nexti) (Ctrl+Shift+F10)"), this, &MainWindow::nextInstructionWorkflow);
    nextiAct->setObjectName(QStringLiteral("actionToolbarNexti"));
    intoBtn->setMenu(stepMenu);
  }

  m_stepOutAction = new QAction(QStringLiteral("Step Out"), this);
  m_stepOutAction->setObjectName(QStringLiteral("debug.step_out"));
  m_stepOutAction->setIconText(QStringLiteral("↱ Out"));
  m_stepOutAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F11));
  connect(m_stepOutAction, &QAction::triggered, this, &MainWindow::stepOutWorkflow);
  toolbar->addAction(m_stepOutAction);

  // Pause Focused Rank & Single Merged Stop Action (Shift+F5).
  m_pauseAction = new QAction(QStringLiteral("Pause Rank"), this);
  m_pauseAction->setObjectName(QStringLiteral("actionPauseRank"));
  m_pauseAction->setIconText(QStringLiteral("⏸"));
  connect(m_pauseAction, &QAction::triggered, this, &MainWindow::pauseRankWorkflow);
  toolbar->addAction(m_pauseAction);

  m_stopAction = new QAction(QStringLiteral("Stop Session"), this);
  m_stopAction->setObjectName(QStringLiteral("debug.stop"));
  m_stopAction->setIconText(QStringLiteral("■ Stop"));
  m_stopAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
  connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopTargetWorkflow);
  toolbar->addAction(m_stopAction);

  // Time-travel Record button next to stepping group (Step Back & Rev Continue stay hidden until recording is active).
  auto *recBtn = new QPushButton(QStringLiteral("⏺ Rec"), toolbar);
  recBtn->setObjectName(QStringLiteral("toolbarRecordHistoryBtn"));
  recBtn->setCheckable(true);
  auto *sbBtn = new QPushButton(QStringLiteral("↶ Back"), toolbar);
  sbBtn->setObjectName(QStringLiteral("toolbarStepBackBtn"));
  auto *rcBtn = new QPushButton(QStringLiteral("⏮ Rev"), toolbar);
  rcBtn->setObjectName(QStringLiteral("toolbarReverseContinueBtn"));
  toolbar->addWidget(recBtn);
  QAction *sbAct = toolbar->addWidget(sbBtn);
  sbAct->setObjectName(QStringLiteral("actionToolbarStepBack"));
  sbAct->setVisible(false);
  QAction *rcAct = toolbar->addWidget(rcBtn);
  rcAct->setObjectName(QStringLiteral("actionToolbarReverseContinue"));
  rcAct->setVisible(false);
  connect(recBtn, &QPushButton::toggled, this, [this](bool on) {
    if (m_coordinator) m_coordinator->setReverseRecordingEnabled(activeSteppingRank(), on);
    updateDebugToolbarState();
  });
  connect(sbBtn, &QPushButton::clicked, this, [this]() { reverseStepOverWorkflow(); });
  connect(rcBtn, &QPushButton::clicked, this, [this]() { reverseContinueWorkflow(); });

  // Initialize state-aware toolbar button enablement and descriptive tooltips.
  updateDebugToolbarState();
  for (QToolButton *btn : toolbar->findChildren<QToolButton *>()) {
    btn->setFixedHeight(26);
    btn->setAttribute(Qt::WA_AlwaysShowToolTips, true);
  }
  for (QPushButton *btn : toolbar->findChildren<QPushButton *>()) {
    btn->setFixedHeight(26);
    btn->setAttribute(Qt::WA_AlwaysShowToolTips, true);
  }

  // Expanding spacer separating execution controls from right-aligned navigation & perspective pills.
  QWidget *spacer = new QWidget(toolbar);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  spacer->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
  toolbar->addWidget(spacer);

  // History back/forward navigation buttons (< / >) with shortcut tooltips.
  m_navBackBtn = new QPushButton(QStringLiteral("<"), toolbar);
  m_navBackBtn->setObjectName("navBackButton");
  m_navBackBtn->setFixedSize(22, 26);
  m_navBackBtn->setEnabled(false);
  m_navBackBtn->setAttribute(Qt::WA_AlwaysShowToolTips, true);
  m_navBackBtn->setToolTip(QStringLiteral("Navigate Back in Address History (Alt+Left)"));
  connect(m_navBackBtn, &QPushButton::clicked, this, &MainWindow::navigateBack);
  toolbar->addWidget(m_navBackBtn);

  m_navForwardBtn = new QPushButton(QStringLiteral(">"), toolbar);
  m_navForwardBtn->setObjectName("navForwardButton");
  m_navForwardBtn->setFixedSize(22, 26);
  m_navForwardBtn->setEnabled(false);
  m_navForwardBtn->setAttribute(Qt::WA_AlwaysShowToolTips, true);
  m_navForwardBtn->setToolTip(QStringLiteral("Navigate Forward in Address History (Alt+Right)"));
  connect(m_navForwardBtn, &QPushButton::clicked, this, &MainWindow::navigateForward);
  toolbar->addWidget(m_navForwardBtn);

  // Global seek bar for hexadecimal address or symbol jumps (Ctrl+G).
  QShortcut *navBackShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
  connect(navBackShortcut, &QShortcut::activated, this, &MainWindow::navigateBack);
  QShortcut *navFwdShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
  connect(navFwdShortcut, &QShortcut::activated, this, &MainWindow::navigateForward);

  m_seekBar = new QLineEdit(toolbar);
  m_seekBar->setObjectName("cutterSeekBar");
  m_seekBar->setPlaceholderText("Go to addr/sym (Ctrl+G)");
  m_seekBar->setToolTip(QStringLiteral("Go to Address or Symbol (Ctrl+G) — enter hex address (0x401000) or function symbol"));
  m_seekBar->setFixedHeight(26);
  m_seekBar->setFixedWidth(88);
  toolbar->addWidget(m_seekBar);

  connect(m_seekBar, &QLineEdit::returnPressed, this, [this]() {
    seekToAddressOrExpression(m_seekBar->text());
  });

  // Secondary toolbar break for address space navigation ribbon.
  addToolBarBreak(Qt::TopToolBarArea);
  QToolBar *navToolbar = addToolBar("Address Space Navigation");
  navToolbar->setObjectName("AddressNavToolbar");
  navToolbar->setMovable(true);
  navToolbar->setFloatable(false);
  navToolbar->setFixedHeight(22);
  navToolbar->setContentsMargins(0, 0, 0, 0);

  // Instantiate interactive address space navigation bar ribbon.
  m_addressNavBar = createAddressSpaceNavBar(this, navToolbar);
  m_addressNavBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  new DynamicAddressRibbonFilter(m_addressNavBar, [this](uint64_t addr) {
    seekToAddressOrExpression(QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')));
  });
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

  // Synchronize workspace directory, FILES tree, and TARGET selector when a TOML session is restored.
  connect(&gridlock::core::managers::SessionManager::instance(),
          &gridlock::core::managers::SessionManager::sessionLoaded,
          this, &MainWindow::switchWorkspaceDirectory);
  // Route Open Folder selections in ProjectExplorerWidget through the unified workspace switcher.
  connect(m_projectExplorerWidget, &ProjectExplorerWidget::workspaceRootChanged,
          this, &MainWindow::switchWorkspaceDirectory);

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
  // Prevent child widgets from being collapsed completely via splitter drag and use opaque resize on Wayland.
  mainVerticalSplitter->setChildrenCollapsible(false); mainVerticalSplitter->setOpaqueResize(true);

  // Center Zone: Source Code (Left) & Disassembly View (Right) side-by-side
  QSplitter *masterHorizontalSplitter =
      new QSplitter(Qt::Horizontal, mainVerticalSplitter);
  masterHorizontalSplitter->setObjectName("MasterHorizontalSplitter");
  masterHorizontalSplitter->setContentsMargins(0, 0, 0, 0);
  masterHorizontalSplitter->setHandleWidth(4);
  // Prevent source editor or disassembly panes from being zeroed out and use opaque resize on Wayland.
  masterHorizontalSplitter->setChildrenCollapsible(false); masterHorizontalSplitter->setOpaqueResize(true);

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

  m_differentialGrid->setColumnHidden(0, true);
  m_differentialGrid->setRowHidden(0, true);
  m_differentialGrid->horizontalHeader()->setVisible(false);
  m_differentialGrid->verticalHeader()->setVisible(false);
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

  // Encapsulate watch container in a standard dockable QDockWidget with concise WATCH title.
  QDockWidget *watchDock = new QDockWidget("WATCH", this);
  watchDock->setObjectName("WatchExpressionsDock");
  watchDock->setWidget(watchContainer);
  setupDockTitleBar(watchDock, QStringLiteral("WATCH"));
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

  // Consolidate right sidebar into two balanced vertical zones:
  // Top Right (55%): Tabbed VARIABLES | REGISTERS | WATCH inspection pane.
  // Bottom Right (45%): SERVER RACK multi-rank cluster topology status grid.
  addDockWidget(Qt::RightDockWidgetArea, variablesDock);
  splitDockWidget(variablesDock, serverRackDock, Qt::Vertical);
  tabifyDockWidget(variablesDock, registersDock);
  tabifyDockWidget(registersDock, watchDock);
  variablesDock->raise();
  resizeDocks({variablesDock, serverRackDock}, {520, 380}, Qt::Vertical);
  resizeDocks({variablesDock}, {340}, Qt::Horizontal);
  // Configure dock tab bars to avoid scroll buttons and fit tabs side-by-side.
  for (auto *tb : findChildren<QTabBar *>()) {
    tb->setUsesScrollButtons(false);
    tb->setElideMode(Qt::ElideNone);
    tb->setExpanding(false);
  }

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

  // Synchronize DisassemblyView decompiler suppression whenever Source Editor is shown or hidden.
  class SourceEditorVisibilitySync final : public QObject {
  public:
    // Bind filter lifetime to m_editorTabManager and notify m_disassemblyView on Show/Hide.
    SourceEditorVisibilitySync(QWidget *ed, DisassemblyView *dv) : QObject(ed), m_ed(ed), m_dv(dv) {}
    bool eventFilter(QObject *w, QEvent *ev) override {
      if (w == m_ed && m_dv && (ev->type() == QEvent::Show || ev->type() == QEvent::ShowToParent ||
                                ev->type() == QEvent::Hide || ev->type() == QEvent::HideToParent)) {
        m_dv->setSourceEditorActive(!m_ed->isHidden());
        if (auto *mw = qobject_cast<MainWindow *>(m_ed->window())) mw->syncDisassemblyDecompilerVisibility(false);
      }
      return QObject::eventFilter(w, ev);
    }
  private:
    QWidget *m_ed = nullptr;
    DisassemblyView *m_dv = nullptr;
  };
  m_editorTabManager->installEventFilter(new SourceEditorVisibilitySync(m_editorTabManager, m_disassemblyView));
  m_disassemblyView->setSourceEditorActive(!m_editorTabManager->isHidden());

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
  // 7. Clean target program stdout/stderr console with read-only launch command header.
  m_bottomTabs->addTab(createProgramOutputTab(), QStringLiteral("Program Output"));
  // 8. Offline Dash/Zeal reference manual tab.
  m_bottomTabs->addTab(m_referenceManualWidget, "Reference Manual");
  m_bottomTabs->setCurrentWidget(m_memView);
  connect(m_bottomTabs, &QTabWidget::currentChanged, this, [this](int) {
    QWidget* activeTab = m_bottomTabs ? m_bottomTabs->currentWidget() : nullptr;
    if (activeTab == m_terminalDock && m_terminalDock) m_terminalDock->ensureShellStarted();
    else if (activeTab == m_spackManager && m_spackManager) static_cast<HpcConsoleWidget*>(m_spackManager)->ensurePackagesLoaded();
    else if (activeTab == m_referenceManualWidget && m_referenceManualWidget) m_referenceManualWidget->ensureDocsetsInitialized();
  });

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

  // Locate or initialize View menu to register toolbar/dock toggle actions and redock command.
  QMenu *viewMenu = nullptr;
  for (QAction *action : menuBar()->actions()) {
    if (action->menu() && action->text() == "&View") { viewMenu = action->menu(); break; }
  }
  // Expose Toolbars submenu, split editor actions, and batch recovery in the View menu.
  if (viewMenu) {
    QMenu *toolbarsMenu = viewMenu->addMenu(QStringLiteral("Toolbars"));
    for (auto *tb : findChildren<QToolBar *>()) {
      tb->setMovable(true);
      toolbarsMenu->addAction(tb->toggleViewAction());
    }
    toolbarsMenu->addSeparator();
    toolbarsMenu->addAction(QStringLiteral("Reset Toolbars"), this, [this]() {
      for (auto *tb : findChildren<QToolBar *>()) { tb->show(); addToolBar(Qt::TopToolBarArea, tb); }
    });
    viewMenu->addSeparator();
    // Register Split Editor Right (horizontal side-by-side split pane).
    viewMenu->addAction(QStringLiteral("Split Editor Right"), this, [this]() {
      if (m_editorTabManager) m_editorTabManager->splitEditor(Qt::Horizontal);
    });
    // Register Split Editor Down (vertical stacked split pane).
    viewMenu->addAction(QStringLiteral("Split Editor Down"), this, [this]() {
      if (m_editorTabManager) m_editorTabManager->splitEditor(Qt::Vertical);
    });
    // Register Close Split to collapse the secondary split editor pane.
    viewMenu->addAction(QStringLiteral("Close Split"), this, [this]() {
      if (m_editorTabManager) m_editorTabManager->closeSplit();
    });
    viewMenu->addSeparator();
    // Register Redock All Floating Panels action with Ctrl+Shift+R shortcut.
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
  settings.remove("MainWindow/Geometry_v8"); settings.remove("MainWindow/State_v8");
  settings.remove("MainWindow/Splitter_v8_MasterHorizontalSplitter"); settings.remove("MainWindow/Splitter_v8_MainVerticalSplitter");
}
/** @brief Saves the active workspace session and project settings to workspace.toml. */
bool MainWindow::saveSession(bool showStatusMessage) {
  // Resolve workspace root and target session file path.
  const QString wsRoot = !m_activeWorkspaceDir.isEmpty() ? m_activeWorkspaceDir : (m_projectExplorerWidget ? m_projectExplorerWidget->rootPath() : QDir::currentPath());
  const QString targetPath = !m_activeSessionFilePath.isEmpty() ? m_activeSessionFilePath : QDir(wsRoot).filePath(QStringLiteral(".gridlock/workspace.toml"));
  QDir().mkpath(QFileInfo(targetPath).absolutePath()); gridlock::core::managers::SessionState state;
  // Collect watched variables, persistent breakpoints, and focused rank.
  if (m_differentialGrid) for (const auto &w : m_differentialGrid->getWatchExpressions()) state.watchedVariables.push_back(QString::fromStdString(w));
  for (auto it = m_persistentBreakpoints.constBegin(); it != m_persistentBreakpoints.constEnd(); ++it) { for (int line : *it) state.activeBreakpoints.push_back(QStringLiteral("%1:%2").arg(it.key()).arg(line)); }
  state.selectedRanks.push_back(m_focusedRank);
  // Persist project settings and session TOML to disk.
  auto &cfg = gridlock::core::ConfigManager::instance(); cfg.saveProjectSettings(cfg.projectSettings());
  const bool ok = gridlock::core::managers::SessionManager::instance().saveSession(targetPath, state);
  if (ok) { m_activeSessionFilePath = targetPath; rebuildRecentSessionsMenu(); if (showStatusMessage && statusBar()) statusBar()->showMessage(QStringLiteral("Session saved to %1").arg(targetPath), 3000); }
  return ok;
}

}  // namespace gridlock::ui

#include "widgets/HpcConsoleWidget.cpp"

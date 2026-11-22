/**
 * @file MainWindowWorkspace.cpp
 * @brief Implements the four-zone docking architecture and workspace layout choreography.
 *
 * Constructs and arranges the left symbol/project explorer dock, central source and
 * disassembly/CFG splitters, right-hand CPU state inspection docks (Variables, Registers,
 * Watch Expressions, Server Rack), and bottom console/memory tabs.
 */

#include "MainWindow.hpp"
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

#include <QButtonGroup>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <memory>

namespace gridlock::ui {

/** @brief Constructs and docks all secondary workspace inspection panels and editors. */
void MainWindow::setupDocks() {
  // Left Zone: Project Explorer / Symbols & Functions list
  m_projectExplorerWidget = new ProjectExplorerWidget(this);
  m_projectExplorerWidget->setObjectName("ProjectExplorerDock");
  addDockWidget(Qt::LeftDockWidgetArea, m_projectExplorerWidget);
  setupDockTitleBar(m_projectExplorerWidget, QStringLiteral("PROJECT / SYMBOLS"));
  m_dockHomeSlots.insert(m_projectExplorerWidget, {nullptr, -1, Qt::LeftDockWidgetArea});

  connect(&gridlock::core::managers::SessionManager::instance(), &gridlock::core::managers::SessionManager::sessionLoaded,
          this, [this](const QString& dir) { switchWorkspaceDirectory(dir); m_sessionRestoredEmptyBps = m_persistentBreakpoints.isEmpty(); });
  connect(m_projectExplorerWidget, &ProjectExplorerWidget::workspaceRootChanged, this, &MainWindow::switchWorkspaceDirectory);
  connect(m_projectExplorerWidget, &ProjectExplorerWidget::fileDoubleClicked, this,
          [this](const QString& filePath) { if (m_editorTabManager) loadSourceFile(filePath); });
  connect(m_projectExplorerWidget, &ProjectExplorerWidget::symbolAddressSelected, this,
          [this](uint64_t address, const QString&) { seekToAddressOrExpression(QStringLiteral("0x%1").arg(address, 8, 16, QChar('0'))); });

  // Center & Bottom Zone Splitter: Divides central code/disassembly from bottom tabs.
  auto* mainVerticalSplitter = new QSplitter(Qt::Vertical, this);
  mainVerticalSplitter->setObjectName("MainVerticalSplitter");
  mainVerticalSplitter->setMinimumWidth(240);
  mainVerticalSplitter->setContentsMargins(0, 0, 0, 0);
  mainVerticalSplitter->setHandleWidth(4);
  mainVerticalSplitter->setChildrenCollapsible(false);
  mainVerticalSplitter->setOpaqueResize(true);

  // Center Zone: Source Code (Left) & Disassembly View (Right) side-by-side
  auto* masterHorizontalSplitter = new QSplitter(Qt::Horizontal, mainVerticalSplitter);
  masterHorizontalSplitter->setObjectName("MasterHorizontalSplitter");
  masterHorizontalSplitter->setContentsMargins(0, 0, 0, 0);
  masterHorizontalSplitter->setHandleWidth(4);
  masterHorizontalSplitter->setChildrenCollapsible(false);
  masterHorizontalSplitter->setOpaqueResize(true);

  // Instantiate multi-tab source code editor manager.
  m_editorTabManager = new EditorTabManager(masterHorizontalSplitter);
  m_editorTabManager->setMinimumWidth(120);
  m_editorTabManager->setMaximumWidth(QWIDGETSIZE_MAX);
  m_editorTabManager->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_editorTabManager->setDocumentMode(true);
  m_editorTabManager->setContentsMargins(0, 0, 0, 0);
  if (m_editorTabManager->tabBar()) {
    m_editorTabManager->tabBar()->setUsesScrollButtons(true);
    m_editorTabManager->tabBar()->setElideMode(Qt::ElideNone);
  }
  connect(m_editorTabManager, &EditorTabManager::toggleBreakpointRequested, this, [this](const QString& loc) {
    if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) gdbCoord->insertBreakpoint(loc);
  });
  connect(m_editorTabManager, &EditorTabManager::breakpointToggled, this,
          [this](const QString& file, int line, bool isSet, const QString& condition) {
            const QString absoluteFilePath = QFileInfo(file).absoluteFilePath();
            if (!isSet) m_persistentBreakpoints[absoluteFilePath].remove(line);
            else m_persistentBreakpoints[absoluteFilePath].insert(line);
            gridlock::core::ConfigManager::instance().saveBreakpoints(m_persistentBreakpoints);
            if (m_disassemblyView) {
              const uint64_t bpAddr = sourceLineToAddress(line);
              if (bpAddr != 0ULL && m_disassemblyView->isBookmarked(bpAddr) != isSet) m_disassemblyView->toggleBookmark(bpAddr);
            }
            if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
              gdbCoord->broadcastBreakpoint(absoluteFilePath, line, isSet, condition);
            } else if (auto* dapCoord = dynamic_cast<DapCoordinator*>(m_coordinator)) {
              dapCoord->toggleBreakpoint(absoluteFilePath, line);
            }
          });
  connect(m_editorTabManager, &EditorTabManager::hoverVariableRequested, this,
          [this](const QString& varName, const QPoint& globalPos) {
            if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
              gdbCoord->evaluateHoverVariable(m_focusedRank, varName, globalPos);
            }
          });
  connect(m_editorTabManager, &EditorTabManager::hideHoverTooltip, this, &MainWindow::hideHoverTooltipPopup);

  // Right Inspection Zone: Variables, Registers, Watch Expressions, and Server Rack
  m_variablesDockWidget = new VariablesDockWidget(this);
  m_variablesDockWidget->setObjectName("VariablesWidget");
  connect(m_variablesDockWidget, &VariablesDockWidget::plotArrayRequested, this,
          [this](const QString& expr, const QString& typ) { openArrayPlotter(expr, typ); });
  connect(m_variablesDockWidget, &VariablesDockWidget::visualizeInPointerGraphRequested, this,
          [this](const QString& expr, const QString&, const QString& val, const QString& typ) { openPointerGraphVisualizer(expr, val, typ); });
  auto* variablesDock = new QDockWidget("Variables", this);
  variablesDock->setObjectName("VariablesDock");
  variablesDock->setWidget(m_variablesDockWidget);
  setupDockTitleBar(variablesDock, QStringLiteral("VARIABLES"));
  m_dockHomeSlots.insert(variablesDock, {nullptr, -1, Qt::RightDockWidgetArea});

  m_registerView = new RegisterView(this);
  m_registerView->setObjectName("RegisterViewWidget");
  connect(m_registerView, &RegisterView::addressActivated, this,
          [this](uint64_t addr) { seekToAddressOrExpression(QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0'))); });
  auto* registersDock = new QDockWidget("Registers", this);
  registersDock->setObjectName("RegisterViewDock");
  registersDock->setWidget(m_registerView);
  setupDockTitleBar(registersDock, QStringLiteral("REGISTERS"));
  m_dockHomeSlots.insert(registersDock, {nullptr, -1, Qt::RightDockWidgetArea});

  // Differential Grid: Cross-rank tabular matrix comparing watched variable values.
  m_differentialGrid = new DifferentialGrid(this);
  m_differentialGrid->setObjectName("DifferentialGrid");
  m_differentialGrid->verticalHeader()->setDefaultSectionSize(20);
  m_differentialGrid->horizontalHeader()->setDefaultSectionSize(96);
  connect(m_differentialGrid, &DifferentialGrid::watchVariableAdded, this, [this](const QString& name) {
    if (m_coordinator) m_coordinator->registerWatchVariable(name);
  });
  connect(m_differentialGrid, &DifferentialGrid::watchVariableRemoved, this, [this](const QString& name) {
    if (m_coordinator) m_coordinator->unregisterWatchVariable(name);
  });
  connect(m_differentialGrid, &DifferentialGrid::plotArrayRequested, this, [this](const QString& expr) { openArrayPlotter(expr); });
  connect(m_differentialGrid, &DifferentialGrid::graphPointerRequested, this,
          [this](const QString& expr, const QString& val) { openPointerGraphVisualizer(expr, val); });

  m_differentialGrid->setColumnHidden(0, true);
  m_differentialGrid->setRowHidden(0, true);
  m_differentialGrid->horizontalHeader()->setVisible(false);
  m_differentialGrid->verticalHeader()->setVisible(false);
  m_differentialGrid->horizontalHeader()->setMinimumSectionSize(64);
  m_differentialGrid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  m_differentialGrid->horizontalHeader()->setStretchLastSection(true);

  auto* watchContainer = new QWidget(this);
  auto* watchLayout = new QVBoxLayout(watchContainer);
  watchLayout->setContentsMargins(0, 0, 0, 0);
  watchLayout->setSpacing(0);
  auto* watchToolbar = new QWidget(watchContainer);
  watchToolbar->setObjectName("watchToolbar");
  auto* watchBarLayout = new QHBoxLayout(watchToolbar);
  watchBarLayout->setContentsMargins(3, 2, 3, 2);
  watchBarLayout->setSpacing(3);
  auto* watchInput = new QLineEdit(watchToolbar);
  watchInput->setObjectName("watchExpressionInput");
  watchInput->setPlaceholderText("New watch expression...");
  watchInput->setFixedHeight(22);
  auto* addWatchBtn = new QPushButton("+ Watch", watchToolbar);
  addWatchBtn->setObjectName("addWatchBtn");
  addWatchBtn->setFixedHeight(22);
  auto submitWatch = [this, watchInput]() {
    if (const QString expr = watchInput->text().trimmed(); !expr.isEmpty() && m_differentialGrid) {
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
  auto* watchDock = new QDockWidget("WATCH", this);
  watchDock->setObjectName("WatchExpressionsDock");
  watchDock->setWidget(watchContainer);
  setupDockTitleBar(watchDock, QStringLiteral("WATCH"));
  m_dockHomeSlots.insert(watchDock, {nullptr, -1, Qt::RightDockWidgetArea});

  // Server Rack Overview with compact state filter strip (All, Deadlocked/Diverged, Stopped, Running).
  auto* rackContainer = new QWidget(this);
  auto* rackLayout = new QVBoxLayout(rackContainer);
  rackLayout->setContentsMargins(0, 0, 0, 0);
  rackLayout->setSpacing(0);
  auto* rackFilterBar = new QWidget(rackContainer);
  rackFilterBar->setObjectName(QStringLiteral("serverRackFilterBar"));
  auto* rackFilterLay = new QHBoxLayout(rackFilterBar);
  rackFilterLay->setContentsMargins(3, 2, 3, 2);
  rackFilterLay->setSpacing(3);
  auto* rackFilterGrp = new QButtonGroup(rackFilterBar);
  rackFilterGrp->setExclusive(true);
  m_serverRackView = new ServerRackView(rackContainer);
  m_serverRackView->setObjectName("ServerRackViewWidget");
  auto addRackFilterBtn = [&](const QString& label, const QString& objName, const QString& filterKey,
                              const QString& tip, bool checked) {
    auto* b = new QToolButton(rackFilterBar);
    b->setObjectName(objName); b->setText(label); b->setToolTip(tip);
    b->setCheckable(true); b->setChecked(checked); b->setFixedHeight(20); b->setAutoRaise(false);
    rackFilterGrp->addButton(b); rackFilterLay->addWidget(b);
    connect(b, &QToolButton::toggled, this, [this, filterKey](bool on) {
      if (on && m_serverRackView) m_serverRackView->setStateFilter(filterKey);
    });
  };
  addRackFilterBtn(QStringLiteral("All"), QStringLiteral("serverRackFilterAllBtn"), QStringLiteral("all"), QStringLiteral("Show all MPI ranks"), true);
  addRackFilterBtn(QStringLiteral("Deadlock"), QStringLiteral("serverRackFilterDeadlockBtn"), QStringLiteral("deadlock"), QStringLiteral("Show deadlocked or diverged ranks"), false);
  addRackFilterBtn(QStringLiteral("Stopped"), QStringLiteral("serverRackFilterStoppedBtn"), QStringLiteral("stopped"), QStringLiteral("Show paused / breakpoint-stopped ranks"), false);
  addRackFilterBtn(QStringLiteral("Running"), QStringLiteral("serverRackFilterRunningBtn"), QStringLiteral("running"), QStringLiteral("Show actively running ranks"), false);
  rackFilterLay->addStretch(1);
  rackLayout->addWidget(rackFilterBar);
  rackLayout->addWidget(m_serverRackView, 1);
  connect(m_serverRackView, &ServerRackView::ranksUpdated, this, [this](const QMap<int, uint64_t>& rankPcMap) {
    if (m_disassemblyView) m_disassemblyView->setRankAddresses(rankPcMap);
  });
  connect(m_serverRackView, &ServerRackView::rankSelected, this, [this](int rankId, uint64_t pcAddr) {
    onRankSelected(rankId);
    if (m_disassemblyView) m_disassemblyView->setSelectedRank(rankId);
    if (pcAddr != 0ULL) seekToAddressOrExpression(QStringLiteral("0x%1").arg(pcAddr, 8, 16, QChar('0')));
    else updateStatusTelemetry();
  });
  auto* serverRackDock = new QDockWidget("Server Rack", this);
  serverRackDock->setObjectName("ServerRackDock");
  serverRackDock->setWidget(rackContainer);
  setupDockTitleBar(serverRackDock, QStringLiteral("SERVER RACK"));
  m_dockHomeSlots.insert(serverRackDock, {nullptr, -1, Qt::RightDockWidgetArea});

  // Consolidate right sidebar into two balanced vertical zones.
  addDockWidget(Qt::RightDockWidgetArea, variablesDock);
  splitDockWidget(variablesDock, serverRackDock, Qt::Vertical);
  tabifyDockWidget(variablesDock, registersDock);
  tabifyDockWidget(registersDock, watchDock);
  variablesDock->raise();
  resizeDocks({variablesDock, serverRackDock}, {520, 380}, Qt::Vertical);
  resizeDocks({variablesDock}, {340}, Qt::Horizontal);
  for (auto* tb : findChildren<QTabBar*>()) {
    tb->setUsesScrollButtons(false);
    tb->setElideMode(Qt::ElideNone);
    tb->setExpanding(false);
  }

  connect(m_editorTabManager, &EditorTabManager::pinVariableRequested, this, [this, watchDock](const QString& varName) {
    if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) gdbCoord->registerWatchVariable(varName);
    if (m_differentialGrid) m_differentialGrid->addVariableColumn(varName);
    if (watchDock) { watchDock->show(); watchDock->raise(); }
  });

  // Disassembly & CFG Graph View: Dual-mode linear assembly and basic-block graph renderer.
  m_disassemblyView = new DisassemblyView(masterHorizontalSplitter);
  m_disassemblyView->setMinimumWidth(120);
  m_disassemblyView->setMaximumWidth(QWIDGETSIZE_MAX);
  m_disassemblyView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_disassemblyView->setContentsMargins(0, 0, 0, 0);
  connect(m_disassemblyView, &DisassemblyView::addressSelected, this, [this](uint64_t addr) {
    seekToAddressOrExpression(QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')));
    if (SourceCodeView* srcView = sourceCodeView()) srcView->highlightExecutionLine(addressToSourceLine(addr));
  });
  auto savedInstBytes = std::make_shared<QMap<uint64_t, QByteArray>>();
  connect(m_disassemblyView, &DisassemblyView::instructionPatched, this, [this, savedInstBytes](uint64_t addr, const QByteArray& bytes) {
    if (m_elfInspector && m_elfInspector->isValid()) {
      if (!savedInstBytes->contains(addr)) savedInstBytes->insert(addr, m_elfInspector->readVirtualMemory(addr, bytes.size()));
      m_elfInspector->writeVirtualMemory(addr, bytes);
    }
    if (m_coordinator && (m_debugSessionActive || m_coordinator->getProcessCount() > 0)) {
      m_coordinator->writeMemory(activeSteppingRank(), addr, bytes);
    }
  });
  connect(m_disassemblyView, &DisassemblyView::patchesReverted, this, [this, savedInstBytes]() {
    for (auto it = savedInstBytes->cbegin(); it != savedInstBytes->cend(); ++it) {
      if (it.value().isEmpty()) continue;
      if (m_elfInspector && m_elfInspector->isValid()) m_elfInspector->writeVirtualMemory(it.key(), it.value());
      if (m_coordinator && (m_debugSessionActive || m_coordinator->getProcessCount() > 0)) {
        m_coordinator->writeMemory(activeSteppingRank(), it.key(), it.value());
      }
    }
    savedInstBytes->clear();
  });

  masterHorizontalSplitter->addWidget(m_editorTabManager);
  masterHorizontalSplitter->addWidget(m_disassemblyView);

  // Synchronize DisassemblyView decompiler suppression whenever Source Editor is shown or hidden.
  class SourceEditorVisibilitySync final : public QObject {
   public:
    SourceEditorVisibilitySync(QWidget* ed, DisassemblyView* dv) : QObject(ed), m_ed(ed), m_dv(dv) {}
    bool eventFilter(QObject* w, QEvent* ev) override {
      if (w == m_ed && m_dv && (ev->type() == QEvent::Show || ev->type() == QEvent::ShowToParent ||
                                ev->type() == QEvent::Hide || ev->type() == QEvent::HideToParent)) {
        m_dv->setSourceEditorActive(!m_ed->isHidden());
        if (auto* mw = qobject_cast<MainWindow*>(m_ed->window())) mw->syncDisassemblyDecompilerVisibility(false);
      }
      return QObject::eventFilter(w, ev);
    }
   private:
    QWidget* m_ed = nullptr;
    DisassemblyView* m_dv = nullptr;
  };
  m_editorTabManager->installEventFilter(new SourceEditorVisibilitySync(m_editorTabManager, m_disassemblyView));
  m_disassemblyView->setSourceEditorActive(!m_editorTabManager->isHidden());

  masterHorizontalSplitter->setStretchFactor(0, 45);
  masterHorizontalSplitter->setStretchFactor(1, 55);
  masterHorizontalSplitter->setSizes({430, 520});
  mainVerticalSplitter->addWidget(masterHorizontalSplitter);

  // Bottom Zone: Left 36px vertical tool-window icon rail paired with linked contextual sub-tabs.
  auto* bottomDockContainer = new QWidget(mainVerticalSplitter);
  bottomDockContainer->setObjectName(QStringLiteral("bottomDockContainer"));
  bottomDockContainer->setMinimumHeight(180);
  auto* bottomRowLayout = new QHBoxLayout(bottomDockContainer);
  bottomRowLayout->setContentsMargins(0, 0, 0, 0);
  bottomRowLayout->setSpacing(0);

  m_bottomToolRail = new QWidget(bottomDockContainer);
  m_bottomToolRail->setObjectName(QStringLiteral("bottomToolWindowRail"));
  m_bottomToolRail->setFixedWidth(36);
  m_bottomToolRail->setAttribute(Qt::WA_StyledBackground, true);

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

  m_terminalDock = new TerminalDockWidget("Terminal", m_bottomTabs);
  m_terminalDock->setObjectName("TerminalDock");

  m_expressionEvaluatorWidget = new ExpressionEvaluatorWidget(m_bottomTabs);
  m_expressionEvaluatorWidget->setObjectName("ExpressionEvaluatorDock");
  auto* emptyEvalTitle = new QWidget(m_expressionEvaluatorWidget);
  emptyEvalTitle->setFixedSize(0, 0);
  m_expressionEvaluatorWidget->setTitleBarWidget(emptyEvalTitle);
  connect(m_expressionEvaluatorWidget, &ExpressionEvaluatorWidget::evaluateRequested, this,
          [this](const QString& expr) { if (m_coordinator) m_coordinator->evaluateExpression(m_focusedRank, expr); });

  m_referenceManualWidget = new ReferenceManualWidget(m_bottomTabs);
  m_referenceManualWidget->setObjectName("ReferenceManualWidget");

  m_gdbConsoleWidget = new GdbConsoleWidget(m_bottomTabs);
  m_gdbConsoleWidget->setObjectName("GdbConsoleWidget");

  m_memView = new MemView(m_bottomTabs);
  m_memView->setObjectName("MemViewDock");

  m_spackManager = new HpcConsoleWidget(m_hpcBackend, m_bottomTabs);
  m_spackManager->setObjectName("HpcConsoleDock");

  connect(m_memView, &MemView::requestMemory, this, [this](const QString& address, int length) {
    seekToAddressOrExpression(address);
    if (m_coordinator) m_coordinator->readMemory(m_focusedRank, address, length);
  });
  connect(m_memView, &MemView::bytesPatched, this, [this](uint64_t addr, const QByteArray& bytes) {
    if (m_elfInspector && m_elfInspector->isValid()) m_elfInspector->writeVirtualMemory(addr, bytes);
    if (m_coordinator && (m_debugSessionActive || m_coordinator->getProcessCount() > 0)) {
      m_coordinator->writeMemory(activeSteppingRank(), addr, bytes);
    }
  });
  connect(m_memView, &MemView::requestMemoryDiff, this,
          [this](int baseRank, int targetRank, const QString& addrStr, int length) {
            if (!m_coordinator) return;
            const pid_t basePid = m_coordinator->getPidForRank(baseRank);
            const pid_t targetPid = m_coordinator->getPidForRank(targetRank);
            bool ok = false;
            const quint64 addrInt = addrStr.toULongLong(&ok, 16);
            if (!ok) return;
            void* remoteAddr = reinterpret_cast<void*>(addrInt);
            QPointer<MainWindow> weakThis(this);
            (void)QtConcurrent::run([weakThis, basePid, targetPid, remoteAddr, length]() {
              auto result = gridlock::core::hpc::MemoryDiffer::compareMemory(basePid, targetPid, remoteAddr, length);
              QMetaObject::invokeMethod(weakThis, [weakThis, result, remoteAddr]() {
                if (weakThis && weakThis->m_memView) weakThis->m_memView->displayMemoryDiff(result, remoteAddr);
              });
            });
          });

  m_mpiDiagnosticsWidget = new MpiDiagnosticsWidget(m_bottomTabs);
  connect(m_mpiDiagnosticsWidget, &MpiDiagnosticsWidget::jumpToFrameRequested, this, &MainWindow::onRankSelected);

  m_bottomTabs->addTab(m_terminalDock, "Terminal");
  m_bottomTabs->addTab(m_gdbConsoleWidget, "GDB Console");
  m_bottomTabs->addTab(m_memView, "Memory View");
  m_bottomTabs->addTab(m_mpiDiagnosticsWidget, "MPI Diagnostics");
  m_bottomTabs->addTab(m_spackManager, QStringLiteral("Package Manager"));
  m_bottomTabs->addTab(m_expressionEvaluatorWidget, "Evaluator");
  m_bottomTabs->addTab(createProgramOutputTab(), QStringLiteral("Program Output"));
  m_bottomTabs->addTab(m_referenceManualWidget, "Reference Manual");
  m_bottomTabs->setCurrentWidget(m_memView);
  connect(m_bottomTabs, &QTabWidget::currentChanged, this, [this](int) {
    QWidget* activeTab = m_bottomTabs ? m_bottomTabs->currentWidget() : nullptr;
    if (activeTab == m_terminalDock && m_terminalDock) m_terminalDock->ensureShellStarted();
    else if (activeTab == m_spackManager && m_spackManager) static_cast<HpcConsoleWidget*>(m_spackManager)->ensurePackagesLoaded();
    else if (activeTab == m_referenceManualWidget && m_referenceManualWidget) m_referenceManualWidget->ensureDocsetsInitialized();
  });

  harmonizeBottomConsoleTabs();
  mainVerticalSplitter->addWidget(bottomDockContainer);
  mainVerticalSplitter->setStretchFactor(0, 68);
  mainVerticalSplitter->setStretchFactor(1, 32);
  mainVerticalSplitter->setSizes({600, 260});
  setCentralWidget(mainVerticalSplitter);

  const QList<QDockWidget*> sideDocks = {
      m_projectExplorerWidget, variablesDock, registersDock, watchDock, serverRackDock};
  for (QDockWidget* dock : sideDocks) {
    if (!dock) continue;
    dock->setMinimumWidth(150);
    dock->setMaximumWidth(QWIDGETSIZE_MAX);
    dock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    if (QWidget* inner = dock->widget()) {
      inner->setMinimumWidth(150);
      inner->setMaximumWidth(QWIDGETSIZE_MAX);
      inner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
  }
  resizeDocks({m_projectExplorerWidget, registersDock}, {215, 255}, Qt::Horizontal);
  resizeDocks({registersDock, watchDock, serverRackDock}, {320, 210, 290}, Qt::Vertical);
  registerViewMenuToolbarAndDockActions();
}

/** @brief Saves the active workspace session and project settings to workspace.toml. */
bool MainWindow::saveSession(bool showStatusMessage) {
  const QString wsRoot = !m_activeWorkspaceDir.isEmpty()
                             ? m_activeWorkspaceDir
                             : (m_projectExplorerWidget ? m_projectExplorerWidget->rootPath() : QDir::currentPath());
  const QString targetPath = !m_activeSessionFilePath.isEmpty()
                                 ? m_activeSessionFilePath
                                 : QDir(wsRoot).filePath(QStringLiteral(".gridlock/workspace.toml"));
  QDir().mkpath(QFileInfo(targetPath).absolutePath());
  gridlock::core::managers::SessionState state;
  if (m_differentialGrid) {
    for (const auto& w : m_differentialGrid->getWatchExpressions()) state.watchedVariables.push_back(QString::fromStdString(w));
  }
  for (auto it = m_persistentBreakpoints.constBegin(); it != m_persistentBreakpoints.constEnd(); ++it) {
    for (int line : *it) state.activeBreakpoints.push_back(QStringLiteral("%1:%2").arg(it.key()).arg(line));
  }
  state.selectedRanks.push_back(m_focusedRank);
  auto& cfg = gridlock::core::ConfigManager::instance();
  cfg.saveProjectSettings(cfg.projectSettings());
  const bool ok = gridlock::core::managers::SessionManager::instance().saveSession(targetPath, state);
  if (ok) {
    m_activeSessionFilePath = targetPath;
    rebuildRecentSessionsMenu();
    if (showStatusMessage && statusBar()) {
      statusBar()->showMessage(QStringLiteral("Session saved to %1").arg(targetPath), 3000);
    }
  }
  return ok;
}

}  // namespace gridlock::ui

#include "widgets/HpcConsoleWidget.cpp"

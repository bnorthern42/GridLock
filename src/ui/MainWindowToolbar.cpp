/**
 * @file MainWindowToolbar.cpp
 * @brief Primary debugger toolbar, Launch Options popover, execution controls, and View menu registration.
 *
 * Implements MainWindow::setupUi, applyCutterWorkspaceLayout, setupToolbar,
 * updateDebugToolbarState, isRunButtonLatched, and View menu toolbar/dock wiring.
 */

#include "MainWindow.hpp"

#include "../core/managers/ConfigManager.hpp"
#include "EditorTabManager.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "utils/FileDialogHelper.hpp"
#include "views/DisassemblyView.hpp"
#include "views/ServerRackView.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

namespace gridlock::ui {

/** @brief Initializes main window geometry, Cutter-style dock layout, and toolbars. */
void MainWindow::setupUi() {
  setWindowTitle("GridLock - MPI Graphical Debugger");
  setWindowIcon(QIcon());
  resize(1440, 900);
  applyCutterWorkspaceLayout();
  setupToolbar();
}

/** @brief Configures nested/tabbed reverse-engineering workstation docking rules. */
void MainWindow::applyCutterWorkspaceLayout() {
  setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
  setDockNestingEnabled(true);
  setDocumentMode(true);
  setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::North);
  setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
  setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
  setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
}

/** @brief Constructs primary debugger control toolbar and secondary address space ribbon. */
void MainWindow::setupToolbar() {
  QToolBar* toolbar = addToolBar("Main Toolbar");
  toolbar->setObjectName("MainToolbar");
  toolbar->setMovable(true);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
  toolbar->setFixedHeight(32);
  toolbar->setIconSize(QSize(10, 10));
  toolbar->setContentsMargins(1, 0, 1, 0);
  toolbar->setStyleSheet(QStringLiteral(
      "#MainToolbar QToolButton, #MainToolbar QPushButton { padding: 1px 2px; margin: 0px; min-width: 0px; }"
      " #MainToolbar QToolButton[popupMode=\"1\"] { padding-right: 16px; }"));
  if (QLayout* tbLayout = toolbar->layout()) tbLayout->setSpacing(0);

  // Target selector + inline ⚙ Launch Options popover (--args, Working Directory, Stop at main()).
  m_targetSelector = new QComboBox(toolbar);
  m_targetSelector->setObjectName(QStringLiteral("toolbarTargetSelector"));
  m_targetSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_targetSelector->setMinimumContentsLength(12);
  m_targetSelector->setMinimumWidth(165);
  m_targetSelector->setMaximumWidth(166);
  m_targetSelector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_targetSelector->setFixedHeight(26);
  if (m_targetSelector->view()) { m_targetSelector->view()->setTextElideMode(Qt::ElideMiddle); m_targetSelector->view()->setMinimumWidth(260); }
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
    const QString full = (raw.isEmpty() || raw == QStringLiteral("__open_binary__"))
                             ? m_targetSelector->itemText(idx)
                             : resolveWorkspacePath(raw);
    m_targetSelector->setToolTip(QStringLiteral("Active Target: %1\nPath: %2")
                                     .arg(m_targetSelector->itemText(idx), full.isEmpty() ? raw : full));
  };
  syncTargetTooltip();
  m_targetSelector->setProperty("gl_confirmed_idx", 0);
  connect(m_targetSelector, &QComboBox::currentIndexChanged, this, [this, syncTargetTooltip](int index) {
    if (!m_targetSelector || index < 0) return;
    const int prevIdx = m_targetSelector->property("gl_confirmed_idx").toInt();
    const QString data = m_targetSelector->itemData(index).toString();
    if (data == QStringLiteral("__open_binary__")) {
      m_targetSelector->blockSignals(true);
      m_targetSelector->setCurrentIndex(prevIdx >= 0 && prevIdx < m_targetSelector->count() ? prevIdx : 0);
      m_targetSelector->blockSignals(false);
      syncTargetTooltip();
      openFile();
    } else if (!data.isEmpty()) {
      if (m_debugSessionActive && !m_switchingWorkspace && index != prevIdx &&
          !property("gl_bypass_confirm").toBool()) {
        if (QMessageBox::warning(
                this, tr("Switch Active Target"),
                tr("A debug session is active on Rank %1.\n\nTerminate the active session and switch target to '%2'?")
                    .arg(activeSteppingRank())
                    .arg(m_targetSelector->itemText(index)),
                QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
          m_targetSelector->blockSignals(true);
          m_targetSelector->setCurrentIndex(prevIdx >= 0 && prevIdx < m_targetSelector->count() ? prevIdx : 0);
          m_targetSelector->blockSignals(false);
          syncTargetTooltip();
          return;
        }
      }
      m_targetSelector->setProperty("gl_confirmed_idx", index);
      selectTargetByName(data);
      syncTargetTooltip();
    }
  });
  toolbar->addWidget(m_targetSelector);

  // Inline ⚙ Launch Options popover bound directly to ProjectSettings (--args, Working Directory, Stop at main()).
  m_launchOptionsBtn = new QToolButton(toolbar);
  m_launchOptionsBtn->setObjectName(QStringLiteral("toolbarLaunchOptionsBtn"));
  m_launchOptionsBtn->setText(QStringLiteral("Config"));
  m_launchOptionsBtn->setPopupMode(QToolButton::InstantPopup);
  m_launchOptionsBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  m_launchOptionsBtn->setFixedHeight(26);
  m_launchOptionsBtn->setToolTip(
      QStringLiteral("Launch Options: Program Arguments (--args), Working Directory, and Entry Breakpoint"));
  auto* launchOptMenu = new QMenu(m_launchOptionsBtn);
  launchOptMenu->setObjectName(QStringLiteral("toolbarLaunchOptionsMenu"));
  auto* launchPanel = new QWidget(launchOptMenu);
  launchPanel->setMinimumWidth(320);
  launchPanel->setStyleSheet(QStringLiteral(
      "QWidget { background: #1e222a; color: #d7dae0; }"
      "QLineEdit { background: #15181e; color: #e5e9f0; border: 1px solid #353b45; border-radius: 3px; padding: 2px 6px; min-height: 22px; }"
      "QPushButton { background: #282c34; color: #d7dae0; border: 1px solid #3b4048; border-radius: 3px; padding: 2px 8px; min-height: 22px; }"
      "QCheckBox { spacing: 6px; }"));
  auto* lpForm = new QFormLayout(launchPanel);
  lpForm->setContentsMargins(10, 8, 10, 8);
  lpForm->setSpacing(6);
  m_launchOptionsArgsEdit = new QLineEdit(launchPanel);
  m_launchOptionsArgsEdit->setObjectName(QStringLiteral("launchOptionsArgsEdit"));
  m_launchOptionsArgsEdit->setPlaceholderText(QStringLiteral("--nx 64 --tol 1e-6"));
  lpForm->addRow(QStringLiteral("Program Arguments (--args):"), m_launchOptionsArgsEdit);
  auto* wdRow = new QWidget(launchPanel);
  auto* wdHb = new QHBoxLayout(wdRow);
  wdHb->setContentsMargins(0, 0, 0, 0);
  wdHb->setSpacing(4);
  m_launchOptionsWorkDirEdit = new QLineEdit(wdRow);
  m_launchOptionsWorkDirEdit->setObjectName(QStringLiteral("launchOptionsWorkDirEdit"));
  m_launchOptionsWorkDirEdit->setPlaceholderText(QStringLiteral("Workspace root (default)"));
  auto* wdBrowseBtn = new QPushButton(QStringLiteral("..."), wdRow);
  wdBrowseBtn->setObjectName(QStringLiteral("launchOptionsWorkDirBrowseBtn"));
  wdBrowseBtn->setFixedWidth(28);
  wdHb->addWidget(m_launchOptionsWorkDirEdit, 1);
  wdHb->addWidget(wdBrowseBtn);
  lpForm->addRow(QStringLiteral("Working Directory:"), wdRow);
  m_launchOptionsStopAtMainCheck = new QCheckBox(QStringLiteral("Stop at main()"), launchPanel);
  m_launchOptionsStopAtMainCheck->setObjectName(QStringLiteral("launchOptionsStopAtMainCheck"));
  m_launchOptionsStopAtMainCheck->setChecked(m_stopAtMainOnLaunch);
  m_launchOptionsStopAtMainCheck->setToolTip(
      QStringLiteral("Automatically insert a temporary breakpoint at main() when no user breakpoints are set"));
  lpForm->addRow(QString(), m_launchOptionsStopAtMainCheck);
  auto* launchWidgetAct = new QWidgetAction(launchOptMenu);
  launchWidgetAct->setDefaultWidget(launchPanel);
  launchOptMenu->addAction(launchWidgetAct);
  m_launchOptionsBtn->setMenu(launchOptMenu);

  auto persistLaunchOpts = [this]() {
    auto& cfg = gridlock::core::ConfigManager::instance();
    auto ps = cfg.loadProjectSettings();
    ps.targetArguments = m_launchOptionsArgsEdit ? m_launchOptionsArgsEdit->text().trimmed() : QString();
    ps.programArguments = ps.targetArguments.toStdString();
    ps.workingDirectory = (m_launchOptionsWorkDirEdit ? m_launchOptionsWorkDirEdit->text().trimmed() : QString()).toStdString();
    cfg.saveProjectSettings(ps);
    if (!ps.workingDirectory.empty() && m_terminalDock && QDir(QString::fromStdString(ps.workingDirectory)).exists()) {
      m_terminalDock->setWorkingDirectory(QString::fromStdString(ps.workingDirectory));
    }
    syncCoordinatorWithConfig();
  };
  connect(launchOptMenu, &QMenu::aboutToShow, this, [this]() {
    const auto ps = gridlock::core::ConfigManager::instance().loadProjectSettings();
    const QString args = !ps.targetArguments.isEmpty() ? ps.targetArguments : QString::fromStdString(ps.programArguments);
    const QString wd = !ps.workingDirectory.empty() ? QString::fromStdString(ps.workingDirectory) : m_activeWorkspaceDir;
    if (m_launchOptionsArgsEdit) { QSignalBlocker b(m_launchOptionsArgsEdit); m_launchOptionsArgsEdit->setText(args); }
    if (m_launchOptionsWorkDirEdit) { QSignalBlocker b(m_launchOptionsWorkDirEdit); m_launchOptionsWorkDirEdit->setText(wd); }
    if (m_launchOptionsStopAtMainCheck) { QSignalBlocker b(m_launchOptionsStopAtMainCheck); m_launchOptionsStopAtMainCheck->setChecked(m_stopAtMainOnLaunch); }
  });
  for (QLineEdit* edit : {m_launchOptionsArgsEdit, m_launchOptionsWorkDirEdit}) {
    connect(edit, &QLineEdit::textEdited, this, [persistLaunchOpts](const QString&) { persistLaunchOpts(); });
    connect(edit, &QLineEdit::editingFinished, this, persistLaunchOpts);
  }
  connect(wdBrowseBtn, &QPushButton::clicked, this, [this, persistLaunchOpts]() {
    const QString cur = (m_launchOptionsWorkDirEdit && !m_launchOptionsWorkDirEdit->text().trimmed().isEmpty())
                            ? m_launchOptionsWorkDirEdit->text().trimmed() : m_activeWorkspaceDir;
    const QString picked = FileDialogHelper::getExistingDirectory(this, QStringLiteral("Select Working Directory"), cur);
    if (!picked.isEmpty() && m_launchOptionsWorkDirEdit) { m_launchOptionsWorkDirEdit->setText(picked); persistLaunchOpts(); }
  });
  connect(m_launchOptionsStopAtMainCheck, &QCheckBox::toggled, this, &MainWindow::setStopAtMainOnLaunch);
  toolbar->addWidget(m_launchOptionsBtn)->setToolTip(m_launchOptionsBtn->toolTip());

  // Compact self-labeled MPI rank count spin box ("np: 8").
  m_rankCountSpin = new QSpinBox(toolbar);
  m_rankCountSpin->setObjectName(QStringLiteral("toolbarRankSpin"));
  m_rankCountSpin->setPrefix(QStringLiteral("np: "));
  m_rankCountSpin->setRange(1, 256);
  m_rankCountSpin->setValue(8);
  m_rankCountSpin->setMinimumWidth(80);
  m_rankCountSpin->setMaximumWidth(80);
  m_rankCountSpin->setFixedHeight(26);
  m_rankCountSpin->setFont(selectorFont);
  m_rankCountSpin->setToolTip(QStringLiteral("MPI Rank Count (-np): number of parallel processes spawned when launching the target"));
  connect(m_rankCountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
    if (!m_debugSessionActive && !m_previewFixtureOnly && m_serverRackView) m_serverRackView->setSessionIdle(val);
    syncRankCountChrome(val);
    syncExecutionScopeUi();
  });
  toolbar->addWidget(m_rankCountSpin);
  toolbar->addSeparator();

  auto makeDbgAct = [&](const QString& text, const QString& obj, const QString& iconText,
                        const QKeySequence& ks, auto slot, bool addToBar = true) {
    auto* a = new QAction(text, this);
    a->setObjectName(obj);
    if (!iconText.isEmpty()) a->setIconText(iconText);
    if (!ks.isEmpty()) a->setShortcut(ks);
    connect(a, &QAction::triggered, this, slot);
    if (addToBar) toolbar->addAction(a);
    return a;
  };

  // Compact Build / Clean / Rebuild split button (#toolbarBuildSplitBtn).
  auto* toolbarBuildAct = makeDbgAct(QStringLiteral("Build Target"), QString(), QStringLiteral("⚙ Build"),
                                     QKeySequence(), &MainWindow::buildTargetWorkflow, false);
  toolbarBuildAct->setToolTip(QStringLiteral("Build Active Target (Ctrl+B) — click arrow for Clean / Rebuild"));
  auto* buildSplitBtn = new QToolButton(toolbar);
  buildSplitBtn->setObjectName(QStringLiteral("toolbarBuildSplitBtn"));
  buildSplitBtn->setPopupMode(QToolButton::MenuButtonPopup);
  buildSplitBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
  buildSplitBtn->setDefaultAction(toolbarBuildAct);
  auto* buildMenu = new QMenu(buildSplitBtn);
  buildMenu->addAction(QStringLiteral("Build Target (Ctrl+B)"), this, &MainWindow::buildTargetWorkflow);
  buildMenu->addAction(QStringLiteral("Clean Target"), this, &MainWindow::cleanTargetWorkflow);
  buildMenu->addAction(QStringLiteral("Rebuild Target"), this, &MainWindow::rebuildTargetWorkflow);
  buildSplitBtn->setMenu(buildMenu);
  toolbar->addWidget(buildSplitBtn);

  // Run Action: Compiles active target on-demand, latches Running state only on valid launch, and pauses at main() or first BP.
  m_runAction = makeDbgAct(QStringLiteral("▶ Run Target"), QStringLiteral("actionRunTarget"), QStringLiteral("▶ Run"),
                           QKeySequence(), [this]() {
                             m_sessionRestoredEmptyBps = false;
                             if (!m_previewFixtureOnly && !ensureTargetBinaryCompiled()) { updateDebugToolbarState(); return; }
                             startTargetWorkflow();
                             updateDebugToolbarState();
                           });
  m_runAction->setCheckable(true);
  if (QWidget* runBtn = toolbar->widgetForAction(m_runAction)) runBtn->setObjectName(QStringLiteral("toolbarRunTargetBtn"));

  // Multi-rank execution scope pill: All Ranks vs. Focused Rank k (shown automatically when ranks > 1).
  m_executionScopeCombo = new QComboBox(toolbar);
  m_executionScopeCombo->setObjectName(QStringLiteral("toolbarExecutionScopeCombo"));
  m_executionScopeCombo->setFont(selectorFont);
  m_executionScopeCombo->setMinimumWidth(115);
  m_executionScopeCombo->setMaximumWidth(115);
  m_executionScopeCombo->setFixedHeight(26);
  m_executionScopeCombo->addItem(QStringLiteral("All Ranks"), -1);
  m_executionScopeCombo->addItem(QStringLiteral("Rank 0"), 0);
  m_executionScopeCombo->setCurrentIndex(0);
  connect(m_executionScopeCombo, &QComboBox::currentIndexChanged, this, [this](int) { syncExecutionScopeUi(); });
  m_executionScopeAction = toolbar->addWidget(m_executionScopeCombo);
  m_executionScopeAction->setVisible(false);

  // Continue (F5), Step Over (F10), Step Into (F11 + stepi/nexti menu), Step Out (Shift+F11), Pause, Pause All (F6), Stop (Shift+F5).
  m_continueAction = makeDbgAct(QStringLiteral("Continue"), QStringLiteral("debug.continue"), QStringLiteral("Cont"),
                                QKeySequence(Qt::Key_F5), &MainWindow::continueExecutionWorkflow);
  if (style()) m_continueAction->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
  if (auto* contBtn = qobject_cast<QToolButton*>(toolbar->widgetForAction(m_continueAction))) {
    contBtn->setObjectName(QStringLiteral("toolbarContinueBtn"));
    contBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    contBtn->setStyleSheet(QStringLiteral("QToolButton { padding: 2px 4px; }"));
  }
  connect(m_continueAction, &QAction::changed, this, &MainWindow::syncExecutionScopeUi);
  m_stepOverAction = makeDbgAct(QStringLiteral("Step Over"), QStringLiteral("debug.step_over"), QStringLiteral("↷ Over"),
                                QKeySequence(Qt::Key_F10), &MainWindow::stepOverWorkflow);
  m_stepIntoAction = makeDbgAct(QStringLiteral("Step Into"), QStringLiteral("debug.step_into"), QStringLiteral("↳ Into"),
                                QKeySequence(Qt::Key_F11), &MainWindow::stepIntoWorkflow);
  m_stepAction = makeDbgAct(QStringLiteral("Step Instruction (stepi)"), QStringLiteral("actionStepInstruction"),
                            QString(), QKeySequence(), &MainWindow::stepInstructionWorkflow, false);
  if (auto* intoBtn = qobject_cast<QToolButton*>(toolbar->widgetForAction(m_stepIntoAction))) {
    intoBtn->setObjectName(QStringLiteral("toolbarStepIntoBtn"));
    intoBtn->setPopupMode(QToolButton::MenuButtonPopup);
    auto* stepMenu = new QMenu(intoBtn);
    stepMenu->addAction(m_stepAction);
    QAction* nextiAct = stepMenu->addAction(QStringLiteral("Step Over Instruction (nexti) (Ctrl+Shift+F10)"), this,
                                            &MainWindow::nextInstructionWorkflow);
    nextiAct->setObjectName(QStringLiteral("actionToolbarNexti"));
    intoBtn->setMenu(stepMenu);
  }
  m_stepOutAction = makeDbgAct(QStringLiteral("Step Out"), QStringLiteral("debug.step_out"), QStringLiteral("↱ Out"),
                               QKeySequence(Qt::SHIFT | Qt::Key_F11), &MainWindow::stepOutWorkflow);
  m_pauseAction = makeDbgAct(QStringLiteral("Pause"), QStringLiteral("actionPauseRank"), QStringLiteral("⏸"),
                             QKeySequence(), &MainWindow::pauseRankWorkflow);
  m_pauseAllAction = makeDbgAct(QStringLiteral("Pause All Ranks"), QStringLiteral("actionPauseAllRanks"),
                                QStringLiteral("⏸All"), QKeySequence(Qt::Key_F6), &MainWindow::pauseAllRanksWorkflow, false);
  auto* pauseAllBtn = new QToolButton(toolbar);
  pauseAllBtn->setObjectName(QStringLiteral("toolbarPauseAllRanksBtn"));
  pauseAllBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
  pauseAllBtn->setDefaultAction(m_pauseAllAction);
  QAction* pauseAllTbAct = toolbar->addWidget(pauseAllBtn);
  pauseAllTbAct->setObjectName(QStringLiteral("actionToolbarPauseAllRanks"));
  pauseAllTbAct->setVisible(false);
  m_stopAction = makeDbgAct(QStringLiteral("Stop Session"), QStringLiteral("debug.stop"), QStringLiteral("■ Stop"),
                            QKeySequence(Qt::SHIFT | Qt::Key_F5), &MainWindow::stopTargetWorkflow);

  // Time-travel Record button next to stepping group (Step Back & Rev Continue stay hidden until recording is active).
  auto* recBtn = new QPushButton(QStringLiteral("⏺ Rec"), toolbar);
  recBtn->setObjectName(QStringLiteral("toolbarRecordHistoryBtn"));
  recBtn->setCheckable(true);
  auto* sbBtn = new QPushButton(QStringLiteral("↶ Back"), toolbar);
  sbBtn->setObjectName(QStringLiteral("toolbarStepBackBtn"));
  auto* rcBtn = new QPushButton(QStringLiteral("⏮ Rev"), toolbar);
  rcBtn->setObjectName(QStringLiteral("toolbarReverseContinueBtn"));
  toolbar->addWidget(recBtn);
  QAction* sbAct = toolbar->addWidget(sbBtn);
  sbAct->setObjectName(QStringLiteral("actionToolbarStepBack"));
  sbAct->setVisible(false);
  QAction* rcAct = toolbar->addWidget(rcBtn);
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
  syncExecutionScopeUi();
  for (QToolButton* btn : toolbar->findChildren<QToolButton*>()) { btn->setFixedHeight(26); btn->setAttribute(Qt::WA_AlwaysShowToolTips, true); }
  for (QPushButton* btn : toolbar->findChildren<QPushButton*>()) { btn->setFixedHeight(26); btn->setAttribute(Qt::WA_AlwaysShowToolTips, true); }

  // Expanding spacer separating execution controls from right-aligned navigation & seek controls.
  auto* spacer = new QWidget(toolbar);
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  spacer->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
  toolbar->addWidget(spacer);

  // History back/forward navigation buttons (< / >) and Alt+Left/Right shortcuts.
  auto makeNavBtn = [&](const QString& label, const QString& obj, const QString& actObj, const QString& tip, auto slot) {
    auto* btn = new QPushButton(label, toolbar);
    btn->setObjectName(obj);
    btn->setFixedSize(20, 26);
    btn->setEnabled(false);
    btn->setAttribute(Qt::WA_AlwaysShowToolTips, true);
    btn->setToolTip(tip);
    connect(btn, &QPushButton::clicked, this, slot);
    QAction* act = toolbar->addWidget(btn);
    act->setObjectName(actObj);
    act->setVisible(false);
    return btn;
  };
  m_navBackBtn = makeNavBtn(QStringLiteral("<"), QStringLiteral("navBackButton"), QStringLiteral("actionToolbarNavBack"),
                            QStringLiteral("Navigate Back in Address History (Alt+Left)"), &MainWindow::navigateBack);
  m_navForwardBtn = makeNavBtn(QStringLiteral(">"), QStringLiteral("navForwardButton"), QStringLiteral("actionToolbarNavForward"),
                               QStringLiteral("Navigate Forward in Address History (Alt+Right)"), &MainWindow::navigateForward);
  connect(new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this), &QShortcut::activated, this, &MainWindow::navigateBack);
  connect(new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this), &QShortcut::activated, this, &MainWindow::navigateForward);

  // Global Search Everywhere pill button (#toolbarSearchEverywhereBtn) advertising Double Shift / Ctrl+P.
  auto* searchEverywhereBtn = new QPushButton(QStringLiteral("⌕"), toolbar);
  searchEverywhereBtn->setObjectName(QStringLiteral("toolbarSearchEverywhereBtn"));
  searchEverywhereBtn->setFixedSize(22, 26);
  searchEverywhereBtn->setAttribute(Qt::WA_AlwaysShowToolTips, true);
  searchEverywhereBtn->setToolTip(QStringLiteral("Search Everywhere (Double Shift / Ctrl+P)"));
  connect(searchEverywhereBtn, &QPushButton::clicked, this, [this]() { openSearchEverywhere(0); });
  toolbar->addWidget(searchEverywhereBtn);

  m_seekBar = new QLineEdit(toolbar);
  m_seekBar->setObjectName("cutterSeekBar");
  m_seekBar->setPlaceholderText(QStringLiteral("Search Everywhere (Double Shift / Ctrl+P)"));
  m_seekBar->setToolTip(QStringLiteral("Search Everywhere (Double Shift / Ctrl+P) — or Go to Address / Symbol (Ctrl+G)"));
  m_seekBar->setFixedHeight(26);
  m_seekBar->setFixedWidth(40);
  QAction* seekAct = toolbar->addWidget(m_seekBar);
  seekAct->setObjectName(QStringLiteral("actionToolbarSeekBar"));
  seekAct->setVisible(false);
  connect(m_seekBar, &QLineEdit::returnPressed, this, [this]() { seekToAddressOrExpression(m_seekBar->text()); });

  // Secondary toolbar break for interactive address space navigation ribbon (hidden until Binary perspective / DisassemblyView).
  addToolBarBreak(Qt::TopToolBarArea);
  QToolBar* navToolbar = addToolBar("Address Space Navigation");
  navToolbar->setObjectName("AddressNavToolbar");
  navToolbar->setMovable(true);
  navToolbar->setFloatable(false);
  navToolbar->setFixedHeight(22);
  navToolbar->setContentsMargins(0, 0, 0, 0);
  m_addressNavBar = createAddressSpaceNavBar(this, navToolbar);
  m_addressNavBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  new DynamicAddressRibbonFilter(m_addressNavBar, [this](uint64_t addr) {
    seekToAddressOrExpression(QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0')));
  });
  navToolbar->addWidget(m_addressNavBar);
  navToolbar->setVisible(false);
}

/** @brief Synchronizes #MainToolbar action enablement, state-aware tooltips, and reverse-debug controls. */
void MainWindow::updateDebugToolbarState() {
  const bool active = m_debugSessionActive;
  const QString idleWhy = QStringLiteral(" — unavailable while idle (click ▶ Run first)");
  if (m_runAction) {
    m_runAction->setEnabled(true);
    m_runAction->setCheckable(true);
    m_runAction->setChecked(active);
    m_runAction->setText(active ? QStringLiteral("● Running") : QStringLiteral("▶ Run Target"));
    m_runAction->setIconText(active ? QStringLiteral("● Running") : QStringLiteral("▶ Run"));
    m_runAction->setToolTip(active ? QStringLiteral("Target Session Active — click ■ Stop (Shift+F5) to end session")
                                   : QStringLiteral("Run Target (Alt+R) — compile if needed and launch MPI debug session"));
  }
  auto syncAct = [&](QAction* a, const QString& onTip, const QString& name) {
    if (a) { a->setEnabled(active); a->setToolTip(active ? onTip : (name + idleWhy)); }
  };
  syncAct(m_continueAction, QStringLiteral("Continue Execution (F5) — resume suspended ranks until the next breakpoint"), QStringLiteral("Continue Execution (F5)"));
  syncAct(m_stepOverAction, QStringLiteral("Step Over (F10) — execute current source line without entering callees"), QStringLiteral("Step Over (F10)"));
  syncAct(m_stepIntoAction, QStringLiteral("Step Into (F11) — step into called function (click arrow for stepi / nexti)"), QStringLiteral("Step Into (F11)"));
  syncAct(m_stepOutAction, QStringLiteral("Step Out (Shift+F11) — finish current stack frame and pause in caller"), QStringLiteral("Step Out (Shift+F11)"));
  syncAct(m_stepAction, QStringLiteral("Step Instruction (stepi) — execute one machine instruction"), QStringLiteral("Step Instruction (stepi)"));
  syncAct(findChild<QAction*>(QStringLiteral("actionNextInstruction")), QStringLiteral("Step Over Instruction (nexti) (Ctrl+Shift+F10) — step machine instruction over CALL"), QStringLiteral("Step Over Instruction (nexti) (Ctrl+Shift+F10)"));
  syncAct(findChild<QAction*>(QStringLiteral("actionToolbarNexti")), QStringLiteral("Step Over Instruction (nexti) (Ctrl+Shift+F10) — step machine instruction over CALL"), QStringLiteral("Step Over Instruction (nexti) (Ctrl+Shift+F10)"));
  syncAct(m_pauseAction, QStringLiteral("Pause Focused Rank — interrupt execution on the active MPI rank"), QStringLiteral("Pause Focused Rank"));
  syncAct(m_stopAction, QStringLiteral("Stop Session (Shift+F5) — terminate active debugging session"), QStringLiteral("Stop Session (Shift+F5)"));
  auto syncBtn = [&](QWidget* w, const QString& onTip, const QString& name) {
    if (w) { w->setEnabled(active); w->setToolTip(active ? onTip : (name + idleWhy)); }
  };
  syncBtn(findChild<QPushButton*>(QStringLiteral("disasmStepiBtn")), QStringLiteral("Step Instruction (stepi) — execute one machine instruction"), QStringLiteral("Step Instruction (stepi)"));
  syncBtn(findChild<QPushButton*>(QStringLiteral("disasmNextiBtn")), QStringLiteral("Next Instruction (nexti) (Ctrl+Shift+F10)"), QStringLiteral("Next Instruction (nexti) (Ctrl+Shift+F10)"));
  syncBtn(findChild<QPushButton*>(QStringLiteral("addHardwareWatchpointBtn")), QStringLiteral("Arm CPU Hardware Data Watchpoint (Write / Read / R/W)"), QStringLiteral("Add Hardware Watchpoint"));
  if (auto* wpCombo = findChild<QComboBox*>(QStringLiteral("watchpointAccessCombo"))) wpCombo->setEnabled(active);
  if (auto* recBtn = findChild<QPushButton*>(QStringLiteral("toolbarRecordHistoryBtn"))) {
    const bool recOn = active && (m_coordinator ? m_coordinator->isReverseRecordingEnabled(activeSteppingRank()) : recBtn->isChecked());
    if (recBtn->isChecked() != recOn) { recBtn->blockSignals(true); recBtn->setChecked(recOn); recBtn->blockSignals(false); }
    recBtn->setEnabled(active);
    recBtn->setToolTip(active ? (recOn ? QStringLiteral("Time-Travel Recording Active — click to stop recording")
                                       : QStringLiteral("Record Execution History — enable reverse stepping on focused rank"))
                              : (QStringLiteral("Record Execution History") + idleWhy));
    if (auto* sbAct = findChild<QAction*>(QStringLiteral("actionToolbarStepBack"))) sbAct->setVisible(recOn);
    if (auto* rcAct = findChild<QAction*>(QStringLiteral("actionToolbarReverseContinue"))) rcAct->setVisible(recOn);
    if (auto* sbBtn = findChild<QPushButton*>(QStringLiteral("toolbarStepBackBtn"))) {
      sbBtn->setVisible(recOn); sbBtn->setEnabled(recOn);
      sbBtn->setToolTip(QStringLiteral("Step Back — reverse step over previous source line (-exec-next --reverse)"));
    }
    if (auto* rcBtn = findChild<QPushButton*>(QStringLiteral("toolbarReverseContinueBtn"))) {
      rcBtn->setVisible(recOn); rcBtn->setEnabled(recOn);
      rcBtn->setToolTip(QStringLiteral("Reverse Continue — run backward to previous breakpoint (-exec-continue --reverse)"));
    }
  }
  if (auto* rf = DynamicAddressRibbonFilter::fromNavBar(m_addressNavBar)) {
    rf->updateFromBinaryAndSession(m_elfInspector.get(), active);
  }
}

/** @brief Checks whether the toolbar Run Target button is currently latched in Running state. */
bool MainWindow::isRunButtonLatched() const { return m_debugSessionActive && m_runAction && m_runAction->isChecked(); }

/** @brief Populates View menu toolbar/split/dock toggle actions and clears legacy geometry keys. */
void MainWindow::registerViewMenuToolbarAndDockActions() {
  QMenu* viewMenu = nullptr;
  for (QAction* action : menuBar()->actions()) {
    if (action->menu() && action->text() == "&View") { viewMenu = action->menu(); break; }
  }
  if (viewMenu) {
    QMenu* toolbarsMenu = viewMenu->addMenu(QStringLiteral("Toolbars"));
    for (auto* tb : findChildren<QToolBar*>()) {
      tb->setMovable(true);
      toolbarsMenu->addAction(tb->toggleViewAction());
    }
    toolbarsMenu->addSeparator();
    toolbarsMenu->addAction(QStringLiteral("Reset Toolbars"), this, [this]() {
      for (auto* tb : findChildren<QToolBar*>()) {
        addToolBar(Qt::TopToolBarArea, tb);
        const bool isAddrNav = (tb->objectName() == QStringLiteral("AddressNavToolbar"));
        tb->setVisible(!isAddrNav || m_workspacePerspective == QStringLiteral("Binary") ||
                       (m_disassemblyView && !static_cast<QWidget*>(m_disassemblyView)->isHidden()));
      }
    });
    viewMenu->addSeparator();
    viewMenu->addAction(QStringLiteral("Split Editor Right"), this, [this]() {
      if (m_editorTabManager) m_editorTabManager->splitEditor(Qt::Horizontal);
    });
    viewMenu->addAction(QStringLiteral("Split Editor Down"), this, [this]() {
      if (m_editorTabManager) m_editorTabManager->splitEditor(Qt::Vertical);
    });
    viewMenu->addAction(QStringLiteral("Close Split"), this, [this]() {
      if (m_editorTabManager) m_editorTabManager->closeSplit();
    });
    viewMenu->addSeparator();
    auto* redockAct = viewMenu->addAction(QStringLiteral("Redock All Floating Panels"),
                                          this, &MainWindow::redockAllFloatingPanels);
    redockAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
  }

  for (auto* dock : findChildren<QDockWidget*>()) {
    if (viewMenu) viewMenu->addAction(dock->toggleViewAction());
    dock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    dock->setContentsMargins(0, 0, 0, 0);
    if (QWidget* inner = dock->widget()) {
      inner->setContentsMargins(0, 0, 0, 0);
      if (inner->layout()) {
        inner->layout()->setContentsMargins(0, 0, 0, 0);
        inner->layout()->setSpacing(0);
      }
    }
  }

  QSettings settings("gridlock", "debugger");
  settings.remove("MainWindow/Geometry_v8");
  settings.remove("MainWindow/State_v8");
  settings.remove("MainWindow/Splitter_v8_MasterHorizontalSplitter");
  settings.remove("MainWindow/Splitter_v8_MainVerticalSplitter");
}

}  // namespace gridlock::ui

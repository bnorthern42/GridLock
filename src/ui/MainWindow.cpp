/**
 * @file MainWindow.cpp
 * @brief Core construction, subview accessors, and menu bar setup for GridLock's MainWindow.
 *
 * Implements primary workstation initialization, persisted desktop geometry/splitter
 * restoration, live theme/font signal bindings, hover tooltip display, and the
 * application menu bar (File, Edit, View, Tools, Help).
 */

#include "MainWindow.hpp"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QProcess>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

#include "../core/Version.hpp"
#include "../core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../core/hpc/system/LocalSlurmBackend.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/ShortcutManager.hpp"
#include "../core/managers/SpackManager.hpp"
#include "../core/managers/ThemeManager.hpp"
#include "EditorTabManager.hpp"
#include "dialogs/project/ProjectSettingsDialog.hpp"
#include "dialogs/project/ProjectWizard.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "utils/FileDialogHelper.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/ExpressionEvaluatorWidget.hpp"
#include "widgets/HpcConsoleWidget.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/MpiNetworkLogWidget.hpp"

namespace gridlock {
class PlainJaneTooltip : public QLabel {
public:
    explicit PlainJaneTooltip(QWidget* parent = nullptr)
        : QLabel(parent, Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                             Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
        setStyleSheet("padding: 4px;");
        setTextFormat(Qt::RichText);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }

    void showTooltip(const QPoint& globalPos, const QString& text) {
        setText(text);
        adjustSize();
        QPoint pos = globalPos + QPoint(10, 10);
        if (QScreen* screen = QGuiApplication::screenAt(globalPos)) {
            const QRect avail = screen->availableGeometry();
            if (pos.x() + width() > avail.right()) pos.setX(globalPos.x() - width() - 10);
            if (pos.y() + height() > avail.bottom()) pos.setY(globalPos.y() - height() - 10);
            pos.setX(qMax(pos.x(), avail.left()));
            pos.setY(qMax(pos.y(), avail.top()));
        }
        move(pos);
        show();
    }

    void hideTooltip() { hide(); }
};
}  // namespace gridlock

namespace gridlock::ui {

void showPlainJaneTooltip(gridlock::PlainJaneTooltip* tooltip, const QPoint& globalPos, const QString& htmlText) {
    if (tooltip) tooltip->showTooltip(globalPos, htmlText);
}

/** @brief Dismisses any visible tooltip popover displayed by PlainJaneTooltip. */
void MainWindow::hideHoverTooltipPopup() {
    if (m_plainJaneTooltip) m_plainJaneTooltip->hideTooltip();
}

/** @brief Returns the active network log widget embedded inside MPI diagnostics. */
MpiNetworkLogWidget* MainWindow::networkLog() const {
    return m_mpiDiagnosticsWidget ? m_mpiDiagnosticsWidget->networkLogWidget() : nullptr;
}

/** @brief Retrieves the active SourceCodeView tab in the editor manager. */
SourceCodeView* MainWindow::sourceCodeView() const {
    return m_editorTabManager ? m_editorTabManager->currentSourceCodeView() : nullptr;
}

/** @brief Backward-compatible accessor for active SourceCodeView. */
SourceCodeView* MainWindow::getSourceCodeView() const {
    return sourceCodeView();
}

/**
 * @brief Constructs the main application window and initializes docking architecture.
 * @param parent Optional parent widget context.
 */
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    auto* slurmBackend = new gridlock::core::LocalSlurmBackend(this);
    m_hpcBackend = slurmBackend;
    setupUi();
    setupMenu();
    setupDocks();
    setupCallStackDock();
    setupStatusBar();

    connect(slurmBackend, &gridlock::core::LocalSlurmBackend::statusMessage, this,
            [this](const QString& msg, bool isErr) {
                if (m_isShuttingDown || m_isClosing) return;
                if (m_spackManager) m_spackManager->appendMessage(msg, isErr);
                appendProgramOutput(-1, QStringLiteral("[Slurm] %1").arg(msg), isErr);
                flushProgramOutputBatch();
            });
    connect(slurmBackend, &gridlock::core::LocalSlurmBackend::jobStateChanged, this,
            [this](int jobId, const QString& state) {
                if (m_isShuttingDown || m_isClosing) return;
                updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("SLURM JOB %1: %2").arg(jobId).arg(state));
                updateDebugToolbarState();
            });
    connect(slurmBackend, &gridlock::core::LocalSlurmBackend::jobFailed, this,
            [this](int jobId, const QString&) {
                if (m_isShuttingDown || m_isClosing) return;
                updateStatusTelemetry(m_currentSeekOrPc,
                                      jobId > 0 ? QStringLiteral("SLURM FAILED (%1)").arg(jobId)
                                                : QStringLiteral("SLURM ERROR"));
                updateDebugToolbarState();
            });
    connect(slurmBackend, &gridlock::core::LocalSlurmBackend::attachTargetsReady, this,
            [this, slurmBackend](int jobId, const QList<RemoteTargetSpec>& targets, const QStringList& computeEndpoints) {
                if (m_isShuttingDown || m_isClosing) return;
                m_attachingSlurmJob = true;
                const bool attached = attachToRemoteTargets(targets);
                m_attachingSlurmJob = false;
                if (!attached) {
                    const QString err = QStringLiteral("Failed to attach GDB to Slurm job %1 endpoints (%2); cancelling allocation.")
                                            .arg(jobId)
                                            .arg(computeEndpoints.join(QStringLiteral(", ")));
                    if (m_spackManager) m_spackManager->appendMessage(err, true);
                    appendProgramOutput(-1, QStringLiteral("[Slurm] %1").arg(err), true);
                    flushProgramOutputBatch();
                    slurmBackend->cancelSlurmJob(jobId);
                    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("SLURM ATTACH FAILED"));
                    updateDebugToolbarState();
                    return;
                }
                const int rankCount = targets.size();
                for (int r = 0; r < rankCount && r < computeEndpoints.size(); ++r) {
                    if (m_mpiDiagnosticsWidget) {
                        m_mpiDiagnosticsWidget->updateLiveRankRow(
                            r, 0, QStringLiteral("PAUSED"), QStringLiteral("slurm"),
                            computeEndpoints[r], QStringLiteral("0x00000000"), rankCount);
                    }
                }
                updateStatusTelemetry(m_currentSeekOrPc,
                                      QStringLiteral("SLURM JOB %1 (%2 RANK%3)")
                                          .arg(jobId)
                                          .arg(rankCount)
                                          .arg(rankCount == 1 ? QString() : QStringLiteral("S")));
                updateDebugToolbarState();
            });

    // Keep active file tracking synchronized whenever F12 Go-to-Definition switches editor tabs.
    if (m_editorTabManager) {
        connect(m_editorTabManager, &EditorTabManager::definitionJumpRequested, this,
                [this](const QString& filePath, int oneBasedLine, int) {
                    m_currentFile = filePath;
                    if (networkLog()) {
                        networkLog()->appendText(QStringLiteral("Navigated to definition: %1:%2\n").arg(filePath).arg(oneBasedLine));
                    }
                });
    }

    gridlock::core::ShortcutManager::instance().initialize(this);
    qApp->installEventFilter(this);

    m_plainJaneTooltip = new gridlock::PlainJaneTooltip(this);
    if (m_targetSelector) {
        // Register the full 7-program MPI showcase suite ahead of the Open ELF / Binary... item.
        const QList<QPair<QString, QString>> showcaseTargets = {
            {QStringLiteral("inspection_demo (MPI)"), QStringLiteral("tutorial/inspection_demo.cpp")},
            {QStringLiteral("deadlock_ring (MPI)"), QStringLiteral("tutorial/deadlock_ring.cpp")},
            {QStringLiteral("collective_mismatch (MPI)"), QStringLiteral("tutorial/collective_mismatch.cpp")},
            {QStringLiteral("ghost_cell_drift (MPI)"), QStringLiteral("tutorial/ghost_cell_drift.cpp")},
            {QStringLiteral("heap_imbalance (MPI)"), QStringLiteral("tutorial/heap_imbalance.cpp")},
            {QStringLiteral("tag_mismatch_stall (MPI)"), QStringLiteral("tutorial/tag_mismatch_stall.cpp")},
            {QStringLiteral("simd_struct_watch (MPI)"), QStringLiteral("tutorial/simd_struct_watch.cpp")},
            {QStringLiteral("stencil_omp (Fortran MPI+OpenMP)"), QStringLiteral("tutorial/stencil_omp.f90")}};
        for (const auto& entry : showcaseTargets) {
            const bool localExists = QFileInfo::exists(entry.second) || QFileInfo::exists(QStringLiteral("../") + entry.second);
            const QString targetData = localExists ? entry.second : resolveTutorialTarget(entry.second);
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
    const QString initialSource = resolveTutorialTarget(QStringLiteral("tutorial/inspection_demo.cpp"));
    if (QFileInfo::exists(initialSource)) loadSourceFile(initialSource);
    bindSourceViewGraphSignals(this, sourceCodeView());

    m_persistentBreakpoints = gridlock::core::ConfigManager::instance().getBreakpoints();
    if (m_editorTabManager) connect(m_editorTabManager, &EditorTabManager::rankBreakpointChanged, this, &MainWindow::onRankBreakpointChanged);

    // Restore saved desktop geometry and dock configuration when running with an interactive display server.
    if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
        QSettings settings(gridlock::core::ConfigManager::debuggerSettingsPath(), QSettings::IniFormat);
        if (settings.contains("MainWindow/Geometry_v9")) restoreGeometry(settings.value("MainWindow/Geometry_v9").toByteArray());
        if (settings.contains("MainWindow/State_v9")) restoreState(settings.value("MainWindow/State_v9").toByteArray());
        for (auto* splitter : findChildren<QSplitter*>())
            if (!splitter->objectName().isEmpty()) {
                auto st = settings.value("MainWindow/Splitter_v9_" + splitter->objectName()).toByteArray();
                if (!st.isEmpty()) splitter->restoreState(st);
            }
    }
    if (width() < 800 || height() < 600) resize(1440, 900);
    for (auto* dock : findChildren<QDockWidget*>()) {
        const auto area = dockWidgetArea(dock);
        if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
            dock->setMinimumWidth(150); dock->setMaximumWidth(QWIDGETSIZE_MAX);
            if (dock->widget()) { dock->widget()->setMinimumWidth(150); dock->widget()->setMaximumWidth(QWIDGETSIZE_MAX); }
        }
    }
    for (auto* tabBar : findChildren<QTabBar*>()) {
        if (tabBar->count() == 0) tabBar->hide();
        tabBar->setUsesScrollButtons(false); tabBar->setElideMode(Qt::ElideNone); tabBar->setExpanding(false);
    }

    auto& cfg = gridlock::core::ConfigManager::instance();
    if (windowFlags() & Qt::FramelessWindowHint) {
        setWindowFlags((windowFlags() & ~Qt::FramelessWindowHint) | Qt::Window | Qt::WindowTitleHint |
                       Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    }
    setMenuBarWindowControlsVisible(cfg.showMenuBarWindowControls());
    cfg.registerMenuBarWindowControlsListener(this, [this](bool vis) { setMenuBarWindowControlsVisible(vis); });
    if (m_submitSlurmJobAction) m_submitSlurmJobAction->setVisible(cfg.getHpcSettings().enableSlurmMenu);
    cfg.registerSlurmMenuVisibilityListener(this, [this](bool vis) {
        if (m_submitSlurmJobAction) m_submitSlurmJobAction->setVisible(vis);
    });
    applyPreferredScreenRouting(cfg.preferredMonitor(), cfg.openUnfocused());
    loadRemoteTargetDialogState();
    syncCoordinatorWithConfig();
    initializePluginSubsystem();
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
    connect(&gridlock::core::ThemeManager::instance(), &gridlock::core::ThemeManager::themeModeChanged,
            this, [this]() { if (!m_isShuttingDown && !m_isClosing) propagateThemeToAllWidgets(); });
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
        for (QAction* ma : menuBar()->actions()) {
            if (QMenu* dm = ma ? ma->menu() : nullptr; dm && dm->title().remove(QLatin1Char('&')) == QLatin1String("Debug") && !dm->actions().contains(m_pauseAllAction)) {
                const QList<QAction*> acts = dm->actions();
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

void MainWindow::setMenuBarWindowControlsVisible(bool visible) {
    m_menuBarWindowControlsVisible = visible;
    if (QMenuBar* mb = menuBar(); mb && m_windowControlsWidget) {
        if (visible) {
            if (mb->cornerWidget(Qt::TopRightCorner) != m_windowControlsWidget) {
                mb->setCornerWidget(m_windowControlsWidget, Qt::TopRightCorner);
            }
            m_windowControlsWidget->setVisible(true);
            mb->updateGeometry();
        } else {
            if (mb->cornerWidget(Qt::TopRightCorner) == m_windowControlsWidget) {
                mb->setCornerWidget(nullptr, Qt::TopRightCorner);
            }
            m_windowControlsWidget->setParent(this);
            m_windowControlsWidget->setVisible(false);
            mb->updateGeometry();
        }
    }
}

/**
 * @brief Constructs application menu bar actions, shortcuts, and dialog triggers.
 *
 * Populates File, Edit, View, Tools, and Help menus with project wizards,
 * session serialization, SLURM batch submissions, and build triggers.
 */
void MainWindow::setupMenu() {
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);
    class MenuBarDragFilter final : public QObject {
    public:
        MenuBarDragFilter(MainWindow* mw, QMenuBar* mb) : QObject(mb), m_mw(mw), m_mb(mb) {}
        bool eventFilter(QObject*, QEvent* ev) override {
            if (!m_mw || !m_mb || !m_mw->areMenuBarWindowControlsVisible()) return false;
            if (ev->type() == QEvent::MouseButtonPress || ev->type() == QEvent::MouseButtonDblClick) {
                auto* me = static_cast<QMouseEvent*>(ev);
                if (me->button() == Qt::LeftButton && !m_mb->actionAt(me->position().toPoint())) {
                    if (ev->type() == QEvent::MouseButtonDblClick) m_mw->isMaximized() ? m_mw->showNormal() : m_mw->showMaximized();
                    else if (QWindow* win = m_mw->windowHandle()) win->startSystemMove();
                    return true;
                }
            }
            return false;
        }
    private:
        MainWindow* m_mw = nullptr; QMenuBar* m_mb = nullptr;
    };
    menuBar->installEventFilter(new MenuBarDragFilter(this, menuBar));
    m_windowControlsWidget = new QWidget(this); m_windowControlsWidget->setObjectName(QStringLiteral("windowControlsWidget"));
    auto* wcLay = new QHBoxLayout(m_windowControlsWidget); wcLay->setContentsMargins(4, 0, 2, 0); wcLay->setSpacing(2);
    auto mkWinBtn = [&](const QString& objName, const QString& glyph, const QString& tip, bool isClose) {
        auto* btn = new QToolButton(m_windowControlsWidget); btn->setObjectName(objName); btn->setText(glyph); btn->setToolTip(tip);
        btn->setFixedSize(28, 22); btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(isClose ? QStringLiteral("QToolButton { border: none; border-radius: 3px; font-weight: 700; } QToolButton:hover { background: #e06c75; color: #ffffff; }")
                                   : QStringLiteral("QToolButton { border: none; border-radius: 3px; font-weight: 700; } QToolButton:hover { background: rgba(128,140,160,0.25); }"));
        wcLay->addWidget(btn); return btn;
    };
    m_windowMinimizeBtn = mkWinBtn(QStringLiteral("windowMinimizeBtn"), QStringLiteral("\u2500"), tr("Minimize Window"), false);
    m_windowMaximizeBtn = mkWinBtn(QStringLiteral("windowMaximizeBtn"), QStringLiteral("\u25a1"), tr("Maximize / Restore Window"), false);
    m_windowCloseBtn = mkWinBtn(QStringLiteral("windowCloseBtn"), QStringLiteral("\u2715"), tr("Close Window"), true);
    connect(m_windowMinimizeBtn, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(m_windowMaximizeBtn, &QToolButton::clicked, this, [this]() { isMaximized() ? showNormal() : showMaximized(); });
    connect(m_windowCloseBtn, &QToolButton::clicked, this, &QWidget::close);

    // Construct File menu with workspace, project wizard, session, and SLURM triggers.
    QMenu* fileMenu = menuBar->addMenu("&File");

    QAction* newAction = fileMenu->addAction("New Project...");
    connect(newAction, &QAction::triggered, this, [this]() {
        gridlock::ui::dialogs::ProjectWizard dialog(this);
        if (dialog.exec() == QDialog::Accepted) {
            const QString projectRoot = dialog.getProjectRoot();
            if (projectRoot.isEmpty()) return;

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

            auto dbgSettings = gridlock::core::ConfigManager::instance().getDebuggerSettings();
            dbgSettings.mpiExecutable = "mpirun"; dbgSettings.defaultRanks = dialog.getRanks();
            dbgSettings.mpiArgs = ps.mpiArgs;
            gridlock::core::ConfigManager::instance().saveDebuggerSettings(dbgSettings);

            if (m_coordinator) startDebuggingSession(dialog.getTargetBinary(), dialog.getRanks());
        }
    });

    QAction* openProjAction = fileMenu->addAction("Open Workspace...");
    connect(openProjAction, &QAction::triggered, this, [this]() {
        const QString dir = FileDialogHelper::getExistingDirectory(this, QStringLiteral("Open Workspace"), m_activeWorkspaceDir);
        if (!dir.isEmpty()) switchWorkspaceDirectory(dir);
    });

    QAction* openAction = fileMenu->addAction("Open Source File");
    connect(openAction, &QAction::triggered, this, &MainWindow::openFile);

    QAction* quickOpenAction = fileMenu->addAction(QStringLiteral("Quick Open File..."));
    quickOpenAction->setObjectName(QStringLiteral("actionQuickOpen"));
    quickOpenAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
    connect(quickOpenAction, &QAction::triggered, this, [this]() { openCommandPalette(QString()); });

    QAction* saveAction = fileMenu->addAction("Save Source File");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, [this]() {
        if (auto* v = getSourceCodeView(); v && v->document()->isModified()) v->saveToFile();
    });

    QAction* saveAllAction = fileMenu->addAction("Save All");
    saveAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(saveAllAction, &QAction::triggered, this, [this]() {
        if (m_editorTabManager) m_editorTabManager->saveAllModifiedTabs();
    });

    QAction* switchHdrAct = fileMenu->addAction("Switch Header/Source");
    switchHdrAct->setObjectName(QStringLiteral("file.switch_header_source"));
    switchHdrAct->setShortcut(QKeySequence(Qt::Key_F4));
    connect(switchHdrAct, &QAction::triggered, this, [this]() {
        if (m_editorTabManager) m_editorTabManager->switchHeaderSource();
    });
    fileMenu->addSeparator();

    QAction* saveNowAct = fileMenu->addAction("Save Session"); saveNowAct->setObjectName(QStringLiteral("actionSaveSession"));
    connect(saveNowAct, &QAction::triggered, this, [this]() { saveSession(true); });
    QAction* saveSessionAction = fileMenu->addAction("Save Session As..."); connect(saveSessionAction, &QAction::triggered, this, &MainWindow::saveSessionAs);
    QAction* loadSessionAction = fileMenu->addAction("Load Session..."); connect(loadSessionAction, &QAction::triggered, this, &MainWindow::loadSession);

    m_recentSessionsMenu = fileMenu->addMenu("Recent Sessions"); m_recentSessionsMenu->setObjectName(QStringLiteral("menuRecentSessions"));
    connect(m_recentSessionsMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentSessionsMenu);
    rebuildRecentSessionsMenu();
    fileMenu->addSeparator();

    QAction* projSetAction = fileMenu->addAction("Project Settings");
    connect(projSetAction, &QAction::triggered, this, [this]() {
        ProjectSettingsDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            syncCoordinatorWithConfig();
            if (m_terminalDock) m_terminalDock->setWorkingDirectory(QString::fromStdString(gridlock::core::ConfigManager::instance().projectSettings().workingDirectory));
        }
    });

    QAction* attachProcAction = fileMenu->addAction("Attach to Process...");
    attachProcAction->setObjectName(QStringLiteral("actionAttachProcess"));
    attachProcAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P));
    connect(attachProcAction, &QAction::triggered, this, [this]() { attachToProcessWorkflow(); });

    m_submitSlurmJobAction = fileMenu->addAction(QStringLiteral("Submit Slurm Job..."));
    m_submitSlurmJobAction->setObjectName(QStringLiteral("actionSubmitSlurmJob"));
    m_submitSlurmJobAction->setVisible(gridlock::core::ConfigManager::instance().getHpcSettings().enableSlurmMenu);
    connect(m_submitSlurmJobAction, &QAction::triggered, this, [this]() { onSubmitSlurmJob(); });
    fileMenu->addSeparator();
    fileMenu->addAction("Exit", this, &MainWindow::close);

    // Construct Edit menu with focus-aware editor operations and Preferences dialog trigger.
    QMenu* editMenu = menuBar->addMenu("&Edit");
    const QList<QPair<QString, QKeySequence>> editActions = {
        {QStringLiteral("Undo"), QKeySequence::Undo}, {QStringLiteral("Redo"), QKeySequence::Redo}, {QStringLiteral("Cut"), QKeySequence::Cut},
        {QStringLiteral("Copy"), QKeySequence::Copy}, {QStringLiteral("Paste"), QKeySequence::Paste}, {QStringLiteral("Select All"), QKeySequence::SelectAll},
        {QStringLiteral("Find in View..."), QKeySequence(Qt::CTRL | Qt::Key_F)}, {QStringLiteral("Go to Line..."), QKeySequence(Qt::CTRL | Qt::Key_L)},
        {QStringLiteral("Toggle Comment"), QKeySequence(Qt::CTRL | Qt::Key_Slash)}};
    for (int i = 0; i < editActions.size(); ++i) {
        if (i == 2 || i == 6) editMenu->addSeparator();
        QAction* act = editMenu->addAction(editActions[i].first); act->setShortcut(editActions[i].second);
        if (editActions[i].first.startsWith(QStringLiteral("Find"))) act->setObjectName(QStringLiteral("actionFindInView"));
        const QString actLabel = editActions[i].first;
        connect(act, &QAction::triggered, this, [this, actLabel]() { triggerEditMenuAction(actLabel); });
    }
    QAction* gotoDefAction = editMenu->addAction(QStringLiteral("Go to Definition"));
    connect(gotoDefAction, &QAction::triggered, this, &MainWindow::goToDefinitionAtCursor);
    editMenu->addSeparator();
    QAction* prefAction = editMenu->addAction("Preferences");
    prefAction->setShortcut(QKeySequence::Preferences);
    connect(prefAction, &QAction::triggered, this, &MainWindow::openPreferences);

    QMenu* viewMenu = menuBar->addMenu("&View");
    QAction* cmdPaletteAction = viewMenu->addAction(QStringLiteral("Command Palette..."));
    cmdPaletteAction->setObjectName(QStringLiteral("actionCommandPalette"));
    cmdPaletteAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P));
    connect(cmdPaletteAction, &QAction::triggered, this, [this]() { openCommandPalette(QStringLiteral(">")); });

    // Construct Tools menu for smart build system compilation, cleaning, rebuilding, and unit tests.
    QMenu* toolsMenu = menuBar->addMenu("&Tools");
    QAction* buildAction = toolsMenu->addAction("Build Target");
    buildAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    connect(buildAction, &QAction::triggered, this, &MainWindow::buildTargetWorkflow);

    QAction* cleanAction = toolsMenu->addAction("Clean Target");
    connect(cleanAction, &QAction::triggered, this, &MainWindow::cleanTargetWorkflow);

    QAction* rebuildAction = toolsMenu->addAction("Rebuild Target");
    connect(rebuildAction, &QAction::triggered, this, &MainWindow::rebuildTargetWorkflow);

    toolsMenu->addSeparator();
    QAction* runTestsAction = toolsMenu->addAction("Run Tests");
    connect(runTestsAction, &QAction::triggered, this, &MainWindow::runTargetRequested);
    QAction* ptrGraphAction = toolsMenu->addAction("Pointer & Struct Graph Visualizer...");
    connect(ptrGraphAction, &QAction::triggered, this, [this]() { openPointerGraphVisualizer(); });
    QAction* progCalcAction = toolsMenu->addAction(QStringLiteral("Programmer's Calculator..."));
    progCalcAction->setObjectName(QStringLiteral("actionProgrammerCalculator"));
    progCalcAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_C));
    connect(progCalcAction, &QAction::triggered, this, &MainWindow::openProgrammerCalculator);

    toolsMenu->addSeparator();
    QAction* exportMiTraceAction = toolsMenu->addAction(QStringLiteral("Export Sanitized GDB/MI Trace..."));
    exportMiTraceAction->setObjectName(QStringLiteral("actionExportSanitizedMiTrace"));
    connect(exportMiTraceAction, &QAction::triggered, this, [this]() { (void)exportSanitizedMiTrace(); });

    // Construct Help menu with About dialog and attribution metadata.
    QMenu* helpMenu = menuBar->addMenu("&Help");
    QAction* aboutAction = helpMenu->addAction("About GridLock");
    connect(aboutAction, &QAction::triggered, this, [this]() {
        QProcess gitProc;
        gitProc.start("git", {"ls-remote", "--tags", "--sort=v:refname", "https://github.com/bnorthern42/GridLock.git"});
        gitProc.waitForFinished(3000);
        QString latestTag = "v" + gridlock::core::Version::getString();
        if (gitProc.exitStatus() == QProcess::NormalExit && gitProc.exitCode() == 0) {
            const QString output = QString::fromUtf8(gitProc.readAllStandardOutput()).trimmed();
            if (!output.isEmpty()) {
                const QString tag = output.split('\n').last().split('\t').last().replace("refs/tags/", "");
                if (!tag.isEmpty()) latestTag = tag;
            }
        }

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
        msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);
        msgBox.exec();
    });
}

bool MainWindow::exportSanitizedMiTrace(const QString& explicitPath) {
    QString targetPath = explicitPath.trimmed();
    if (targetPath.isEmpty()) {
        targetPath = QFileDialog::getSaveFileName(
            this,
            QStringLiteral("Export Sanitized GDB/MI Trace"),
            QStringLiteral("gridlock_mi_trace.mi.log"),
            QStringLiteral("GDB/MI Trace Logs (*.mi.log *.txt);;All Files (*)"));
        if (targetPath.isEmpty()) return false;
    }

    QString errMsg;
    bool ok = false;
    if (auto* gdbCoord = qobject_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
        gdbCoord->miTranscriptRecorder().setGridLockVersion(gridlock::core::Version::getString());
        ok = gdbCoord->exportSanitizedMiTraceToFile(targetPath, &errMsg);
    } else {
        gridlock::core::GdbMiTranscriptRecorder emptyRecorder;
        emptyRecorder.setGridLockVersion(gridlock::core::Version::getString());
        ok = emptyRecorder.exportToFile(targetPath, &errMsg);
    }

    if (ok) {
        if (statusBar()) {
            statusBar()->showMessage(QStringLiteral("Exported sanitized GDB/MI trace to %1").arg(targetPath), 5000);
        }
    } else if (explicitPath.trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Export Sanitized GDB/MI Trace"),
                             QStringLiteral("Failed to write GDB/MI trace:\n%1").arg(errMsg));
    }
    return ok;
}

}  // namespace gridlock::ui
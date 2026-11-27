/**
 * @file MainWindowDebugger.cpp
 * @brief Dynamic statement-to-instruction mapping, rank-aware stepping, and debug session lifecycle.
 *
 * Implements dynamic source line resolution, per-rank preview control-flow stepping,
 * start/step/continue/stop/shutdown debug session workflows, and Debug menu action bootstrapping.
 */

#include "MainWindow.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>

#include "../RankState.hpp"
#include "../core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../core/hpc/system/LocalSlurmBackend.hpp"
#include "../core/hpc/IBackendCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/managers/ShortcutManager.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "dialogs/plotter/ArrayPlotDialog.hpp"
#include "dialogs/graph/PointerGraphDialog.hpp"
#include "docks/CallStackDockWidget.hpp"
#include "views/DisassemblyView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"

namespace gridlock::ui {

/** @brief Strips inline C-style block and single-line (`//`) comments from one source line. */
static QString stripLineComments(const QString& rawLine, bool* inBlockComment) {
    QString line = rawLine.trimmed();
    if (inBlockComment && *inBlockComment) {
        const int endIdx = line.indexOf(QStringLiteral("*/"));
        if (endIdx == -1) return QString();
        *inBlockComment = false;
        line = line.mid(endIdx + 2).trimmed();
    }
    while (line.contains(QStringLiteral("/*"))) {
        const int s = line.indexOf(QStringLiteral("/*")), e = line.indexOf(QStringLiteral("*/"), s + 2);
        if (e != -1) line = (line.left(s) + line.mid(e + 2)).trimmed();
        else { if (inBlockComment) *inBlockComment = true; line = line.left(s).trimmed(); break; }
    }
    const int sc = line.indexOf(QStringLiteral("//"));
    return (sc != -1) ? line.left(sc).trimmed() : line;
}

struct MainWindowDebugMenuBootstrap : public QObject {
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event && (event->type() == QEvent::Show || event->type() == QEvent::Polish)) {
            if (auto* mw = qobject_cast<MainWindow*>(watched); mw && !mw->findChild<QAction*>(QStringLiteral("actionSignalsAndCatchpoints"))) {
                mw->rebuildDynamicSourceLineMap();
            }
        }
        return false;
    }
};
static const bool s_debugMenuHooked = []() {
    QCoreApplication::startingUp();
    qAddPreRoutine([]() { if (qApp) qApp->installEventFilter(new MainWindowDebugMenuBootstrap()); });
    if (qApp) qApp->installEventFilter(new MainWindowDebugMenuBootstrap());
    return true;
}();

void MainWindow::wireTemporaryBreakpointHit() {
    if (!m_coordinator || m_coordinator->property("gl_tbreak_hit_wired").toBool()) return;
    m_coordinator->setProperty("gl_tbreak_hit_wired", true);
    connect(m_coordinator, &IBackendCoordinator::temporaryBreakpointHit, this, [this](int, const QString& f, int ln) {
        const QString base = QFileInfo(f).fileName();
        auto clr = [&](SourceCodeView* v) { if (v && (v->currentFile() == f || QFileInfo(v->currentFile()).fileName() == base)) v->removeBreakpointQuietly(ln); };
        if (m_editorTabManager) { for (SourceCodeView* v : m_editorTabManager->openViews()) clr(v); }
        else clr(sourceCodeView());
    });
}

void MainWindow::applyPreviewStopState(int rank, uint64_t addr, const QString& status) {
    const QString hex = QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0'));
    const int line = addressToSourceLine(addr);
    const bool keepLinear = m_disassemblyView && !m_disassemblyView->isGraphMode() && !m_disassemblyView->isDecompilerMode() &&
                            !m_disassemblyView->isSplitGraphDecompilerMode() && !m_forceDecompilerFallback &&
                            m_workspacePerspective == QStringLiteral("Code");
    seekToAddressOrExpression(hex);
    if (keepLinear && m_disassemblyView) { m_showingDecompilerFallback = false; m_disassemblyView->setSplitGraphDecompilerMode(false); m_disassemblyView->setGraphMode(false); }
    if (m_disassemblyView) m_disassemblyView->setActiveSteppingRank(rank, addr);
    if (SourceCodeView* srcView = sourceCodeView()) srcView->highlightExecutionLine(line);
    if (m_arrayPlotDialog && m_arrayPlotDialog->isVisible()) m_arrayPlotDialog->onTargetStopped(rank);
    if (m_pointerGraphDialog && m_pointerGraphDialog->isVisible()) m_pointerGraphDialog->onTargetStopped(rank);
    updateStatusTelemetry(hex, status);
}

void MainWindow::rebuildDynamicSourceLineMap() {
    if (!findChild<QAction*>(QStringLiteral("actionArrayPlotter")) && menuBar()) {
        for (QAction* topAct : menuBar()->actions()) {
            if (QMenu* m = topAct->menu(); m && topAct->text().contains(QStringLiteral("Tools"), Qt::CaseInsensitive)) {
                QAction* a = m->addAction(QStringLiteral("Array & Field Plotter..."), this, [this]() { openArrayPlotter(); });
                a->setObjectName(QStringLiteral("actionArrayPlotter"));
                a->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G));
                break;
            }
        }
    }
    if (!findChild<QAction*>(QStringLiteral("actionSignalsAndCatchpoints")) && menuBar()) {
        QMenu* debugMenu = nullptr;
        QAction* toolsTopAct = nullptr;
        for (QAction* topAct : menuBar()->actions()) {
            if (topAct->text().contains(QStringLiteral("Debug"), Qt::CaseInsensitive)) debugMenu = topAct->menu();
            else if (topAct->text().contains(QStringLiteral("Tools"), Qt::CaseInsensitive)) toolsTopAct = topAct;
        }
        if (!debugMenu) {
            debugMenu = new QMenu(QStringLiteral("&Debug"), menuBar());
            if (toolsTopAct) menuBar()->insertMenu(toolsTopAct, debugMenu); else menuBar()->addMenu(debugMenu);
        }
        for (QAction* sa : {m_runAction, m_continueAction, m_stepOverAction, m_stepIntoAction, m_stepOutAction, m_stepAction, m_pauseAction, m_stopAction}) {
            if (sa) debugMenu->addAction(sa);
        }
        debugMenu->addSeparator();
        auto addDbgAct = [&](const QString& text, const QString& obj, const QKeySequence& ks, auto slotFn) {
            QAction* a = debugMenu->addAction(text, this, slotFn);
            a->setObjectName(obj);
            if (!ks.isEmpty()) a->setShortcut(ks);
        };
        addDbgAct(QStringLiteral("Step Over Instruction (nexti)"), QStringLiteral("actionNextInstruction"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F10), [this]() { nextInstructionWorkflow(); });
        addDbgAct(QStringLiteral("Run to Cursor"), QStringLiteral("actionRunToCursor"), QKeySequence(Qt::CTRL | Qt::Key_F10), [this]() { runToCursorWorkflow(); });
        addDbgAct(QStringLiteral("Set Next Statement / Jump..."), QStringLiteral("actionJumpToLine"), QKeySequence(), [this]() {
            SourceCodeView* sv = sourceCodeView(); if (!sv) return;
            int ln = sv->getCurrentLineNumber();
            if (ln <= 0 || ln == sv->executionLine()) {
                bool ok = false;
                ln = QInputDialog::getInt(this, tr("Set Next Statement / Jump"), tr("Target source line (1-based):"),
                                          qMax(1, ln), 1, qMax(1, sv->document()->blockCount()), 1, &ok);
                if (!ok) return;
            }
            jumpToLocationWorkflow(sv->currentFile(), ln);
        });
        addDbgAct(QStringLiteral("Force Early Return..."), QStringLiteral("actionForceEarlyReturn"), QKeySequence(), [this]() { forceEarlyReturnWorkflow(QString(), true); });
        addDbgAct(QStringLiteral("Break on Function Regex (rbreak)..."), QStringLiteral("actionRegexBreakpoint"), QKeySequence(), [this]() { insertRegexBreakpointWorkflow(QString(), true); });
        debugMenu->addSeparator();
        addDbgAct(QStringLiteral("Process Checkpoints..."), QStringLiteral("actionProcessCheckpoints"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_K), [this]() { openProcessCheckpoints(); });
        addDbgAct(QStringLiteral("Signals & Catchpoints..."), QStringLiteral("actionSignalsAndCatchpoints"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S), [this]() { openSignalsAndCatchpoints(); });
        gridlock::core::ShortcutManager::instance().syncMenuActionsFromMainWindow();
    }

    if (m_disassemblyView && !m_disassemblyView->property("gl_exec_wired").toBool()) {
        m_disassemblyView->setProperty("gl_exec_wired", true);
        connect(m_disassemblyView, &DisassemblyView::stepInstructionRequested, this, &MainWindow::stepInstructionWorkflow);
        connect(m_disassemblyView, &DisassemblyView::nextInstructionRequested, this, &MainWindow::nextInstructionWorkflow);
        connect(m_disassemblyView, &DisassemblyView::runToAddressRequested, this, &MainWindow::runToAddressWorkflow);
        connect(m_disassemblyView, &DisassemblyView::jumpToAddressRequested, this, [this](uint64_t a) { jumpToAddressWorkflow(a); });
    }
    wireTemporaryBreakpointHit();
    auto bindSrcExec = [this](SourceCodeView* sv) {
        if (!sv || sv->property("gl_exec_wired").toBool()) return;
        sv->setProperty("gl_exec_wired", true);
        connect(sv, &SourceCodeView::runToCursorRequested, this, [this](const QString& f, int ln) { runToCursorWorkflow(f, ln); });
        connect(sv, &SourceCodeView::jumpToLineRequested, this, [this](const QString& f, int ln) { jumpToLocationWorkflow(f, ln); });
        connect(sv, &SourceCodeView::breakpointEnabledToggled, this, [this](const QString& f, int ln, bool en) { if (m_coordinator) m_coordinator->setBreakpointEnabled(f, ln, en); });
        connect(sv, &SourceCodeView::temporaryBreakpointInserted, this, [this](const QString& f, int ln, const QString& c, int rf) { wireTemporaryBreakpointHit(); if (m_coordinator) m_coordinator->insertTemporaryBreakpoint(f, ln, c, rf); });
        connect(sv, &SourceCodeView::breakpointCommandsChanged, this, [this](const QString& f, int ln, const QStringList& cmds) { if (m_coordinator) m_coordinator->setBreakpointCommands(f, ln, cmds); });
    };
    if (m_editorTabManager) {
        for (SourceCodeView* sv : m_editorTabManager->openViews()) bindSrcExec(sv);
        if (!m_editorTabManager->property("gl_exec_wired").toBool()) {
            m_editorTabManager->setProperty("gl_exec_wired", true);
            connect(m_editorTabManager, &EditorTabManager::currentChanged, this, [this, bindSrcExec](int) { bindSrcExec(getSourceCodeView()); });
        }
    } else bindSrcExec(sourceCodeView());

    if (auto* tbl = m_callStackDockWidget ? m_callStackDockWidget->findChild<QTableWidget*>(QStringLiteral("callStackTable")) : nullptr;
        tbl && !tbl->property("gl_exec_wired").toBool()) {
        tbl->setProperty("gl_exec_wired", true);
        tbl->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tbl, &QWidget::customContextMenuRequested, this, [this, tbl](const QPoint& pos) {
            if (const int row = tbl->rowAt(pos.y()); row >= 0 && m_callStackDockWidget) m_callStackDockWidget->selectFrameRow(row);
            QMenu m(tbl);
            m.addAction(QStringLiteral("Force Early Return..."), this, [this]() { forceEarlyReturnWorkflow(QString(), true); });
            m.exec(tbl->viewport()->mapToGlobal(pos));
        });
    }

    m_dynamicLineToAddress.clear(); m_dynamicAddressToLine.clear();
    if (m_useRealBinaryTarget && !m_dwarfLineToAddress.isEmpty()) {
        m_dynamicLineToAddress = m_dwarfLineToAddress; m_dynamicAddressToLine = m_dwarfAddressToLine; return;
    }
    SourceCodeView* srcView = sourceCodeView();
    if (!srcView || srcView->toPlainText().isEmpty()) return;
    const QStringList lines = srcView->toPlainText().split(QLatin1Char('\n'));
    bool inBlockComment = false;
    int mainLine = -1, initLine = -1, structLine = -1, forLoopLine = -1, dataIdLine = -1, coordsLine = -1, printfLine = -1, finalizeLine = -1, returnLine = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const int ln = i + 1;
        const QString line = stripLineComments(lines[i], &inBlockComment);
        if (line.isEmpty()) continue;
        if (mainLine == -1 && (line.contains(QStringLiteral("int main")) || line.contains(QStringLiteral("main(")))) mainLine = ln;
        else if (mainLine != -1 && initLine == -1 && line.contains(QStringLiteral("MPI_Init"))) initLine = ln;
        else if (mainLine != -1 && structLine == -1 && (line.contains(QStringLiteral("data[")) || (line.contains(QStringLiteral("DeeplyNested")) && line.contains(QStringLiteral("data"))))) structLine = ln;
        else if (forLoopLine == -1 && line.contains(QStringLiteral("for")) && line.contains(QStringLiteral("int i"))) forLoopLine = ln;
        else if (dataIdLine == -1 && line.contains(QStringLiteral("data[i].id"))) dataIdLine = ln;
        else if (coordsLine == -1 && (line.contains(QStringLiteral("coords")) || line.contains(QStringLiteral("values[")))) coordsLine = ln;
        else if (printfLine == -1 && line.contains(QStringLiteral("printf"))) printfLine = ln;
        else if (finalizeLine == -1 && line.contains(QStringLiteral("MPI_Finalize"))) finalizeLine = ln;
        else if (returnLine == -1 && line.contains(QStringLiteral("return 0"))) returnLine = ln;
    }
    for (auto [var, def] : std::initializer_list<std::pair<int*, int>>{
             {&mainLine, 12}, {&initLine, 13}, {&structLine, 15}, {&forLoopLine, 16}, {&dataIdLine, 17},
             {&coordsLine, 19}, {&printfLine, 23}, {&finalizeLine, 24}, {&returnLine, 25}}) if (*var == -1) *var = def;
    m_dynamicLineToAddress = {{mainLine, 0x00401120ULL}, {structLine, 0x00401128ULL}, {initLine, 0x0040113dULL},
                              {forLoopLine, 0x00401149ULL}, {dataIdLine, 0x0040114fULL}, {coordsLine, 0x00401157ULL},
                              {printfLine, 0x0040116eULL}, {finalizeLine, 0x00401178ULL}, {returnLine, 0x00401183ULL}};
    m_dynamicAddressToLine = {{0x00401120ULL, mainLine}, {0x00401124ULL, mainLine}, {0x00401125ULL, mainLine},
                              {0x00401128ULL, structLine}, {0x0040112fULL, structLine}, {0x00401136ULL, structLine},
                              {0x0040113dULL, initLine}, {0x00401142ULL, forLoopLine}, {0x00401149ULL, forLoopLine},
                              {0x0040114dULL, forLoopLine}, {0x0040114fULL, dataIdLine}, {0x00401152ULL, dataIdLine},
                              {0x00401155ULL, dataIdLine}, {0x00401157ULL, coordsLine}, {0x0040115fULL, coordsLine},
                              {0x00401163ULL, coordsLine}, {0x00401168ULL, coordsLine}, {0x0040116aULL, coordsLine},
                              {0x0040116eULL, printfLine}, {0x00401178ULL, finalizeLine}, {0x0040117dULL, finalizeLine},
                              {0x00401182ULL, returnLine}, {0x00401183ULL, returnLine}};
}

/** @brief Scans active SourceCodeView text and returns 1-based line of first matching statement. */
int MainWindow::findSourceLineForStatement(const QString& pattern) const {
    SourceCodeView* srcView = sourceCodeView();
    if (!srcView) return -1;
    const QStringList lines = srcView->toPlainText().split(QLatin1Char('\n'));
    bool inBlockComment = false;
    for (int i = 0; i < lines.size(); ++i) if (stripLineComments(lines[i], &inBlockComment).contains(pattern, Qt::CaseSensitive)) return i + 1;
    return -1;
}

/** @brief Returns the 0-based active stepping rank governed by the master setFocusedRank() hub. */
int MainWindow::activeSteppingRank() const { return m_focusedRank; }

/** @brief Computes the next instruction address for an MPI rank considering control-flow branches and loops. */
uint64_t MainWindow::advanceRankProgramCounter(int rank, uint64_t pc) {
    switch (pc) {
        case 0x00401142ULL: m_rankLoopCounter[rank] = 0; return 0x00401149ULL;
        case 0x00401149ULL: return 0x0040114dULL;
        case 0x0040114dULL: return (m_rankLoopCounter.value(rank, 0) >= 3) ? 0x00401178ULL : 0x0040114fULL;
        case 0x0040114fULL: return 0x00401152ULL;
        case 0x00401152ULL: return 0x00401155ULL;
        case 0x00401155ULL: return (rank == 0) ? 0x00401168ULL : 0x00401157ULL;
        case 0x00401157ULL: return 0x0040115fULL;
        case 0x0040115fULL: m_rankLoopCounter[rank] = m_rankLoopCounter.value(rank, 0) + 1; return 0x00401163ULL;
        case 0x00401163ULL: case 0x0040116eULL: return 0x00401149ULL;
        case 0x00401168ULL: return 0x0040116aULL;
        case 0x0040116aULL: m_rankLoopCounter[rank] = m_rankLoopCounter.value(rank, 0) + 1; return 0x0040116eULL;
        case 0x00401178ULL: return 0x0040117dULL;
        case 0x0040117dULL: return 0x00401182ULL;
        case 0x00401182ULL: case 0x00401183ULL: return 0x00401183ULL;
        default: return m_disassemblyView ? m_disassemblyView->nextInstructionAddress(pc) : (pc + 4);
    }
}

/** @brief Compiles the active C/C++/MPI target, syncs UI breakpoints, and launches a live parallel debug session. */
void MainWindow::startTargetWorkflow() {
    if (!m_previewFixtureOnly) {
        syncCoordinatorWithConfig();
        if (!ensureTargetBinaryCompiled() || !m_coordinator) { updateDebugToolbarState(); return; }
        const QString binPath = activeTargetBinaryPath();
        if (binPath.isEmpty() || !QFileInfo::exists(binPath)) { updateDebugToolbarState(); return; }
        const int rankCount = m_rankCountSpin ? std::clamp(m_rankCountSpin->value(), 1, 256)
                                              : std::max(1, gridlock::core::ConfigManager::instance().getDefaultRanks());
        QString preflightErr;
        if (!m_coordinator->validateLaunchEnvironment(rankCount, &preflightErr)) {
            appendProgramOutput(-1, QStringLiteral("[Launch Aborted] %1").arg(preflightErr), true);
            flushProgramOutputBatch();
            selectBottomToolGroup(QStringLiteral("Debug"));
            if (auto* po = findChild<QWidget*>(QStringLiteral("ProgramOutputDock")); po && m_bottomTabs) m_bottomTabs->setCurrentWidget(po);
            updateStatusTelemetry(QStringLiteral("0x00000000"), QStringLiteral("LAUNCH FAILED"));
            updateDebugToolbarState();
            return;
        }
        const QString mpiArgs = gridlock::core::ConfigManager::instance().getMpiArgs();
        const int preserveRank = (m_serverRackView && m_serverRackView->selectedRank() >= 0)
                                     ? std::clamp(m_serverRackView->selectedRank(), 0, rankCount - 1) : 0;
        m_coordinator->clearBreakpoints();
        const QString srcPath = activeTargetSourcePath();
        QSet<QString> syncedFiles;
        int activeUserBpCount = 0;
        const auto syncOpenView = [&](SourceCodeView* sv) {
            if (!sv) return;
            const QString viewPath = sv->currentFile().isEmpty() ? srcPath : QFileInfo(sv->currentFile()).absoluteFilePath();
            sv->highlightExecutionLine(-1);
            syncedFiles.insert(viewPath); syncedFiles.insert(QFileInfo(viewPath).canonicalFilePath());
            for (int bpLine : sv->breakpoints()) {
                int rf = sv->breakpointRankFilterForLine(bpLine);
                if (rf < 0 && sv->breakpointSuspendScope(bpLine).compare(QStringLiteral("Rank"), Qt::CaseInsensitive) == 0) rf = preserveRank;
                const auto bm = sv->breakpointInfoForLine(bpLine);
                if (bm.enabled) ++activeUserBpCount;
                if (bm.temporary) m_coordinator->insertTemporaryBreakpoint(viewPath, bpLine, sv->breakpointCondition(bpLine), rf);
                else m_coordinator->setBreakpoint(viewPath, bpLine, sv->breakpointCondition(bpLine), rf);
                m_coordinator->configureBreakpointOptions(viewPath, bpLine, bm.ignoreCount, bm.logMessage);
                if (!bm.enabled) m_coordinator->setBreakpointEnabled(viewPath, bpLine, false);
                if (!bm.commands.isEmpty()) m_coordinator->setBreakpointCommands(viewPath, bpLine, bm.commands);
                if (const uint64_t a = sourceLineToAddress(bpLine); m_disassemblyView && a != 0ULL && !m_disassemblyView->isBookmarked(a)) m_disassemblyView->toggleBookmark(a);
            }
            if (!sv->property("gl_bp_opt_bound").toBool()) {
                sv->setProperty("gl_bp_opt_bound", true);
                connect(sv, &SourceCodeView::breakpointOptionsChanged, this, [this](const QString& f, int ln, int ign, const QString& msg) {
                    if (m_coordinator) m_coordinator->configureBreakpointOptions(f, ln, ign, msg);
                });
            }
        };
        wireTemporaryBreakpointHit();
        if (m_editorTabManager) { for (SourceCodeView* sv : m_editorTabManager->openViews()) syncOpenView(sv); }
        else syncOpenView(sourceCodeView());
        for (auto it = m_persistentBreakpoints.constBegin(); it != m_persistentBreakpoints.constEnd(); ++it) {
            const QString absKey = QFileInfo(it.key()).absoluteFilePath();
            if (syncedFiles.contains(absKey) || !QFileInfo::exists(it.key())) continue;
            const auto metaMap = m_breakpointRankMeta.value(absKey);
            for (int bpLine : *it) { const auto bm = metaMap.value(bpLine); ++activeUserBpCount; m_coordinator->setBreakpoint(it.key(), bpLine, bm.condition, bm.rankFilter); }
        }
        if (m_stopAtMainOnLaunch && (!m_sessionRestoredEmptyBps || m_stopAtMainExplicit) && (activeUserBpCount == 0 || m_stopAtMainExplicit)) {
            QString mainFile = QFileInfo(srcPath).isFile()
                                   ? QFileInfo(srcPath).absoluteFilePath()
                                   : (sourceCodeView() && QFileInfo(sourceCodeView()->currentFile()).isFile() ? QFileInfo(sourceCodeView()->currentFile()).absoluteFilePath() : QString());
            if (mainFile.isEmpty() && m_targetSelector) {
                for (int i = 0; i < m_targetSelector->count(); ++i) {
                    const QString d = resolveWorkspacePath(m_targetSelector->itemData(i).toString());
                    if (QFileInfo(d).isFile() && (d.endsWith(QStringLiteral(".cpp")) || d.endsWith(QStringLiteral(".c")))) { mainFile = d; break; }
                }
            }
            int mainLine = 0;
            if (!mainFile.isEmpty()) {
                QFile mf(mainFile);
                if (mf.open(QIODevice::ReadOnly)) {
                    static const QRegularExpression mainRe(QStringLiteral("^\\s*(?:int|void|auto)\\s+main\\s*\\("));
                    for (int ln = 1; !mf.atEnd(); ++ln) if (mainRe.match(QString::fromUtf8(mf.readLine())).hasMatch()) { mainLine = ln; break; }
                }
                if (mainLine <= 0 && !m_dwarfLineToAddress.isEmpty()) mainLine = m_dwarfLineToAddress.firstKey();
            }
            if (!mainFile.isEmpty() && mainLine > 0) m_coordinator->insertTemporaryBreakpoint(mainFile, mainLine);
            else m_coordinator->insertRegexBreakpoint(QStringLiteral("^main$"));
        }

        m_latestStates.clear(); m_rankLoopCounter.clear(); m_focusedRank = preserveRank;
        if (m_disassemblyView) m_disassemblyView->setDeadlockCycleRanks({});
        if (m_gdbConsoleWidget) m_gdbConsoleWidget->resetRanks(rankCount);
        QList<RankState> initialRanks; initialRanks.reserve(rankCount);
        for (int r = 0; r < rankCount; ++r) {
            RankState st; st.rankId = r; st.currentState = QStringLiteral("running"); m_latestStates[r] = st; initialRanks.append(st);
            if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->updateRankStateRow(r, QStringLiteral("RUNNING"), QStringLiteral("main"), QStringLiteral("ALL RANKS"), 0ULL, rankCount);
        }
        if (m_serverRackView) { m_serverRackView->updateRanks(initialRanks); m_serverRackView->selectRank(preserveRank); }
        m_debugSessionActive = true; m_remoteAttachSession = false;
        if (m_workspacePerspective == QStringLiteral("Source")) setWorkspacePerspective(QStringLiteral("Debug"));
        updateDebugToolbarState(); syncRankCountChrome(rankCount); updateStatusTelemetry(QStringLiteral("0x00000000"), QStringLiteral("RUNNING"));
        const auto projCfg = gridlock::core::ConfigManager::instance().loadProjectSettings();
        const QString tArgs = !projCfg.targetArguments.isEmpty() ? projCfg.targetArguments : QString::fromStdString(projCfg.programArguments);
        const QString tWd = QString::fromStdString(projCfg.workingDirectory).trimmed().isEmpty() ? m_activeWorkspaceDir : QString::fromStdString(projCfg.workingDirectory).trimmed();
        m_coordinator->setTargetExecutionContext(tArgs, tWd);
        m_coordinator->launchParallelSession(binPath, rankCount, projCfg.mpiArgs.isEmpty() ? mpiArgs : projCfg.mpiArgs);
        recordRecentWorkspace(!m_activeWorkspaceDir.isEmpty() ? m_activeWorkspaceDir : binPath);
        syncProgramOutputForLaunch(rankCount);
        if (m_differentialGrid) {
            if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
                for (const auto& w : m_differentialGrid->getWatchExpressions()) gdbCoord->registerWatchVariable(QString::fromStdString(w));
            }
        }
        return;
    }

    m_debugSessionActive = true;
    if (m_workspacePerspective == QStringLiteral("Source")) setWorkspacePerspective(QStringLiteral("Debug"));
    updateDebugToolbarState(); rebuildDynamicSourceLineMap();
    if (m_disassemblyView) m_disassemblyView->setDeadlockCycleRanks({});
    const int rankCount = m_rankCountSpin ? std::clamp(m_rankCountSpin->value(), 1, 64) : 8;
    uint64_t stopAddr = 0x00401120ULL;
    QSet<uint64_t> bpAddrs;
    if (SourceCodeView* srcView = sourceCodeView()) {
        for (int bpLine : srcView->breakpoints()) if (const uint64_t a = sourceLineToAddress(bpLine); a != 0ULL) bpAddrs.insert(a);
    }
    if (!bpAddrs.isEmpty() && !bpAddrs.contains(stopAddr)) {
        for (uint64_t cur = stopAddr, step = 0; step < 64; ++step) {
            const uint64_t next = advanceRankProgramCounter(0, cur);
            if (next == cur || next == 0ULL || (next < cur && next != 0x00401149ULL)) break;
            cur = next;
            if (bpAddrs.contains(cur)) { stopAddr = cur; break; }
        }
    }
    const QString stopHex = QStringLiteral("0x%1").arg(stopAddr, 8, 16, QChar('0'));
    const int stopLine = addressToSourceLine(stopAddr);
    const int preserveRank = (m_serverRackView && m_serverRackView->selectedRank() >= 0) ? m_serverRackView->selectedRank() : 0;
    m_latestStates.clear(); m_rankLoopCounter.clear(); QList<RankState> rankList;
    for (int r = 0; r < rankCount; ++r) {
        RankState st; st.rankId = r; st.currentState = (r == preserveRank) ? QStringLiteral("stopped") : QStringLiteral("running");
        st.registers[16] = stopHex; st.currentFunction = QStringLiteral("main"); st.currentLine = stopLine; m_latestStates[r] = st; rankList.append(st);
    }
    m_focusedRank = preserveRank;
    if (m_serverRackView) { m_serverRackView->updateRanks(rankList); m_serverRackView->selectRank(preserveRank); }
    applyPreviewStopState(preserveRank, stopAddr, QStringLiteral("PAUSED (BREAKPOINT)"));
}

/** @brief Executes a single machine instruction on the active MPI rank via `IBackendCoordinator::stepInstruction`. */
bool MainWindow::stepInstructionWorkflow() {
    if (!m_previewFixtureOnly) {
        if (!m_debugSessionActive || !m_coordinator) return false;
        const int activeRank = activeSteppingRank(); m_focusedRank = activeRank;
        updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("STEPPING"));
        m_coordinator->stepInstruction(activeRank + 1);
        return true;
    }
    if (!m_disassemblyView) return false;
    m_debugSessionActive = true; updateDebugToolbarState();
    const int activeRank = activeSteppingRank(); m_focusedRank = activeRank;
    const uint64_t nextPc = advanceRankProgramCounter(activeRank, (m_disassemblyView->currentAddress() != 0ULL) ? m_disassemblyView->currentAddress() : 0x00401120ULL);
    const QString nextPcHex = QStringLiteral("0x%1").arg(nextPc, 8, 16, QChar('0'));
    RankState& st = m_latestStates[activeRank];
    st.rankId = activeRank; st.currentState = QStringLiteral("stopped"); st.registers[16] = nextPcHex; st.currentLine = addressToSourceLine(nextPc);
    if (m_serverRackView) m_serverRackView->updateRankState(activeRank, st);
    applyPreviewStopState(activeRank, nextPc, QStringLiteral("PAUSED (STEP)"));
    if (m_registerView) m_registerView->updateRegisters({{QStringLiteral("rip"), nextPcHex}, {QStringLiteral("rax"), QStringLiteral("0x%1").arg(m_rankLoopCounter.value(activeRank, 0), 16, 16, QChar('0'))}});
    return true;
}

/** @brief Resumes live execution on `IBackendCoordinator` until the next breakpoint, signal, or exit. */
bool MainWindow::continueExecutionWorkflow() {
    if (!m_previewFixtureOnly) {
        if (!m_debugSessionActive || !m_coordinator) return false;
        m_focusedRank = activeSteppingRank();
        if (SourceCodeView* srcView = sourceCodeView()) srcView->highlightExecutionLine(-1);
        for (auto& [rk, st] : m_latestStates) {
            if (st.currentState == QLatin1String("stopped")) { st.currentState = QStringLiteral("running"); if (m_serverRackView) m_serverRackView->updateRankState(rk, st); }
        }
        updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("RUNNING"));
        m_coordinator->continueExecution(-1);
        return true;
    }
    if (!m_disassemblyView) return false;
    m_debugSessionActive = true; updateDebugToolbarState();
    QSet<uint64_t> bpAddrs;
    if (SourceCodeView* srcView = sourceCodeView()) {
        for (int bpLine : srcView->breakpoints()) if (const uint64_t a = sourceLineToAddress(bpLine); a != 0ULL) bpAddrs.insert(a);
    }
    const int activeRank = activeSteppingRank(); m_focusedRank = activeRank;
    uint64_t cur = (m_disassemblyView->currentAddress() != 0ULL) ? m_disassemblyView->currentAddress() : 0x00401120ULL;
    for (int step = 0; step < 64; ++step) {
        const uint64_t next = advanceRankProgramCounter(activeRank, cur);
        if (next == cur || next == 0ULL || (next < cur && next != 0x00401149ULL)) break;
        cur = next;
        if (bpAddrs.contains(cur) || cur == 0x00401183ULL) break;
    }
    RankState& st = m_latestStates[activeRank];
    st.rankId = activeRank; st.currentState = QStringLiteral("stopped");
    st.registers[16] = QStringLiteral("0x%1").arg(cur, 8, 16, QChar('0')); st.currentLine = addressToSourceLine(cur);
    if (m_serverRackView) m_serverRackView->updateRankState(activeRank, st);
    applyPreviewStopState(activeRank, cur, QStringLiteral("PAUSED (BREAKPOINT)"));
    return true;
}

/** @brief Terminates the active debugging session, reaps child ranks, and restores static ELF inspection. */
void MainWindow::stopTargetWorkflow() {
    m_debugSessionActive = false; m_remoteAttachSession = false; m_rankLoopCounter.clear();
    if (!m_attachingSlurmJob && m_hpcBackend) {
        if (auto* slurm = dynamic_cast<gridlock::core::LocalSlurmBackend*>(m_hpcBackend); slurm && slurm->isJobActive()) {
            slurm->cancelActiveJob();
        }
    }
    if (m_isShuttingDown || m_isClosing) {
        if (m_runAction) m_runAction->setChecked(false);
        if (m_coordinator) m_coordinator->terminateSession();
        return;
    }
    updateDebugToolbarState();
    if (m_coordinator) m_coordinator->terminateSession();
    for (auto& [rankId, st] : m_latestStates) {
        st.currentState = QStringLiteral("idle"); st.registers[16] = QStringLiteral("—");
        if (m_serverRackView) m_serverRackView->updateRankState(rankId, st);
    }
    syncRankCountChrome();
    if (m_disassemblyView) { m_disassemblyView->setDeadlockCycleRanks({}); m_disassemblyView->setActiveSteppingRank(-1, 0ULL); }
    if (SourceCodeView* srcView = sourceCodeView()) srcView->highlightExecutionLine(-1);
    if (m_editorTabManager) m_editorTabManager->clearInlineVariableValues();
    clearAllInspectionDocks(m_variablesDockWidget, m_registerView, m_callStackDockWidget, m_mpiDiagnosticsWidget, networkLog(), m_memView);
    m_cachedStackFrames.clear(); m_cachedThreads.clear(); m_selectedFrameLevel.clear();
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("IDLE"));
    refreshStatusBarMemoryTelemetry();
    if (!m_previewFixtureOnly && !m_loadedBinaryPath.isEmpty() && QFileInfo::exists(m_loadedBinaryPath)) loadBinaryTarget(m_loadedBinaryPath);
}

/** @brief Maps a 1-based source line index to virtual instruction address using DWARF or dynamic mapping. */
uint64_t MainWindow::sourceLineToAddress(int lineOneBased) const {
    if (m_dynamicLineToAddress.isEmpty()) const_cast<MainWindow*>(this)->rebuildDynamicSourceLineMap();
    if (m_dynamicLineToAddress.contains(lineOneBased)) return m_dynamicLineToAddress.value(lineOneBased);
    if (m_useRealBinaryTarget && !m_dynamicLineToAddress.isEmpty()) {
        auto it = m_dynamicLineToAddress.lowerBound(lineOneBased);
        return (it != m_dynamicLineToAddress.end()) ? it.value() : (--it).value();
    }
    auto it = m_dynamicLineToAddress.upperBound(lineOneBased);
    return (it != m_dynamicLineToAddress.begin()) ? (--it).value() : 0x00401120ULL;
}

/** @brief Maps a virtual instruction address to 1-based source line using DWARF or dynamic mapping. */
int MainWindow::addressToSourceLine(uint64_t address) const {
    if (m_dynamicAddressToLine.isEmpty()) const_cast<MainWindow*>(this)->rebuildDynamicSourceLineMap();
    if (m_dynamicAddressToLine.contains(address)) return m_dynamicAddressToLine.value(address);
    auto it = m_dynamicAddressToLine.upperBound(address);
    if (it != m_dynamicAddressToLine.begin()) return (--it).value();
    return !m_dynamicAddressToLine.isEmpty() ? m_dynamicAddressToLine.begin().value() : 12;
}

}  // namespace gridlock::ui

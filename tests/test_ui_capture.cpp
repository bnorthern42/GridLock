/**
 * @file test_ui_capture.cpp
 * @brief Documentation screenshot capture harness (`GRIDLOCK_CAPTURE_SCREENSHOTS`) and theme/preferences visual tests.
 *
 * Implements tutorial and v0.8.7 tool documentation screenshot generators alongside
 * PreferencesDialog contrast / SLURM auto-detection and full-workstation theme propagation checks.
 */

#include "test_ui.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QtTest/QtTest>

#include <cmath>
#include <cstring>
#include <limits>
#include <unistd.h>

#include "../src/core/hpc/DeadlockAnalyzer.hpp"
#include "../src/core/hpc/GdbRankCoordinator.hpp"
#include "../src/core/managers/ThemeManager.hpp"
#include "../src/core/models/VariableTreeModel.hpp"
#include "../src/ui/EditorTabManager.hpp"
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/dialogs/ArrayPlotDialog.hpp"
#include "../src/ui/dialogs/CheckpointDialog.hpp"
#include "../src/ui/dialogs/CommandPaletteDialog.hpp"
#include "../src/ui/dialogs/PreferencesDialog.hpp"
#include "../src/ui/dialogs/ProgrammerCalculatorDialog.hpp"
#include "../src/ui/dialogs/SignalCatchpointDialog.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/views/MemView.hpp"
#include "../src/ui/views/RegisterView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
#include "../src/ui/views/SourceCodeView.hpp"
#include "../src/ui/widgets/DifferentialGrid.hpp"
#include "../src/ui/widgets/MpiDiagnosticsWidget.hpp"
#include "../src/ui/widgets/ProjectExplorerWidget.hpp"

namespace gridlock::tests {

namespace {
template <typename T> auto getActiveCanvasBg(T* w, int) -> decltype(w->activeCanvasBackgroundColor()) { return w->activeCanvasBackgroundColor(); }
template <typename T> QColor getActiveCanvasBg(T*, ...) { return QColor(QStringLiteral("#16181d")); }
template <typename T> auto callPropagateTheme(T* mw, int) -> decltype(mw->propagateThemeToAllWidgets(), void()) { mw->propagateThemeToAllWidgets(); }
template <typename T> void callPropagateTheme(T*, ...) {}
template <typename T> auto callAreAllChromeAligned(const T* mw, int) -> decltype(mw->areAllChromeToolbarsAndDockHeadersThemeAligned()) { return mw->areAllChromeToolbarsAndDockHeadersThemeAligned(); }
template <typename T> bool callAreAllChromeAligned(const T*, ...) { return false; }
}  // namespace

bool captureTutorialDocumentationScreenshots(gridlock::ui::MainWindow& mw, const QString& outDirPath) {
    QDir dir(outDirPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return false;
    const QString repoRoot = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp"))
                                 ? QDir::currentPath()
                                 : QFileInfo(QStringLiteral("..")).absoluteFilePath();
    const QString tutDir = repoRoot + QStringLiteral("/tutorial");
    mw.switchWorkspaceDirectory(repoRoot);
    mw.resize(1600, 960);
    mw.setWorkspacePerspective(QStringLiteral("Code"));
    if (auto* projExp = mw.findChild<gridlock::ui::ProjectExplorerWidget*>()) {
        if (auto* tree = projExp->findChild<QTreeView*>()) {
            if (auto* fsModel = qobject_cast<QFileSystemModel*>(tree->model())) {
                tree->setRootIndex(fsModel->setRootPath(tutDir));
            }
        }
    }
    auto* targetCombo = mw.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector"));
    auto* rankSpin = mw.findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin"));
    auto* tabMgr = mw.findChild<gridlock::ui::EditorTabManager*>();
    auto* rack = mw.serverRackView();
    auto* varsDock = mw.findChild<QDockWidget*>(QStringLiteral("VariablesDock"));
    auto* regsDock = mw.findChild<QDockWidget*>(QStringLiteral("RegisterViewDock"));
    auto* watchDock = mw.findChild<QDockWidget*>(QStringLiteral("WatchExpressionsDock"));
    auto* varModel = mw.findChild<gridlock::VariableTreeModel*>();
    auto* varsTree = varsDock ? varsDock->findChild<QTreeView*>() : nullptr;
    auto* regView = mw.findChild<gridlock::ui::RegisterView*>();
    auto* diffGrid = mw.findChild<gridlock::ui::DifferentialGrid*>();
    auto* mpiDiag = mw.findChild<gridlock::ui::MpiDiagnosticsWidget*>();
    auto* bottomTabs = mw.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    auto* memView = mw.findChild<gridlock::ui::MemView*>();
    auto* statusLbl = mw.findChild<QLabel*>(QStringLiteral("cutterStatusTelemetry"));
    if (!targetCombo || !rankSpin || !tabMgr || !rack || !varModel || !regView || !diffGrid || !mpiDiag) return false;

    targetCombo->blockSignals(true);
    targetCombo->clear();
    for (const QString& t : {QStringLiteral("inspection_demo"), QStringLiteral("deadlock_ring"),
                             QStringLiteral("collective_mismatch"), QStringLiteral("ghost_cell_drift"),
                             QStringLiteral("heap_imbalance"), QStringLiteral("tag_mismatch_stall"),
                             QStringLiteral("simd_struct_watch")}) {
        targetCombo->addItem(t + QStringLiteral(" (MPI)"), tutDir + QLatin1Char('/') + t + QStringLiteral(".cpp"));
    }
    targetCombo->blockSignals(false);

    auto setupScene = [&](const QString& baseName, int bpLine, const QString& bpCond,
                          int execLine, int topLine, int focusedRank, const QMap<QString, QString>& ghosts) -> uint64_t {
        mw.selectTargetByName(baseName);
        rankSpin->blockSignals(true); rankSpin->setValue(4); rankSpin->blockSignals(false);
        for (QToolBar* tb : mw.findChildren<QToolBar*>()) {
            for (QAction* act : tb->actions()) act->setEnabled(true);
        }
        const QString srcPath = tutDir + QLatin1Char('/') + baseName + QStringLiteral(".cpp");
        tabMgr->clearAllTabs();
        auto* sv = tabMgr->openFile(srcPath);
        QApplication::processEvents();
        uint64_t pc = mw.sourceLineToAddress(execLine);
        if (pc == 0ULL) pc = 0x0040113dULL;
        QList<gridlock::RankState> states;
        for (int r = 0; r < 4; ++r) {
            gridlock::RankState rs; rs.rankId = r; rs.currentState = QStringLiteral("stopped");
            rs.registers.insert(16, QStringLiteral("0x%1").arg(pc, 8, 16, QLatin1Char('0')));
            states.append(rs);
        }
        rack->blockSignals(true); rack->resetRanks(4); rack->updateRanks(states); rack->selectRank(focusedRank); rack->blockSignals(false);
        mw.seekToAddressOrExpression(QStringLiteral("0x%1").arg(pc, 8, 16, QLatin1Char('0')));
        if (sv) {
            sv->setBreakpoints(QSet<int>());
            if (bpLine > 0) {
                if (!bpCond.isEmpty()) sv->setConditionalBreakpoint(bpLine, bpCond);
                else sv->toggleBreakpoint(bpLine);
            }
            if (execLine > 0) sv->highlightExecutionLine(execLine);
            sv->setInlineVariableValues(ghosts);
            QTextCursor cur(sv->document()->findBlockByNumber(qMax(0, execLine - 1)));
            sv->setTextCursor(cur);
            QApplication::processEvents();
            if (sv->verticalScrollBar()) sv->verticalScrollBar()->setValue(qMax(0, topLine - 1));
        }
        return pc;
    };
    auto mkVar = [](const QString& n, const QString& v, const QString& t, int ref = 0) {
        return QJsonObject{{QStringLiteral("name"), n}, {QStringLiteral("value"), v},
                           {QStringLiteral("type"), t}, {QStringLiteral("variablesReference"), ref}};
    };

    // 1. tutorial_launch_toolbar.png
    if (varsDock) varsDock->raise();
    const uint64_t pc1 = setupScene(QStringLiteral("inspection_demo"), 19, QStringLiteral("rank == 2"), 23, 1, 0,
                                    {{QStringLiteral("rank"), QStringLiteral("0")}, {QStringLiteral("size"), QStringLiteral("4")}, {QStringLiteral("i"), QStringLiteral("3")}});
    varModel->clear();
    varModel->onLocalsUpdated(0, 0, QJsonArray{
        mkVar(QStringLiteral("rank"), QStringLiteral("0"), QStringLiteral("int")),
        mkVar(QStringLiteral("size"), QStringLiteral("4"), QStringLiteral("int")),
        mkVar(QStringLiteral("data"), QStringLiteral("{...} @ 0x7fffffffe120"), QStringLiteral("DeeplyNested[3]"), 101)});
    varModel->onLocalsUpdated(0, 101, QJsonArray{
        mkVar(QStringLiteral("[0]"), QStringLiteral("{id = 0, coords = {0.0, 0.0, 0.0}}"), QStringLiteral("DeeplyNested")),
        mkVar(QStringLiteral("[1]"), QStringLiteral("{id = 1, coords = {1.1, 2.2, 3.3}}"), QStringLiteral("DeeplyNested"), 102),
        mkVar(QStringLiteral("[2]"), QStringLiteral("{id = 2, coords = {2.2, 4.4, 6.6}}"), QStringLiteral("DeeplyNested"))});
    varModel->onLocalsUpdated(0, 102, QJsonArray{
        mkVar(QStringLiteral("id"), QStringLiteral("1"), QStringLiteral("int")),
        mkVar(QStringLiteral("values"), QStringLiteral("{0.0, 1.0, 2.0, 3.0, 4.0}"), QStringLiteral("double[5]")),
        mkVar(QStringLiteral("coords.x"), QStringLiteral("1.1f"), QStringLiteral("float")),
        mkVar(QStringLiteral("coords.y"), QStringLiteral("2.2f"), QStringLiteral("float")),
        mkVar(QStringLiteral("coords.z"), QStringLiteral("3.3f"), QStringLiteral("float"))});
    if (varsTree) varsTree->expandAll();
    mw.selectBottomToolGroup(QStringLiteral("Debug"));
    if (bottomTabs && memView) bottomTabs->setCurrentWidget(memView);
    if (statusLbl) statusLbl->setText(QStringLiteral("ARCH: x86_64 | MPI RANK: 0 | PC/SEEK: 0x%1 | STATUS: PAUSED (BRK) | STACK: 64 KB | HEAP: 128 KB | HOST RAM: 8.0 / 32.0 GB | CPU: 4.2%").arg(pc1, 8, 16, QLatin1Char('0')));
    QApplication::processEvents();
    {
        QPixmap launchPix = mw.grab();
        if (auto* mainTb = mw.findChild<QToolBar*>(QStringLiteral("MainToolbar"))) {
            QPainter p(&launchPix); p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(QPen(QColor(QStringLiteral("#38bdf8")), 2));
            p.drawRoundedRect(QRect(mainTb->mapTo(&mw, QPoint(4, 1)), QSize(qMin(mainTb->width() - 8, 1140), mainTb->height() - 2)), 5, 5);
            p.end();
        }
        if (!launchPix.save(dir.filePath(QStringLiteral("tutorial_launch_toolbar.png")), "PNG", 88)) return false;
    }

    // 2. tutorial_deadlock_analyzer.png
    if (varsDock) varsDock->raise();
    const uint64_t pc2 = setupScene(QStringLiteral("deadlock_ring"), 34, QString(), 34, 15, 0,
                                    {{QStringLiteral("rank"), QStringLiteral("0")}, {QStringLiteral("size"), QStringLiteral("4")},
                                     {QStringLiteral("next_rank"), QStringLiteral("1")}, {QStringLiteral("prev_rank"), QStringLiteral("3")}, {QStringLiteral("tag"), QStringLiteral("100")}});
    varModel->clear();
    varModel->onLocalsUpdated(0, 0, QJsonArray{
        mkVar(QStringLiteral("rank"), QStringLiteral("0"), QStringLiteral("int")),
        mkVar(QStringLiteral("size"), QStringLiteral("4"), QStringLiteral("int")),
        mkVar(QStringLiteral("next_rank"), QStringLiteral("1"), QStringLiteral("const int")),
        mkVar(QStringLiteral("prev_rank"), QStringLiteral("3"), QStringLiteral("const int")),
        mkVar(QStringLiteral("tag"), QStringLiteral("100"), QStringLiteral("const int")),
        mkVar(QStringLiteral("send_buf"), QStringLiteral("size=16384 (64 KB) @ 0x618040"), QStringLiteral("std::vector<int>")),
        mkVar(QStringLiteral("recv_buf"), QStringLiteral("size=16384 (64 KB) @ 0x628080"), QStringLiteral("std::vector<int>"))});
    mw.selectBottomToolGroup(QStringLiteral("MPI"));
    if (bottomTabs) bottomTabs->setCurrentWidget(mpiDiag);
    mpiDiag->beginLiveSession(4);
    for (int r = 0; r < 4; ++r) {
        mpiDiag->updateLiveRankRow(r, 42010 + r, QStringLiteral("WAITING (BLOCKED)"), QStringLiteral("MPI_Send"),
                                   QStringLiteral("-> Rank %1 (tag=100, 64 KB)").arg((r + 1) % 4),
                                   QStringLiteral("tutorial/deadlock_ring.cpp:34"), 4);
    }
    mpiDiag->onDeadlockDetected(gridlock::core::DeadlockInfo{
        0, QStringLiteral("MPI_Send"), QStringLiteral("tutorial/deadlock_ring.cpp"), 34,
        QStringLiteral("Wait-For Cycle: Rank 0 -> Rank 1 -> Rank 2 -> Rank 3 -> Rank 0 (MPI_Send, tag=100)"), false});
    mw.detectAndHighlightDeadlock({{0, 1}, {1, 2}, {2, 3}, {3, 0}});
    if (statusLbl) statusLbl->setText(QStringLiteral("ARCH: x86_64 | MPI RANK: 0 | PC/SEEK: 0x%1 | STATUS: CYCLE DETECTED | STACK: 64 KB | HEAP: 256 KB | HOST RAM: 8.0 / 32.0 GB | CPU: 1.1%").arg(pc2, 8, 16, QLatin1Char('0')));
    QApplication::processEvents();
    if (!mw.grab().save(dir.filePath(QStringLiteral("tutorial_deadlock_analyzer.png")), "PNG", 88)) return false;

    // 3. tutorial_differential_grid.png
    const uint64_t pc3 = setupScene(QStringLiteral("ghost_cell_drift"), 69, QStringLiteral("step == 3"), 69, 43, 2,
                                    {{QStringLiteral("step"), QStringLiteral("3")}, {QStringLiteral("rank"), QStringLiteral("2")},
                                     {QStringLiteral("faulty_rank"), QStringLiteral("2")}, {QStringLiteral("send_right"), QStringLiteral("nan")},
                                     {QStringLiteral("left_ghost"), QStringLiteral("110.0")}});
    if (watchDock) { watchDock->show(); watchDock->raise(); }
    diffGrid->clearWatches();
    diffGrid->setRowHidden(0, true);
    diffGrid->setColumnHidden(0, true);
    const QStringList sendRightVals{QStringLiteral("100.0"), QStringLiteral("110.0"), QStringLiteral("nan"), QStringLiteral("130.0")};
    const QStringList leftGhostVals{QStringLiteral("130.0"), QStringLiteral("100.0"), QStringLiteral("110.0"), QStringLiteral("nan")};
    const QStringList centerVals{QStringLiteral("100.0"), QStringLiteral("110.0"), QStringLiteral("120.0"), QStringLiteral("nan")};
    for (int r = 0; r < 4; ++r) {
        diffGrid->updateVariableDisplay(r, QStringLiteral("step"), QStringLiteral("3"));
        diffGrid->updateVariableDisplay(r, QStringLiteral("send_right"), sendRightVals[r]);
        diffGrid->updateVariableDisplay(r, QStringLiteral("left_ghost"), leftGhostVals[r]);
        diffGrid->updateVariableDisplay(r, QStringLiteral("stencil_center"), centerVals[r]);
    }
    for (int row = 1; row < diffGrid->rowCount(); ++row) {
        for (int col = 1; col < diffGrid->columnCount(); ++col) {
            if (QTableWidgetItem* it = diffGrid->item(row, col); it && it->text() == QStringLiteral("nan")) {
                auto* pill = new QLabel(QStringLiteral(" nan"), diffGrid);
                pill->setStyleSheet(QStringLiteral("background-color: #4c1d24; color: #ff8a8a; font-weight: bold; border-left: 3px solid #ef4444; padding-left: 4px;"));
                diffGrid->setCellWidget(row, col, pill);
            }
        }
    }
    mw.selectBottomToolGroup(QStringLiteral("Debug"));
    if (bottomTabs && memView) bottomTabs->setCurrentWidget(memView);
    if (statusLbl) statusLbl->setText(QStringLiteral("ARCH: x86_64 | MPI RANK: 2 | PC/SEEK: 0x%1 | STATUS: PAUSED (step == 3) | STACK: 64 KB | HEAP: 192 KB | HOST RAM: 8.0 / 32.0 GB | CPU: 3.6%").arg(pc3, 8, 16, QLatin1Char('0')));
    QApplication::processEvents();
    if (!mw.grab().save(dir.filePath(QStringLiteral("tutorial_differential_grid.png")), "PNG", 88)) return false;

    // 4. tutorial_register_simd.png
    const uint64_t pc4 = setupScene(QStringLiteral("simd_struct_watch"), 62, QString(), 62, 36, 0,
                                    {{QStringLiteral("rank"), QStringLiteral("0")}, {QStringLiteral("world_size"), QStringLiteral("4")},
                                     {QStringLiteral("local_trace"), QStringLiteral("46.0")}, {QStringLiteral("global_trace"), QStringLiteral("0.0")}});
    if (regsDock) { regsDock->show(); regsDock->raise(); }
    auto mkReg = [](const QString& n, const QString& v) { return QJsonObject{{QStringLiteral("name"), n}, {QStringLiteral("value"), v}}; };
    regView->clearRegisters();
    regView->updateRegisters(QJsonArray{
        mkReg(QStringLiteral("rip"), QStringLiteral("0x%1").arg(pc4 - 4, 16, 16, QLatin1Char('0'))),
        mkReg(QStringLiteral("rsp"), QStringLiteral("0x00007fffffffe060")),
        mkReg(QStringLiteral("rbp"), QStringLiteral("0x00007fffffffe110")),
        mkReg(QStringLiteral("rdi"), QStringLiteral("0x00007fffffffe080 (&packet)")),
        mkReg(QStringLiteral("eflags"), QStringLiteral("0x00000202 [ IF ]")),
        mkReg(QStringLiteral("xmm0"), QStringLiteral("{f32 = {0.0, 0.0, 0.0, 0.0}}")),
        mkReg(QStringLiteral("xmm1"), QStringLiteral("{f32 = {4.0, 11.0, 6.0, 7.0}}")),
        mkReg(QStringLiteral("xmm2"), QStringLiteral("{f32 = {8.0, 9.0, 12.0, 11.0}}")),
        mkReg(QStringLiteral("xmm3"), QStringLiteral("{f32 = {12.0, 13.0, 14.0, 13.0}}")),
        mkReg(QStringLiteral("ymm0"), QStringLiteral("0x0000000000000000_0000000000000000")),
        mkReg(QStringLiteral("ymm1"), QStringLiteral("0x40e0000040c00000_4130000040800000"))});
    regView->updateRegisters(QJsonArray{
        mkReg(QStringLiteral("rip"), QStringLiteral("0x%1").arg(pc4, 16, 16, QLatin1Char('0'))),
        mkReg(QStringLiteral("rsp"), QStringLiteral("0x00007fffffffe060")),
        mkReg(QStringLiteral("rbp"), QStringLiteral("0x00007fffffffe110")),
        mkReg(QStringLiteral("rdi"), QStringLiteral("0x00007fffffffe080 (&packet)")),
        mkReg(QStringLiteral("eflags"), QStringLiteral("0x00000206 [ PF IF ]")),
        mkReg(QStringLiteral("xmm0"), QStringLiteral("{f32 = {10.0, 1.0, 2.0, 3.0}}")),
        mkReg(QStringLiteral("xmm1"), QStringLiteral("{f32 = {4.0, 11.0, 6.0, 7.0}}")),
        mkReg(QStringLiteral("xmm2"), QStringLiteral("{f32 = {8.0, 9.0, 12.0, 11.0}}")),
        mkReg(QStringLiteral("xmm3"), QStringLiteral("{f32 = {12.0, 13.0, 14.0, 13.0}}")),
        mkReg(QStringLiteral("ymm0"), QStringLiteral("0x4040000040000000_3f80000041200000")),
        mkReg(QStringLiteral("ymm1"), QStringLiteral("0x40e0000040c00000_4130000040800000"))});
    if (memView) {
        QByteArray pktBytes(112, '\0');
        const uint8_t hdr[16] = {0xCA, 0xFE, 0x00, 0x04, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCD, 0xAB};
        std::memcpy(pktBytes.data(), hdr, 16);
        const float mat[16] = {10.f, 1.f, 2.f, 3.f, 4.f, 11.f, 6.f, 7.f, 8.f, 9.f, 12.f, 11.f, 12.f, 13.f, 14.f, 13.f};
        std::memcpy(pktBytes.data() + 16, mat, sizeof(mat));
        const double norms[4] = {114.0, 222.0, 410.0, 674.0};
        std::memcpy(pktBytes.data() + 80, norms, sizeof(norms));
        memView->setMemoryData(QStringLiteral("0x7fffffffe080"), pktBytes);
    }
    if (statusLbl) statusLbl->setText(QStringLiteral("ARCH: x86_64 | MPI RANK: 0 | PC/SEEK: 0x%1 | STATUS: PAUSED | STACK: 64 KB | HEAP: 128 KB | HOST RAM: 8.0 / 32.0 GB | CPU: 2.9%").arg(pc4, 8, 16, QLatin1Char('0')));
    QApplication::processEvents();
    const bool ok = mw.grab().save(dir.filePath(QStringLiteral("tutorial_register_simd.png")), "PNG", 88);
    if (varsDock) varsDock->raise();
    return ok;
}

/** @brief Captures 5 sanitized v0.8.7 tool screenshots into @p outputDir when GRIDLOCK_CAPTURE_SCREENSHOTS=1. */
bool captureV087ToolScreenshots(gridlock::ui::MainWindow& mw, const QString& outputDir) {
    QDir dir(outputDir); if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return false;
    gridlock::core::ThemeManager::instance().applyTheme(QStringLiteral("gridlock_slate"), false);
    mw.resize(1600, 960); mw.setWorkspacePerspective(QStringLiteral("Code")); mw.show(); QApplication::processEvents();
    {
        using gridlock::ui::CommandPaletteDialog; using gridlock::ui::PaletteItem;
        CommandPaletteDialog pal(&mw);
        pal.setItems({
            {PaletteItem::Category::Action, QStringLiteral("Debug: Run to Cursor"), QStringLiteral("Menu -> Debug"), QStringLiteral("Ctrl+F10"), {}},
            {PaletteItem::Category::Action, QStringLiteral("Debug: Step Over Instruction (nexti)"), QStringLiteral("Menu -> Debug"), QStringLiteral("Ctrl+Shift+F10"), {}},
            {PaletteItem::Category::Action, QStringLiteral("Debug: Process Checkpoints..."), QStringLiteral("Menu -> Debug"), QStringLiteral("Ctrl+Alt+K"), {}},
            {PaletteItem::Category::Action, QStringLiteral("Debug: Signals & Catchpoints..."), QStringLiteral("Menu -> Debug"), QStringLiteral("Ctrl+Alt+S"), {}},
            {PaletteItem::Category::Action, QStringLiteral("Debug: Break on Function Regex (rbreak)..."), QStringLiteral("Menu -> Debug"), QStringLiteral("rbreak"), {}},
            {PaletteItem::Category::Action, QStringLiteral("Tools: Programmer's Calculator"), QStringLiteral("Menu -> Tools"), QStringLiteral("Ctrl+Alt+C"), {}},
            {PaletteItem::Category::Action, QStringLiteral("Tools: Array & Field Plotter..."), QStringLiteral("Menu -> Tools"), QStringLiteral("Ctrl+Alt+G"), {}},
            {PaletteItem::Category::File, QStringLiteral("tutorial/catchpoint_exceptions.cpp"), QStringLiteral("tutorial"), QStringLiteral("C/C++"), {}},
            {PaletteItem::Category::Symbol, QStringLiteral("SolverConvergenceException"), QStringLiteral("catchpoint_exceptions.cpp:58"), QStringLiteral("Class"), {}},
            {PaletteItem::Category::Rank, QStringLiteral("Rank 2"), QStringLiteral("Focus MPI Rank 2 (PAUSED)"), QStringLiteral("BRK"), {}}});
        pal.openWithPrefix(QStringLiteral(">")); pal.resize(680, 440); QApplication::processEvents();
        QPixmap base = mw.grab(); QPainter p(&base); p.fillRect(base.rect(), QColor(0, 0, 0, 95));
        const QPixmap palPix = pal.grab(); p.drawPixmap((base.width() - palPix.width()) / 2, 86, palPix); p.end();
        pal.hide(); if (!base.save(dir.filePath(QStringLiteral("v087_command_palette.png")), "PNG", 88)) return false;
    }
    {
        gridlock::ui::ProgrammerCalculatorDialog calc(&mw);
        calc.setWordWidth(32); calc.setSigned(false); calc.setValue(0x3F8CCCCDULL);
        calc.resize(740, 620); calc.show(); QApplication::processEvents();
        if (!calc.grab().save(dir.filePath(QStringLiteral("v087_programmer_calculator.png")), "PNG", 88)) return false;
        calc.close();
    }
    {
        gridlock::ui::ArrayPlotDialog plotDlg(nullptr, &mw); plotDlg.setExpression(QStringLiteral("pressure_field[0..63]"));
        QVector<double> prev(64), curr(64);
        for (int i = 0; i < 64; ++i) { const double x = static_cast<double>(i) * 0.22; prev[i] = std::exp(-x * 0.08) * std::sin(x) * 12.0; curr[i] = std::exp(-x * 0.07) * std::sin(x + 0.35) * 14.5 + ((i >= 24 && i <= 30) ? 4.8 : 0.0); }
        curr[44] = std::numeric_limits<double>::quiet_NaN();
        plotDlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(prev.constData()), prev.size() * sizeof(double)), gridlock::ui::ArrayPlotDialog::ElementType::Float64, 1, 64);
        plotDlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(curr.constData()), curr.size() * sizeof(double)), gridlock::ui::ArrayPlotDialog::ElementType::Float64, 1, 64);
        plotDlg.resize(1040, 600); plotDlg.show(); QApplication::processEvents();
        if (!plotDlg.grab().save(dir.filePath(QStringLiteral("v087_array_plotter_1d_2d.png")), "PNG", 88)) return false;
        plotDlg.close();
    }
    {
        gridlock::GdbRankCoordinator coord; coord.initializeMockSession(4);
        coord.processGdbOutput(0, QStringLiteral("=thread-group-started,id=\"i1\",pid=\"4200\"\n*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401280\",func=\"solveSubdomain\",fullname=\"tutorial/catchpoint_exceptions.cpp\",line=\"80\",args=[]}\n"));
        coord.insertCatchpoint({CatchpointSpec::Kind::CppThrow, QStringLiteral("SolverConvergenceException"), -1, true});
        coord.insertCatchpoint({CatchpointSpec::Kind::Syscall, QStringLiteral("openat, mmap"), -1, true});
        coord.insertCatchpoint({CatchpointSpec::Kind::LoadLib, QStringLiteral("libmpi"), -1, true});
        gridlock::ui::SignalCatchpointDialog sigDlg(&coord, &mw);
        sigDlg.setSignalDispositionRow(QStringLiteral("SIGUSR1"), false, true, true); sigDlg.setSignalDispositionRow(QStringLiteral("SIGFPE"), true, true, false);
        sigDlg.resize(860, 620); sigDlg.show(); QApplication::processEvents();
        const QPixmap tab0 = sigDlg.grab();
        if (auto* tabs = sigDlg.findChild<QTabWidget*>(QStringLiteral("signalCatchpointTabs"))) tabs->setCurrentIndex(1);
        QApplication::processEvents();
        const QPixmap tab1 = sigDlg.grab();
        QPixmap both(tab0.width() + 16 + tab1.width(), std::max(tab0.height(), tab1.height())); both.fill(QColor(QStringLiteral("#0d1117")));
        { QPainter bp(&both); bp.drawPixmap(0, 0, tab0); bp.drawPixmap(tab0.width() + 16, 0, tab1); }
        if (!both.save(dir.filePath(QStringLiteral("v087_signals_and_catchpoints.png")), "PNG", 88)) return false;
        sigDlg.close();
        QTemporaryDir fakeProc; if (!fakeProc.isValid()) return false;
        const QString pDir = fakeProc.filePath(QStringLiteral("4200")), fdDir = pDir + QStringLiteral("/fd");
        if (!QDir().mkpath(fdDir)) return false;
        (void)::symlink("socket:[41022]", (fdDir + QStringLiteral("/3")).toUtf8().constData()); (void)::symlink("pipe:[41098]", (fdDir + QStringLiteral("/4")).toUtf8().constData());
        { QFile mf(pDir + QStringLiteral("/maps")); if (mf.open(QIODevice::WriteOnly)) mf.write("7ffff6000000-7ffff6400000 rw-s 00000000 00:18 2 /dev/shm/openmpi_vader_rank0\n"); }
        coord.createCheckpoint(0, QStringLiteral("before halo exchange"));
        coord.processGdbOutput(0, QStringLiteral("~\"checkpoint 1: fork returned pid 4242.\\n\"\n212^done\n"));
        coord.processGdbOutput(0, QStringLiteral("~\"  2 process 4249 at 0x4012b4, file catchpoint_exceptions.cpp, line 85\\n\"\n~\"* 1 process 4242 at 0x401280, file catchpoint_exceptions.cpp, line 80\\n\"\n~\"  0 process 4200 (main process) at 0x401190, file catchpoint_exceptions.cpp, line 121\\n\"\n213^done\n"));
        gridlock::ui::CheckpointDialog cpDlg(&coord, &mw); cpDlg.setProcRootForTesting(fakeProc.path());
        if (auto* diagBtn = cpDlg.findChild<QPushButton*>(QStringLiteral("checkpointDiagnosticsToggleBtn"))) diagBtn->setChecked(true);
        cpDlg.resize(1160, 560); cpDlg.show(); QApplication::processEvents();
        if (!cpDlg.grab().save(dir.filePath(QStringLiteral("v087_cow_checkpoints.png")), "PNG", 88)) return false;
        cpDlg.close();
    }
    return true;
}

}  // namespace gridlock::tests

/** @brief Verifies Preferences sidebar contrast under solarized_light, 12 themes, >=15 terminal fonts, and SLURM auto-detect. */
void TestMainWindowUI::testPreferencesSidebarContrastTerminalFontsAndSlurmAutoDetect() {
    auto& tm = gridlock::core::ThemeManager::instance();
    tm.initialize(); tm.applyTheme(QStringLiteral("solarized_light"));
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1280, 800); mainWindow.loadPreviewFixture();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    gridlock::ui::PreferencesDialog dialog(&mainWindow); dialog.resize(860, 620); dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog)); QApplication::processEvents();
    const QImage dlgImg = dialog.grab().toImage(); QVERIFY(!dlgImg.isNull());
    auto* categoryList = dialog.findChild<QListWidget*>();
    QVERIFY(categoryList != nullptr && categoryList->viewport() != nullptr && categoryList->item(0) != nullptr);
    const QImage catImg = categoryList->viewport()->grab().toImage();
    const QRect itemRect = categoryList->visualItemRect(categoryList->item(0)).intersected(catImg.rect());
    int darkHighlightPixels = 0, brightTextPixels = 0;
    for (int y = itemRect.top() + 3; y <= itemRect.bottom() - 3; ++y)
        for (int x = itemRect.left() + 42; x <= itemRect.right() - 6; ++x) {
            const int l = catImg.pixelColor(x, y).lightness();
            if (l < 150) ++darkHighlightPixels; else if (l > 210) ++brightTextPixels;
        }
    QVERIFY2(darkHighlightPixels >= 100 && brightTextPixels >= 15, qPrintable(QStringLiteral("Selected sidebar row pixel check failed: darkBg=%1 brightText=%2").arg(darkHighlightPixels).arg(brightTextPixels)));
    QVERIFY(categoryList->minimumWidth() >= 185 && categoryList->palette().color(QPalette::Inactive, QPalette::HighlightedText).lightness() >= 200);
    auto hasLightPixel = [](const QImage& img, int x0, int x1, int y0, int y1, int minL) {
        for (int y = qMax(0, y0); y < qMin(img.height(), y1); ++y) for (int x = qMax(0, x0); x < qMin(img.width(), x1); ++x) if (img.pixelColor(x, y).lightness() > minL) return true;
        return false;
    };
    QVERIFY2(hasLightPixel(catImg, 10, 30, catImg.height() - 30, catImg.height() - 10, 200) && hasLightPixel(dlgImg, dlgImg.width() - 60, dlgImg.width() - 20, dlgImg.height() / 2 - 20, dlgImg.height() / 2 + 20, 210), "PreferencesDialog canvas and sidebar must render solid light surfaces");

    auto* themeCombo = dialog.findChild<QComboBox*>(QStringLiteral("themeSelectorCombo"));
    QVERIFY2(themeCombo != nullptr && themeCombo->count() == 12, qPrintable(QStringLiteral("Expected 12 themes, got %1").arg(themeCombo ? themeCombo->count() : -1)));
    for (const QString& reqTheme : {QStringLiteral("nord_dark"), QStringLiteral("one_dark_pro"), QStringLiteral("dracula_dark"), QStringLiteral("one_light")})
        QVERIFY2(themeCombo->findData(reqTheme) >= 0, qPrintable(QStringLiteral("Missing theme in combo: ") + reqTheme));

    themeCombo->setCurrentIndex(themeCombo->findData(QStringLiteral("gruvbox_dark"))); QApplication::processEvents();
    bool foundAccentDarkText = false, foundCanvasLightText = false;
    for (QLabel* lbl : dialog.findChildren<QLabel*>()) {
        const bool hasHex = lbl->text().contains(QStringLiteral("#fabd2f"), Qt::CaseInsensitive) || lbl->toolTip().contains(QStringLiteral("#fabd2f"), Qt::CaseInsensitive);
        if ((lbl->text().contains(QStringLiteral("Accent")) || lbl->toolTip().contains(QStringLiteral("Accent"))) && hasHex) {
            QVERIFY2(lbl->palette().color(QPalette::WindowText).lightness() < 60 && lbl->styleSheet().contains(QStringLiteral("#16181d"), Qt::CaseInsensitive), "Bright #fabd2f Accent swatch must use dark text");
            foundAccentDarkText = true;
        }
        const bool hasCanvas = lbl->text().contains(QStringLiteral("#1d2021"), Qt::CaseInsensitive) || lbl->toolTip().contains(QStringLiteral("#1d2021"), Qt::CaseInsensitive);
        if ((lbl->text().contains(QStringLiteral("Canvas")) || lbl->toolTip().contains(QStringLiteral("Canvas"))) && hasCanvas) {
            QVERIFY2(lbl->palette().color(QPalette::WindowText).lightness() > 200, "Dark #1d2021 Canvas swatch must use light text");
            foundCanvasLightText = true;
        }
    }
    QVERIFY2(foundAccentDarkText && foundCanvasLightText, "Palette swatches must include hex code and luminance-aware foreground contrast");

    auto* termFontCombo = dialog.findChild<QComboBox*>(QStringLiteral("terminalFontFamilyCombo"));
    QVERIFY2(termFontCombo != nullptr && termFontCombo->count() >= 15, "Terminal Font Family combo must offer >= 15 monospace font families");
    for (QPushButton* btn : dialog.findChildren<QPushButton*>()) if (btn->text().contains(QStringLiteral("Browse"), Qt::CaseInsensitive)) QVERIFY2(btn->minimumWidth() >= 88, qPrintable(QStringLiteral("Browse button minWidth %1 < 88").arg(btn->minimumWidth())));
    const QString sampleSbatch = QStringLiteral("#!/bin/bash\n#SBATCH --job-name=wpt_fem_solver\n#SBATCH --account=ttu_hpc\n#SBATCH --partition=gpu_a100\n#SBATCH --qos=high\n#SBATCH --nodes=4\n#SBATCH --ntasks-per-node=16\n#SBATCH --cpus-per-task=4\n#SBATCH --mem=128G\n#SBATCH --time=06:30:00\n#SBATCH --gres=gpu:a100:2\n#SBATCH --constraint=nvlink\n#SBATCH --output= solver_%j.out\nmodule load openmpi/5.0 gcc/13\nsrun --mpi=pmix ./solver {FILE}\n");
    QVERIFY(dialog.parseSlurmBatchScript(sampleSbatch));
    for (const auto& [k, v] : QList<QPair<QString, QString>>{{"job_name", "wpt_fem_solver"}, {"account", "ttu_hpc"}, {"partition", "gpu_a100"}, {"qos", "high"}, {"nodes", "4"}, {"ntasks_per_node", "16"}, {"cpus_per_task", "4"}, {"mem", "128G"}, {"time", "06:30:00"}, {"gres_gpu", "a100:2"}, {"constraint", "nvlink"}}) QCOMPARE(dialog.slurmFieldValue(k), v);
    QVERIFY(dialog.slurmFieldValue(QStringLiteral("modules")).contains(QStringLiteral("openmpi/5.0")));
    tm.applyTheme(QStringLiteral("gridlock_slate"));
}

/** @brief Verifies full workstation light theme propagation, non-colliding palette swatches, theme switching, and toolbar dropdown clearance. */
void TestMainWindowUI::testFullWorkstationLightThemePropagationAndLineCountHeadroom() {
    auto& tm = gridlock::core::ThemeManager::instance(); const QStringList themeIds = tm.availableThemeIds();
    QVERIFY(themeIds.size() >= 12 && !tm.paletteForTheme(QStringLiteral("one_light")).isDark && tm.paletteForTheme(QStringLiteral("nord_dark")).isDark);
    tm.applyTheme(QStringLiteral("catppuccin_latte")); QVERIFY(!tm.isDarkMode()); tm.applyTheme(QStringLiteral("solarized_light")); QVERIFY(!tm.isDarkMode());
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    gridlock::tests::callPropagateTheme(&mainWindow, 0); QApplication::processEvents();
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>(); auto* diffGrid = mainWindow.findChild<gridlock::ui::DifferentialGrid*>(); auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>(); auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(memView && diffGrid && serverRack && disasmView);
    QVERIFY2(gridlock::tests::getActiveCanvasBg(memView, 0).lightness() >= 200 && gridlock::tests::getActiveCanvasBg(diffGrid, 0).lightness() >= 200 && gridlock::tests::getActiveCanvasBg(serverRack, 0).lightness() >= 200, "Inspection views must have light canvas >= 200 in solarized_light");
    QVERIFY2(gridlock::tests::callAreAllChromeAligned(&mainWindow, 0), "All chrome toolbars and dock headers must be theme aligned without hardcoded #16181d/#1b1e24");
    auto* sigLabel = disasmView->findChild<QLabel*>(QStringLiteral("disasmSignatureLabel")); QVERIFY(sigLabel != nullptr);
    const auto match = QRegularExpression(QStringLiteral("color:\\s*(#[0-9a-fA-F]{3,8}|rgba?\\([^)]+\\))")).match(sigLabel->styleSheet());
    const QColor sigTextColor = match.hasMatch() ? QColor(match.captured(1)) : sigLabel->palette().color(QPalette::WindowText);
    QVERIFY2(sigTextColor.isValid() && sigTextColor.lightness() < 120, qPrintable(QString("Decompiler signature text color lightness %1 >= 120").arg(sigTextColor.lightness())));
    tm.applyTheme(QStringLiteral("gridlock_slate")); gridlock::tests::callPropagateTheme(&mainWindow, 0); QApplication::processEvents();
    QVERIFY2(gridlock::tests::getActiveCanvasBg(memView, 0).lightness() < 60, "MemView canvas background must be dark (< 60 lightness) in gridlock_slate");
    auto* mainTb = mainWindow.findChild<QToolBar*>(QStringLiteral("MainToolbar")); QVERIFY(mainTb != nullptr);
    int popupBtnsChecked = 0;
    for (QToolButton* btn : mainTb->findChildren<QToolButton*>()) {
        if (!btn->isVisible() || btn->popupMode() == QToolButton::DelayedPopup) continue;
        const int adv = btn->fontMetrics().horizontalAdvance(btn->text());
        QVERIFY2(btn->width() >= btn->sizeHint().width() && btn->width() >= adv + 34, qPrintable(QStringLiteral("Popup QToolButton '%1' (%2) width=%3 sizeHint=%4 textAdv=%5").arg(btn->text(), btn->objectName()).arg(btn->width()).arg(btn->sizeHint().width()).arg(adv)));
        ++popupBtnsChecked;
    }
    QVERIFY2(popupBtnsChecked >= 3, "Expected Build, Into, and Launch Options popup QToolButtons in #MainToolbar");
    if (!qEnvironmentVariableIsEmpty("GRIDLOCK_SAVE_TOOLBAR_SHOT")) QVERIFY(mainTb->grab().save(QStringLiteral("/tmp/toolbar_dropdown_verify.png")));
    gridlock::ui::PreferencesDialog prefsDialog; prefsDialog.resize(860, 620); prefsDialog.show(); QApplication::processEvents();
    QList<QLabel*> swatchBoxes;
    for (QLabel* lbl : prefsDialog.findChildren<QLabel*>()) for (const QString& r : {QStringLiteral("Canvas"), QStringLiteral("Header"), QStringLiteral("Border"), QStringLiteral("Accent"), QStringLiteral("Text")}) if (lbl->text().trimmed() == r) { swatchBoxes.append(lbl); break; }
    QVERIFY2(swatchBoxes.size() == 5 && swatchBoxes.first()->toolTip().contains('#'), "Expected 5 spaced role swatch boxes with hex tooltips");
}

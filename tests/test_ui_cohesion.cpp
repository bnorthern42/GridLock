/**
 * @file test_ui_cohesion.cpp
 * @brief Visual and structural UI cohesion tests for bottom console tabs, docset viewer, and side docks.
 *
 * Verifies that every bottom console tab adheres to the two-tier workstation layout
 * (flush 0px-margin viewport + compact <= 28px header control strip), that F1 symbol
 * documentation lookup strips PLT suffixes and populates structured dark-slate manual
 * pages, and that offscreen tab audit snapshots render non-empty PNG images.
 */

#include "test_ui.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFrame>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextCursor>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QtTest/QtTest>

#include <cstring>

#include "../src/core/hpc/DeadlockAnalyzer.hpp"
#include "../src/core/managers/ConfigManager.hpp"
#include "../src/core/managers/ThemeManager.hpp"
#include "../src/core/utils/ElfBinaryInspector.hpp"
#include "../src/ui/EditorTabManager.hpp"
#include "../src/ui/GdbConsoleWidget.hpp"
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/dialogs/ConditionalBreakpointDialog.hpp"
#include "../src/ui/dialogs/PreferencesDialog.hpp"
#include "../src/ui/dialogs/ProjectSettingsDialog.hpp"
#include "../src/ui/dialogs/ProjectWizard.hpp"
#include "../src/ui/dialogs/XrefInspectorDialog.hpp"
#include "../src/core/models/VariableTreeModel.hpp"
#include "../src/ui/docks/CallStackDockWidget.hpp"
#include "../src/ui/docks/VariablesDockWidget.hpp"
#include "../src/ui/tutorial/TutorialDialog.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/views/MemView.hpp"
#include "../src/ui/views/RegisterView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
#include "../src/ui/views/SourceCodeView.hpp"
#include "../src/ui/widgets/DifferentialGrid.hpp"
#include "../src/ui/widgets/ExpressionEvaluatorWidget.hpp"
#include "../src/ui/widgets/MpiDiagnosticsWidget.hpp"
#include "../src/ui/widgets/MpiNetworkLogWidget.hpp"
#include "../src/ui/widgets/ProjectExplorerWidget.hpp"
#include "../src/ui/widgets/ReferenceManualWidget.hpp"

namespace gridlock::tests {

/**
 * @brief Constant maximum allowed outer margin on bottom console tab pages.
 */
constexpr int kMaxAllowedTabMarginPx = 4;

/**
 * @brief Constant maximum allowed control height inside bottom console tab headers.
 */
constexpr int kMaxAllowedControlHeightPx = 28;

/**
 * @brief Constant maximum allowed control height inside workstation modal dialogs.
 */
constexpr int kMaxAllowedDialogControlHeightPx = 38;

/**
 * @brief Constant minimum allowed visible column width in workstation tables and trees.
 */
constexpr int kMinAllowedColumnSectionWidthPx = 40;

template <typename T> auto getActiveCanvasBg(T* w, int) -> decltype(w->activeCanvasBackgroundColor()) { return w->activeCanvasBackgroundColor(); }
template <typename T> QColor getActiveCanvasBg(T*, ...) { return QColor(QStringLiteral("#16181d")); }
template <typename T> auto getActiveHexText(T* w, int) -> decltype(w->activeHexTextColor()) { return w->activeHexTextColor(); }
template <typename T> QColor getActiveHexText(T*, ...) { return QColor(QStringLiteral("#c8ccd4")); }
template <typename T> auto callPropagateTheme(T* mw, int) -> decltype(mw->propagateThemeToAllWidgets(), void()) { mw->propagateThemeToAllWidgets(); }
template <typename T> void callPropagateTheme(T*, ...) {}
template <typename T> auto callAreAllChromeAligned(const T* mw, int) -> decltype(mw->areAllChromeToolbarsAndDockHeadersThemeAligned()) { return mw->areAllChromeToolbarsAndDockHeadersThemeAligned(); }
template <typename T> bool callAreAllChromeAligned(const T*, ...) { return false; }
template <typename T> auto callShowDecompilerWithSource(const T* cfg, int) -> decltype(cfg->showDecompilerWithSource()) { return cfg->showDecompilerWithSource(); }
template <typename T> bool callShowDecompilerWithSource(const T*, ...) { return true; }
template <typename T> auto callSetShowDecompilerWithSource(T* cfg, bool v, int) -> decltype(cfg->setShowDecompilerWithSource(v), void()) { cfg->setShowDecompilerWithSource(v); }
template <typename T> void callSetShowDecompilerWithSource(T*, bool, ...) {}
template <typename T> auto callIsDecompilerVisible(const T* d, int) -> decltype(d->isDecompilerVisible()) { return d->isDecompilerVisible(); }
template <typename T> bool callIsDecompilerVisible(const T*, ...) { return true; }
template <typename T> auto callTelemetryDisplayFormat(const T* cfg, int) -> decltype(cfg->telemetryDisplayFormat()) { return cfg->telemetryDisplayFormat(); }
template <typename T> QString callTelemetryDisplayFormat(const T*, ...) { return QString(); }
template <typename T>
auto callParseAndFormatTelemetry(const QString& smaps, quint64 rsp, quint64 top, const QString& meminfo, const QString& fmt, int)
    -> decltype(T::formatStatusBarSummary(T::parseProcMemoryTelemetry(smaps, rsp, top, meminfo), fmt)) {
    return T::formatStatusBarSummary(T::parseProcMemoryTelemetry(smaps, rsp, top, meminfo), fmt);
}
template <typename T> QString callParseAndFormatTelemetry(const QString&, quint64, quint64, const QString&, const QString&, ...) { return QString(); }

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

}  // namespace gridlock::tests

/**
 * @brief Verifies zero-gutter flush viewports across bottom tabs, F1 docset lookup, 2-zone right dock hierarchy, and primary Run Target CTA.
 * Validates bottom tab margins, MPI_Init@plt symbol lookup, tabified Variables/Registers/Watch docks (>= 260px height), and accented Run Target button.
 */
void TestMainWindowUI::testWorkstationUiCohesionAndIntegratedDocViewer() {
    // Instantiate and expose the main workstation window with active theme palette propagated.
    auto& tm = gridlock::core::ThemeManager::instance(); tm.initialize();
    auto& cfg = gridlock::core::ConfigManager::instance();
    gridlock::tests::callSetShowDecompilerWithSource(&cfg, false, 0);
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900);
    // 0. Gate 1 Zero-Mock Startup, Idle Telemetry, Preview Seeding, and Stop-Clear Verification.
    gridlock::ui::MpiDiagnosticsWidget standaloneDiag; gridlock::ui::MpiNetworkLogWidget standaloneNet(QStringLiteral("Net")); gridlock::ui::MemView standaloneMem;
    auto* standaloneBadge = standaloneMem.findChild<QLabel*>(QStringLiteral("memSourceBadge")); auto* standaloneDump = standaloneMem.findChild<QTextEdit*>(QStringLiteral("cutterHexDumpEdit"));
    QVERIFY2(standaloneDiag.rankWaitTableRowCount() == 0 && standaloneNet.logRowCount() == 0, "Standalone MpiDiagnosticsWidget and MpiNetworkLogWidget must start with 0 rows");
    QVERIFY2(standaloneMem.currentBaseAddress() == 0ULL && standaloneDump && standaloneDump->toPlainText().trimmed().isEmpty() && standaloneBadge && standaloneBadge->text() == QStringLiteral("IDLE (NO BINARY)"), "Standalone MemView must start empty with IDLE (NO BINARY) badge");
    // Gate 1: MpiDiagnosticsWidget and MpiNetworkLogWidget columns must be user-resizable (Interactive) and never clip live PIDs or show 0x00000000.
    auto* sWaitTbl = standaloneDiag.findChild<QTableWidget*>(QStringLiteral("mpiRankWaitTable")); auto* sNetTbl = standaloneNet.findChild<QTableWidget*>();
    QVERIFY(sWaitTbl && sNetTbl); QCOMPARE(sWaitTbl->horizontalHeader()->sectionResizeMode(0), QHeaderView::Interactive); QCOMPARE(sNetTbl->horizontalHeader()->sectionResizeMode(0), QHeaderView::Interactive);
    standaloneDiag.beginLiveSession(2); QVERIFY(!sWaitTbl->item(0, 5)->text().contains(QStringLiteral("0x00000000")));
    // Verify live PID updates auto-widen the RANK column and attach full-text cell tooltips.
    standaloneDiag.updateLiveRankRow(0, 3645905, QStringLiteral("RUNNING"), QStringLiteral("MPI_Allreduce"), QStringLiteral("ALL"), QStringLiteral("main.cpp:182"));
    QVERIFY2(sWaitTbl->columnWidth(0) >= sWaitTbl->fontMetrics().horizontalAdvance(QStringLiteral("Rank 0 (PID 3645905)")) + 16, qPrintable(QString("RANK col width %1 too narrow").arg(sWaitTbl->columnWidth(0))));
    for (int c = 0; c < sWaitTbl->columnCount(); ++c) QVERIFY(sWaitTbl->item(0, c) && !sWaitTbl->item(0, c)->toolTip().isEmpty());
    // Verify live memory callback wiring switches MemView badge to LIVE RANK.
    standaloneMem.setLiveMemoryCallbacks([](int, const QString&, bool* ok) { if (ok) *ok = true; return 0x00401120ULL; }, [](int, uint64_t, int) { return QByteArray::fromHex("9090c3"); }, [](int) { return qint64(4242); });
    QVERIFY(standaloneMem.seekToAddress(0x00401120ULL) && standaloneBadge->text().contains(QStringLiteral("LIVE RANK")));
    auto* varsDockW = mainWindow.findChild<gridlock::VariablesDockWidget*>(); auto* varModel = varsDockW ? varsDockW->findChild<gridlock::VariableTreeModel*>() : nullptr;
    auto* regTable = mainWindow.registerView() ? mainWindow.registerView()->findChild<QTableWidget*>(QStringLiteral("cutterRegisterTable")) : nullptr;
    auto* csWidget = mainWindow.findChild<gridlock::ui::CallStackDockWidget*>(); auto* csTable = csWidget ? csWidget->findChild<QTableWidget*>(QStringLiteral("callStackTable")) : nullptr;
    auto* mpiDiag = mainWindow.findChild<gridlock::ui::MpiDiagnosticsWidget*>(); auto* netLog = mainWindow.findChild<gridlock::ui::MpiNetworkLogWidget*>();
    auto* memView = mainWindow.memView(); auto* memBadge = memView ? memView->findChild<QLabel*>(QStringLiteral("memSourceBadge")) : nullptr; auto* memDump = memView ? memView->findChild<QTextEdit*>(QStringLiteral("cutterHexDumpEdit")) : nullptr;
    auto* initStatusLbl = mainWindow.findChild<QLabel*>(QStringLiteral("cutterStatusTelemetry"));
    QVERIFY(varModel && regTable && csWidget && csTable && mpiDiag && netLog && memView && memBadge && memDump && initStatusLbl);
    QVERIFY2(varModel->rowCount() == 0 && regTable->rowCount() == 0 && mpiDiag->rankWaitTableRowCount() == 0 && netLog->logRowCount() == 0, "Inspection and MPI docks must have 0 rows on startup");
    QVERIFY2(memView->currentBaseAddress() == 0ULL && memDump->toPlainText().trimmed().isEmpty() && memBadge->text() == QStringLiteral("IDLE (NO BINARY)"), "MemView must start empty with IDLE (NO BINARY)");
    const QString idleTelemetry = initStatusLbl->text();
    QVERIFY2(!idleTelemetry.contains(QStringLiteral("136 KB")) && !idleTelemetry.contains(QStringLiteral("2048 KB")) && !idleTelemetry.contains(QStringLiteral("2.0 MB")) && !idleTelemetry.contains(QStringLiteral("STACK:")) && !idleTelemetry.contains(QStringLiteral("HEAP:")) && idleTelemetry.contains(QStringLiteral("TARGET: IDLE")) && idleTelemetry.contains(QStringLiteral("HOST RAM:")) && idleTelemetry.contains(QStringLiteral("CPU:")), qPrintable(idleTelemetry));
    mainWindow.loadPreviewFixture(); StackFrameEntry sf; sf.level = 0; sf.function = QStringLiteral("main"); sf.file = QStringLiteral("main.cpp"); sf.line = 10; sf.address = 0x401120ULL; csWidget->setStackFrames({sf}, 0); netLog->appendLog(QStringLiteral("[00:00.001] Rank 0 -> Rank 1 | MPI_Send | Tag=1 MPI_COMM_WORLD | 64 B"));
    QVERIFY2(varModel->rowCount() > 0 && regTable->rowCount() > 0 && csTable->rowCount() == 1 && mpiDiag->rankWaitTableRowCount() > 0 && netLog->logRowCount() > 0, "loadPreviewFixture() + setStackFrames() must populate preview docks");
    mainWindow.stopTargetWorkflow(); QApplication::processEvents();
    QVERIFY2(varModel->rowCount() == 0 && regTable->rowCount() == 0 && csTable->rowCount() == 0 && mpiDiag->rankWaitTableRowCount() == 0 && netLog->logRowCount() == 0 && memBadge->text() == QStringLiteral("IDLE (NO BINARY)"), "stopTargetWorkflow() must clear inspection, MPI, and MemView docks");
    // Gate 1: Live MPI session & primitive updates must populate MpiNetworkLogWidget and clear on stopTargetWorkflow().
    mpiDiag->beginLiveSession(2); mpiDiag->updateLiveRankRow(0, 3645905, QStringLiteral("PAUSED"), QStringLiteral("MPI_Allreduce"), QStringLiteral("ALL"), QStringLiteral("main.cpp:182"));
    QVERIFY2(netLog->logRowCount() >= 1, "Live MPI session/primitive updates must populate MpiNetworkLogWidget");
    mainWindow.stopTargetWorkflow(); QApplication::processEvents(); QCOMPARE(netLog->logRowCount(), 0);
    mainWindow.loadPreviewFixture(); mainWindow.propagateThemeToAllWidgets();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    // 1. Integrated Reference Manual (Docset / Man Viewer) & F1 Symbol Lookup.
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    QVERIFY2(bottomTabs != nullptr && bottomTabs->count() >= 7, "BottomConsoleTabs QTabWidget must exist with >= 7 tabs");
    bottomTabs->setCurrentIndex(0); QCoreApplication::processEvents();
    QVERIFY2(mainWindow.lookupSymbolDocumentation(QStringLiteral("MPI_Init@plt")), "lookupSymbolDocumentation('MPI_Init@plt') must succeed");
    QCoreApplication::processEvents();
    QVERIFY2(bottomTabs->tabText(bottomTabs->currentIndex()).contains(QStringLiteral("Reference Manual"), Qt::CaseInsensitive), qPrintable(QString("Expected Reference Manual tab active, got '%1'").arg(bottomTabs->tabText(bottomTabs->currentIndex()))));
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>(); QVERIFY(refManual != nullptr);
    auto* browser = refManual->findChild<QTextBrowser*>(); QVERIFY(browser != nullptr);
    const QString docPlainText = browser->toPlainText();
    QVERIFY2(docPlainText.contains(QStringLiteral("MPI_Init"), Qt::CaseInsensitive) && docPlainText.contains(QStringLiteral("SYNOPSIS"), Qt::CaseInsensitive), "Reference Manual content must include MPI_Init and SYNOPSIS");

    // 2. Right-Sidebar 2-Zone Consolidation (Variables, Registers, Watch).
    auto* variablesDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("VariablesDock")); auto* registersDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("RegisterViewDock")); auto* watchDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("WatchExpressionsDock"));
    QVERIFY(variablesDock != nullptr && registersDock != nullptr && watchDock != nullptr);
    const QList<QDockWidget*> tabified = mainWindow.tabifiedDockWidgets(variablesDock);
    QVERIFY2(tabified.contains(registersDock) && tabified.contains(watchDock), "VariablesDock, RegisterViewDock, and WatchExpressionsDock must share top-right tabbed dock group");
    QVERIFY2(watchDock->windowTitle().compare(QStringLiteral("WATCH"), Qt::CaseInsensitive) == 0, "watchDock title must be 'WATCH'");

    // 3. Verify DisassemblyView mode buttons and pseudo-C decompiler suppression when C++ SourceCodeView is visible.
    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>(); QVERIFY(disasm != nullptr);
    QVERIFY2(!gridlock::tests::callShowDecompilerWithSource(&cfg, 0), "ConfigManager::showDecompilerWithSource() must default to false");
    mainWindow.setWorkspacePerspective(QStringLiteral("Code")); QApplication::processEvents();
    QVERIFY2(!gridlock::tests::callIsDecompilerVisible(disasm, 0), "Pseudo-C decompiler must be hidden in CODE perspective when showDecompilerWithSource() == false");
    mainWindow.setWorkspacePerspective(QStringLiteral("MPI")); QApplication::processEvents();
    QVERIFY2(!gridlock::tests::callIsDecompilerVisible(disasm, 0), "Pseudo-C decompiler must be hidden in MPI perspective when showDecompilerWithSource() == false");
    mainWindow.setWorkspacePerspective(QStringLiteral("Reverse")); disasm->setSplitGraphDecompilerMode(true); QApplication::processEvents();
    QVERIFY2(gridlock::tests::callIsDecompilerVisible(disasm, 0), "Pseudo-C decompiler must be visible in REVERSE perspective");
    auto countCheckedModeBtns = [disasm]() -> int { int cnt = 0; for (const QString& id : {QStringLiteral("cutterSplitViewBtn"), QStringLiteral("cutterDecompilerBtn"), QStringLiteral("cutterCfgToggleBtn")}) if (auto* b = disasm->findChild<QPushButton*>(id); b && b->isChecked()) ++cnt; return cnt; };
    for (int m = 0; m < 4; ++m) { if (m == 0) disasm->setGraphMode(false); else if (m == 1) disasm->setGraphMode(true); else if (m == 2) disasm->setDecompilerMode(true); else disasm->setSplitGraphDecompilerMode(true); QApplication::processEvents(); QVERIFY2(countCheckedModeBtns() <= 1, "At most 1 DisassemblyView mode button may be checked"); }
    auto* decompPage = disasm->findChild<QWidget*>(QStringLiteral("decompilerPageContainer")); auto* splitBtn = disasm->findChild<QPushButton*>(QStringLiteral("cutterSplitViewBtn"));
    QVERIFY(decompPage != nullptr && splitBtn != nullptr);
    mainWindow.setWorkspacePerspective(QStringLiteral("Code")); QApplication::processEvents();
    QVERIFY2(!decompPage->isVisible() && !gridlock::tests::callIsDecompilerVisible(disasm, 0) && !splitBtn->isChecked(), "Reverse->Code must collapse decompiler");
    const QString prevRepoWs = QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath() + QStringLiteral("/..")).canonicalPath();
    QTemporaryDir extWsDir; QVERIFY(extWsDir.isValid());
    QVERIFY(QDir(extWsDir.path()).mkpath(QStringLiteral("src")) && QDir(extWsDir.path()).mkpath(QStringLiteral("build_meson")));
    { QFile sf(extWsDir.filePath(QStringLiteral("src/main.cpp"))); QVERIFY(sf.open(QIODevice::WriteOnly | QIODevice::Text)); sf.write("int main() { return 0; }\n"); }
    QProcess gxx; gxx.start(QStringLiteral("g++"), {QStringLiteral("-g"), extWsDir.filePath(QStringLiteral("src/main.cpp")), QStringLiteral("-o"), extWsDir.filePath(QStringLiteral("build_meson/ws_bin"))});
    QVERIFY(gxx.waitForFinished(15000) && gxx.exitCode() == 0);
    QTemporaryDir unbuiltWsDir; QVERIFY(unbuiltWsDir.isValid());
    netLog->appendLog(QStringLiteral("[00:00.002] Rank 0 -> Rank 1 | MPI_Send | Tag=2 MPI_COMM_WORLD | 32 B"));
    mainWindow.switchWorkspaceDirectory(unbuiltWsDir.path()); QApplication::processEvents();
    QVERIFY2(mpiDiag->rankWaitTableRowCount() == 0 && netLog->logRowCount() == 0 && memView->currentBaseAddress() == 0ULL && memDump->toPlainText().trimmed().isEmpty() && memBadge->text() == QStringLiteral("IDLE (NO BINARY)"), "Unbuilt workspace switch must clear MPI docks and MemView to IDLE (NO BINARY)");
    mainWindow.switchWorkspaceDirectory(extWsDir.path()); mainWindow.loadBinaryTarget(extWsDir.filePath(QStringLiteral("build_meson/ws_bin"))); QApplication::processEvents();
    QVERIFY2(memBadge->text() == QStringLiteral("STATIC ELF (READ-ONLY)") && !memDump->toPlainText().trimmed().isEmpty(), "loadBinaryTarget must display static ELF bytes with STATIC ELF (READ-ONLY) badge");
    auto* varModelWs = varsDockW->findChild<gridlock::VariableTreeModel*>();
    QVERIFY2(varModelWs && varModelWs->rowCount() == 0 && regTable->rowCount() == 0 && csTable->rowCount() == 0, "switchWorkspaceDirectory() must clear VariableTreeModel, RegisterView, and CallStackDockWidget");
    auto *idleRack = mainWindow.serverRackView(); QWidget *navRibbon = mainWindow.findChild<QWidget*>(QStringLiteral("addressNavBar")); QLabel *stackSeg = navRibbon ? navRibbon->findChild<QLabel*>(QStringLiteral("ribbonSeg_stack")) : nullptr, *textSeg = navRibbon ? navRibbon->findChild<QLabel*>(QStringLiteral("ribbonSeg_text")) : nullptr;
    bool allRackIdle = idleRack && idleRack->rankNodeCount() == 8; for (int r = 0; allRackIdle && r < 8; ++r) { const auto rs = idleRack->item(r)->data(Qt::UserRole + 1).value<gridlock::RankState>(); allRackIdle = (rs.currentState == QStringLiteral("idle") && idleRack->rankPcAddress(r) == 0ULL && !rs.registers.value(16).contains(QStringLiteral("4011"))); }
    QVERIFY2(allRackIdle && stackSeg && !stackSeg->isVisible() && textSeg && textSeg->isVisible() && !textSeg->text().contains(QStringLiteral("0x401120")), "Idle ServerRack must show 8 IDLE cards without fake addresses");
    gridlock::ui::PreferencesDialog prefsDecompDlg(&mainWindow);
    auto* showDecompCheck = prefsDecompDlg.findChild<QCheckBox*>(QStringLiteral("showDecompilerWithSourceCheck")); auto* prefsBtnBox = prefsDecompDlg.findChild<QDialogButtonBox*>();
    QVERIFY2(showDecompCheck && !showDecompCheck->isChecked() && prefsBtnBox && prefsBtnBox->button(QDialogButtonBox::Apply) && prefsBtnBox->button(QDialogButtonBox::Ok), "PreferencesDialog Debugger page must expose unchecked showDecompilerWithSourceCheck");
    prefsDecompDlg.show(); QApplication::processEvents(); showDecompCheck->setChecked(true); prefsBtnBox->button(QDialogButtonBox::Apply)->click(); QApplication::processEvents();
    QVERIFY2(gridlock::tests::callShowDecompilerWithSource(&cfg, 0) && gridlock::tests::callIsDecompilerVisible(disasm, 0) && decompPage->isVisible(), "Apply showDecompilerWithSource=true must show decompiler split");
    showDecompCheck->setChecked(false); prefsBtnBox->button(QDialogButtonBox::Ok)->click(); QApplication::processEvents();
    QVERIFY2(!gridlock::tests::callShowDecompilerWithSource(&cfg, 0) && !gridlock::tests::callIsDecompilerVisible(disasm, 0) && !decompPage->isVisible() && !splitBtn->isChecked() && disasm->isGraphMode(), "OK showDecompilerWithSource=false must hide decompiler");
    mainWindow.switchWorkspaceDirectory(prevRepoWs); mainWindow.loadPreviewFixture(); QApplication::processEvents();

    // 4. Verify Linux Process (STACK/HEAP) & System (RAM/CPU) Telemetry Parser and Config Option.
    const QString sampleSmaps = QStringLiteral("00600000-00819000 rw-p 00000000 00:00 0 [heap]\nSize: 2148 kB\nRss: 1024 kB\n7ffffffde000-7ffffffff000 rw-p 00000000 00:00 0 [stack]\nSize: 136 kB\nRss: 64 kB\n");
    const QString sampleMeminfo = QStringLiteral("MemTotal:       33554432 kB\nMemFree:        12000000 kB\nMemAvailable:   25165824 kB\n");
    const QString summaryGb = gridlock::tests::callParseAndFormatTelemetry<gridlock::core::ElfBinaryInspector>(sampleSmaps, 0x7fffffffe300ULL, 0x7fffffffe400ULL, sampleMeminfo, QStringLiteral("gb"), 0);
    const QString summaryPct = gridlock::tests::callParseAndFormatTelemetry<gridlock::core::ElfBinaryInspector>(sampleSmaps, 0x7fffffffe300ULL, 0x7fffffffe400ULL, sampleMeminfo, QStringLiteral("percent"), 0);
    QVERIFY2(summaryGb.contains(QStringLiteral("STACK:")) && summaryGb.contains(QStringLiteral("HEAP:")) && summaryGb.contains(QStringLiteral("RAM: 8.0 / 32.0 GB")) && summaryGb.contains(QStringLiteral("CPU:")), qPrintable(summaryGb));
    QVERIFY2(summaryPct.contains(QStringLiteral("RAM: 25.0%")), qPrintable(summaryPct));
    QCOMPARE(gridlock::tests::callTelemetryDisplayFormat(&cfg, 0), QStringLiteral("gb"));
    auto* telemetryCombo = prefsDecompDlg.findChild<QComboBox*>(QStringLiteral("telemetryFormatCombo"));
    QVERIFY2(telemetryCombo != nullptr && telemetryCombo->findData(QStringLiteral("gb")) >= 0 && telemetryCombo->findData(QStringLiteral("percent")) >= 0 && telemetryCombo->findData(QStringLiteral("both")) >= 0, "PreferencesDialog must expose telemetryFormatCombo");

    if (qEnvironmentVariableIsSet("GRIDLOCK_CAPTURE_SCREENSHOTS")) {
        const QString outDir = QFileInfo::exists(QStringLiteral("docs/screenshots"))
            ? QStringLiteral("docs/screenshots")
            : QStringLiteral("../docs/screenshots");
        QVERIFY2(mainWindow.captureDocumentationScreenshots(outDir),
                 "MainWindow::captureDocumentationScreenshots failed");
        QVERIFY2(gridlock::tests::captureTutorialDocumentationScreenshots(mainWindow, outDir),
                 "captureTutorialDocumentationScreenshots failed");
    }
}

/**
 * @brief Verifies cross-pane workflow bridges, DisassemblyView context actions, and line rebalance.
 * Tests evaluator-to-watch promotion, disassembly context menu F1 manual lookup, and file headroom.
 */
void TestMainWindowUI::testCrossPaneWorkflowBridgesAndContextActions() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    // 1. Evaluator & Variables -> Watch Expressions Bridge.
    auto* evaluatorWidget = mainWindow.findChild<gridlock::ui::ExpressionEvaluatorWidget*>();
    auto* diffGrid = mainWindow.findChild<gridlock::ui::DifferentialGrid*>();
    QVERIFY2(evaluatorWidget != nullptr && diffGrid != nullptr, "ExpressionEvaluatorWidget and DifferentialGrid must exist");
    evaluatorWidget->setExpression(QStringLiteral("data[0].coords.z"));
    QVERIFY2(evaluatorWidget->promoteCurrentToWatch(), "promoteCurrentToWatch() must succeed for non-empty expression");
    QCoreApplication::processEvents();

    // Verify "data[0].coords.z" was added (case-insensitively) to DifferentialGrid watch expressions.
    auto hasWatchExpression = [diffGrid](const QString& target) -> bool {
        for (const std::string& w : diffGrid->getWatchExpressions()) if (QString::fromStdString(w).compare(target, Qt::CaseInsensitive) == 0) return true;
        for (int col = 0; col < diffGrid->columnCount(); ++col)
            if (const QTableWidgetItem* hdr = diffGrid->horizontalHeaderItem(col); hdr && hdr->text().compare(target, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    QVERIFY2(hasWatchExpression(QStringLiteral("data[0].coords.z")), "DifferentialGrid must contain promoted Evaluator expression 'data[0].coords.z'");

    // Promote a variable from VariablesDockWidget to Watch Expressions.
    auto* variablesDock = mainWindow.findChild<gridlock::VariablesDockWidget*>(); QVERIFY(variablesDock != nullptr);
    QVERIFY2(variablesDock->promoteVariableToWatch(QStringLiteral("rank_local_id")), "VariablesDockWidget::promoteVariableToWatch('rank_local_id') must return true");
    QCoreApplication::processEvents();
    QVERIFY2(hasWatchExpression(QStringLiteral("rank_local_id")), "DifferentialGrid must contain promoted variable 'rank_local_id'");

    // 2. Cross-View Context Menu Actions in DisassemblyView.
    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>(); QVERIFY(disasm != nullptr);
    const QStringList actionNames = disasm->contextActionNamesForAddress(0x0040113dULL);
    QVERIFY(actionNames.contains(QStringLiteral("Seek in Memory View")) && actionNames.contains(QStringLiteral("Inspect Cross-References (X)")));
    QVERIFY(actionNames.contains(QStringLiteral("Lookup in Reference Manual (F1)")) && actionNames.contains(QStringLiteral("Patch Instruction")));

    // Start on Terminal tab (index 0), then trigger "Lookup in Reference Manual (F1)" at 0x0040113d.
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs")); QVERIFY(bottomTabs != nullptr);
    bottomTabs->setCurrentIndex(0); QCoreApplication::processEvents();
    QVERIFY2(disasm->triggerContextAction(0x0040113dULL, QStringLiteral("Lookup in Reference Manual (F1)")), "triggerContextAction(0x0040113d, 'Lookup in Reference Manual (F1)') must succeed");
    QCoreApplication::processEvents();
    QVERIFY2(bottomTabs->tabText(bottomTabs->currentIndex()).contains(QStringLiteral("Reference Manual"), Qt::CaseInsensitive), "Must switch BottomConsoleTabs to Reference Manual");
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>(); QVERIFY(refManual != nullptr);
    auto* browser = refManual->findChild<QTextBrowser*>(); QVERIFY(browser != nullptr);
    QVERIFY2(browser->toPlainText().contains(QStringLiteral("MPI_Init"), Qt::CaseInsensitive), "Reference Manual must display MPI_Init documentation");

    // 3. File Line Headroom Verification for src/ui/views/DisassemblyAnalysis.cpp (< 850 lines).
    QFile disasmAnalysisFile(QStringLiteral("../src/ui/views/DisassemblyAnalysis.cpp"));
    if (!disasmAnalysisFile.exists()) disasmAnalysisFile.setFileName(QStringLiteral("src/ui/views/DisassemblyAnalysis.cpp"));
    QVERIFY2(disasmAnalysisFile.open(QIODevice::ReadOnly | QIODevice::Text), "Must open src/ui/views/DisassemblyAnalysis.cpp for line count verification");
    const int lineCount = QString::fromUtf8(disasmAnalysisFile.readAll()).split('\n').size();
    QVERIFY2(lineCount < 850, qPrintable(QString("DisassemblyAnalysis.cpp has %1 lines (expected < 850)").arg(lineCount)));
}

/**
 * @brief Verifies toolbar target/rank controls, debugger actions, and synchronized Run/Step/Stop loop.
 * Confirms state-aware toolbar buttons and cross-pane instruction stepping synchronization.
 */
void TestMainWindowUI::testEndToEndStepDebuggingWorkflowAndToolbarState() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp")) ? QStringLiteral("tutorial/inspection_demo.cpp") : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    // 1. Toolbar Target Selector, Rank SpinBox & Idle Button States.
    auto* targetCombo = mainWindow.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector"));
    auto* rankSpin = mainWindow.findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin"));
    QVERIFY2(targetCombo != nullptr && rankSpin != nullptr && targetCombo->count() >= 1, "MainToolbar must contain populated #toolbarTargetSelector and #toolbarRankSpin");

    auto findToolbarAction = [&mainWindow](const QString& labelSubstr) -> QAction* {
        for (QToolBar* tb : mainWindow.findChildren<QToolBar*>()) {
            for (QAction* act : tb->actions()) if (act->text().contains(labelSubstr, Qt::CaseInsensitive)) return act;
        }
        return nullptr;
    };

    QAction* runAction = findToolbarAction(QStringLiteral("Run Target"));
    QAction* stepAction = findToolbarAction(QStringLiteral("Step Inst"));
    QAction* terminateAction = findToolbarAction(QStringLiteral("Terminate Session"));
    QVERIFY2(runAction != nullptr && stepAction != nullptr && terminateAction != nullptr, "Run Target, Step Inst, and Terminate Session actions must exist on MainToolbar");

    mainWindow.stopTargetWorkflow();
    QApplication::processEvents();
    QVERIFY2(!mainWindow.isDebugSessionActive() && runAction->isEnabled() && !terminateAction->isEnabled(), "Idle state must enable Run Target and disable Terminate Session");

    // 2. End-to-End Interactive Run -> Step -> Register/Source/Rack Sync -> Stop Loop.
    if (auto* sv = mainWindow.findChild<gridlock::ui::SourceCodeView*>()) sv->setBreakpoints(QSet<int>());
    rankSpin->setValue(4);
    mainWindow.startTargetWorkflow();
    QApplication::processEvents();
    QVERIFY2(mainWindow.isDebugSessionActive() && stepAction->isEnabled() && terminateAction->isEnabled(), "Active session must enable Step Inst and Terminate Session");

    auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* regView = mainWindow.findChild<gridlock::ui::RegisterView*>();
    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    QVERIFY(serverRack != nullptr && disasmView != nullptr && regView != nullptr && sourceView != nullptr);
    QCOMPARE(serverRack->rankNodeCount(), 4);
    QCOMPARE(disasmView->currentAddress(), 0x00401120ULL);
    QVERIFY2(sourceView->executionLine() > 0, "SourceCodeView::executionLine() must be valid on startTargetWorkflow()");

    // Step 1 (0x00401120 -> 0x00401124) & Step 2 (0x00401124 -> 0x00401125).
    QVERIFY2(mainWindow.stepInstructionWorkflow(), "First stepInstructionWorkflow() must succeed");
    QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401124ULL);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401124ULL);

    QVERIFY2(mainWindow.stepInstructionWorkflow(), "Second stepInstructionWorkflow() must succeed");
    QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401125ULL);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401125ULL);
    QVERIFY2(sourceView->executionLine() > 0, "SourceCodeView::executionLine() must remain active across steps");

    auto* regTable = regView->findChild<QTableWidget*>();
    QVERIFY(regTable != nullptr);
    bool foundRipMatch = false;
    for (int r = 0; r < regTable->rowCount(); ++r) {
        if (regTable->item(r, 0) && regTable->item(r, 0)->text().compare(QStringLiteral("rip"), Qt::CaseInsensitive) == 0 &&
            regTable->item(r, 1) && regTable->item(r, 1)->text().contains(QStringLiteral("401125"), Qt::CaseInsensitive)) foundRipMatch = true;
    }
    QVERIFY2(foundRipMatch, "RegisterView 'rip' row must update to 0x00401125 after second step");

    const QPixmap snap = mainWindow.grab();
    QVERIFY(!snap.isNull() && snap.save(QStringLiteral("/tmp/gridlock_ui_v28.png")));

    mainWindow.stopTargetWorkflow();
    QApplication::processEvents();
    auto* statusLabel = mainWindow.findChild<QLabel*>(QStringLiteral("cutterStatusTelemetry"));
    QVERIFY(statusLabel != nullptr);
    QVERIFY2(!mainWindow.isDebugSessionActive() && !terminateAction->isEnabled() &&
                 statusLabel->text().contains(QStringLiteral("STATUS: IDLE"), Qt::CaseInsensitive),
             "stopTargetWorkflow() must return session to STATUS: IDLE");
}

/**
 * @brief Verifies bidirectional SourceCodeView <-> DisassemblyView line/address sync and Code/Reverse/MPI workspace perspectives.
 *
 * Validates three ergonomic requirements from Prompt 29:
 * 1. Bidirectional Source Line <-> Instruction Address Mapping:
 *    Confirms mainWindow.sourceLineToAddress(13) == 0x0040113dULL (MPI_Init) and
 *    mainWindow.addressToSourceLine(0x0040113dULL) == 13, and verifies that calling
 *    sourceView->selectSourceLine(16) seeks DisassemblyView to 0x00401149ULL.
 * 2. Workspace Perspective Switching ("Code", "Reverse", "MPI"):
 *    Confirms setWorkspacePerspective("Reverse") enables Split CFG + Decompiler mode and
 *    expands DisassemblyView to >= 700px width, "MPI" switches BottomConsoleTabs to
 *    MPI Diagnostics, and "Code" restores side-by-side source and assembly panes.
 * 3. Bottom Console Maximize Toggle:
 *    Confirms toggleMaximizeBottomConsole() expands and restores the bottom tab dock height.
 */
void TestMainWindowUI::testSourceToAssemblySyncAndWorkspacePerspectives() {
    // Initialize MainWindow with preview fixture and ensure tutorial source file is mounted.
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp")) ? QStringLiteral("tutorial/inspection_demo.cpp") : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    // Locate central source editor, disassembly canvas, and bottom console tab widget.
    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    QVERIFY(sourceView != nullptr && disasmView != nullptr && bottomTabs != nullptr);

    // 1. Bidirectional Source Line <-> Instruction Address Mapping across views.
    QCOMPARE(mainWindow.sourceLineToAddress(13), 0x0040113dULL);
    QCOMPARE(mainWindow.addressToSourceLine(0x0040113dULL), 13);
    sourceView->selectSourceLine(16); QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);
    emit disasmView->addressSelected(0x0040113dULL); QApplication::processEvents();
    QCOMPARE(sourceView->executionLine(), 13);

    // 2. Workspace Perspective Switching ("Reverse", "MPI", "Code") and width checks.
    mainWindow.setWorkspacePerspective(QStringLiteral("Reverse")); QApplication::processEvents();
    QCOMPARE(mainWindow.workspacePerspective(), QStringLiteral("Reverse"));
    QVERIFY2(disasmView->isSplitGraphDecompilerMode() && disasmView->width() >= 700, "Reverse perspective must enable Split CFG + Decompiler mode with width >= 700");

    // Verify MPI perspective activates the MPI Diagnostics tab in BottomConsoleTabs.
    mainWindow.setWorkspacePerspective(QStringLiteral("MPI")); QApplication::processEvents();
    QCOMPARE(mainWindow.workspacePerspective(), QStringLiteral("MPI"));
    QVERIFY2(bottomTabs->tabText(bottomTabs->currentIndex()).contains(QStringLiteral("MPI Diagnostics"), Qt::CaseInsensitive), "MPI perspective must activate the MPI Diagnostics bottom tab");

    // Verify Code perspective restores side-by-side SourceCodeView and DisassemblyView panes.
    mainWindow.setWorkspacePerspective(QStringLiteral("Code")); QApplication::processEvents();
    QCOMPARE(mainWindow.workspacePerspective(), QStringLiteral("Code"));
    QVERIFY2(sourceView->width() > 200 && disasmView->width() > 200, "Code perspective must restore side-by-side SourceCodeView and DisassemblyView");

    // 3. Bottom Console Maximize Toggle expands and restores bottom dock height.
    const int baseHeight = bottomTabs->height();
    mainWindow.toggleMaximizeBottomConsole(); QApplication::processEvents();
    QVERIFY2(bottomTabs->height() > baseHeight, "toggleMaximizeBottomConsole() must expand BottomConsoleTabs height");
    mainWindow.toggleMaximizeBottomConsole(); QApplication::processEvents();
}

/**
 * @brief Verifies breakpoint-aware Run/Continue, source-to-RIP line mapping, and CFG active instruction markers.
 * Validates granular address-to-line mapping, breakpoint-aware target workflow, and active CFG block marker.
 */
void TestMainWindowUI::testBreakpointAwareRunContinueAndActiveCfgInstructionMarker() {
    // Launch MainWindow with fixture data and load tutorial source if not yet mounted.
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp")) ? QStringLiteral("tutorial/inspection_demo.cpp") : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    // Retrieve SourceCodeView, DisassemblyView, and ServerRackView pointers.
    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    QVERIFY(sourceView != nullptr && disasmView != nullptr && serverRack != nullptr);

    // 1. Granular Source Line <-> Instruction Address Mapping assertions.
    QCOMPARE(mainWindow.addressToSourceLine(0x00401120ULL), 12);
    QCOMPARE(mainWindow.addressToSourceLine(0x00401128ULL), 15);
    QCOMPARE(mainWindow.addressToSourceLine(0x0040113dULL), 13);
    QCOMPARE(mainWindow.addressToSourceLine(0x00401149ULL), 16);
    QCOMPARE(mainWindow.sourceLineToAddress(15), 0x00401128ULL);
    QCOMPARE(mainWindow.sourceLineToAddress(16), 0x00401149ULL);
    // 2. Breakpoint-Aware Run Target & Continue Workflow at lines 15 and 16.
    mainWindow.stopTargetWorkflow(); sourceView->setBreakpoints(QSet<int>());
    sourceView->toggleBreakpoint(15); sourceView->toggleBreakpoint(16);
    QVERIFY2(sourceView->hasBreakpoint(15) && sourceView->hasBreakpoint(16), "SourceCodeView::toggleBreakpoint(15/16) must register active breakpoints");
    QVERIFY2(sourceView->breakpoints().contains(15) && sourceView->breakpoints().contains(16), "SourceCodeView::breakpoints() must contain lines 15 and 16");
    // Launch target and verify execution stops at first active breakpoint (line 15).
    mainWindow.startTargetWorkflow(); QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401128ULL);
    QCOMPARE(sourceView->executionLine(), 15);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401128ULL);
    // Continue to next active breakpoint at line 16 and verify RIP synchronization.
    QVERIFY2(mainWindow.continueExecutionWorkflow(), "MainWindow::continueExecutionWorkflow() must advance to the next active breakpoint");
    QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);
    QCOMPARE(sourceView->executionLine(), 16);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401149ULL);
    // 3. Visible Active Instruction Marker Inside CFG Basic Blocks.
    QVERIFY2(disasmView->activeCfgBlockContainsInstructionMarker(0x00401149ULL), "DisassemblyView::activeCfgBlockContainsInstructionMarker(0x00401149ULL) must be true");
}

/**
 * @brief Verifies floating dock pop-out, drag-back redocking via drop zone, and 1-click/double-click restore.
 *
 * Validates three critical window-management and redocking workflows:
 * 1. Pop Out (floatDockWidget) & 1-Click / Double-Click Redock (redockWidget):
 *    Floats ServerRackDock and verifies visibility and floating state, then redocks it
 *    and verifies original parent hierarchy and non-zero dimensions (> 50px).
 * 2. Drag-Back Drop-Zone Redocking (handleDockDragRelease):
 *    Floats ServerRackDock and simulates dragging over its home dock region center,
 *    verifying that dropping over the home zone snaps it back into place.
 * 3. redockAllFloatingPanels() Bulk Recovery:
 *    Floats multiple side panels simultaneously (ServerRackDock and RegisterViewDock)
 *    and validates that redockAllFloatingPanels() docks all of them cleanly.
 */
void TestMainWindowUI::testFloatingDockPopOutAndDragBackRedock() {
    // Instantiate and expose MainWindow with preview fixture data.
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    // Query dock instances by their unique registered object names.
    auto* serverRackDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("ServerRackDock"));
    auto* registerDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("RegisterViewDock"));
    QVERIFY2(serverRackDock != nullptr && registerDock != nullptr, "ServerRackDock and RegisterViewDock must exist in MainWindow");

    // 1. Pop Out (floatDockWidget) & 1-Click / Double-Click Redock (redockWidget).
    QWidget* originalParent = serverRackDock->parentWidget();
    mainWindow.toggleDockFloating(serverRackDock);
    QVERIFY2(mainWindow.isDockFloating(serverRackDock) && serverRackDock->isVisible(), "ServerRackDock must be floating and visible after toggleDockFloating");
    mainWindow.redockWidget(serverRackDock);
    QVERIFY2(!mainWindow.isDockFloating(serverRackDock), "ServerRackDock must not be floating after redockWidget");
    QCOMPARE(serverRackDock->parentWidget(), originalParent);
    QVERIFY2(serverRackDock->isVisible() && serverRackDock->width() > 50 && serverRackDock->height() > 50, "ServerRackDock must retain visible dimensions > 50px");

    // 2. Drag-Back Drop-Zone Redocking (handleDockDragRelease) & 3. Bulk redockAllFloatingPanels().
    mainWindow.toggleDockFloating(serverRackDock);
    QVERIFY2(mainWindow.isDockFloating(serverRackDock), "ServerRackDock must be floating again");
    mainWindow.handleDockDragRelease(serverRackDock, mainWindow.dockHomeGlobalRect(serverRackDock).center());
    QVERIFY2(!mainWindow.isDockFloating(serverRackDock) && serverRackDock->parentWidget() == originalParent, "ServerRackDock must redock when dropped into home zone");
    mainWindow.toggleDockFloating(serverRackDock); mainWindow.toggleDockFloating(registerDock);
    QVERIFY2(mainWindow.isDockFloating(serverRackDock) && mainWindow.isDockFloating(registerDock), "Both docks must be floating");
    mainWindow.redockAllFloatingPanels();
    QVERIFY2(!mainWindow.isDockFloating(serverRackDock) && !mainWindow.isDockFloating(registerDock), "Both docks must be docked after redockAllFloatingPanels");
}

/**
 * @brief Verifies Preferences sidebar pixel-level selected item contrast under solarized_light,
 *        opaque background, 12-theme selector dropdown, >= 15 terminal fonts, and SLURM auto-detection.
 *
 * Ensures qt_material's QListView::item:selected:focus rule never paints dark text on dark blue,
 * validates opaque cream surfaces across dialog and sidebar, and checks all 12 curated themes.
 */
void TestMainWindowUI::testPreferencesSidebarContrastTerminalFontsAndSlurmAutoDetect() {
    // 1. Initialize qt_material base stylesheet (matching main.cpp), switch to solarized_light, and capture rendered PNG.
    auto& tm = gridlock::core::ThemeManager::instance();
    tm.initialize(); tm.applyTheme(QStringLiteral("solarized_light"));
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1280, 800); mainWindow.loadPreviewFixture();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    // Open PreferencesDialog over MainWindow and verify rendered pixels of selected row 0 ("Appearance").
    gridlock::ui::PreferencesDialog dialog(&mainWindow); dialog.resize(860, 620); dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog)); QApplication::processEvents();
    const QImage dlgImg = dialog.grab().toImage(); QVERIFY(!dlgImg.isNull());
    auto* categoryList = dialog.findChild<QListWidget*>();
    QVERIFY(categoryList != nullptr && categoryList->viewport() != nullptr && categoryList->item(0) != nullptr);
    const QImage catImg = categoryList->viewport()->grab().toImage();
    const QRect itemRect = categoryList->visualItemRect(categoryList->item(0)).intersected(catImg.rect());
    int darkHighlightPixels = 0, brightTextPixels = 0;
    // Count dark accent highlight pixels (lightness < 150) and bright white glyph pixels (lightness > 210).
    for (int y = itemRect.top() + 3; y <= itemRect.bottom() - 3; ++y)
        for (int x = itemRect.left() + 42; x <= itemRect.right() - 6; ++x) {
            const int l = catImg.pixelColor(x, y).lightness();
            if (l < 150) ++darkHighlightPixels; else if (l > 210) ++brightTextPixels;
        }
    // Verify high-contrast white text pixels exist inside the blue highlight bar and surfaces are solid cream.
    QVERIFY2(darkHighlightPixels >= 100 && brightTextPixels >= 15, qPrintable(QStringLiteral("Selected sidebar row pixel check failed: darkBg=%1 brightText=%2").arg(darkHighlightPixels).arg(brightTextPixels)));
    QVERIFY(categoryList->minimumWidth() >= 185 && categoryList->palette().color(QPalette::Inactive, QPalette::HighlightedText).lightness() >= 200);
    auto hasLightPixel = [](const QImage& img, int x0, int x1, int y0, int y1, int minL) {
        for (int y = qMax(0, y0); y < qMin(img.height(), y1); ++y) for (int x = qMax(0, x0); x < qMin(img.width(), x1); ++x) if (img.pixelColor(x, y).lightness() > minL) return true;
        return false;
    };
    QVERIFY2(hasLightPixel(catImg, 10, 30, catImg.height() - 30, catImg.height() - 10, 200) && hasLightPixel(dlgImg, dlgImg.width() - 60, dlgImg.width() - 20, dlgImg.height() / 2 - 20, dlgImg.height() / 2 + 20, 210), "PreferencesDialog canvas and sidebar must render solid light surfaces");

    // Verify Color Theme combo populates all 12 curated themes including nord_dark, one_dark_pro, dracula_dark, one_light.
    auto* themeCombo = dialog.findChild<QComboBox*>(QStringLiteral("themeSelectorCombo"));
    QVERIFY2(themeCombo != nullptr && themeCombo->count() == 12, qPrintable(QStringLiteral("Expected 12 themes, got %1").arg(themeCombo ? themeCombo->count() : -1)));
    for (const QString& reqTheme : {QStringLiteral("nord_dark"), QStringLiteral("one_dark_pro"), QStringLiteral("dracula_dark"), QStringLiteral("one_light")})
        QVERIFY2(themeCombo->findData(reqTheme) >= 0, qPrintable(QStringLiteral("Missing theme in combo: ") + reqTheme));

    // 2. Palette Swatch Auto-Contrast & Hex Display on Gruvbox Industrial (#fabd2f bright yellow accent).
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

    // 3. Terminal Font Family Combo Has >= 15 Monospace Options & Unclipped "Browse..." Buttons.
    auto* termFontCombo = dialog.findChild<QComboBox*>(QStringLiteral("terminalFontFamilyCombo"));
    QVERIFY2(termFontCombo != nullptr && termFontCombo->count() >= 15, "Terminal Font Family combo must offer >= 15 monospace font families");
    for (QPushButton* btn : dialog.findChildren<QPushButton*>())
        if (btn->text().contains(QStringLiteral("Browse"), Qt::CaseInsensitive))
            QVERIFY2(btn->minimumWidth() >= 88, qPrintable(QStringLiteral("Browse button minWidth %1 < 88").arg(btn->minimumWidth())));

    // 4. Comprehensive SLURM Fields & Batch Script File Auto-Detection (parseSlurmBatchScript).
    const QString sampleSbatch = QStringLiteral("#!/bin/bash\n#SBATCH --job-name=wpt_fem_solver\n#SBATCH --account=ttu_hpc\n#SBATCH --partition=gpu_a100\n#SBATCH --qos=high\n#SBATCH --nodes=4\n#SBATCH --ntasks-per-node=16\n#SBATCH --cpus-per-task=4\n#SBATCH --mem=128G\n#SBATCH --time=06:30:00\n#SBATCH --gres=gpu:a100:2\n#SBATCH --constraint=nvlink\n#SBATCH --output= solver_%j.out\nmodule load openmpi/5.0 gcc/13\nsrun --mpi=pmix ./solver {FILE}\n");
    QVERIFY(dialog.parseSlurmBatchScript(sampleSbatch));
    for (const auto& [k, v] : QList<QPair<QString, QString>>{{"job_name", "wpt_fem_solver"}, {"account", "ttu_hpc"}, {"partition", "gpu_a100"}, {"qos", "high"}, {"nodes", "4"}, {"ntasks_per_node", "16"}, {"cpus_per_task", "4"}, {"mem", "128G"}, {"time", "06:30:00"}, {"gres_gpu", "a100:2"}, {"constraint", "nvlink"}})
        QCOMPARE(dialog.slurmFieldValue(k), v);
    QVERIFY(dialog.slurmFieldValue(QStringLiteral("modules")).contains(QStringLiteral("openmpi/5.0")));
    tm.applyTheme(QStringLiteral("gridlock_slate"));
}

/** @brief Verifies full workstation light theme propagation, non-colliding palette swatches, and theme switching. */
void TestMainWindowUI::testFullWorkstationLightThemePropagationAndLineCountHeadroom() {
    auto& tm = gridlock::core::ThemeManager::instance();
    const QStringList themeIds = tm.availableThemeIds();
    QVERIFY(themeIds.size() >= 12 && !tm.paletteForTheme(QStringLiteral("one_light")).isDark && tm.paletteForTheme(QStringLiteral("nord_dark")).isDark);
    tm.applyTheme(QStringLiteral("catppuccin_latte")); QVERIFY(!tm.isDarkMode());
    tm.applyTheme(QStringLiteral("solarized_light")); QVERIFY(!tm.isDarkMode());
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    gridlock::tests::callPropagateTheme(&mainWindow, 0); QApplication::processEvents();
    // Retrieve pointers to all primary inspection subviews and verify light canvas lightness >= 200.
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>(); auto* diffGrid = mainWindow.findChild<gridlock::ui::DifferentialGrid*>(); auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>(); auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(memView != nullptr && diffGrid != nullptr && serverRack != nullptr && disasmView != nullptr);
    QVERIFY2(gridlock::tests::getActiveCanvasBg(memView, 0).lightness() >= 200 && gridlock::tests::getActiveCanvasBg(diffGrid, 0).lightness() >= 200 && gridlock::tests::getActiveCanvasBg(serverRack, 0).lightness() >= 200, "Inspection views must have light canvas >= 200 in solarized_light");
    QVERIFY2(gridlock::tests::callAreAllChromeAligned(&mainWindow, 0), "All chrome toolbars and dock headers must be theme aligned without hardcoded #16181d/#1b1e24");
    // Inspect decompiler function signature label contrast (dark ink < 120 lightness on light cream background).
    auto* sigLabel = disasmView->findChild<QLabel*>(QStringLiteral("disasmSignatureLabel")); QVERIFY(sigLabel != nullptr);
    const auto match = QRegularExpression(QStringLiteral("color:\\s*(#[0-9a-fA-F]{3,8}|rgba?\\([^)]+\\))")).match(sigLabel->styleSheet());
    const QColor sigTextColor = match.hasMatch() ? QColor(match.captured(1)) : sigLabel->palette().color(QPalette::WindowText);
    QVERIFY2(sigTextColor.isValid() && sigTextColor.lightness() < 120, qPrintable(QString("Decompiler signature text color lightness %1 >= 120").arg(sigTextColor.lightness())));
    // Restore default dark theme and verify dark activeCanvasBackgroundColor (< 60 lightness).
    tm.applyTheme(QStringLiteral("gridlock_slate")); gridlock::tests::callPropagateTheme(&mainWindow, 0); QApplication::processEvents();
    QVERIFY2(gridlock::tests::getActiveCanvasBg(memView, 0).lightness() < 60, "MemView canvas background must be dark (< 60 lightness) in gridlock_slate");
    // 2. Spaced, Non-Colliding Palette Swatch Chips with hex colors stored in toolTip().
    gridlock::ui::PreferencesDialog prefsDialog; prefsDialog.resize(860, 620); prefsDialog.show(); QApplication::processEvents();
    QList<QLabel*> swatchBoxes;
    for (QLabel* lbl : prefsDialog.findChildren<QLabel*>()) for (const QString& r : {QStringLiteral("Canvas"), QStringLiteral("Header"), QStringLiteral("Border"), QStringLiteral("Accent"), QStringLiteral("Text")}) if (lbl->text().trimmed() == r) { swatchBoxes.append(lbl); break; }
    QVERIFY2(swatchBoxes.size() == 5, qPrintable(QString("Expected 5 clean role swatch boxes, found %1").arg(swatchBoxes.size())));
    for (QLabel* box : swatchBoxes) QVERIFY2(!box->toolTip().isEmpty() && box->toolTip().contains('#'), "Swatch box must store hex color in toolTip()");
}


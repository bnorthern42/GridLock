/**
 * @file test_ui_cohesion.cpp
 * @brief Visual and structural UI cohesion tests for bottom console tabs, docset viewer, and side docks.
 *
 * Verifies workstation layout cohesion, F1 symbol documentation lookup, cross-pane workflow
 * bridges, end-to-end stepping, workspace perspectives, breakpoint-aware continuation,
 * and floating dock redocking.
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
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextEdit>
#include <QToolBar>
#include <QtTest/QtTest>

#include "../../src/core/hpc/system/LocalSlurmBackend.hpp"
#include "../../src/core/managers/ConfigManager.hpp"
#include "../../src/core/managers/SpackManager.hpp"
#include "../../src/core/managers/ThemeManager.hpp"
#include "../../src/core/models/VariableTreeModel.hpp"
#include "../../src/core/utils/ElfBinaryInspector.hpp"
#include "../../src/ui/MainWindow.hpp"
#include "../../src/ui/dialogs/preferences/HpcSettingsPages.hpp"
#include "../../src/ui/dialogs/preferences/PreferencesDialog.hpp"
#include "../../src/ui/docks/CallStackDockWidget.hpp"
#include "../../src/ui/docks/VariablesDockWidget.hpp"
#include "../../src/ui/views/DisassemblyView.hpp"
#include "../../src/ui/views/MemView.hpp"
#include "../../src/ui/views/RegisterView.hpp"
#include "../../src/ui/views/ServerRackView.hpp"
#include "../../src/ui/views/SourceCodeView.hpp"
#include "../../src/ui/widgets/DifferentialGrid.hpp"
#include "../../src/ui/widgets/ExpressionEvaluatorWidget.hpp"
#include "../../src/ui/widgets/MpiDiagnosticsWidget.hpp"
#include "../../src/ui/widgets/MpiNetworkLogWidget.hpp"
#include "../../src/ui/widgets/ReferenceManualWidget.hpp"

namespace gridlock::tests {

bool captureTutorialDocumentationScreenshots(gridlock::ui::MainWindow& mw, const QString& outDirPath);
bool captureV087ToolScreenshots(gridlock::ui::MainWindow& mw, const QString& outputDir);

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

}  // namespace gridlock::tests

/** @brief Verifies zero-gutter flush viewports across bottom tabs, F1 docset lookup, 2-zone right dock hierarchy, and primary Run Target CTA. */
void TestMainWindowUI::testWorkstationUiCohesionAndIntegratedDocViewer() {
    auto& tm = gridlock::core::ThemeManager::instance(); tm.initialize();
    auto& cfg = gridlock::core::ConfigManager::instance();
    gridlock::tests::callSetShowDecompilerWithSource(&cfg, false, 0);
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900);
    // 0. Gate 1 Zero-Mock Startup, Idle Telemetry, Preview Seeding, and Stop-Clear Verification.
    gridlock::ui::MpiDiagnosticsWidget standaloneDiag; gridlock::ui::MpiNetworkLogWidget standaloneNet(QStringLiteral("Net")); gridlock::ui::MemView standaloneMem;
    auto* standaloneBadge = standaloneMem.findChild<QLabel*>(QStringLiteral("memSourceBadge")); auto* standaloneDump = standaloneMem.findChild<QTextEdit*>(QStringLiteral("cutterHexDumpEdit"));
    QVERIFY2(standaloneDiag.rankWaitTableRowCount() == 0 && standaloneNet.logRowCount() == 0, "Standalone MpiDiagnosticsWidget and MpiNetworkLogWidget must start with 0 rows");
    QVERIFY2(standaloneMem.currentBaseAddress() == 0ULL && standaloneDump && standaloneDump->toPlainText().trimmed().isEmpty() && standaloneBadge && standaloneBadge->text() == QStringLiteral("IDLE (NO BINARY)"), "Standalone MemView must start empty with IDLE (NO BINARY) badge");
    auto* sWaitTbl = standaloneDiag.findChild<QTableWidget*>(QStringLiteral("mpiRankWaitTable")); auto* sNetTbl = standaloneNet.findChild<QTableWidget*>();
    QVERIFY(sWaitTbl && sNetTbl); QCOMPARE(sWaitTbl->horizontalHeader()->sectionResizeMode(0), QHeaderView::Interactive); QCOMPARE(sNetTbl->horizontalHeader()->sectionResizeMode(0), QHeaderView::Interactive);
    standaloneDiag.beginLiveSession(2); QVERIFY(!sWaitTbl->item(0, 5)->text().contains(QStringLiteral("0x00000000")));
    standaloneDiag.updateLiveRankRow(0, 3645905, QStringLiteral("RUNNING"), QStringLiteral("MPI_Allreduce"), QStringLiteral("ALL"), QStringLiteral("main.cpp:182"));
    QVERIFY2(sWaitTbl->columnWidth(0) >= sWaitTbl->fontMetrics().horizontalAdvance(QStringLiteral("Rank 0 (PID 3645905)")) + 16, qPrintable(QString("RANK col width %1 too narrow").arg(sWaitTbl->columnWidth(0))));
    for (int c = 0; c < sWaitTbl->columnCount(); ++c) QVERIFY(sWaitTbl->item(0, c) && !sWaitTbl->item(0, c)->toolTip().isEmpty());
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
    auto* idleRack = mainWindow.serverRackView(); QWidget* navRibbon = mainWindow.findChild<QWidget*>(QStringLiteral("addressNavBar")); QLabel *stackSeg = navRibbon ? navRibbon->findChild<QLabel*>(QStringLiteral("ribbonSeg_stack")) : nullptr, *textSeg = navRibbon ? navRibbon->findChild<QLabel*>(QStringLiteral("ribbonSeg_text")) : nullptr;
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
        const QString outDir = QFileInfo::exists(QStringLiteral("docs/screenshots")) ? QStringLiteral("docs/screenshots") : QStringLiteral("../docs/screenshots");
        QVERIFY2(mainWindow.captureDocumentationScreenshots(outDir), "MainWindow::captureDocumentationScreenshots failed");
        QVERIFY2(gridlock::tests::captureTutorialDocumentationScreenshots(mainWindow, outDir), "captureTutorialDocumentationScreenshots failed");
        QVERIFY2(gridlock::tests::captureV087ToolScreenshots(mainWindow, outDir), "captureV087ToolScreenshots failed");
    }
}

/** @brief Verifies cross-pane workflow bridges, DisassemblyView context actions, and line rebalance. */
void TestMainWindowUI::testCrossPaneWorkflowBridgesAndContextActions() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    auto* evaluatorWidget = mainWindow.findChild<gridlock::ui::ExpressionEvaluatorWidget*>(); auto* diffGrid = mainWindow.findChild<gridlock::ui::DifferentialGrid*>();
    QVERIFY(evaluatorWidget && diffGrid); evaluatorWidget->setExpression(QStringLiteral("data[0].coords.z"));
    QVERIFY(evaluatorWidget->promoteCurrentToWatch()); QCoreApplication::processEvents();
    auto hasWatchExpression = [diffGrid](const QString& target) -> bool {
        for (const std::string& w : diffGrid->getWatchExpressions()) if (QString::fromStdString(w).compare(target, Qt::CaseInsensitive) == 0) return true;
        for (int col = 0; col < diffGrid->columnCount(); ++col) if (const QTableWidgetItem* hdr = diffGrid->horizontalHeaderItem(col); hdr && hdr->text().compare(target, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    QVERIFY(hasWatchExpression(QStringLiteral("data[0].coords.z")));
    auto* variablesDock = mainWindow.findChild<gridlock::VariablesDockWidget*>(); QVERIFY(variablesDock && variablesDock->promoteVariableToWatch(QStringLiteral("rank_local_id")));
    QCoreApplication::processEvents(); QVERIFY(hasWatchExpression(QStringLiteral("rank_local_id")));
    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>(); QVERIFY(disasm != nullptr);
    const QStringList actionNames = disasm->contextActionNamesForAddress(0x0040113dULL);
    QVERIFY(actionNames.contains(QStringLiteral("Seek in Memory View")) && actionNames.contains(QStringLiteral("Inspect Cross-References (X)")) && actionNames.contains(QStringLiteral("Lookup in Reference Manual (F1)")) && actionNames.contains(QStringLiteral("Patch Instruction")));
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs")); QVERIFY(bottomTabs != nullptr);
    bottomTabs->setCurrentIndex(0); QCoreApplication::processEvents();
    QVERIFY(disasm->triggerContextAction(0x0040113dULL, QStringLiteral("Lookup in Reference Manual (F1)"))); QCoreApplication::processEvents();
    QVERIFY(bottomTabs->tabText(bottomTabs->currentIndex()).contains(QStringLiteral("Reference Manual"), Qt::CaseInsensitive));
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>(); auto* browser = refManual ? refManual->findChild<QTextBrowser*>() : nullptr;
    QVERIFY(refManual && browser && browser->toPlainText().contains(QStringLiteral("MPI_Init"), Qt::CaseInsensitive));
    QFile disasmAnalysisFile(QStringLiteral("../src/ui/views/DisassemblyAnalysis.cpp"));
    if (!disasmAnalysisFile.exists()) disasmAnalysisFile.setFileName(QStringLiteral("src/ui/views/DisassemblyAnalysis.cpp"));
    QVERIFY(disasmAnalysisFile.open(QIODevice::ReadOnly | QIODevice::Text));
    const int lineCount = QString::fromUtf8(disasmAnalysisFile.readAll()).split('\n').size();
    QVERIFY2(lineCount < 850, qPrintable(QString("DisassemblyAnalysis.cpp has %1 lines (expected < 850)").arg(lineCount)));
}

/** @brief Verifies toolbar target/rank controls, debugger actions, and synchronized Run/Step/Stop loop. */
void TestMainWindowUI::testEndToEndStepDebuggingWorkflowAndToolbarState() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp")) ? QStringLiteral("tutorial/inspection_demo.cpp") : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

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
    QAction* stepAction = findToolbarAction(QStringLiteral("Step Over"));
    QAction* stopAction = findToolbarAction(QStringLiteral("Stop"));
    QVERIFY2(runAction != nullptr && stepAction != nullptr && stopAction != nullptr && findToolbarAction(QStringLiteral("Terminate Session")) == nullptr, "Run Target, Step Over, and single Stop action must exist on MainToolbar");

    mainWindow.stopTargetWorkflow(); QApplication::processEvents();
    QVERIFY2(!mainWindow.isDebugSessionActive() && runAction->isEnabled() && !stopAction->isEnabled(), "Idle state must enable Run Target and disable Stop");

    if (auto* sv = mainWindow.findChild<gridlock::ui::SourceCodeView*>()) sv->setBreakpoints(QSet<int>());
    rankSpin->setValue(4);
    mainWindow.startTargetWorkflow(); QApplication::processEvents();
    QVERIFY2(mainWindow.isDebugSessionActive() && stepAction->isEnabled() && stopAction->isEnabled(), "Active session must enable Step Over and Stop");

    auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* regView = mainWindow.findChild<gridlock::ui::RegisterView*>();
    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    QVERIFY(serverRack != nullptr && disasmView != nullptr && regView != nullptr && sourceView != nullptr);
    QCOMPARE(serverRack->rankNodeCount(), 4);
    QCOMPARE(disasmView->currentAddress(), 0x00401120ULL);
    QVERIFY2(sourceView->executionLine() > 0, "SourceCodeView::executionLine() must be valid on startTargetWorkflow()");

    QVERIFY2(mainWindow.stepInstructionWorkflow(), "First stepInstructionWorkflow() must succeed"); QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401124ULL);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401124ULL);

    QVERIFY2(mainWindow.stepInstructionWorkflow(), "Second stepInstructionWorkflow() must succeed"); QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401125ULL);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401125ULL);
    QVERIFY2(sourceView->executionLine() > 0, "SourceCodeView::executionLine() must remain active across steps");

    auto* regTable = regView->findChild<QTableWidget*>(); QVERIFY(regTable != nullptr);
    bool foundRipMatch = false;
    for (int r = 0; r < regTable->rowCount(); ++r) {
        if (regTable->item(r, 0) && regTable->item(r, 0)->text().compare(QStringLiteral("rip"), Qt::CaseInsensitive) == 0 &&
            regTable->item(r, 1) && regTable->item(r, 1)->text().contains(QStringLiteral("401125"), Qt::CaseInsensitive)) foundRipMatch = true;
    }
    QVERIFY2(foundRipMatch, "RegisterView 'rip' row must update to 0x00401125 after second step");

    const QPixmap snap = mainWindow.grab();
    QVERIFY(!snap.isNull() && snap.save(QStringLiteral("/tmp/gridlock_ui_v28.png")));

    mainWindow.stopTargetWorkflow(); QApplication::processEvents();
    auto* statusLabel = mainWindow.findChild<QLabel*>(QStringLiteral("cutterStatusTelemetry")); QVERIFY(statusLabel != nullptr);
    QVERIFY2(!mainWindow.isDebugSessionActive() && !stopAction->isEnabled() &&
                 statusLabel->text().contains(QStringLiteral("STATUS: IDLE"), Qt::CaseInsensitive),
             "stopTargetWorkflow() must return session to STATUS: IDLE");
}

/** @brief Verifies bidirectional SourceCodeView <-> DisassemblyView line/address sync and workspace perspectives. */
void TestMainWindowUI::testSourceToAssemblySyncAndWorkspacePerspectives() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp")) ? QStringLiteral("tutorial/inspection_demo.cpp") : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    QVERIFY(sourceView != nullptr && disasmView != nullptr && bottomTabs != nullptr);

    QCOMPARE(mainWindow.sourceLineToAddress(13), 0x0040113dULL);
    QCOMPARE(mainWindow.addressToSourceLine(0x0040113dULL), 13);
    sourceView->selectSourceLine(16); QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);
    emit disasmView->addressSelected(0x0040113dULL); QApplication::processEvents();
    QCOMPARE(sourceView->executionLine(), 13);

    mainWindow.setWorkspacePerspective(QStringLiteral("Binary")); QApplication::processEvents();
    QCOMPARE(mainWindow.workspacePerspective(), QStringLiteral("Binary"));
    QVERIFY2(disasmView->isSplitGraphDecompilerMode() && disasmView->width() >= 700, "Binary perspective must enable Split CFG + Decompiler mode with width >= 700");

    mainWindow.setWorkspacePerspective(QStringLiteral("MPI")); QApplication::processEvents();
    QCOMPARE(mainWindow.workspacePerspective(), QStringLiteral("MPI"));
    QVERIFY2(bottomTabs->tabText(bottomTabs->currentIndex()).contains(QStringLiteral("MPI Diagnostics"), Qt::CaseInsensitive), "MPI perspective must activate the MPI Diagnostics bottom tab");

    mainWindow.setWorkspacePerspective(QStringLiteral("Debug")); QApplication::processEvents();
    QCOMPARE(mainWindow.workspacePerspective(), QStringLiteral("Debug"));
    QVERIFY2(sourceView->width() > 400, "Debug perspective must keep SourceCodeView wide alongside Variables/CallStack");

    const int baseHeight = bottomTabs->height();
    mainWindow.toggleMaximizeBottomConsole(); QApplication::processEvents();
    QVERIFY2(bottomTabs->height() > baseHeight, "toggleMaximizeBottomConsole() must expand BottomConsoleTabs height");
    mainWindow.toggleMaximizeBottomConsole(); QApplication::processEvents();
}

/** @brief Verifies breakpoint-aware Run/Continue, source-to-RIP line mapping, and CFG active instruction markers. */
void TestMainWindowUI::testBreakpointAwareRunContinueAndActiveCfgInstructionMarker() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp")) ? QStringLiteral("tutorial/inspection_demo.cpp") : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    QVERIFY(sourceView != nullptr && disasmView != nullptr && serverRack != nullptr);

    QCOMPARE(mainWindow.addressToSourceLine(0x00401120ULL), 12);
    QCOMPARE(mainWindow.addressToSourceLine(0x00401128ULL), 15);
    QCOMPARE(mainWindow.addressToSourceLine(0x0040113dULL), 13);
    QCOMPARE(mainWindow.addressToSourceLine(0x00401149ULL), 16);
    QCOMPARE(mainWindow.sourceLineToAddress(15), 0x00401128ULL);
    QCOMPARE(mainWindow.sourceLineToAddress(16), 0x00401149ULL);
    mainWindow.stopTargetWorkflow(); sourceView->setBreakpoints(QSet<int>());
    sourceView->toggleBreakpoint(15); sourceView->toggleBreakpoint(16);
    QVERIFY2(sourceView->hasBreakpoint(15) && sourceView->hasBreakpoint(16), "SourceCodeView::toggleBreakpoint(15/16) must register active breakpoints");
    QVERIFY2(sourceView->breakpoints().contains(15) && sourceView->breakpoints().contains(16), "SourceCodeView::breakpoints() must contain lines 15 and 16");
    mainWindow.startTargetWorkflow(); QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401128ULL);
    QCOMPARE(sourceView->executionLine(), 15);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401128ULL);
    QVERIFY2(mainWindow.continueExecutionWorkflow(), "MainWindow::continueExecutionWorkflow() must advance to the next active breakpoint");
    QApplication::processEvents();
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);
    QCOMPARE(sourceView->executionLine(), 16);
    QCOMPARE(serverRack->rankPcAddress(serverRack->selectedRank()), 0x00401149ULL);
    QVERIFY2(disasmView->activeCfgBlockContainsInstructionMarker(0x00401149ULL), "DisassemblyView::activeCfgBlockContainsInstructionMarker(0x00401149ULL) must be true");
}

/** @brief Verifies floating dock pop-out, drag-back redocking via drop zone, and 1-click/double-click restore. */
void TestMainWindowUI::testFloatingDockPopOutAndDragBackRedock() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    auto* serverRackDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("ServerRackDock"));
    auto* registerDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("RegisterViewDock"));
    QVERIFY2(serverRackDock != nullptr && registerDock != nullptr, "ServerRackDock and RegisterViewDock must exist in MainWindow");

    QWidget* originalParent = serverRackDock->parentWidget();
    mainWindow.toggleDockFloating(serverRackDock);
    QVERIFY2(mainWindow.isDockFloating(serverRackDock) && serverRackDock->isVisible(), "ServerRackDock must be floating and visible after toggleDockFloating");
    mainWindow.redockWidget(serverRackDock);
    QVERIFY2(!mainWindow.isDockFloating(serverRackDock), "ServerRackDock must not be floating after redockWidget");
    QCOMPARE(serverRackDock->parentWidget(), originalParent);
    QVERIFY2(serverRackDock->isVisible() && serverRackDock->width() > 50 && serverRackDock->height() > 50, "ServerRackDock must retain visible dimensions > 50px");

    mainWindow.toggleDockFloating(serverRackDock);
    QVERIFY2(mainWindow.isDockFloating(serverRackDock), "ServerRackDock must be floating again");
    mainWindow.handleDockDragRelease(serverRackDock, mainWindow.dockHomeGlobalRect(serverRackDock).center());
    QVERIFY2(!mainWindow.isDockFloating(serverRackDock) && serverRackDock->parentWidget() == originalParent, "ServerRackDock must redock when dropped into home zone");
    mainWindow.toggleDockFloating(serverRackDock); mainWindow.toggleDockFloating(registerDock);
    QVERIFY2(mainWindow.isDockFloating(serverRackDock) && mainWindow.isDockFloating(registerDock), "Both docks must be floating");
    mainWindow.redockAllFloatingPanels();
    QVERIFY2(!mainWindow.isDockFloating(serverRackDock) && !mainWindow.isDockFloating(registerDock), "Both docks must be docked after redockAllFloatingPanels");
}

void TestMainWindowUI::testSlurmFileMenuOptInToggle() {
    auto& cfg = gridlock::core::ConfigManager::instance();
    const auto origHpc = cfg.getHpcSettings();
    struct HpcRestore {
        gridlock::core::HpcSettings orig;
        ~HpcRestore() { gridlock::core::ConfigManager::instance().saveHpcSettings(orig); }
    } guard{origHpc};

    // Reset opt-in to default false before instantiating MainWindow.
    auto resetHpc = origHpc; resetHpc.enableSlurmMenu = false; cfg.saveHpcSettings(resetHpc);

    gridlock::ui::MainWindow mw; mw.resize(1280, 800); mw.show(); QApplication::processEvents();
    auto* slurmAct = mw.findChild<QAction*>(QStringLiteral("actionSubmitSlurmJob"));
    QVERIFY2(slurmAct != nullptr && !slurmAct->isVisible(), "Submit Slurm Job... must be hidden in File menu by default");

    // Confirm no adjacent double visible separators in the File menu while hidden.
    QMenu* fileMenu = nullptr;
    for (QAction* topAct : mw.menuBar()->actions()) {
        if (topAct && topAct->text().remove(QLatin1Char('&')) == QLatin1String("File")) { fileMenu = topAct->menu(); break; }
    }
    QVERIFY(fileMenu != nullptr && fileMenu->actions().contains(slurmAct));
    bool prevWasSep = false;
    for (QAction* a : fileMenu->actions()) {
        if (!a || !a->isVisible()) continue;
        QVERIFY2(!(prevWasSep && a->isSeparator()), "File menu must not contain adjacent visible separators");
        prevWasSep = a->isSeparator();
    }

    // Open Preferences -> HPC / Cluster and verify unchecked default + bold red warning label.
    gridlock::ui::PreferencesDialog prefs(&mw);
    auto* slurmCheck = prefs.findChild<QCheckBox*>(QStringLiteral("enableSlurmMenuCheck"));
    auto* warnLbl = prefs.findChild<QLabel*>(QStringLiteral("slurmExperimentalWarningLabel"));
    QVERIFY2(slurmCheck != nullptr && !slurmCheck->isChecked() && slurmCheck->text().contains(QStringLiteral("Submit Slurm Job...")),
             "HPC / Cluster settings must expose unchecked opt-in checkbox for Submit Slurm Job...");
    QVERIFY2(warnLbl != nullptr && warnLbl->text() == QStringLiteral("Experimental, untested, not responsible for breakage") &&
             warnLbl->styleSheet().contains(QStringLiteral("#ff4d4f")) && warnLbl->styleSheet().contains(QStringLiteral("bold")),
             "HPC / Cluster settings must display bold red experimental warning");

    // Check the opt-in toggle and Apply: File menu action becomes visible live.
    slurmCheck->setChecked(true);
    auto* btnBox = prefs.findChild<QDialogButtonBox*>();
    QVERIFY(btnBox != nullptr);
    QPushButton* applyBtn = btnBox->button(QDialogButtonBox::Apply);
    QPushButton* okBtn = btnBox->button(QDialogButtonBox::Ok);
    QVERIFY(applyBtn && okBtn);
    applyBtn->click(); QApplication::processEvents();
    QVERIFY2(cfg.getHpcSettings().enableSlurmMenu && slurmAct->isVisible(), "Applying opt-in checkbox must show Submit Slurm Job... in File menu");

    // Uncheck and click OK: File menu action hides again live.
    slurmCheck->setChecked(false);
    okBtn->click(); QApplication::processEvents();
    QVERIFY2(!cfg.getHpcSettings().enableSlurmMenu && !slurmAct->isVisible(), "Unchecking opt-in checkbox and clicking OK must hide Submit Slurm Job...");
}

void TestMainWindowUI::testSlurmParserPersistenceAndLocalBackendWorkflow() {
    (void)gridlock::core::ConfigManager::instance();
    const QString prevCfgPath = QString::fromUtf8(qgetenv("GRIDLOCK_TEST_CONFIG_DIR"));
    QTemporaryDir isolatedDir;
    QVERIFY(isolatedDir.isValid());
    struct ScopedIsolation {
        QString prev;
        ~ScopedIsolation() {
            if (qApp) qApp->setProperty("gridlock_non_interactive_slurm", QVariant());
            if (!prev.isEmpty()) {
                QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, prev);
                QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, prev);
            }
        }
    } isoGuard{prevCfgPath};
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, isolatedDir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, isolatedDir.path());
    qApp->setProperty("gridlock_non_interactive_slurm", true);

    // 1. Verify SlurmBatchParser round-trips -n vs --ntasks-per-node, quoted --job-name, --error, --mem-per-cpu, --chdir, --exclusive, and "{FILE}".
    {
        gridlock::ui::HpcIntegrationSettingsPage page;
        const QString scriptNtasks = QStringLiteral(
            "#!/bin/bash\n"
            "#SBATCH --job-name=\"my mpi job\"\n"
            "#SBATCH --nodes=2\n"
            "#SBATCH -n 8\n"
            "#SBATCH --cpus-per-task=2\n"
            "#SBATCH --mem-per-cpu=4G\n"
            "#SBATCH --exclusive\n"
            "#SBATCH --chdir=\"/scratch/my run\"\n"
            "#SBATCH -o out_%j.log\n"
            "#SBATCH -e err_%j.log\n"
            "module load gcc/13 openmpi/5\n"
            "srun \"{FILE}\"\n");
        QVERIFY(page.parseSlurmBatchScript(scriptNtasks));
        QCOMPARE(page.slurmFieldValue(QStringLiteral("job_name")), QStringLiteral("my mpi job"));
        QCOMPARE(page.slurmFieldValue(QStringLiteral("nodes")), QStringLiteral("2"));
        QCOMPARE(page.slurmFieldValue(QStringLiteral("ntasks_per_node")), QStringLiteral("4"));
        QCOMPARE(page.slurmFieldValue(QStringLiteral("ntasks")), QStringLiteral("8"));
        QCOMPARE(page.slurmFieldValue(QStringLiteral("mem_per_cpu")), QStringLiteral("4G"));
        QVERIFY(page.slurmFieldValue(QStringLiteral("mem")).isEmpty());
        QCOMPARE(page.slurmFieldValue(QStringLiteral("error")), QStringLiteral("err_%j.log"));
        QCOMPARE(page.slurmFieldValue(QStringLiteral("chdir")), QStringLiteral("/scratch/my run"));
        QCOMPARE(page.slurmFieldValue(QStringLiteral("exclusive")), QStringLiteral("true"));

        const QString regen = page.generateSlurmBatchScript();
        QVERIFY2(regen.contains(QStringLiteral("#SBATCH --job-name=\"my mpi job\"")), qPrintable(regen));
        QVERIFY2(regen.contains(QStringLiteral("#SBATCH --nodes=2")) && regen.contains(QStringLiteral("#SBATCH --ntasks=8")) &&
                 !regen.contains(QStringLiteral("--ntasks-per-node=8")), qPrintable(regen));
        QVERIFY2(regen.contains(QStringLiteral("#SBATCH --mem-per-cpu=4G")) && !regen.contains(QStringLiteral("#SBATCH --mem=")), qPrintable(regen));
        QVERIFY2(regen.contains(QStringLiteral("#SBATCH --error=err_%j.log")) &&
                 regen.contains(QStringLiteral("#SBATCH --chdir=\"/scratch/my run\"")) &&
                 regen.contains(QStringLiteral("#SBATCH --exclusive")) &&
                 regen.contains(QStringLiteral("srun \"{FILE}\"")), qPrintable(regen));

        gridlock::ui::HpcIntegrationSettingsPage reparsed;
        QVERIFY(reparsed.parseSlurmBatchScript(regen));
        QCOMPARE(reparsed.slurmFieldValue(QStringLiteral("job_name")), QStringLiteral("my mpi job"));
        QCOMPARE(reparsed.slurmFieldValue(QStringLiteral("ntasks")), QStringLiteral("8"));
        QCOMPARE(reparsed.slurmFieldValue(QStringLiteral("mem_per_cpu")), QStringLiteral("4G"));
        QCOMPARE(reparsed.slurmFieldValue(QStringLiteral("error")), QStringLiteral("err_%j.log"));

        const QString subbed = gridlock::ui::HpcIntegrationSettingsPage::substituteTargetBinary(
            regen, QStringLiteral("/work/space dir/build/solver"), QStringLiteral("--steps 5"));
        QVERIFY2(subbed.contains(QStringLiteral("srun \"/work/space dir/build/solver\" --steps 5")), qPrintable(subbed));
    }

    // 2. Verify all HPC Integration fields persist across PreferencesDialog Apply/OK without manual Sync click.
    const QList<QPair<QString, QString>> fieldPairs = {
        {QStringLiteral("ssh_host"), QStringLiteral("login.hpcc.ttu.edu")},
        {QStringLiteral("ssh_user"), QStringLiteral("raider42")},
        {QStringLiteral("ssh_key_path"), QStringLiteral("~/.ssh/id_ed25519_ttu")},
        {QStringLiteral("script_path"), QStringLiteral("/home/raider42/run.sbatch")},
        {QStringLiteral("job_name"), QStringLiteral("cfd_debug_run")},
        {QStringLiteral("account"), QStringLiteral("engr_alloc")},
        {QStringLiteral("partition"), QStringLiteral("nocona")},
        {QStringLiteral("qos"), QStringLiteral("debug")},
        {QStringLiteral("time"), QStringLiteral("00:30:00")},
        {QStringLiteral("nodes"), QStringLiteral("2")},
        {QStringLiteral("ntasks_per_node"), QStringLiteral("2")},
        {QStringLiteral("cpus_per_task"), QStringLiteral("4")},
        {QStringLiteral("mem"), QStringLiteral("32G")},
        {QStringLiteral("request_gpus"), QStringLiteral("true")},
        {QStringLiteral("gpu_type"), QStringLiteral("a100")},
        {QStringLiteral("gpus_per_node"), QStringLiteral("2")},
        {QStringLiteral("constraint"), QStringLiteral("rome")},
        {QStringLiteral("mpi_launcher"), QStringLiteral("srun --mpi=pmix")},
        {QStringLiteral("modules"), QStringLiteral("gcc/13.2 openmpi/5.0")},
        {QStringLiteral("output"), QStringLiteral("cfd_%j.out")},
        {QStringLiteral("error"), QStringLiteral("cfd_%j.err")},
        {QStringLiteral("mail_type"), QStringLiteral("FAIL")},
        {QStringLiteral("mail_user"), QStringLiteral("raider42@ttu.edu")},
        {QStringLiteral("sbatch_path"), QStringLiteral("/opt/slurm/bin/sbatch")},
        {QStringLiteral("squeue_path"), QStringLiteral("/opt/slurm/bin/squeue")},
        {QStringLiteral("scontrol_path"), QStringLiteral("/opt/slurm/bin/scontrol")},
        {QStringLiteral("spack_root"), QStringLiteral("/opt/ttu_spack")},
    };
    {
        gridlock::ui::PreferencesDialog prefs1;
        auto* hpcInt1 = prefs1.findChild<gridlock::ui::HpcIntegrationSettingsPage*>();
        QVERIFY(hpcInt1 != nullptr);
        for (const auto& [k, v] : fieldPairs) hpcInt1->setSlurmFieldValue(k, v);
        auto* btnBox = prefs1.findChild<QDialogButtonBox*>();
        QVERIFY(btnBox && btnBox->button(QDialogButtonBox::Ok));
        btnBox->button(QDialogButtonBox::Ok)->click();
        QApplication::processEvents();
    }
    {
        gridlock::ui::PreferencesDialog prefs2;
        auto* hpcInt2 = prefs2.findChild<gridlock::ui::HpcIntegrationSettingsPage*>();
        QVERIFY(hpcInt2 != nullptr);
        for (const auto& [k, v] : fieldPairs) {
            if (k == QStringLiteral("request_gpus") || k == QStringLiteral("gpu_type") || k == QStringLiteral("gpus_per_node")) continue;
            QCOMPARE(hpcInt2->slurmFieldValue(k), v);
        }
        QCOMPARE(hpcInt2->slurmFieldValue(QStringLiteral("gres_gpu")), QStringLiteral("a100:2"));
    }

    // 3. End-to-end MainWindow Slurm submission: never substitutes .cpp m_currentFile, maps node01:2345..node02:2348, attaches, and scancels on stop.
    const QString shimDir = isolatedDir.filePath(QStringLiteral("slurm_shims"));
    const QString projDir = isolatedDir.filePath(QStringLiteral("mpi project"));
    QVERIFY(QDir().mkpath(shimDir) && QDir().mkpath(projDir));
    auto writeExec = [](const QString& path, const QByteArray& body) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        f.write(body);
        f.close();
        return QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                               QFileDevice::ReadGroup | QFileDevice::ExeGroup);
    };

    const QString cppFile = QDir(projDir).filePath(QStringLiteral("mini_mpi.cpp"));
    const QString binFile = QDir(projDir).filePath(QStringLiteral("mini_mpi"));
    {
        QFile sf(cppFile);
        QVERIFY(sf.open(QIODevice::WriteOnly | QIODevice::Text));
        sf.write("int main() { return 0; }\n");
        sf.close();
    }
    QProcess gxx;
    gxx.start(QStringLiteral("g++"), {QStringLiteral("-g"), QStringLiteral("-O0"), cppFile, QStringLiteral("-o"), binFile});
    QVERIFY(gxx.waitForFinished(15000) && gxx.exitCode() == 0 && QFileInfo(binFile).isExecutable());

    const QString capturedSbatch = QDir(shimDir).filePath(QStringLiteral("captured.sbatch"));
    const QString pollFile = QDir(shimDir).filePath(QStringLiteral("poll.cnt"));
    const QString scancelLog = QDir(shimDir).filePath(QStringLiteral("scancel.log"));
    const QString gdbCmdLog = QDir(shimDir).filePath(QStringLiteral("gdb_cmds.log"));
    const QString fakeGdb = QDir(shimDir).filePath(QStringLiteral("fake_gdb"));
    QVERIFY(writeExec(QDir(shimDir).filePath(QStringLiteral("sbatch")), QStringLiteral(
        "#!/bin/sh\ncp \"$2\" \"%1\"\necho \"77001\"\n").arg(capturedSbatch).toUtf8()));
    QVERIFY(writeExec(QDir(shimDir).filePath(QStringLiteral("squeue")), QStringLiteral(
        "#!/bin/sh\n"
        "for a in \"$@\"; do if [ \"$a\" = \"%N\" ]; then echo \"node[01-02]\"; exit 0; fi; done\n"
        "c=0; [ -f \"%1\" ] && c=$(cat \"%1\"); c=$((c+1)); echo \"$c\" > \"%1\"\n"
        "if [ \"$c\" -le 1 ]; then echo \"PENDING\"; else echo \"RUNNING\"; fi\n").arg(pollFile).toUtf8()));
    QVERIFY(writeExec(QDir(shimDir).filePath(QStringLiteral("scontrol")),
        "#!/bin/sh\nprintf 'node01\\nnode02\\n'\n"));
    QVERIFY(writeExec(QDir(shimDir).filePath(QStringLiteral("scancel")), QStringLiteral(
        "#!/bin/sh\necho \"$1\" >> \"%1\"\n").arg(scancelLog).toUtf8()));
    QVERIFY(writeExec(fakeGdb, QStringLiteral(
        "#!/bin/sh\n"
        "echo '=thread-group-added,id=\"i1\"'\n"
        "while IFS= read -r ln; do\n"
        "  echo \"$ln\" >> \"%1\"\n"
        "  case \"$ln\" in *-gdb-exit*) exit 0 ;; *-target-select*) echo '=thread-group-started,id=\"i1\",pid=\"42\"'; echo '*stopped,reason=\"signal-received\",signal-name=\"SIGTRAP\",frame={addr=\"0x00401120\",func=\"main\",args=[],file=\"%2\",fullname=\"%2\",line=\"1\"},thread-id=\"1\",stopped-threads=\"all\"'; echo '^connected' ;; *) echo '^done' ;; esac\n"
        "done\n").arg(gdbCmdLog, cppFile).toUtf8()));

    auto& cfg = gridlock::core::ConfigManager::instance();
    cfg.saveSshSettings({});
    auto dbg = cfg.getDebuggerSettings(); dbg.gdbPath = fakeGdb; cfg.saveDebuggerSettings(dbg);
    auto ps = cfg.loadProjectSettings(); ps.customGdbPath = fakeGdb.toStdString(); cfg.saveProjectSettings(ps);
    auto sl = cfg.getSlurmSettings();
    sl.sbatchPath = QDir(shimDir).filePath(QStringLiteral("sbatch"));
    sl.squeuePath = QDir(shimDir).filePath(QStringLiteral("squeue"));
    sl.scontrolPath = QDir(shimDir).filePath(QStringLiteral("scontrol"));
    sl.nodes = 2; sl.tasksPerNode = 2;
    sl.scriptTemplate = QStringLiteral("#!/bin/bash\n#SBATCH --nodes=2\n#SBATCH --ntasks-per-node=2\nsrun \"{FILE}\"\n");
    cfg.saveSlurmSettings(sl);

    gridlock::ui::MainWindow mw;
    mw.resize(1280, 800);
    mw.show();
    QApplication::processEvents();
    auto* slurmBackend = dynamic_cast<gridlock::core::LocalSlurmBackend*>(mw.hpcBackend());
    QVERIFY(slurmBackend != nullptr);
    slurmBackend->setPollIntervalMs(20);

    // Open the .cpp source file so m_currentFile is a .cpp file, while loading the compiled binary target.
    mw.loadBinaryTarget(binFile);
    mw.loadSourceFile(cppFile);
    QVERIFY(mw.onSubmitSlurmJob());

    QTRY_VERIFY_WITH_TIMEOUT(mw.isDebugSessionActive() && mw.serverRackView()->count() == 4, 6000);
    const QStringList expectedEndpoints{
        QStringLiteral("node01:2345"), QStringLiteral("node01:2346"),
        QStringLiteral("node02:2347"), QStringLiteral("node02:2348")};
    QCOMPARE(slurmBackend->resolvedComputeEndpoints(), expectedEndpoints);

    QFile cap(capturedSbatch);
    QVERIFY(cap.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString capBody = QString::fromUtf8(cap.readAll());
    QVERIFY2(capBody.contains(QStringLiteral("\"%1\"").arg(binFile)) && !capBody.contains(cppFile),
             "Staged sbatch script must launch the compiled binary and never substitute the .cpp source path");

    QTRY_VERIFY_WITH_TIMEOUT(([&]() {
        QFile gf(gdbCmdLog);
        if (!gf.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
        const QString g = QString::fromUtf8(gf.readAll());
        return g.contains(QStringLiteral("extended-remote node01:2345")) &&
               g.contains(QStringLiteral("extended-remote node01:2346")) &&
               g.contains(QStringLiteral("extended-remote node02:2347")) &&
               g.contains(QStringLiteral("extended-remote node02:2348"));
    })(), 4000);

    mw.stopTargetWorkflow();
    QTRY_VERIFY_WITH_TIMEOUT(([&]() {
        QFile sf(scancelLog);
        return sf.open(QIODevice::ReadOnly | QIODevice::Text) &&
               QString::fromUtf8(sf.readAll()).trimmed().contains(QStringLiteral("77001"));
    })(), 3000);
}

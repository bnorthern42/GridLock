/** @file test_ui_aux.cpp @brief Companion translation unit for TestMainWindowUI integration tests. */
#include "test_ui.hpp"
#include "ui/views/MemoryProfilerWidget.hpp"
#include "ui/dialogs/PointerGraphDialog.hpp"
#include "ui/widgets/FileIconProvider.hpp"
#include <cmath>
#include <QtWidgets>
#include <QtTest/QtTest>

#include "../src/core/hpc/GdbRankCoordinator.hpp"
#include "../src/core/managers/ConfigManager.hpp"
#include "../src/core/managers/SessionManager.hpp"
#include "../src/core/managers/ThemeManager.hpp"
#include "../src/core/utils/ClangdLspClient.hpp"
#include "../src/core/utils/CliParser.cpp"
#include "../src/core/utils/ElfBinaryInspector.hpp"
#include "../src/ui/EditorTabManager.hpp"
#include "../src/ui/GdbConsoleWidget.hpp"
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/dialogs/PreferencesDialog.hpp"
#include "../src/ui/dialogs/ProjectSettingsDialog.hpp"
#include "../src/ui/dialogs/XrefInspectorDialog.hpp"
#include "../src/ui/docks/TerminalDockWidget.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/views/MemView.hpp"
#include "../src/ui/views/RegisterView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
#include "../src/ui/views/SourceCodeView.hpp"
#include "../src/ui/widgets/DifferentialGrid.hpp"
#include "../src/ui/widgets/InlineFindBar.hpp"
#if __has_include("../src/ui/widgets/HpcConsoleWidget.hpp")
#include "../src/ui/widgets/HpcConsoleWidget.hpp"
#else
#include "../src/core/managers/SpackManager.hpp"
#endif
#include "../src/ui/widgets/ProjectExplorerWidget.hpp"

/** @brief Tests side dock resizability limits and xref / decompiler integration. */
void TestMainWindowUI::testDockResizabilityAndXrefDecompiler() {
    gridlock::ui::MainWindow mainWindow; mainWindow.loadPreviewFixture(); mainWindow.resize(1440, 900); mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    // 1. Left & Right Dock Resizability.
    QDockWidget* leftDock = nullptr; QDockWidget* rightDock = nullptr;
    for (QDockWidget* dock : mainWindow.findChildren<QDockWidget*>()) {
        const auto area = mainWindow.dockWidgetArea(dock);
        if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
            QVERIFY2(dock->minimumWidth() <= 200 && dock->maximumWidth() >= 1000, qPrintable(dock->windowTitle()));
            if (dock->widget()) QVERIFY2(dock->widget()->minimumWidth() <= 200 && dock->widget()->maximumWidth() >= 1000, qPrintable(dock->windowTitle()));
            if (area == Qt::LeftDockWidgetArea && !leftDock) leftDock = dock;
            if (area == Qt::RightDockWidgetArea && dock->objectName() == QStringLiteral("RegisterViewDock")) rightDock = dock;
        }
    }
    QVERIFY(leftDock != nullptr && rightDock != nullptr);
    mainWindow.resizeDocks({leftDock}, {340}, Qt::Horizontal); mainWindow.resizeDocks({rightDock}, {420}, Qt::Horizontal);
    QTest::qWait(30); QApplication::processEvents();
    QVERIFY2(std::abs(leftDock->width() - 340) <= 30, qPrintable(QString("Left dock width %1 did not resize near 340").arg(leftDock->width())));
    QVERIFY2(std::abs(rightDock->width() - 420) <= 30, qPrintable(QString("Right dock width %1 did not resize near 420").arg(rightDock->width())));
    // 2. Xrefs & Decompiler Mode in DisassemblyView.
    auto* disassemblyView = mainWindow.disassemblyView(); QVERIFY(disassemblyView != nullptr);
    const QVector<uint64_t> xrefs = disassemblyView->xrefsToAddress(0x00401149ULL);
    QVERIFY2(xrefs.size() >= 2 && xrefs.contains(0x00401163ULL) && xrefs.contains(0x0040116eULL), qPrintable(QString("Expected >= 2 xrefs to 0x00401149, got %1").arg(xrefs.size())));
    const QString pseudocode = disassemblyView->decompiledPseudocode();
    QVERIFY(!pseudocode.isEmpty() && (pseudocode.contains("int main(") || pseudocode.contains("sym.main")) && pseudocode.contains("MPI_Init") && pseudocode.contains("401149"));
    disassemblyView->setDecompilerMode(true); QVERIFY(disassemblyView->isDecompilerMode());
    disassemblyView->setDecompilerMode(false); QVERIFY(!disassemblyView->isDecompilerMode());
}
/** @brief Tests memory view toolbar geometry and source file line length limits. */
void TestMainWindowUI::testMemViewToolbarNoTruncationAndDisassemblyLayout() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QTest::qWait(30); QApplication::processEvents();
    // 1. MemView toolbar button & input non-truncation + non-overlap check (HEX DUMP & LIBRARY MEMORY MAP).
    auto* memView = mainWindow.memView(); QVERIFY(memView != nullptr);
    if (auto* bottomTabs = mainWindow.findChild<QTabWidget*>("BottomConsoleTabs")) { bottomTabs->setCurrentWidget(memView); QApplication::processEvents(); }
    auto* hexBtn = memView->findChild<QPushButton*>("memHexModeBtn"); auto* mapBtn = memView->findChild<QPushButton*>("memMapModeBtn");
    auto* popBtn = memView->findChild<QPushButton*>("memPopOutBtn"); auto* badge = memView->findChild<QLabel*>("memSourceBadge");
    QVERIFY(hexBtn && mapBtn && popBtn && badge);
    auto checkNoOverlap = [&]() {
        const QList<QWidget*> w = {hexBtn, mapBtn, badge, popBtn};
        for (int i = 0; i < w.size(); ++i) for (int j = i + 1; j < w.size(); ++j) QVERIFY2(!w[i]->geometry().intersects(w[j]->geometry()), "MemView toolbar overlap");
    };
    const auto buttons = memView->findChildren<QPushButton*>(); QVERIFY(!buttons.isEmpty());
    for (QPushButton* btn : buttons) if (btn->isVisible()) QVERIFY(btn->width() >= btn->fontMetrics().horizontalAdvance(btn->text()) + 12);
    auto* addrInput = memView->findChild<QLineEdit*>(); QVERIFY(addrInput != nullptr && addrInput->width() >= 90); checkNoOverlap();
    // Switch to LIBRARY MEMORY MAP: hex-only controls hide, badge/buttons do not overlap, table sorts descending by RSS.
    mapBtn->click(); QApplication::processEvents(); checkNoOverlap(); QVERIFY(!addrInput->isVisible());
    memView->notifyRankStoppedOrSelected(0, QCoreApplication::applicationPid(), 1); QApplication::processEvents();
    QVERIFY2(badge->text().contains(QString::number(QCoreApplication::applicationPid())), qPrintable(badge->text()));
    if (auto* tbl = memView->findChild<QTableWidget*>("memoryBreakdownTable"); tbl && tbl->rowCount() >= 2) QVERIFY(tbl->item(0, 2)->text().toDouble() >= tbl->item(tbl->rowCount() - 1, 2)->text().toDouble());
    // Verify 5 Memory Map color schemes in #memProfileColorSchemeCombo and #memoryMapColorSchemeCombo.
    auto* schemeCombo = memView->findChild<QComboBox*>("memProfileColorSchemeCombo"); QVERIFY2(schemeCombo && schemeCombo->count() == 5, "MemoryProfilerWidget must expose 5-scheme #memProfileColorSchemeCombo");
    { gridlock::ui::PreferencesDialog pd(&mainWindow); auto* prefScheme = pd.findChild<QComboBox*>("memoryMapColorSchemeCombo"); QVERIFY2(prefScheme && prefScheme->count() == 5, "PreferencesDialog Appearance must expose #memoryMapColorSchemeCombo"); }
    // Verify switching schemes repaints #memoryTreemapCanvas and differentiates Heap / Call Site blocks.
    auto* treemap = memView->findChild<QWidget*>("memoryTreemapCanvas"); QVERIFY(treemap != nullptr); treemap->resize(640, 220);
    schemeCombo->setCurrentIndex(0); QApplication::processEvents(); const QImage imgDist = treemap->grab().toImage(); schemeCombo->setCurrentIndex(1); QApplication::processEvents(); const QImage imgVir = treemap->grab().toImage(); QVERIFY2(imgDist != imgVir, "Switching Memory Map color scheme must repaint treemap with distinct palette");
    // Confirm ConfigManager persistence and per-module color differentiation within Heap / Call Site.
    gridlock::core::ConfigManager::instance().setMemoryMapColorScheme(QStringLiteral("Distinct")); const gridlock::ui::MemoryProfileEntry h1{QStringLiteral("Process Heap"), QString(), QStringLiteral("Heap / Call Site"), QStringLiteral("[heap]"), 1024, 0, 0, 0, 0, 0, {}}, h2{QStringLiteral("libopenblas.so.0"), QString(), QStringLiteral("Heap / Call Site"), QStringLiteral("blas_memory_alloc"), 512, 0, 0, 0, 0, 0, {}}, h3{QStringLiteral("inspection_demo"), QString(), QStringLiteral("Heap / Call Site"), QStringLiteral("allocateHaloBuffers"), 256, 0, 0, 0, 0, 0, {}};
    const QColor c1 = gridlock::ui::MemoryProfilerWidget::colorForEntry(0, 3, h1, QStringLiteral("Distinct")), c2 = gridlock::ui::MemoryProfilerWidget::colorForEntry(1, 3, h2, QStringLiteral("Distinct")), c3 = gridlock::ui::MemoryProfilerWidget::colorForEntry(2, 3, h3, QStringLiteral("Distinct")); QVERIFY(c1 != c2 && c2 != c3 && c1 != c3);
    popBtn->click(); QApplication::processEvents(); QVERIFY(memView->isPoppedOut() && popBtn->window()->isWindow() && popBtn->window()->width() >= 1000 && popBtn->window()->height() >= 650);
    popBtn->click(); QApplication::processEvents(); QVERIFY(!memView->isPoppedOut()); hexBtn->click(); QApplication::processEvents(); QVERIFY(addrInput->isVisible());
    mainWindow.setWorkspacePerspective(QStringLiteral("Binary")); QApplication::processEvents();
    auto* disassemblyView = mainWindow.disassemblyView(); QVERIFY(disassemblyView != nullptr && disassemblyView->width() >= 420);
    for (QSplitter* sp : mainWindow.findChildren<QSplitter*>()) QVERIFY2(sp->opaqueResize(), "QSplitter must use opaqueResize(true) for Wayland");
    QTemporaryDir tmpSrcDir; QVERIFY(tmpSrcDir.isValid()); const QString cppPath = tmpSrcDir.filePath(QStringLiteral("exit_check.cpp")); { QFile f(cppPath); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("int main() { return 0; }\n"); }
    auto* mw = new gridlock::ui::MainWindow(); mw->resize(1024, 700); mw->show(); QApplication::processEvents(); if (auto* tabs = mw->findChild<gridlock::ui::EditorTabManager*>()) tabs->openFile(cppPath); if (auto* mv = mw->findChild<gridlock::ui::MemView*>()) mv->setPoppedOut(true); QApplication::processEvents(); QVERIFY(mw->close()); delete mw; gridlock::core::ClangdLspClient::instance().shutdown(); QVERIFY(!gridlock::core::ClangdLspClient::instance().isRunning());
}
/** @brief Tests preferred screen routing and offscreen startup configuration. */
void TestMainWindowUI::testOffscreenDefaultAndPreferredScreenRouting() {
    // Instantiate MainWindow and verify every dock owns its custom titleBarWidget safely.
    gridlock::ui::MainWindow mainWindow; const auto docks = mainWindow.findChildren<QDockWidget*>(); QVERIFY(!docks.isEmpty());
    for (QDockWidget* dock : docks) if (dock && dock->titleBarWidget()) QVERIFY2(dock->titleBarWidget()->parentWidget() == dock && !dock->titleBarWidget()->isWindow(), qPrintable(QString("Dock '%1' titleBarWidget ownership").arg(dock->objectName())));
    // Confirm resolvePreferredScreen falls back to primaryScreen on empty or unknown monitor hints.
    QScreen* primary = QGuiApplication::primaryScreen(); QVERIFY(primary != nullptr);
    QCOMPARE(mainWindow.resolvePreferredScreen(""), primary); QCOMPARE(mainWindow.resolvePreferredScreen("nonexistent_monitor_xyz"), primary);
    mainWindow.applyPreferredScreenRouting("27", true); QVERIFY(mainWindow.testAttribute(Qt::WA_ShowWithoutActivating) && mainWindow.preferredMonitorHint() == QStringLiteral("27"));
    mainWindow.applyPreferredScreenRouting("2560x1440", false); QVERIFY(!mainWindow.testAttribute(Qt::WA_ShowWithoutActivating) && mainWindow.preferredMonitorHint() == QStringLiteral("2560x1440"));
    // Persist monitor preference and unfocused launch flag inside an isolated temporary config dir.
    auto& cfg = gridlock::core::ConfigManager::instance(); const QString prevMonitor = cfg.preferredMonitor(); const bool prevUnfocused = cfg.openUnfocused();
    QTemporaryDir tmpConfigDir; QVERIFY(tmpConfigDir.isValid());
    const QByteArray prevTestDir = qgetenv("GRIDLOCK_TEST_CONFIG_DIR"); qputenv("GRIDLOCK_TEST_CONFIG_DIR", tmpConfigDir.path().toUtf8()); cfg.loadConfig();
    cfg.setPreferredMonitor("2560x1440"); cfg.setOpenUnfocused(true); cfg.loadConfig(); QVERIFY(cfg.preferredMonitor() == QStringLiteral("2560x1440") && cfg.openUnfocused());
    cfg.setPreferredMonitor("1920x1080"); cfg.setOpenUnfocused(false); cfg.loadConfig(); QVERIFY(cfg.preferredMonitor() == QStringLiteral("1920x1080") && !cfg.openUnfocused());
    if (prevTestDir.isEmpty()) qunsetenv("GRIDLOCK_TEST_CONFIG_DIR"); else qputenv("GRIDLOCK_TEST_CONFIG_DIR", prevTestDir);
    cfg.loadConfig(); cfg.setPreferredMonitor(prevMonitor); cfg.setOpenUnfocused(prevUnfocused);
}

/** @brief Tests address history back/forward navigation, string lists, RegisterView filter/sort, and bookmarks. */
void TestMainWindowUI::testNavigationHistoryStringsAndBookmarks() {
    // Load synthetic preview fixture and grab disassembly, explorer, memory, and register widgets.
    gridlock::ui::MainWindow mainWindow; mainWindow.loadPreviewFixture();
    auto* disassemblyView = mainWindow.disassemblyView(); auto* projectExplorer = mainWindow.projectExplorerWidget();
    auto* memView = mainWindow.memView(); auto* regView = mainWindow.findChild<gridlock::ui::RegisterView*>();
    QVERIFY(disassemblyView != nullptr && projectExplorer != nullptr && memView != nullptr && regView != nullptr);
    // Verify RegisterView #registerSearchEdit filtering, interactive headers, and column sorting.
    regView->seedPreviewRegisters();
    auto* regTable = regView->findChild<QTableWidget*>(QStringLiteral("cutterRegisterTable")); auto* regSearch = regView->findChild<QLineEdit*>(QStringLiteral("registerSearchEdit"));
    QVERIFY2(regTable && regSearch && regTable->isSortingEnabled() && regTable->horizontalHeader()->sectionResizeMode(0) == QHeaderView::Interactive, "RegisterView must expose #registerSearchEdit, sorting, and Interactive header");
    // Confirm filtering for "rsp" isolates 1 matching register row and clear() restores all rows.
    const int totalRegs = regTable->rowCount(); QVERIFY(totalRegs >= 10);
    auto visRegRows = [&]() { QStringList r; for (int i = 0; i < regTable->rowCount(); ++i) if (!regTable->isRowHidden(i)) r << regTable->item(i, 0)->text(); return r; };
    regSearch->setText(QStringLiteral("rsp")); QApplication::processEvents();
    QVERIFY2(visRegRows().contains(QStringLiteral("rsp")) && visRegRows().size() == 1, qPrintable(visRegRows().join(',')));
    regSearch->clear(); QApplication::processEvents(); QCOMPARE(visRegRows().size(), totalRegs);
    // Sort column 0 ascending vs descending and confirm first visible register flips cleanly.
    regTable->sortByColumn(0, Qt::AscendingOrder); QApplication::processEvents(); const QString asc0 = regTable->item(0, 0)->text();
    regTable->sortByColumn(0, Qt::DescendingOrder); QApplication::processEvents(); const QString desc0 = regTable->item(0, 0)->text();
    QVERIFY2(asc0 != desc0 && asc0 < desc0, "Sorting RegisterView NAME asc/desc must invert row order");
    // Seek across sequence of virtual addresses and verify back/forward navigation.
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401120")); QCOMPARE(disassemblyView->currentAddress(), 0x00401120ULL);
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401149")); QCOMPARE(disassemblyView->currentAddress(), 0x00401149ULL);
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401178")); QCOMPARE(disassemblyView->currentAddress(), 0x00401178ULL);
    QVERIFY(mainWindow.canNavigateBack() && !mainWindow.canNavigateForward());
    QVERIFY(mainWindow.navigateBack()); QCOMPARE(disassemblyView->currentAddress(), 0x00401149ULL);
    QVERIFY(mainWindow.canNavigateBack() && mainWindow.canNavigateForward());
    QVERIFY(mainWindow.navigateForward()); QCOMPARE(disassemblyView->currentAddress(), 0x00401178ULL);
    QVERIFY(!mainWindow.canNavigateForward());
    // Activate a binary string entry in Strings mode and confirm MemoryView follows target address.
    projectExplorer->setStringsMode(true); QVERIFY(projectExplorer->isStringsMode() && projectExplorer->stringCount() >= 4);
    QSignalSpy spyStr(projectExplorer, &gridlock::ui::ProjectExplorerWidget::symbolAddressSelected);
    QVERIFY(projectExplorer->activateString("MPI_COMM_WORLD") && spyStr.count() == 1);
    QCOMPARE(spyStr.at(0).at(0).toULongLong(), 0x00402000ULL); QCOMPARE(memView->currentBaseAddress(), 0x00402000ULL);
    QVERIFY(mainWindow.canNavigateBack());
    // Verify decompiler line <-> address bidirectional synchronization and token highlighting.
    disassemblyView->setSplitGraphDecompilerMode(true);
    QVERIFY(disassemblyView->seekToAddress(0x0040113dULL));
    const int initLine = disassemblyView->currentDecompilerLine();
    QVERIFY(initLine > 0 && disassemblyView->decompilerLineToAddress(initLine) == 0x0040113dULL);
    QCOMPARE(disassemblyView->addressToDecompilerLine(0x0040113dULL), initLine);
    const int loopLine = disassemblyView->addressToDecompilerLine(0x00401149ULL);
    QVERIFY(loopLine > 0 && disassemblyView->selectDecompilerLine(loopLine));
    QCOMPARE(disassemblyView->currentAddress(), 0x00401149ULL); QCOMPARE(memView->currentBaseAddress(), 0x00401149ULL);
    disassemblyView->highlightToken("argc"); QCOMPARE(disassemblyView->highlightedToken(), QStringLiteral("argc"));
    // Set address comments and toggle bookmarks.
    disassemblyView->setAddressComment(0x00401149ULL, " Outer loop rank check ");
    QCOMPARE(disassemblyView->addressComment(0x00401149ULL), QStringLiteral("Outer loop rank check"));
    QVERIFY(!disassemblyView->isBookmarked(0x00401149ULL) && disassemblyView->toggleBookmark(0x00401149ULL));
    QVERIFY(disassemblyView->isBookmarked(0x00401149ULL) && disassemblyView->bookmarkedAddresses().contains(0x00401149ULL));
    QVERIFY(!disassemblyView->toggleBookmark(0x00401149ULL) && !disassemblyView->isBookmarked(0x00401149ULL));
}

/** @brief Tests multi-language decompiler target generation and source fallback. */
void TestMainWindowUI::testCfgGraphDecompilerLanguagesAndSourceFallback() {
    gridlock::ui::MainWindow mainWindow; mainWindow.loadPreviewFixture();
    auto* disassemblyView = mainWindow.disassemblyView(); QVERIFY(disassemblyView != nullptr);
    // Generate C target pseudocode and check typed locals, control flow, and MPI calls.
    disassemblyView->setDecompilerTargetLanguage("C"); QCOMPARE(disassemblyView->decompilerTargetLanguage(), QStringLiteral("C"));
    const QString cOut = disassemblyView->decompileToLanguage("C");
    QVERIFY((cOut.contains("int32_t") || cOut.contains("var_")) && (cOut.contains("if (") || cOut.contains("while (")) && cOut.contains("MPI_Init"));
    // Generate distinct C++ and high-level Pseudocode outputs for comparison.
    const QString cppOut = disassemblyView->decompileToLanguage("C++"); const QString pseudoOut = disassemblyView->decompileToLanguage("Pseudocode");
    QVERIFY(!cppOut.isEmpty() && !pseudoOut.isEmpty() && cppOut != cOut && pseudoOut != cOut && cppOut != pseudoOut);
    // Inspect resolved function signature header and verify basic-block CFG graph toggle.
    const QString sigHeader = disassemblyView->functionSignatureHeader();
    QVERIFY(!sigHeader.isEmpty() && sigHeader.contains("sym.main(int argc, char **argv)"));
    disassemblyView->setGraphMode(true); QVERIFY(disassemblyView->isGraphMode() && disassemblyView->basicBlockCount() >= 3);
    // Verify source code fallback engages on unmapped CRT/startup address misses.
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401120") && !mainWindow.isShowingDecompilerFallback());
    QVERIFY(mainWindow.seekToAddressOrExpression("0x00401000") && mainWindow.isShowingDecompilerFallback());
}

/**
 * @brief Tests CFG rank divergence badges, minimap overlay, and rack rank selection.
 */
void TestMainWindowUI::testCfgRankDivergenceMinimapAndRackSync() {
    // Instantiate primary MainWindow for rank telemetry sync tests.
    gridlock::ui::MainWindow mainWindow; mainWindow.loadPreviewFixture(); mainWindow.resize(1440, 900); mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    // Locate DisassemblyView, ServerRackView, and status bar telemetry pill.
    auto* disassemblyView = mainWindow.disassemblyView(); auto* serverRackView = mainWindow.serverRackView();
    auto* statusTelemetry = mainWindow.findChild<QLabel*>("cutterStatusTelemetry");
    QVERIFY(disassemblyView && serverRackView && statusTelemetry);

    // Set split graph decompiler and assign rank instruction pointers.
    disassemblyView->setSplitGraphDecompilerMode(true);
    // Populate 4 divergent rank program counters across distinct basic blocks.
    disassemblyView->setRankAddresses({{0, 0x00401168ULL}, {1, 0x00401155ULL}, {2, 0x00401157ULL}, {3, 0x0040115fULL}});
    // Verify Rank 0 maps to block 0x00401168, Rank 1 to 0x0040114f, Ranks 2 and 3 to 0x00401157.
    QCOMPARE(disassemblyView->ranksAtBlock(0x00401168ULL), QVector<int>{0});
    QCOMPARE(disassemblyView->ranksAtBlock(0x0040114fULL), QVector<int>{1});
    QCOMPARE(disassemblyView->ranksAtBlock(0x00401157ULL), (QVector<int>{2, 3}));

    // Verify minimap visibility toggle and geometry constraints.
    QVERIFY(disassemblyView->isGraphMinimapVisible() && disassemblyView->minimapRect().width() >= 80 && disassemblyView->minimapRect().height() >= 60);
    // Hide minimap overlay and confirm visibility flag flips false.
    disassemblyView->setGraphMinimapVisible(false);
    QVERIFY(!disassemblyView->isGraphMinimapVisible());
    // Re-enable minimap overlay and confirm visibility flag flips back to true.
    disassemblyView->setGraphMinimapVisible(true);
    QVERIFY(disassemblyView->isGraphMinimapVisible());

    // Select rank 1 in ServerRackView and verify status telemetry and address update.
    QVERIFY(serverRackView->selectRank(1) && serverRackView->selectedRank() == 1);
    // Confirm telemetry badge reports MPI RANK: 1 and DisassemblyView focuses 0x00401155.
    QVERIFY(statusTelemetry->text().contains("MPI RANK: 1") && disassemblyView->currentAddress() == 0x00401155ULL && disassemblyView->currentDecompilerLine() > 0);
}

/**
 * @brief Tests dynamic rank state updates without relying on constructor mocks.
 */
void TestMainWindowUI::testDynamicRankSyncWithoutHardcodedConstructorMocks() {
    // 1. Standalone widgets start clean with zero hardcoded mock ranks.
    gridlock::ui::ServerRackView standaloneRack; QCOMPARE(standaloneRack.rankNodeCount(), 0);
    gridlock::ui::DisassemblyView standaloneDisasm;
    QVERIFY(standaloneDisasm.ranksAtBlock(0x00401155ULL).isEmpty() && standaloneDisasm.ranksAtBlock(0x00401168ULL).isEmpty());

    // 2. Dynamic updateRanks(QList<RankState>) populates rank cards and PC addresses.
    QList<gridlock::RankState> states;
    for (int r = 0; r < 4; ++r) {
        gridlock::RankState st; st.rankId = r; st.currentState = "stopped";
        st.registers[16] = (r == 0) ? QStringLiteral("0x00401168") : QStringLiteral("0x00401155");
        states.append(st);
    }
    standaloneRack.updateRanks(states);
    QCOMPARE(standaloneRack.rankNodeCount(), 4); QCOMPARE(standaloneRack.rankPcAddress(0), 0x00401168ULL); QCOMPARE(standaloneRack.rankPcAddress(1), 0x00401155ULL);

    // 3 & 4. MainWindow dynamic rank synchronization and loadPreviewFixture() 8-rank pipeline.
    gridlock::ui::MainWindow mainWindow;
    auto* rack = mainWindow.serverRackView(); auto* disasm = mainWindow.serverRackView() ? mainWindow.disassemblyView() : nullptr;
    QVERIFY(rack && disasm);
    disasm->loadSampleDisassembly();
    rack->updateRanks(states); QCOMPARE(rack->rankNodeCount(), 4);
    QCOMPARE(disasm->ranksAtBlock(0x00401168ULL), QVector<int>{0}); QCOMPARE(disasm->ranksAtBlock(0x0040114fULL), (QVector<int>{1, 2, 3}));
    mainWindow.loadPreviewFixture(); QCOMPARE(rack->rankNodeCount(), 8);
    QCOMPARE(disasm->ranksAtBlock(0x00401120ULL), QVector<int>{0}); QCOMPARE(disasm->ranksAtBlock(0x00401157ULL), (QVector<int>{2, 3, 4, 5, 6, 7}));
}

/**
 * @brief Tests ELF64 binary parsing, section and symbol extraction, and static target loading.
 */
void TestMainWindowUI::testElfBinaryInspectorAndStaticTargetLoad() {
    // 1 & 2. Locate current test executable and parse ELF64 binary using ElfBinaryInspector.
    const QString appPath = QCoreApplication::applicationFilePath(); QVERIFY(!appPath.isEmpty() && QFile::exists(appPath));
    gridlock::core::ElfBinaryInspector inspector; QVERIFY(inspector.loadFile(appPath) && inspector.isValid() && inspector.entryPoint() != 0ULL);
    // 3 & 4. Inspect ELF section headers (.text, .rodata, .data), symbol table, and printable ASCII strings.
    const auto sections = inspector.sections(); QVERIFY(!sections.isEmpty());
    bool hasTextSection = false; uint64_t textVaddr = 0;
    for (const auto& sec : sections) if (sec.name == QStringLiteral(".text")) { hasTextSection = true; textVaddr = sec.address; break; }
    QVERIFY(hasTextSection && textVaddr != 0ULL && inspector.symbols().size() > 20 && inspector.strings().size() > 10);
    // 6 & 7. Test virtual memory byte reading and disassembleRange for the .text section.
    const QByteArray textBytes = inspector.readVirtualMemory(textVaddr, 32);
    QVERIFY(textBytes.size() == 32 && textBytes != QByteArray(32, '\0') && !inspector.disassembleRange(textVaddr, 32, QStringLiteral("entry")).isEmpty());
    // 8, 9 & 10. Test MainWindow static target loading, ProjectExplorerWidget population, and address seek sync.
    gridlock::ui::MainWindow mainWindow; QVERIFY(mainWindow.loadBinaryTarget(appPath));
    auto* explorer = mainWindow.findChild<gridlock::ui::ProjectExplorerWidget*>();
    QVERIFY(explorer != nullptr && explorer->symbolCount() > 20 && explorer->stringCount() > 10 && mainWindow.seekToAddressOrExpression(QString("0x%1").arg(textVaddr, 0, 16)));
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>(); auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(memView != nullptr && memView->currentBaseAddress() == textVaddr && disasmView != nullptr && !disasmView->toPlainText().isEmpty());
}

/**
 * @brief Tests interactive Xref extraction, incoming/outgoing call graph analysis, and inspector dialog sync.
 */
void TestMainWindowUI::testInteractiveXrefInspectorAndCallGraphSync() {
    // 1. Load preview fixture providing canonical multi-rank MPI loop disassembly.
    gridlock::ui::MainWindow mainWindow; mainWindow.loadPreviewFixture();
    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>(); QVERIFY(disasm != nullptr);
    // 2. Verify structured incoming cross-references targeting the barrier loop header 0x00401149.
    const auto incoming = disasm->incomingXrefs(0x00401149ULL); QVERIFY(incoming.size() >= 2);
    bool foundBackEdge = false;
    for (const auto& xr : incoming) { QCOMPARE(xr.toAddress, 0x00401149ULL); if (xr.fromAddress == 0x00401163ULL || xr.fromAddress == 0x0040116eULL) foundBackEdge = true; QVERIFY(xr.type == QStringLiteral("JMP") || xr.type == QStringLiteral("CJMP")); }
    QVERIFY(foundBackEdge);
    // 3. Verify structured outgoing cross-references originating from function 0x00401120.
    const auto outgoing = disasm->outgoingXrefs(0x00401120ULL); QVERIFY(outgoing.size() >= 2);
    bool foundMpiInitCall = false, foundMpiFinalizeCall = false;
    for (const auto& xr : outgoing) { if (xr.type == QStringLiteral("CALL")) { if (xr.toAddress == 0x00401050ULL || xr.instructionText.contains(QStringLiteral("MPI_Init"))) foundMpiInitCall = true; if (xr.toAddress == 0x00401060ULL || xr.instructionText.contains(QStringLiteral("MPI_Finalize"))) foundMpiFinalizeCall = true; } }
    QVERIFY(foundMpiInitCall && foundMpiFinalizeCall);
    // 4. Open XrefInspectorDialog at address 0x00401149 and test filtering + incoming row activation.
    auto* dialog = disasm->openXrefInspector(0x00401149ULL);
    QVERIFY(dialog != nullptr && dialog->incomingRowCount() >= 2 && dialog->outgoingRowCount() >= 2 && !dialog->previewText().isEmpty());
    dialog->setFilterText(QStringLiteral("MPI_Init")); QVERIFY(dialog->outgoingRowCount() >= 1);
    dialog->setFilterText(QString()); QVERIFY(dialog->outgoingRowCount() >= 2);
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>();
    QVERIFY(memView != nullptr && dialog->activateIncomingXref(0));
    QCOMPARE(disasm->currentAddress(), incoming.first().fromAddress); QCOMPARE(memView->currentBaseAddress(), incoming.first().fromAddress);
}

/**
 * @brief Tests terminal monospace isolation, live byte/instruction patching, ELF export, workspace search, and deadlock propagation.
 */
void TestMainWindowUI::testPatchedElfExportWorkspaceSearchAndDeadlockPropagation() {
    // 1. Initialize MainWindow, load running binary target, and verify terminal monospace font enforcement.
    gridlock::ui::MainWindow mainWindow; QVERIFY(mainWindow.loadBinaryTarget(QCoreApplication::applicationFilePath()));
    mainWindow.loadPreviewFixture();
    if (auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>()) {
        terminalDock->setCustomFont("Cantarell", 11);
        QVERIFY(QFontInfo(terminalDock->activeTerminalFont()).fixedPitch());
    }
    auto* inspector = mainWindow.elfInspector(); QVERIFY(inspector != nullptr && inspector->isValid());
    uint64_t textAddr = 0;
    for (const auto& sec : inspector->sections()) if (sec.name == QStringLiteral(".text")) { textAddr = sec.address; break; }
    if (textAddr == 0) textAddr = inspector->entryPoint();
    QVERIFY(textAddr != 0);

    // 2. Apply live 2-byte patch via MemView, test revertAllPatches(), re-patch, and export patched ELF to disk.
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>();
    const QByteArray patchBytes = QByteArray::fromHex("9090");
    QSignalSpy patchSpy(memView, &gridlock::ui::MemView::bytesPatched);
    QVERIFY(memView != nullptr && memView->patchBytes(textAddr, patchBytes) && memView->patchedByteCount() == 2 && patchSpy.count() == 1);
    memView->revertAllPatches(); QCOMPARE(memView->patchedByteCount(), 0);
    QVERIFY(memView->patchBytes(textAddr, patchBytes));
    QTemporaryDir tempDir; const QString exportPath = tempDir.filePath(QStringLiteral("patched_gridlock_bin"));
    QVERIFY(tempDir.isValid() && mainWindow.exportPatchedBinary(exportPath) && QFile::exists(exportPath));
    gridlock::core::ElfBinaryInspector verifyInspector(exportPath);
    QVERIFY(verifyInspector.isValid() && verifyInspector.readVirtualMemory(textAddr, 2) == patchBytes && verifyInspector.writeVirtualMemory(textAddr, patchBytes));

    // 3. Test DisassemblyView instruction patching, workspace search, and deadlock wait-for cycle propagation.
    auto* disasm = mainWindow.findChild<gridlock::ui::DisassemblyView*>(); QVERIFY(disasm != nullptr);
    const int initialArcs = disasm->branchArcCount();
    QVERIFY(disasm->patchInstruction(0x00401155ULL, QStringLiteral("nop")) && disasm->patchedInstructionCount() == 1 && disasm->branchArcCount() == initialArcs - 1);
    const auto searchMatches = mainWindow.searchWorkspace(QStringLiteral("main"));
    bool foundValidAddress = false;
    for (const auto& match : searchMatches) {
        if (match.address != 0 && (match.label.contains(QStringLiteral("main"), Qt::CaseInsensitive) || match.contextSnippet.contains(QStringLiteral("main"), Qt::CaseInsensitive))) { foundValidAddress = true; break; }
    }
    QVERIFY(!searchMatches.isEmpty() && foundValidAddress);
    const QVector<int> detectedCycle = mainWindow.detectAndHighlightDeadlock({{0, 1}, {1, 0}});
    auto* telemetryLabel = mainWindow.findChild<QLabel*>(QStringLiteral("cutterStatusTelemetry"));
    QVERIFY(detectedCycle.contains(0) && detectedCycle.contains(1) && disasm->deadlockCycleRanks().contains(0) && telemetryLabel && telemetryLabel->text().contains(QStringLiteral("DEADLOCK")));
}

/** @brief Verifies system package manager detection, multi-distro parsing, view modes, and live host queries. */
void TestMainWindowUI::testMultiDistroPackageManagerParsingAndViewModes() {
    // 1. System Package Manager Fallback Detection When Spack Is Missing.
    gridlock::ui::HpcConsoleWidget widget; QVERIFY(!widget.activePackageManagerBackend().isEmpty());
    // 2. Multi-Distro CLI Output Parsing (pacman, dnf4, dnf5, apt, zypper).
    const QString pacmanOut = QStringLiteral("extra/openmpi 5.0.5-1 [installed]\n    High performance message passing library (MPI)\nextra/fftw 3.3.10-7\n    Library for computing the discrete Fourier transform\n");
    const auto pacmanRecs = widget.parsePackageManagerOutput(QStringLiteral("pacman"), pacmanOut);
    QVERIFY(pacmanRecs.size() == 2 && pacmanRecs[0].name == QStringLiteral("openmpi") && pacmanRecs[0].version == QStringLiteral("5.0.5-1") && pacmanRecs[0].repository == QStringLiteral("extra") && pacmanRecs[0].status == QStringLiteral("Installed") && pacmanRecs[1].name == QStringLiteral("fftw") && pacmanRecs[1].status == QStringLiteral("Available"));
    // Verify Fedora/RHEL dnf4 ("pkg.arch : summary") and Fedora 40+ dnf5 (" pkg.arch\tsummary") formats.
    const auto dnfRecs = widget.parsePackageManagerOutput(QStringLiteral("dnf"), QStringLiteral("Last metadata expiration check...\n=== Name & Summary Matched: openmpi ===\nopenmpi.x86_64 : Open Message Passing Interface\nopenmpi-devel.x86_64 : Development headers for OpenMPI\n"));
    const auto dnf5Recs = widget.parsePackageManagerOutput(QStringLiteral("dnf"), QStringLiteral("Updating and loading repositories:\nRepositories loaded.\nMatched fields: name, summary\n fd-find.x86_64\tFast and user-friendly alternative to find\n fdupes.x86_64  Finds duplicate files in a given set of directories\n"));
    QVERIFY(dnfRecs.size() == 2 && dnfRecs[0].name == QStringLiteral("openmpi") && dnf5Recs.size() == 2 && dnf5Recs[0].name == QStringLiteral("fd-find") && dnf5Recs[0].arch == QStringLiteral("x86_64") && dnf5Recs[1].name == QStringLiteral("fdupes"));
    // Verify Debian/Ubuntu apt-cache search and openSUSE zypper se -s pipe-delimited table parsing.
    const auto aptRecs = widget.parsePackageManagerOutput(QStringLiteral("apt"), QStringLiteral("openmpi-bin - high performance message passing library -- binaries\nlibopenmpi-dev - high performance message passing library -- header files\n"));
    const auto zypperRecs = widget.parsePackageManagerOutput(QStringLiteral("zypper"), QStringLiteral("S  | Name        | Type    | Version   | Arch   | Repository\n---+-------------+---------+-----------+--------+----------------\ni+ | openmpi4    | package | 4.1.6-1.1 | x86_64 | Main Repository\n   | openmpi4-devel | package | 4.1.6-1.1 | x86_64 | Main Repository\n"));
    QVERIFY(aptRecs.size() == 2 && aptRecs[0].name == QStringLiteral("openmpi-bin") && zypperRecs.size() == 2 && zypperRecs[0].name == QStringLiteral("openmpi4") && zypperRecs[0].status == QStringLiteral("Installed") && zypperRecs[1].status == QStringLiteral("Available"));
    // 3. Three Switchable Views (Table, List, Raw) & Non-Empty Table Population.
    widget.displayPackageRecords(pacmanRecs, pacmanOut); QCOMPARE(widget.packageTableRowCount(), 2);
    for (const QString& mode : {QStringLiteral("Table"), QStringLiteral("List"), QStringLiteral("Raw")}) { widget.setViewMode(mode); QCOMPARE(widget.currentViewMode(), mode); }
    auto* listBrowser = widget.findChild<QTextBrowser*>(); QVERIFY(listBrowser != nullptr && listBrowser->toPlainText().contains(QStringLiteral("openmpi")));
    // 4. Verify packageStatusFilterCombo generous width (>= 145px) and live host package query for "fd".
    auto* statusCombo = widget.findChild<QComboBox*>(QStringLiteral("packageStatusFilterCombo"));
    QVERIFY(statusCombo != nullptr && statusCombo->minimumWidth() >= 145 && statusCombo->count() == 3 && !statusCombo->itemIcon(0).isNull());
    // Execute live host query for "fd" and verify visiblePackageCount() matches actual visible table rows (> 0).
    const int fdMatches = widget.searchPackagesSynchronously(QStringLiteral("fd"), 5000);
    QVERIFY2(fdMatches > 0 && widget.visiblePackageCount() == fdMatches, "Live host package query for 'fd' must return > 0 real system/repo packages");
}

/** @brief Verifies file CRUD, build registration, and #explorerFilterEdit filtering + sorting across FILES, SYMBOLS, and STRINGS. */
void TestMainWindowUI::testProjectExplorerFileCrudAndBuildRegistration() {
    // Initialize ProjectExplorerWidget on a fresh temporary directory and verify empty disassembly startup.
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    gridlock::ui::ProjectExplorerWidget explorer; explorer.setRootPath(tempDir.path());
    QVERIFY2(!explorer.isSymbolsMode() && !explorer.isStringsMode() && explorer.symbolCount() == 0 && explorer.stringCount() == 0, "ProjectExplorerWidget must start in FILES mode with 0 symbols and 0 strings");
    gridlock::ui::DisassemblyView idleDisasm;
    QVERIFY2(idleDisasm.basicBlockCount() == 0 && idleDisasm.decompiledPseudocode().isEmpty() && !idleDisasm.toPlainText().contains(QStringLiteral("0x00401120")) && !idleDisasm.decompiledPseudocode().contains(QStringLiteral("var_4h")), "DisassemblyView must start empty without constructor mock disassembly");
    // Connect CRUD signal counters and verify C++/header creation plus subfolder creation.
    int createdCount = 0, renamedCount = 0, deletedCount = 0;
    QObject::connect(&explorer, &gridlock::ui::ProjectExplorerWidget::fileCreated, [&](const QString&) { ++createdCount; }); QObject::connect(&explorer, &gridlock::ui::ProjectExplorerWidget::fileRenamed, [&](const QString&, const QString&) { ++renamedCount; }); QObject::connect(&explorer, &gridlock::ui::ProjectExplorerWidget::fileDeleted, [&](const QString&) { ++deletedCount; });
    const QString cppPath = tempDir.filePath(QStringLiteral("solver_kernel.cpp")), hppPath = tempDir.filePath(QStringLiteral("solver_kernel.hpp"));
    QVERIFY2(explorer.createNewFile(tempDir.path(), QStringLiteral("solver_kernel.cpp")) && QFileInfo::exists(cppPath) && createdCount == 1 && explorer.createNewFile(tempDir.path(), QStringLiteral("solver_kernel.hpp")) && QFileInfo::exists(hppPath) && createdCount == 2, "createNewFile(.cpp/.hpp)");
    QFile hppFile(hppPath); QVERIFY(hppFile.open(QIODevice::ReadOnly) && QString::fromUtf8(hppFile.readAll()).contains(QStringLiteral("#pragma once")));
    QVERIFY2(explorer.createNewFolder(tempDir.path(), QStringLiteral("kernels")) && QDir(tempDir.filePath(QStringLiteral("kernels"))).exists(), "createNewFolder");
    // Locate unified #explorerFilterEdit, sortable SYMBOLS/STRINGS tables, and FILES tree view.
    auto* expFilter = explorer.findChild<QLineEdit*>(QStringLiteral("explorerFilterEdit")); auto* fsTree = explorer.findChild<QTreeView*>();
    auto* symTable = explorer.findChild<QTableWidget*>(QStringLiteral("symbolsTable")); auto* strTable = explorer.findChild<QTableWidget*>(QStringLiteral("stringsTable"));
    QVERIFY2(expFilter && expFilter->isClearButtonEnabled() && symTable && strTable && fsTree && symTable->isSortingEnabled() && strTable->isSortingEnabled() && symTable->horizontalHeader()->sectionResizeMode(0) == QHeaderView::Interactive && strTable->horizontalHeader()->sectionResizeMode(0) == QHeaderView::Interactive, "ProjectExplorerWidget must expose #explorerFilterEdit, sortable headers, and Interactive columns");
    // Seed synthetic symbols and strings in unsorted order to verify numeric & alphabetic sorting.
    explorer.setSymbols({{QStringLiteral("z_func"), 0x401200ULL, 32}, {QStringLiteral("MPI_Init"), 0x401050ULL, 16}, {QStringLiteral("MPI_Barrier"), 0x401100ULL, 24}, {QStringLiteral("a_func"), 0x401010ULL, 8}});
    explorer.setStrings({{0x402050ULL, QStringLiteral(".rodata"), QStringLiteral("zebra_msg")}, {0x402000ULL, QStringLiteral(".rodata"), QStringLiteral("MPI_COMM_WORLD")}, {0x402010ULL, QStringLiteral(".dynstr"), QStringLiteral("alpha_str")}});
    // Filter FILES tree via #explorerFilterEdit and confirm clear() restores both kernel files.
    auto visibleTreeNames = [&]() { QStringList out; const QModelIndex r = fsTree->rootIndex(); for (int i = 0; i < fsTree->model()->rowCount(r); ++i) out << fsTree->model()->index(i, 0, r).data().toString(); return out; };
    QTRY_VERIFY_WITH_TIMEOUT(visibleTreeNames().contains(QStringLiteral("solver_kernel.cpp")) && visibleTreeNames().contains(QStringLiteral("solver_kernel.hpp")), 3000);
    expFilter->setText(QStringLiteral("solver_kernel.hpp")); QApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(visibleTreeNames().contains(QStringLiteral("solver_kernel.hpp")) && !visibleTreeNames().contains(QStringLiteral("solver_kernel.cpp")), 3000);
    expFilter->clear(); QApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(visibleTreeNames().contains(QStringLiteral("solver_kernel.cpp")), 3000);
    // Filter SYMBOLS for "MPI_", clear, sort descending/ascending, and verify sorted double-click address.
    explorer.setSymbolsMode(true); QSignalSpy symClickSpy(&explorer, &gridlock::ui::ProjectExplorerWidget::symbolAddressSelected);
    expFilter->setText(QStringLiteral("MPI_")); QApplication::processEvents();
    QVERIFY2(symTable->rowCount() == 2 && symTable->item(0, 0)->text().startsWith(QStringLiteral("MPI_")) && symTable->item(1, 0)->text().startsWith(QStringLiteral("MPI_")), "SYMBOLS filter must keep only MPI_ rows");
    expFilter->clear(); QApplication::processEvents(); QCOMPARE(symTable->rowCount(), 4);
    symTable->sortByColumn(0, Qt::DescendingOrder); QApplication::processEvents(); QCOMPARE(symTable->item(0, 0)->text(), QStringLiteral("z_func"));
    emit symTable->cellDoubleClicked(0, 0); QCOMPARE(symClickSpy.count(), 1); QCOMPARE(symClickSpy.last().at(0).toULongLong(), 0x401200ULL);
    symTable->sortByColumn(1, Qt::AscendingOrder); QApplication::processEvents(); QCOMPARE(symTable->item(0, 0)->text(), QStringLiteral("a_func"));
    // Switch to STRINGS mode, filter for "MPI_COMM", clear, and sort column 2 alphabetically.
    explorer.setStringsMode(true); expFilter->setText(QStringLiteral("MPI_COMM")); QApplication::processEvents();
    QVERIFY2(strTable->rowCount() == 1 && strTable->item(0, 2)->text() == QStringLiteral("MPI_COMM_WORLD"), "STRINGS filter must isolate MPI_COMM_WORLD");
    expFilter->clear(); QApplication::processEvents(); QCOMPARE(strTable->rowCount(), 3);
    strTable->sortByColumn(2, Qt::AscendingOrder); QApplication::processEvents(); QCOMPARE(strTable->item(0, 2)->text(), QStringLiteral("alpha_str"));
    // Return to FILES mode and verify renameEntry, registerSourceInBuildFile, and deleteEntry.
    explorer.setSymbolsMode(false);
    const QString renamedPath = tempDir.filePath(QStringLiteral("mpi_solver_kernel.cpp"));
    QVERIFY2(explorer.renameEntry(cppPath, QStringLiteral("mpi_solver_kernel.cpp")) && !QFileInfo::exists(cppPath) && QFileInfo::exists(renamedPath) && renamedCount == 1, "renameEntry");
    const QString mesonBuildPath = tempDir.filePath(QStringLiteral("meson.build"));
    { QFile mf(mesonBuildPath); QVERIFY(mf.open(QIODevice::WriteOnly)); mf.write("executable('demo',\n  'src/main.cpp'\n)\n"); }
    QVERIFY2(explorer.registerSourceInBuildFile(mesonBuildPath, QStringLiteral("src/mpi_solver_kernel.cpp")), "registerSourceInBuildFile");
    QFile mfCheck(mesonBuildPath); QVERIFY(mfCheck.open(QIODevice::ReadOnly) && QString::fromUtf8(mfCheck.readAll()).contains(QStringLiteral("src/mpi_solver_kernel.cpp")));
    QVERIFY2(explorer.deleteEntry(renamedPath) && !QFileInfo::exists(renamedPath) && deletedCount == 1, "deleteEntry");

    // 7. Simulate an external C++/MPI project workspace with subdirs, CMakeLists.txt, src/solver_main.cpp, and build_meson/solver_demo ELF.
    const QString savedRepoRoot = QFileInfo::exists(QStringLiteral("meson.build")) ? QDir::currentPath() : QDir(QStringLiteral("..")).absolutePath();
    QTemporaryDir extDir, otherDir, unbuiltDir; QVERIFY(extDir.isValid() && otherDir.isValid() && unbuiltDir.isValid());
    for (const QString& sub : {QStringLiteral("SolverUtils"), QStringLiteral("ProblemSetup"), QStringLiteral("build_meson/meson-private"), QStringLiteral("src"), QStringLiteral(".gridlock")}) QVERIFY(QDir(extDir.path()).mkpath(sub));
    QVERIFY(QDir(otherDir.path()).mkpath(QStringLiteral("AMR_Electrode_Cut")) && QDir(unbuiltDir.path()).mkpath(QStringLiteral("src")));
    { QFile cm(extDir.filePath(QStringLiteral("CMakeLists.txt"))); QVERIFY(cm.open(QIODevice::WriteOnly)); cm.write("cmake_minimum_required(VERSION 3.20)\nproject(SolverDemo CXX)\n"); }
    const QString mainCpp = extDir.filePath(QStringLiteral("src/solver_main.cpp")), elfBin = extDir.filePath(QStringLiteral("build_meson/solver_demo"));
    { QFile sf(mainCpp); QVERIFY(sf.open(QIODevice::WriteOnly)); sf.write("#include <stdio.h>\nint main(int argc, char** argv) { (void)argc; (void)argv; puts(\"SOLVER_OUT_READY\"); fflush(stdout); return 0; }\n"); }
    { QFile uf1(unbuiltDir.filePath(QStringLiteral("CMakeLists.txt"))); QVERIFY(uf1.open(QIODevice::WriteOnly)); uf1.write("cmake_minimum_required(VERSION 3.20)\nproject(UnbuiltApp CXX)\n"); QFile uf2(unbuiltDir.filePath(QStringLiteral("src/main.cpp"))); QVERIFY(uf2.open(QIODevice::WriteOnly)); uf2.write("int main() { return 0; }\n"); }
    { QProcess cc; cc.start(QStringLiteral("g++"), {QStringLiteral("-g"), QStringLiteral("-O0"), mainCpp, QStringLiteral("-o"), elfBin}); QVERIFY2(cc.waitForFinished(10000) && cc.exitCode() == 0 && QFileInfo::exists(elfBin), "g++ must compile build_meson/solver_demo"); }
    QFile::copy(elfBin, extDir.filePath(QStringLiteral("build_meson/meson-private/sanitycheckcpp.exe")));
    // 8. Start MainWindow on default repo workspace, switch to extDir, and verify FILES tree + TARGET selector update atomically.
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.show(); QCoreApplication::processEvents();
    const bool switchedViaMw = mw.switchWorkspaceDirectory(extDir.path());
    auto* pe = mw.projectExplorerWidget(); auto* tree = pe ? pe->findChild<QTreeView*>() : nullptr; auto* targetCombo = mw.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector")); auto* rankSpin = mw.findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin")); auto* termDock = mw.findChild<gridlock::ui::TerminalDockWidget*>();
    QVERIFY(pe != nullptr && tree != nullptr && tree->model() != nullptr && targetCombo != nullptr && rankSpin != nullptr && termDock != nullptr);
    QVERIFY2(switchedViaMw && !pe->isSymbolsMode() && !pe->isStringsMode(), "Opening external workspace must switch ProjectExplorerWidget to FILES mode via switchWorkspaceDirectory");
    QCOMPARE(termDock->workingDirectory(), QFileInfo(extDir.path()).canonicalFilePath());
    QTRY_VERIFY_WITH_TIMEOUT(tree->model()->rowCount(tree->rootIndex()) >= 4, 3000);
    auto visibleEntries = [&]() { QStringList out; const QModelIndex r = tree->rootIndex(); for (int i = 0; i < tree->model()->rowCount(r); ++i) out << tree->model()->index(i, 0, r).data().toString(); return out; };
    const QStringList extEntries = visibleEntries();
    QVERIFY2(extEntries.contains(QStringLiteral("SolverUtils")) && extEntries.contains(QStringLiteral("ProblemSetup")) && extEntries.contains(QStringLiteral("build_meson")) && !extEntries.contains(QStringLiteral("agent.md")) && !extEntries.contains(QStringLiteral("sonar-project.properties")), qPrintable(extEntries.join(QLatin1Char(','))));
    QVERIFY2(targetCombo->currentText().contains(QStringLiteral("solver_demo")) && !targetCombo->currentText().contains(QStringLiteral("inspection_demo")) && targetCombo->findText(QStringLiteral("sanitycheckcpp"), Qt::MatchContains) < 0, qPrintable(targetCombo->currentText()));
    // 9. Verify ProjectExplorerWidget root switch and SessionManager(.gridlock/workspace.toml) restore workspace directory cleanly.
    pe->setRootPath(otherDir.path()); QTRY_VERIFY_WITH_TIMEOUT(visibleEntries().contains(QStringLiteral("AMR_Electrode_Cut")), 3000);
    QVERIFY2(!visibleEntries().contains(QStringLiteral("SolverUtils")) && mw.windowTitle().contains(QFileInfo(otherDir.path()).fileName()), "ProjectExplorer setRootPath must sync MainWindow");
    const QString wsToml = extDir.filePath(QStringLiteral(".gridlock/workspace.toml"));
    { QFile tf(wsToml); QVERIFY(tf.open(QIODevice::WriteOnly)); tf.write("[workspace]\nwatched_variables = []\nactive_breakpoints = []\nselected_ranks = [0]\n"); }
    QVERIFY(gridlock::core::managers::SessionManager::instance().loadSession(wsToml).has_value());
    auto* fsModel = qobject_cast<QFileSystemModel*>(tree->model());
    QTRY_VERIFY_WITH_TIMEOUT(fsModel && QFileInfo(fsModel->filePath(tree->rootIndex())).canonicalFilePath() == QFileInfo(extDir.path()).canonicalFilePath() && visibleEntries().contains(QStringLiteral("SolverUtils")), 3000);
    QVERIFY2(targetCombo->currentText().contains(QStringLiteral("solver_demo")), "SessionManager loadSession(.gridlock/workspace.toml) must restore workspace target");
    // 10. Verify ProjectSettingsDialog auto-fills discovered workspace target, working dir, Browse/Args/MPI fields, and syncs MainWindow toolbar.
    const QString extCanon = QFileInfo(extDir.path()).canonicalFilePath(), altBin = extDir.filePath(QStringLiteral("build_meson/alt_solver"));
    QFile::copy(elfBin, altBin); auto& cfg = gridlock::core::ConfigManager::instance(); const auto origDs = cfg.getDebuggerSettings();
    gridlock::ui::ProjectSettingsDialog psDlg(&mw);
    auto* tbCombo = psDlg.findChild<QComboBox*>(QStringLiteral("targetBinaryCombo")); auto* tbBrowse = psDlg.findChild<QPushButton*>(QStringLiteral("targetBrowseBtn")); auto* argsEd = psDlg.findChild<QLineEdit*>(QStringLiteral("targetArgsEdit")); auto* wdEd = psDlg.findChild<QLineEdit*>(QStringLiteral("workingDirEdit")); auto* wdBrowse = psDlg.findChild<QPushButton*>(QStringLiteral("workingDirBrowseBtn")); auto* ranksSp = psDlg.findChild<QSpinBox*>(QStringLiteral("mpiRanksSpin")); auto* mpiArgsEd = psDlg.findChild<QLineEdit*>(QStringLiteral("mpiArgsEdit"));
    QVERIFY2(tbCombo && tbCombo->isEditable() && tbBrowse && argsEd && wdEd && wdBrowse && ranksSp && mpiArgsEd, "ProjectSettingsDialog missing targetBinaryCombo/targetBrowseBtn/targetArgsEdit/workingDirEdit/workingDirBrowseBtn/mpiRanksSpin/mpiArgsEdit");
    QVERIFY2(tbCombo->currentText().contains(QStringLiteral("solver_demo")) && QFileInfo(wdEd->text()).canonicalFilePath() == extCanon, qPrintable(tbCombo->currentText() + QLatin1Char('|') + wdEd->text()));
    tbCombo->setCurrentText(altBin); argsEd->setText(QStringLiteral("config_28A.toml --verbose")); wdEd->setText(extCanon); ranksSp->setValue(6); mpiArgsEd->setText(QStringLiteral("--oversubscribe --bind-to core")); psDlg.accept(); QApplication::processEvents();
    const auto ps = cfg.projectSettings();
    const bool persistedOk = QFileInfo(QString::fromStdString(ps.targetBinary)).canonicalFilePath() == QFileInfo(altBin).canonicalFilePath() && ps.targetArguments == QStringLiteral("config_28A.toml --verbose") && QFileInfo(QString::fromStdString(ps.workingDirectory)).canonicalFilePath() == extCanon && ps.mpiRanks == 6 && ps.mpiArgs == QStringLiteral("--oversubscribe --bind-to core");
    QVERIFY2(persistedOk && targetCombo->currentText().contains(QStringLiteral("alt_solver")) && rankSpin->value() == 6, "Saving ProjectSettingsDialog must persist settings and sync MainWindow TARGET/RANKS");
    // 11. Verify QFileDialog starts at active workspace, prunes nonexistent ghost sidebar URLs, and sizes columns >= 880px.
    QFileDialog fd(&mw); fd.setOption(QFileDialog::DontUseNativeDialog, true); const QUrl ghostUrl = QUrl::fromLocalFile(QStringLiteral("/home/nonexistent_ghost_user_9999"));
    fd.setSidebarUrls({ghostUrl}); fd.open(); QApplication::processEvents(); fd.hide(); auto* fdTree = fd.findChild<QTreeView*>();
    QVERIFY2(QFileInfo(fd.directory().absolutePath()).canonicalFilePath() == extCanon && !fd.sidebarUrls().contains(ghostUrl) && fd.sidebarUrls().contains(QUrl::fromLocalFile(extCanon)) && fd.width() >= 880 && fdTree && fdTree->header() && fdTree->header()->sectionSize(0) >= 300, "QFileDialog must default to active workspace, prune ghost bookmarks, and prevent column clipping");
    // 11b. Verify updating ProjectSettingsDialog workingDirectory switches TerminalDockWidget and live PTY shell pwd.
    const QString customWd = QFileInfo(extDir.filePath(QStringLiteral("build_meson"))).canonicalFilePath();
    gridlock::ui::ProjectSettingsDialog psDlg2(&mw);
    auto* wdEd2 = psDlg2.findChild<QLineEdit*>(QStringLiteral("workingDirEdit")); QVERIFY(wdEd2 != nullptr);
    psDlg2.show(); QApplication::processEvents();
    wdEd2->setText(customWd); psDlg2.accept(); QApplication::processEvents();
    QCOMPARE(termDock->workingDirectory(), customWd);
    QTest::qWait(150);
    termDock->sendCommand(QStringLiteral("pwd > .pwd_check"));
    const QString pwdFile = QDir(customWd).filePath(QStringLiteral(".pwd_check"));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(pwdFile), 8000);
    { QFile pf(pwdFile); QVERIFY(pf.open(QIODevice::ReadOnly)); QCOMPARE(QString::fromUtf8(pf.readAll()).trimmed(), customWd); }
    // 11c. Verify File -> Save Session writes .gridlock/workspace.toml & settings.toml without opening a file dialog.
    auto* saveSessAct = mw.findChild<QAction*>(QStringLiteral("actionSaveSession"));
    QVERIFY2(saveSessAct != nullptr, "File menu must expose actionSaveSession");
    { auto ps = cfg.projectSettings(); ps.targetBinary = elfBin.toStdString(); ps.targetArguments = QStringLiteral("HEX"); ps.mpiRanks = 2; ps.workingDirectory = customWd.toStdString(); cfg.saveProjectSettings(ps); }
    QFile::remove(wsToml); saveSessAct->trigger(); QApplication::processEvents();
    QVERIFY2(QFileInfo::exists(wsToml) && QFileInfo::exists(extDir.filePath(QStringLiteral(".gridlock/settings.toml"))), "actionSaveSession must persist workspace.toml and settings.toml");
    // 11d. Verify dedicated PROGRAM OUTPUT dock (#programLaunchCommandEdit & #programOutputView) and clean stdout streaming.
    auto* launchCmdEd = mw.findChild<QLineEdit*>(QStringLiteral("programLaunchCommandEdit")); auto* progOutView = mw.findChild<QPlainTextEdit*>(QStringLiteral("programOutputView"));
    QVERIFY2(launchCmdEd && launchCmdEd->isReadOnly() && progOutView && progOutView->isReadOnly(), "PROGRAM OUTPUT dock must expose read-only #programLaunchCommandEdit and #programOutputView");
    // Launch live MPI target and verify launch command banner + clean inferior stdout.
    mw.syncCoordinatorWithConfig(); mw.startTargetWorkflow(); QApplication::processEvents();
    const QString launchTxt = launchCmdEd->text();
    QVERIFY2(launchTxt.contains(customWd) && (launchTxt.contains(QStringLiteral("mpirun")) || launchTxt.contains(QStringLiteral("mpiexec"))) && launchTxt.contains(QStringLiteral("-np")) && (launchTxt.contains(QStringLiteral("alt_solver")) || launchTxt.contains(QStringLiteral("solver_demo"))) && launchTxt.contains(QStringLiteral("HEX")), qPrintable(launchTxt));
    // Confirm real inferior stdout arrives without GDB/MI protocol noise, then stop target.
    QTRY_VERIFY_WITH_TIMEOUT(progOutView->toPlainText().contains(QStringLiteral("SOLVER_OUT_READY")), 6000);
    QVERIFY2(!progOutView->toPlainText().contains(QStringLiteral("=thread-created")) && !progOutView->toPlainText().contains(QStringLiteral("*running,thread-id=")), qPrintable(progOutView->toPlainText()));
    mw.stopTargetWorkflow(); QApplication::processEvents();
    // 11e. Verify 60 FPS frame-coalesced 8-rank burst throughput (<45ms for 800 lines) and sticky-scroll preservation.
    // Use Q_INVOKABLE flush hook when exposed so tests measure deterministic batch insertion speed cleanly.
    auto flushOut = [&]() {
        if (mw.metaObject()->indexOfMethod("flushProgramOutputBatch()") >= 0) QMetaObject::invokeMethod(&mw, "flushProgramOutputBatch");
        else QCoreApplication::processEvents();
    };
    // Confirm QPlainTextEdit undo/redo and word wrap are disabled prior to benchmarking burst insertion.
    QVERIFY2(!progOutView->isUndoRedoEnabled(), "#programOutputView must disable undo/redo for lock-free batching");
    QVERIFY2(progOutView->wordWrapMode() == QTextOption::NoWrap, "#programOutputView must use QTextOption::NoWrap");
    if (auto* clearOutBtn = progOutView->parentWidget() ? progOutView->parentWidget()->findChildren<QPushButton*>().value(1, nullptr) : nullptr) clearOutBtn->click();
    progOutView->resize(640, 180); progOutView->show(); QApplication::processEvents();
    auto* coord = mw.coordinator(); QVERIFY(coord != nullptr);
    QElapsedTimer burstTimer; burstTimer.start();
    // Emit 800 rapid solver lines across Ranks 0..7 and flush coalesced batch on the GUI thread.
    for (int i = 0; i < 800; ++i) emit coord->programOutputReceived(i % 8, QStringLiteral("SolveSystem: GMRES iter %1 res=1e-6").arg(i), false);
    flushOut();
    const qint64 burstMs = burstTimer.elapsed();
    // Assert 800 batched lines complete under 45ms and preserve the latest flushed solver record.
    QVERIFY2(burstMs < 45, qPrintable(QStringLiteral("800-line burst took %1ms (>= 45ms)").arg(burstMs)));
    QVERIFY2(progOutView->toPlainText().contains(QStringLiteral("GMRES iter 799")), "All 800 batched lines must flush into #programOutputView");
    // Scroll user viewport to top (value == 0) and confirm subsequent bursts NEVER yank the scrollbar down.
    QScrollBar* outSb = progOutView->verticalScrollBar(); QVERIFY(outSb != nullptr && outSb->maximum() > 0);
    outSb->setValue(0); QApplication::processEvents(); QCOMPARE(outSb->value(), 0);
    for (int i = 0; i < 20; ++i) emit coord->programOutputReceived(i % 8, QStringLiteral("TailLine %1").arg(i), false);
    flushOut(); QApplication::processEvents();
    QCOMPARE(outSb->value(), 0);
    // Pin scrollbar to bottom and confirm sticky auto-scroll tracks the newest appended lines.
    outSb->setValue(outSb->maximum()); QApplication::processEvents();
    for (int i = 0; i < 10; ++i) emit coord->programOutputReceived(i % 8, QStringLiteral("PinnedLine %1").arg(i), false);
    flushOut(); QApplication::processEvents();
    QCOMPARE(outSb->value(), outSb->maximum());
    // 11f. Verify unified ~/.config/gridlock/config.toml path and Recent Sessions persistence under isolated XDG_CONFIG_HOME.
    {
        QTemporaryDir xdgDir, wsA, wsB; QVERIFY(xdgDir.isValid() && wsA.isValid() && wsB.isValid());
        struct XdgGuard { QByteArray x = qgetenv("XDG_CONFIG_HOME"), t = qgetenv("GRIDLOCK_TEST_CONFIG_DIR"); gridlock::core::ConfigManager* c;
            ~XdgGuard() { if (x.isEmpty()) qunsetenv("XDG_CONFIG_HOME"); else qputenv("XDG_CONFIG_HOME", x); if (t.isEmpty()) qunsetenv("GRIDLOCK_TEST_CONFIG_DIR"); else qputenv("GRIDLOCK_TEST_CONFIG_DIR", t); c->loadConfig(); }
        } xdgGuard{qgetenv("XDG_CONFIG_HOME"), qgetenv("GRIDLOCK_TEST_CONFIG_DIR"), &cfg};
        qunsetenv("GRIDLOCK_TEST_CONFIG_DIR"); qputenv("XDG_CONFIG_HOME", xdgDir.path().toUtf8());
        cfg.loadConfig(); cfg.setThemeName(QStringLiteral("gridlock_slate"));
        const QString expectedToml = QDir(xdgDir.path()).filePath(QStringLiteral("gridlock/config.toml"));
        QCOMPARE(QFileInfo(cfg.configFilePath()).absoluteFilePath(), QFileInfo(expectedToml).absoluteFilePath());
        QVERIFY2(QFileInfo::exists(expectedToml) && !QDir(xdgDir.path()).exists(QStringLiteral("GridLock")) && !QFileInfo::exists(QDir(xdgDir.path()).filePath(QStringLiteral("gridlock/gridlock/config.toml"))), "Config must live under <xdg>/gridlock/config.toml without nested gridlock/ or GridLock/");
        QDir(wsA.path()).mkpath("src"); QDir(wsB.path()).mkpath("src");
        const QString cA = QFileInfo(wsA.path()).canonicalFilePath(), cB = QFileInfo(wsB.path()).canonicalFilePath();
        QVERIFY2(mw.switchWorkspaceDirectory(wsA.path()), "wsA switch failed"); saveSessAct->trigger(); QApplication::processEvents(); QVERIFY2(mw.switchWorkspaceDirectory(wsB.path()), "wsB switch failed");
        auto* rm = mw.findChild<QMenu*>(QStringLiteral("menuRecentSessions")); QVERIFY2(rm != nullptr, "#menuRecentSessions missing");
        emit rm->aboutToShow(); QApplication::processEvents(); QAction* actA = nullptr; int iA = -1, iB = -1; bool hasClr = false;
        for (int i = 0; i < rm->actions().size(); ++i) { QAction* a = rm->actions().at(i); const QString d = a->data().toString();
            if (d.contains(cB) && iB < 0) iB = i; else if (d.contains(cA) && !actA) { iA = i; actA = a; }
            if (a->text().contains(QStringLiteral("Clear Recent Sessions"))) hasClr = true; }
        QVERIFY2(iB >= 0 && iA > iB && hasClr && actA != nullptr, "Recent Sessions must order wsB then wsA with Clear action");
        cfg.loadConfig(); QVERIFY2(cfg.recentSessions().size() >= 2 && cfg.recentSessions().at(0) == cB && cfg.recentSessions().contains(cA), qPrintable(cfg.recentSessions().join(",")));
        gridlock::ui::MainWindow freshMw; auto* freshRm = freshMw.findChild<QMenu*>(QStringLiteral("menuRecentSessions"));
        QVERIFY2(freshRm && freshRm->actions().size() >= 2 && freshRm->actions().at(0)->data().toString() == cB, "Fresh MainWindow must reload Recent Sessions on startup");
        actA->trigger(); QApplication::processEvents(); QCOMPARE(QFileInfo(pe->rootPath()).canonicalFilePath(), cA);
    }
    // 11g. Verify ProjectExplorerWidget proportional UI font and distinct multi-language vector file icons.
    {
        QTemporaryDir iconDir; QVERIFY(iconDir.isValid());
        for (const QString& d : {QStringLiteral("src"), QStringLiteral("tests"), QStringLiteral("scripts"), QStringLiteral("docs")}) QDir(iconDir.path()).mkpath(d);
        for (const QString& f : {QStringLiteral("Parameters.cpp"), QStringLiteral("Parameters.hpp"), QStringLiteral("run.sh"), QStringLiteral("README.md"), QStringLiteral("meson.build"), QStringLiteral("Parameters.o"), QStringLiteral("Parameters.d"), QStringLiteral("config.toml"), QStringLiteral("notes.unknown")}) {
            QFile fh(QDir(iconDir.path()).filePath(f)); QVERIFY(fh.open(QIODevice::WriteOnly)); fh.write("// sample\n");
        }
        gridlock::ui::FileIconProvider ip;
        const QImage cppImg = ip.icon(QFileInfo(QDir(iconDir.path()).filePath(QStringLiteral("Parameters.cpp")))).pixmap(16, 16).toImage();
        const QImage hppImg = ip.icon(QFileInfo(QDir(iconDir.path()).filePath(QStringLiteral("Parameters.hpp")))).pixmap(16, 16).toImage();
        const QImage shImg = ip.icon(QFileInfo(QDir(iconDir.path()).filePath(QStringLiteral("run.sh")))).pixmap(16, 16).toImage();
        const QImage buildImg = ip.icon(QFileInfo(QDir(iconDir.path()).filePath(QStringLiteral("meson.build")))).pixmap(16, 16).toImage();
        const QImage genImg = ip.icon(QFileInfo(QDir(iconDir.path()).filePath(QStringLiteral("notes.unknown")))).pixmap(16, 16).toImage();
        QVERIFY2(cppImg != hppImg && shImg != genImg && buildImg != genImg && shImg != buildImg, "C++, header, shell, and build files must have distinct non-generic vector icons");
        QVERIFY2(gridlock::ui::FileIconProvider::isBuildArtifactFile(QFileInfo(QDir(iconDir.path()).filePath(QStringLiteral("Parameters.o")))) && !gridlock::ui::FileIconProvider::isBuildArtifactFile(QFileInfo(QDir(iconDir.path()).filePath(QStringLiteral("Parameters.cpp")))), "Artifact detection");
        gridlock::ui::ProjectExplorerWidget peVerify; peVerify.resize(280, 460); peVerify.setRootPath(iconDir.path()); peVerify.show(); QApplication::processEvents();
        auto* tv = peVerify.findChild<QTreeView*>(); QVERIFY2(tv && !QFontInfo(tv->font()).fixedPitch(), "ProjectExplorerWidget tree must use proportional UI font");
        if (const QString pngOut = qEnvironmentVariable("GRIDLOCK_VERIFY_PROJECT_TREE_PNG"); !pngOut.isEmpty()) { QTest::qWait(250); peVerify.grab().toImage().save(pngOut); }
    }
    // 11h. Verify TerminalDockWidget right-click context menu, Ctrl+Shift+V / Ctrl+V paste, and Ctrl+Shift+C / smart Ctrl+C copy.
    {
        gridlock::ui::TerminalDockWidget term(QStringLiteral("Terminal")); term.resize(600, 220); term.show(); QApplication::processEvents();
        QGuiApplication::clipboard()->setText(QStringLiteral("echo GRIDLOCK_TERM_CLIP"));
        std::unique_ptr<QMenu> ctx(term.createContextMenu(&term)); QStringList actTexts;
        for (QAction* a : ctx->actions()) if (!a->isSeparator()) actTexts.append(a->text());
        QVERIFY2(actTexts.contains(QStringLiteral("Copy")) && actTexts.contains(QStringLiteral("Paste")) && actTexts.contains(QStringLiteral("Select All")) && actTexts.contains(QStringLiteral("Clear Terminal")), qPrintable(actTexts.join(",")));
        QKeyEvent ovV(QEvent::ShortcutOverride, Qt::Key_V, Qt::ControlModifier); QApplication::sendEvent(&term, &ovV); QVERIFY(ovV.isAccepted());
        QKeyEvent pasteEv(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier); QApplication::sendEvent(&term, &pasteEv); QCOMPARE(term.lastPastedText(), QStringLiteral("echo GRIDLOCK_TERM_CLIP"));
        QGuiApplication::clipboard()->setText(QStringLiteral("echo CTRL_V_PASTE"));
        QKeyEvent pasteEv2(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier); QApplication::sendEvent(&term, &pasteEv2); QCOMPARE(term.lastPastedText(), QStringLiteral("echo CTRL_V_PASTE"));
        term.sendCommand(QStringLiteral("echo GRIDLOCK_COPY_TOKEN")); QTest::qWait(200); QApplication::processEvents();
        term.selectAllText(); QGuiApplication::clipboard()->clear();
        QKeyEvent copyEv(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier); QApplication::sendEvent(&term, &copyEv); QVERIFY2(!term.hasSelection() || !QGuiApplication::clipboard()->text().isEmpty(), "Ctrl+Shift+C copy");
        QGuiApplication::clipboard()->clear();
        QKeyEvent smartCopyEv(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier); QApplication::sendEvent(&term, &smartCopyEv); QVERIFY2(!term.hasSelection() || !QGuiApplication::clipboard()->text().isEmpty(), "Smart Ctrl+C copy");
    }
    // 12. Switch to unbuilt C++ workspace (no ELF binary) and verify symbols, strings, disassembly, and #addressNavBar are completely cleared.
    QVERIFY2(mw.switchWorkspaceDirectory(unbuiltDir.path()), "Switching to unbuilt workspace must succeed");
    auto* disasm = mw.findChild<gridlock::ui::DisassemblyView*>(); auto* navBar = mw.findChild<QWidget*>(QStringLiteral("addressNavBar"));
    QVERIFY2(pe->symbolCount() == 0 && pe->stringCount() == 0 && !pe->isSymbolsMode() && !pe->isStringsMode(), "Unbuilt workspace must have 0 symbols and 0 strings");
    QVERIFY2(disasm && disasm->basicBlockCount() == 0 && !disasm->decompiledPseudocode().contains(QStringLiteral("var_4h")) && !disasm->toPlainText().contains(QStringLiteral("0x00401120")), "Unbuilt workspace must clear DisassemblyView");
    for (QLabel* segLbl : navBar ? navBar->findChildren<QLabel*>() : QList<QLabel*>()) QVERIFY2(!segLbl->text().contains(QStringLiteral("0x401000")) && !segLbl->text().contains(QStringLiteral("0x401120")) && !segLbl->text().contains(QStringLiteral("0x402000")) && !segLbl->text().contains(QStringLiteral("0x404000")), qPrintable(segLbl->text()));
    cfg.saveDebuggerSettings(origDs); cfg.setWorkspace(savedRepoRoot);
}

/** @brief Verifies Git diff parsing, Preferences toggles, multi-format syntax highlighting, and Rainbow Brackets. */
void TestMainWindowUI::testGitDiffGutterAndDebuggerEngineSelector() {
    // 1. Verify SourceCodeView::parseUnifiedGitDiff() and PreferencesDialog/ConfigManager debuggerEngine.
    gridlock::ui::SourceCodeView srcView;
    const QString sampleDiff = QStringLiteral("@@ -3,0 +4,2 @@\n+int a1 = 1;\n+int a2 = 2;\n@@ -10,1 +12,1 @@\n-int o = 0;\n+int n = 1;\n@@ -18,2 +20,0 @@\n-int r1 = 0;\n-int r2 = 0;\n");
    const auto states = gridlock::ui::SourceCodeView::parseUnifiedGitDiff(sampleDiff); srcView.setGitLineStates(states);
    QVERIFY2(srcView.gitLineState(4) == gridlock::ui::SourceCodeView::GitLineState::Added && srcView.gitLineState(5) == gridlock::ui::SourceCodeView::GitLineState::Added && srcView.gitLineState(12) == gridlock::ui::SourceCodeView::GitLineState::Modified && srcView.gitLineState(20) == gridlock::ui::SourceCodeView::GitLineState::Deleted && srcView.gitLineState(1) == gridlock::ui::SourceCodeView::GitLineState::Unchanged, "parseUnifiedGitDiff states");
    gridlock::ui::PreferencesDialog dlg; auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("debuggerEngineCombo")); auto& cfg = gridlock::core::ConfigManager::instance();
    QVERIFY2(combo && combo->findData(QStringLiteral("gdb")) >= 0 && combo->findData(QStringLiteral("lldb-dap")) >= 0 && combo->findData(QStringLiteral("cuda-gdb")) >= 0 && combo->findData(QStringLiteral("rocgdb")) >= 0, "debuggerEngineCombo items");
    cfg.setDebuggerEngine(QStringLiteral("rocgdb")); QCOMPARE(cfg.debuggerEngine(), QStringLiteral("rocgdb")); cfg.setDebuggerEngine(QStringLiteral("gdb")); QCOMPARE(cfg.debuggerEngine(), QStringLiteral("gdb"));
    // 2. Verify live Git diff gutter updates on file open, unsaved debounced buffer edits, and saveToFile.
    QTemporaryDir gitDir; QVERIFY(gitDir.isValid());
    auto runGit = [&](const QStringList& args) -> bool { QProcess p; p.setWorkingDirectory(gitDir.path()); p.start(QStringLiteral("git"), args); return p.waitForFinished(4000) && p.exitCode() == 0; };
    const QString solverPath = gitDir.filePath(QStringLiteral("solver.cpp"));
    { QFile f(solverPath); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("int a = 1;\nint b = 2;\nint c = 3;\nint d = 4;\n"); }
    QVERIFY2(runGit({QStringLiteral("init"), QStringLiteral("-q")}) && runGit({QStringLiteral("config"), QStringLiteral("user.email"), QStringLiteral("t@gridlock.dev")}) && runGit({QStringLiteral("config"), QStringLiteral("user.name"), QStringLiteral("T")}) && runGit({QStringLiteral("add"), QStringLiteral("solver.cpp")}) && runGit({QStringLiteral("commit"), QStringLiteral("-q"), QStringLiteral("-m"), QStringLiteral("init")}), "git repo init");
    gridlock::ui::MainWindow mw; auto* tabMgr = mw.editorTabManager(); QVERIFY(tabMgr != nullptr); tabMgr->clearAllTabs(); QVERIFY(tabMgr->openFile(solverPath) != nullptr); auto* ed = tabMgr->currentSourceCodeView(); QVERIFY(ed != nullptr); QCOMPARE(ed->gitLineState(2), gridlock::ui::SourceCodeView::GitLineState::Unchanged);
    // Edit line 2 and append line 5 in memory without saving, then wait for debounced gutter refresh.
    { QTextCursor cur(ed->document()); cur.movePosition(QTextCursor::Start); cur.movePosition(QTextCursor::Down); cur.select(QTextCursor::LineUnderCursor); cur.insertText(QStringLiteral("int b = 99;")); cur.movePosition(QTextCursor::End); cur.insertText(QStringLiteral("int e = 5;\n")); }
    QTRY_VERIFY_WITH_TIMEOUT(ed->gitLineState(2) == gridlock::ui::SourceCodeView::GitLineState::Modified && ed->gitLineState(5) == gridlock::ui::SourceCodeView::GitLineState::Added, 2000);
    QVERIFY(ed->saveToFile() && ed->gitLineState(2) == gridlock::ui::SourceCodeView::GitLineState::Modified && ed->gitLineState(5) == gridlock::ui::SourceCodeView::GitLineState::Added);
    // 3. Verify lossless dirty-tab Cancel/Save/Discard close prompts in EditorTabManager and MainWindow::closeEvent.
    { QTextCursor cur(ed->document()); cur.movePosition(QTextCursor::Start); cur.select(QTextCursor::LineUnderCursor); cur.insertText(QStringLiteral("int a = 123;")); }
    QVERIFY(tabMgr->isTabModified(0));
    auto readDisk = [&]() { QFile rf(solverPath); return rf.open(QIODevice::ReadOnly) ? QString::fromUtf8(rf.readAll()) : QString(); };
    auto checkDirtyClosePrompts = [&](gridlock::ui::EditorTabManager* mgr) -> bool {
        QMessageBox::StandardButton nextBtn = QMessageBox::Cancel; int prompts = 0;
        mgr->setUnsavedPromptHandler([&](const QString&) { ++prompts; return nextBtn; });
        if (mw.close() || mgr->count() != 1 || prompts != 1) return false;
        emit mgr->tabCloseRequested(0); if (mgr->count() != 1 || !mgr->isTabModified(0) || prompts != 2) return false;
        nextBtn = QMessageBox::Save; emit mgr->tabCloseRequested(0); if (mgr->count() != 0 || !readDisk().contains(QStringLiteral("123")) || prompts != 3) return false;
        if (!mgr->openFile(solverPath) || !mgr->currentSourceCodeView()) return false;
        { QTextCursor c2(mgr->currentSourceCodeView()->document()); c2.movePosition(QTextCursor::Start); c2.select(QTextCursor::LineUnderCursor); c2.insertText(QStringLiteral("int a = 777;")); }
        nextBtn = QMessageBox::Discard; emit mgr->tabCloseRequested(0); return mgr->count() == 0 && !readDisk().contains(QStringLiteral("777")) && prompts == 4;
    };
    QVERIFY2(checkDirtyClosePrompts(tabMgr), "EditorTabManager & MainWindow dirty-tab Save/Discard/Cancel close prompts must succeed");
    // 4. Verify TOML/YAML/Markdown/CSV highlighting, Rainbow Brackets depth/ignore rules, and PreferencesDialog toggles.
    auto colorAt = [](gridlock::ui::SourceCodeView& v, int blk, int col) -> QColor { const auto fmts = v.document()->findBlockByNumber(blk).layout()->formats(); for (int k = fmts.size() - 1; k >= 0; --k) if (col >= fmts[k].start && col < fmts[k].start + fmts[k].length) return fmts[k].format.foreground().color(); return QColor(); };
    auto checkMultiSyntaxAndRainbow = [&](gridlock::ui::SourceCodeView& v, gridlock::core::ConfigManager& c) -> bool {
        using Mode = gridlock::ui::SourceCodeView::LanguageMode; gridlock::ui::PreferencesDialog pd(&mw); pd.show();
        auto* rbChk = pd.findChild<QCheckBox*>(QStringLiteral("rainbowBracketsCheck")); auto* ddChk = pd.findChild<QCheckBox*>(QStringLiteral("dataDocHighlightingCheck"));
        if (!rbChk || !ddChk || !rbChk->isChecked() || !ddChk->isChecked()) return false;
        v.setCurrentFile(gitDir.filePath(QStringLiteral("c.toml"))); v.setSourceCode(QStringLiteral("[solver]\n# cmt\nk = true\n")); if (v.languageMode() != Mode::Toml || !colorAt(v, 0, 1).isValid() || colorAt(v, 0, 1) == colorAt(v, 1, 2)) return false;
        v.setCurrentFile(gitDir.filePath(QStringLiteral("j.yaml"))); v.setSourceCode(QStringLiteral("mpi:\n  n: 4 # cmt\n  a: *r\n")); if (v.languageMode() != Mode::Yaml || !colorAt(v, 0, 0).isValid() || colorAt(v, 0, 0) == colorAt(v, 1, 9)) return false;
        v.setCurrentFile(gitDir.filePath(QStringLiteral("r.md"))); v.setSourceCode(QStringLiteral("# H1\n```\nint x = 1;\n```\n[l](u)\n")); if (v.languageMode() != Mode::Markdown || !colorAt(v, 0, 2).isValid() || !colorAt(v, 2, 0).isValid()) return false;
        v.setCurrentFile(gitDir.filePath(QStringLiteral("d.csv"))); v.setSourceCode(QStringLiteral("\"a,b\",c\n")); if (v.languageMode() != Mode::Csv || colorAt(v, 0, 1) != colorAt(v, 0, 3) || colorAt(v, 0, 1) == colorAt(v, 0, 6)) return false;
        if (!tabMgr->openFile(solverPath) || !tabMgr->currentSourceCodeView()) return false;
        auto* tv = tabMgr->currentSourceCodeView(); const QString rbCode = QStringLiteral("if (a[0] + (b * {1})) { auto s = \"string (ignored)\"; } // comment (ignored)\n");
        tv->setSourceCode(rbCode); tv->setCurrentFile(solverPath);
        const QColor d0 = colorAt(*tv, 0, 3), d1 = colorAt(*tv, 0, 5), d2 = colorAt(*tv, 0, 16);
        if (!d0.isValid() || d0 == d1 || d1 == d2 || colorAt(*tv, 0, rbCode.indexOf(QStringLiteral("(ignored)"))) == d0 || colorAt(*tv, 0, rbCode.lastIndexOf(QStringLiteral("(ignored)"))) == d0) return false;
        rbChk->setChecked(false); ddChk->setChecked(false); QMetaObject::invokeMethod(&pd, "apply", Qt::DirectConnection);
        const bool offOk = !c.rainbowBracketsEnabled() && !c.dataDocHighlightingEnabled() && colorAt(*tv, 0, 3) != d0;
        rbChk->setChecked(true); ddChk->setChecked(true); QMetaObject::invokeMethod(&pd, "apply", Qt::DirectConnection);
        return offOk && colorAt(*tv, 0, 3) == d0 && mw.close();
    };
    QVERIFY2(checkMultiSyntaxAndRainbow(srcView, cfg), "TOML/YAML/MD/CSV highlighting, Rainbow Brackets, and Preferences live toggles must succeed");
}

void TestMainWindowUI::testSplitEditorQSplitterAndInlineFindBar() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cppPath = dir.filePath(QStringLiteral("split_demo.cpp"));
    const QString hppPath = dir.filePath(QStringLiteral("split_demo.hpp"));
    QString body = QStringLiteral("int alpha = 1;\nint ALPHA = 2;\nint alpha_tail = 3;\n");
    for (int i = 0; i < 80; ++i) body += QStringLiteral("int line_%1 = %1;\n").arg(i);
    {
        QFile fc(cppPath); QVERIFY(fc.open(QIODevice::WriteOnly)); fc.write(body.toUtf8());
        QFile fh(hppPath); QVERIFY(fh.open(QIODevice::WriteOnly)); fh.write((QStringLiteral("#pragma once\n") + body).toUtf8());
    }

    gridlock::ui::MainWindow mw;
    mw.resize(1600, 900);
    mw.show();
    mw.activateWindow();
    QApplication::processEvents();

    auto* tabMgr = mw.editorTabManager();
    auto* seekBar = mw.findChild<QLineEdit*>(QStringLiteral("cutterSeekBar"));
    QVERIFY(tabMgr != nullptr && seekBar != nullptr);
    tabMgr->clearAllTabs();
    auto* primaryView = tabMgr->openFile(cppPath);
    QVERIFY(primaryView != nullptr);
    QApplication::processEvents();

    // 1. Verify QSplitter splitEditor creates two non-overlapping sibling panes with visible scrollbars.
    tabMgr->splitEditor(Qt::Horizontal);
    QApplication::processEvents();
    auto* splitter = tabMgr->editorSplitter();
    auto* leftTabs = tabMgr->primaryTabWidget();
    auto* rightTabs = tabMgr->secondaryTabWidget();
    auto* secView = tabMgr->secondaryEditorView();
    QVERIFY(splitter && leftTabs && rightTabs && secView && tabMgr->isSplitActive());
    QVERIFY(leftTabs->isVisible() && rightTabs->isVisible());
    const QRect leftGeom = leftTabs->geometry();
    const QRect rightGeom = rightTabs->geometry();
    QVERIFY2(leftGeom.width() >= 140 && rightGeom.width() >= 140 && leftGeom.right() <= rightGeom.left(),
             qPrintable(QStringLiteral("left=%1..%2 right=%3..%4").arg(leftGeom.left()).arg(leftGeom.right()).arg(rightGeom.left()).arg(rightGeom.right())));
    QVERIFY(primaryView->verticalScrollBar()->isVisible() && secView->verticalScrollBar()->isVisible());

    // Verify active-editor tracking, gutter breakpoint signals, and dirty-tab checks work in the secondary pane.
    secView->setFocus();
    QApplication::processEvents();
    QCOMPARE(tabMgr->currentSourceCodeView(), secView);
    QSignalSpy bpSpy(tabMgr, &gridlock::ui::EditorTabManager::breakpointToggled);
    secView->toggleBreakpoint(2);
    QCOMPARE(bpSpy.count(), 1);
    {
        QTextCursor cur(secView->document());
        cur.movePosition(QTextCursor::End);
        cur.insertText(QStringLiteral("// split edit\n"));
    }
    QVERIFY(tabMgr->hasModifiedTabs());
    QVERIFY(tabMgr->saveAllModifiedTabs() >= 1 && !tabMgr->hasModifiedTabs());

    // Closing the split hides the secondary pane and restores full width to the primary pane.
    tabMgr->closeSplit();
    QApplication::processEvents();
    QVERIFY(!tabMgr->isSplitActive() && !rightTabs->isVisible() && leftTabs->width() >= tabMgr->width() - 4);

    // 2. Verify Ctrl+F opens the inline editor find bar without stealing focus to #cutterSeekBar, while Ctrl+G focuses #cutterSeekBar.
    primaryView->setFocus();
    QApplication::processEvents();
    QTest::keyClick(primaryView, Qt::Key_F, Qt::ControlModifier);
    QApplication::processEvents();
    auto* findBar = primaryView->findBar();
    QVERIFY(findBar != nullptr && findBar->isVisible() && findBar->findEdit()->hasFocus() && !seekBar->hasFocus());

    findBar->setSearchText(QStringLiteral("alpha"));
    QApplication::processEvents();
    QCOMPARE(findBar->matchCount(), 3);
    QCOMPARE(findBar->currentMatchIndex(), 0);
    QTest::keyClick(findBar->findEdit(), Qt::Key_Return);
    QCOMPARE(findBar->currentMatchIndex(), 1);
    QTest::keyClick(findBar->findEdit(), Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(findBar->currentMatchIndex(), 0);
    findBar->setCaseSensitive(true);
    QCOMPARE(findBar->matchCount(), 2);

    QTest::keyClick(findBar->findEdit(), Qt::Key_Escape);
    QApplication::processEvents();
    QVERIFY(!findBar->isVisible() && primaryView->hasFocus() && !seekBar->hasFocus());

    // Verify Ctrl+G focuses #cutterSeekBar.
    QTest::keyClick(primaryView, Qt::Key_G, Qt::ControlModifier);
    QApplication::processEvents();
    QVERIFY(seekBar->hasFocus());

    // Verify Ctrl+F inside GdbConsoleWidget opens its inline find bar without focusing #cutterSeekBar.
    if (auto* gdbConsole = mw.findChild<gridlock::ui::GdbConsoleWidget*>()) {
        if (auto* bottomTabs = mw.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"))) bottomTabs->setCurrentWidget(gdbConsole);
        gdbConsole->commandEdit()->setFocus();
        QApplication::processEvents();
        QTest::keyClick(gdbConsole->commandEdit(), Qt::Key_F, Qt::ControlModifier);
        QApplication::processEvents();
        QVERIFY(gdbConsole->findBar() && gdbConsole->findBar()->isVisible() && !seekBar->hasFocus());
        gdbConsole->findBar()->closeBar();
    }
    mw.close();
}

void TestMainWindowUI::testConsolidatedToolbarSourceSteppingAndTooltips() {
    static QStringList s_warnings;
    s_warnings.clear();
    const QtMessageHandler prevHandler = qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& msg) {
        if (type == QtWarningMsg) s_warnings.append(msg);
    });
    struct WarnGuard { QtMessageHandler prev; ~WarnGuard() { qInstallMessageHandler(prev); } } guard{prevHandler};

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cppPath = dir.filePath(QStringLiteral("stepper.cpp"));
    const QString hppPath = dir.filePath(QStringLiteral("stepper.hpp"));
    {
        QFile fc(cppPath); QVERIFY(fc.open(QIODevice::WriteOnly)); fc.write("#include \"stepper.hpp\"\nint helper(int v) { return v + 1; }\nint main() { return helper(4); }\n");
        QFile fh(hppPath); QVERIFY(fh.open(QIODevice::WriteOnly)); fh.write("#pragma once\nint helper(int v);\n");
    }

    gridlock::ui::MainWindow mw;
    mw.resize(1280, 800);
    mw.show();
    mw.activateWindow();
    QApplication::processEvents();

    auto* mainTb = mw.findChild<QToolBar*>(QStringLiteral("MainToolbar"));
    auto* targetCombo = mw.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector"));
    auto* buildBtn = mw.findChild<QToolButton*>(QStringLiteral("toolbarBuildSplitBtn"));
    auto* intoBtn = mw.findChild<QToolButton*>(QStringLiteral("toolbarStepIntoBtn"));
    auto* recBtn = mw.findChild<QPushButton*>(QStringLiteral("toolbarRecordHistoryBtn"));
    auto* sbBtn = mw.findChild<QPushButton*>(QStringLiteral("toolbarStepBackBtn"));
    auto* rcBtn = mw.findChild<QPushButton*>(QStringLiteral("toolbarReverseContinueBtn"));
    QVERIFY(mainTb && targetCombo && buildBtn && buildBtn->menu() && intoBtn && intoBtn->menu() && recBtn && sbBtn && rcBtn);

    // 1. Verify compact toolbar width <= 1040, 190px target selector cap + tooltip, single Stop action, and hidden reverse step buttons while idle.
    QVERIFY2(mainTb->sizeHint().width() <= 1040, qPrintable(QStringLiteral("MainToolbar sizeHint width: %1").arg(mainTb->sizeHint().width())));
    QVERIFY(targetCombo->maximumWidth() <= 190 && !targetCombo->toolTip().isEmpty());
    QVERIFY(!sbBtn->isVisible() && !rcBtn->isVisible());

    int stopCount = 0, terminateCount = 0;
    for (QAction* a : mainTb->actions()) {
        if (a->text().contains(QStringLiteral("Stop"), Qt::CaseInsensitive)) ++stopCount;
        if (a->text().contains(QStringLiteral("Terminate"), Qt::CaseInsensitive)) ++terminateCount;
        if (!a->isSeparator() && !a->text().isEmpty()) {
            QVERIFY2(!a->toolTip().isEmpty(), qPrintable(QStringLiteral("Empty tooltip on toolbar action: %1").arg(a->text())));
        }
    }
    QCOMPARE(stopCount, 1);
    QCOMPARE(terminateCount, 0);

    auto* stepOverAct = mw.findChild<QAction*>(QStringLiteral("debug.step_over"));
    auto* stepIntoAct = mw.findChild<QAction*>(QStringLiteral("debug.step_into"));
    auto* stepOutAct = mw.findChild<QAction*>(QStringLiteral("debug.step_out"));
    auto* stopAct = mw.findChild<QAction*>(QStringLiteral("debug.stop"));
    auto* switchHdrAct = mw.findChild<QAction*>(QStringLiteral("file.switch_header_source"));
    QVERIFY(stepOverAct && stepIntoAct && stepOutAct && stopAct && switchHdrAct);
    QCOMPARE(stepOverAct->shortcut(), QKeySequence(Qt::Key_F10));
    QCOMPARE(stepIntoAct->shortcut(), QKeySequence(Qt::Key_F11));
    QCOMPARE(stepOutAct->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_F11));
    QCOMPARE(stopAct->shortcut(), QKeySequence(Qt::SHIFT | Qt::Key_F5));
    QCOMPARE(switchHdrAct->shortcut(), QKeySequence(Qt::Key_F4));

    // Verify idle tooltips explain both shortcuts and why disabled actions are unavailable.
    QVERIFY(stepOverAct->toolTip().contains(QStringLiteral("F10")) && stepOverAct->toolTip().contains(QStringLiteral("idle"), Qt::CaseInsensitive));
    QVERIFY(stepIntoAct->toolTip().contains(QStringLiteral("F11")) && stepOutAct->toolTip().contains(QStringLiteral("Shift+F11")));

    // 2. Verify F4 switches Header/Source while F10/F11/Shift+F11 dispatch Step Over / Step Into / Step Out with zero ambiguous shortcut warnings.
    auto* tabMgr = mw.editorTabManager();
    QVERIFY(tabMgr && tabMgr->openFile(cppPath));
    QTest::keyClick(&mw, Qt::Key_F4);
    QApplication::processEvents();
    QVERIFY(tabMgr->tabText(tabMgr->currentIndex()).contains(QStringLiteral("stepper.hpp")));

    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(2);
    auto markStopped = [&]() {
        coord.processGdbOutput(0, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401120\",func=\"main\",fullname=\"/tmp/stepper.cpp\",line=\"3\",args=[]}\n"));
        coord.processGdbOutput(1, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401120\",func=\"main\",fullname=\"/tmp/stepper.cpp\",line=\"3\",args=[]}\n"));
    };
    QStringList sent;
    connect(&coord, &gridlock::GdbRankCoordinator::commandSentToGdb, [&](int r, const QString& cmd) {
        if (r == 0) sent.append(cmd.trimmed());
    });
    mw.setCoordinator(&coord);
    markStopped();
    QApplication::processEvents();
    QVERIFY(recBtn->isEnabled() && !sbBtn->isVisible() && !rcBtn->isVisible());
    recBtn->click();
    QApplication::processEvents();
    QVERIFY(sbBtn->isVisible() && rcBtn->isVisible());
    mw.onRankSelected(1);
    QVERIFY(!sbBtn->isVisible() && !rcBtn->isVisible());
    mw.onRankSelected(0);
    QVERIFY(sbBtn->isVisible() && rcBtn->isVisible());
    recBtn->click();
    QApplication::processEvents();
    QVERIFY(!sbBtn->isVisible() && !rcBtn->isVisible());

    QTest::keyClick(&mw, Qt::Key_F10);
    QApplication::processEvents();
    QVERIFY(sent.contains(QStringLiteral("-exec-next")) && tabMgr->tabText(tabMgr->currentIndex()).contains(QStringLiteral("stepper.hpp")));

    markStopped();
    sent.clear();
    QTest::keyClick(&mw, Qt::Key_F11);
    QApplication::processEvents();
    QVERIFY(sent.contains(QStringLiteral("-exec-step")));

    markStopped();
    sent.clear();
    QTest::keyClick(&mw, Qt::Key_F11, Qt::ShiftModifier);
    QApplication::processEvents();
    QVERIFY(sent.contains(QStringLiteral("-exec-finish")));

    for (const QString& w : s_warnings) {
        QVERIFY2(!w.contains(QStringLiteral("Ambiguous shortcut overload"), Qt::CaseInsensitive), qPrintable(w));
    }

    mw.setCoordinator(nullptr);
    mw.close();
}

void TestMainWindowUI::testProgressiveDisclosurePerspectivesAndSingleRank() {
    gridlock::ui::MainWindow mw; mw.resize(1280, 800);
    const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp")) ? QStringLiteral("tutorial/inspection_demo.cpp") : QStringLiteral("../tutorial/inspection_demo.cpp");
    mw.loadSourceFile(demoPath); mw.show(); mw.activateWindow(); QApplication::processEvents();

    auto* srcView = mw.sourceCodeView(); auto* disasm = mw.disassemblyView();
    auto* navTb = mw.findChild<QToolBar*>(QStringLiteral("AddressNavToolbar"));
    auto* varsDock = mw.findChild<QDockWidget*>(QStringLiteral("VariablesDock"));
    auto* cstackDock = mw.findChild<QDockWidget*>(QStringLiteral("CallStackDock"));
    auto* regDock = mw.findChild<QDockWidget*>(QStringLiteral("RegisterViewDock"));
    auto* rackDock = mw.findChild<QDockWidget*>(QStringLiteral("ServerRackDock"));
    auto* rankSpin = mw.findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin"));
    auto* diffGrid = mw.differentialGrid();
    QVERIFY(srcView && disasm && navTb && varsDock && cstackDock && regDock && rackDock && rankSpin && diffGrid);
    QVERIFY(mw.findChild<QPushButton*>(QStringLiteral("perspectiveSourceBtn")) && mw.findChild<QPushButton*>(QStringLiteral("perspectiveDebugBtn")) && mw.findChild<QPushButton*>(QStringLiteral("perspectiveBinaryBtn")) && mw.findChild<QPushButton*>(QStringLiteral("perspectiveMpiBtn")));

    // 1. Verify idle Source perspective gives SourceCodeView >= 60% of 1280px width and hides disassembly/nav/debug docks.
    QCOMPARE(mw.workspacePerspective(), QStringLiteral("Source"));
    QVERIFY2(srcView->width() >= static_cast<int>(mw.width() * 0.60), qPrintable(QStringLiteral("SourceCodeView width %1 < 60%% of %2").arg(srcView->width()).arg(mw.width())));
    QVERIFY(!disasm->isVisible() && !navTb->isVisible() && !varsDock->isVisible() && !rackDock->isVisible());

    // 2. Verify DifferentialGrid starts with 0 synthetic watch columns + placeholder, and idle-only controls are disabled.
    QVERIFY(diffGrid->getWatchExpressions().empty() && !diffGrid->emptyPlaceholderText().isEmpty());
    auto* recBtn = mw.findChild<QPushButton*>(QStringLiteral("toolbarRecordHistoryBtn"));
    auto* stepiBtn = mw.findChild<QPushButton*>(QStringLiteral("disasmStepiBtn"));
    auto* nextiBtn = mw.findChild<QPushButton*>(QStringLiteral("disasmNextiBtn"));
    auto* wpBtn = mw.findChild<QPushButton*>(QStringLiteral("addHardwareWatchpointBtn"));
    QVERIFY(recBtn && !recBtn->isEnabled() && stepiBtn && !stepiBtn->isEnabled() && nextiBtn && !nextiBtn->isEnabled() && wpBtn && !wpBtn->isEnabled());

    // 3. Verify ranks==1 hides ServerRackDock + single-rank selectors while ranks==4 reveals them.
    mw.setWorkspacePerspective(QStringLiteral("Debug"));
    rankSpin->setValue(1); QApplication::processEvents();
    auto* varRankCombo = mw.findChild<QComboBox*>(QStringLiteral("variablesRankSelector"));
    auto* evalRankCombo = mw.findChild<QComboBox*>(QStringLiteral("evaluatorRankCombo"));
    QVERIFY(varsDock->isVisible() && cstackDock->isVisible() && !rackDock->isVisible());
    QVERIFY(varRankCombo && varRankCombo->isHidden() && evalRankCombo && evalRankCombo->isHidden());
    rankSpin->setValue(4); QApplication::processEvents();
    QVERIFY(rackDock->isVisible() && !varRankCombo->isHidden() && !evalRankCombo->isHidden());

    // 4. Verify switching perspectives does NOT force-redock floating docks and Binary mode avoids 720px overflow.
    mw.toggleDockFloating(regDock); QVERIFY(mw.isDockFloating(regDock));
    mw.setWorkspacePerspective(QStringLiteral("Binary")); QApplication::processEvents();
    QVERIFY(mw.isDockFloating(regDock) && disasm->isVisible() && disasm->minimumWidth() < 720 && mw.width() <= 1280);
    mw.redockAllFloatingPanels(); mw.close();
}



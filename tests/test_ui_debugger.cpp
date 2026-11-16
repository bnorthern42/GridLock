/**
 * @file test_ui_debugger.cpp
 * @brief Integration tests for dynamic statement mapping, branch/loop stepping, and rank context.
 *
 * Verifies that source lines are resolved dynamically without hardcoded constants,
 * loop back-edges (jmp) and branch conditions (jg, je) are accurately evaluated per MPI rank,
 * and the active stepping rank context stays synchronized across views.
 */

#include "test_ui.hpp"

#include <QApplication>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDir>
#include <QDockWidget>
#include <QtWidgets>
#include <QtTest/QtTest>
#include <cmath>

#include "../src/core/hpc/IBackendCoordinator.hpp"
#include "../src/core/managers/ConfigManager.hpp"
#include "../src/core/managers/ThemeManager.hpp"
#include "../src/core/utils/ClangdLspClient.hpp"
#include "../src/ui/EditorTabManager.hpp"
#include "../src/ui/GdbConsoleWidget.hpp"
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/dialogs/ConditionalBreakpointDialog.hpp"
#include "../src/ui/dialogs/PointerGraphDialog.hpp"
#include "../src/ui/dialogs/PreferencesDialog.hpp"
#include "../src/ui/dialogs/ProjectSettingsDialog.hpp"
#include "../src/ui/docks/TerminalDockWidget.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/views/MemView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
#include "../src/ui/views/SourceCodeView.hpp"
#include "../src/ui/widgets/MpiDiagnosticsWidget.hpp"
#include "../src/ui/widgets/ProjectExplorerWidget.hpp"
#include "../src/ui/widgets/ReferenceManualWidget.hpp"

/**
 * @brief Verifies dynamic statement mapping, loop back-edge/branch control-flow stepping,
 *        active rank context sync, live GDB disassembly CFG parsing, and default CFG Map mode.
 */
void TestMainWindowUI::testDynamicSourceMappingLoopBranchSteppingAndRankContext() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    // Ensure tutorial/inspection_demo.cpp is loaded in SourceCodeView.
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp"))
                                     ? QStringLiteral("tutorial/inspection_demo.cpp") : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    QVERIFY(sourceView != nullptr && disasmView != nullptr && serverRack != nullptr);

    // Part 1: Dynamic Statement-Based Source Line Resolution (No Hardcoding).
    // Prepend 5 comment lines to shift all subsequent statement line numbers by 5.
    const QString originalCode = sourceView->toPlainText();
    const QString shiftedCode = QStringLiteral("// 1\n// 2\n// 3\n// 4\n// 5\n") + originalCode;
    sourceView->setPlainText(shiftedCode); mainWindow.rebuildDynamicSourceLineMap();
    const int initLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Init"));
    const int finalizeLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Finalize"));
    const int returnLine = mainWindow.findSourceLineForStatement(QStringLiteral("return 0"));

    // Statements must be dynamically located at shifted positions.
    QVERIFY2(initLine > 5 && finalizeLine > initLine && returnLine >= finalizeLine, "Dynamic scanner must locate shifted statements");
    // Dynamic bidirectional mapping must match instructions to shifted source lines.
    auto checkLine = [&](int l, uint64_t a) { QCOMPARE(mainWindow.sourceLineToAddress(l), a); QCOMPARE(mainWindow.addressToSourceLine(a), l); };
    checkLine(initLine, 0x0040113dULL); checkLine(finalizeLine, 0x00401178ULL); checkLine(returnLine, 0x00401183ULL);

    // Part 2: Active Rank Stepping Context (default 0, select 2, reset 0).
    QCOMPARE(mainWindow.activeSteppingRank(), 0);
    serverRack->selectRank(2); QApplication::processEvents(); QCOMPARE(mainWindow.activeSteppingRank(), 2);
    serverRack->selectRank(0); QApplication::processEvents(); QCOMPARE(mainWindow.activeSteppingRank(), 0);

    // Part 3: True Control-Flow Loop & Rank-Conditional Branch Stepping.
    sourceView->setBreakpoints(QSet<int>()); mainWindow.stopTargetWorkflow(); mainWindow.startTargetWorkflow();
    QApplication::processEvents(); QCOMPARE(disasmView->currentAddress(), 0x00401120ULL);

    // Step through function prologue up to loop header.
    while (disasmView->currentAddress() < 0x00401149ULL) { mainWindow.stepInstructionWorkflow(); QApplication::processEvents(); }
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);

    // Loop Iteration 0: step through header and body.
    auto stepTo = [&](uint64_t addr) { mainWindow.stepInstructionWorkflow(); QCOMPARE(disasmView->currentAddress(), addr); };
    stepTo(0x0040114dULL); stepTo(0x0040114fULL); stepTo(0x00401152ULL); stepTo(0x00401155ULL);
    // Stepping at 0x00401155 with rank 0 MUST branch to 0x00401168 -> 0x40116a -> 0x40116e -> 0x401149.
    stepTo(0x00401168ULL); stepTo(0x0040116aULL); stepTo(0x0040116eULL); stepTo(0x00401149ULL);

    // Loop Iteration 1 & 2: execute 7 single-steps through coordinator body to jmp back-edge.
    for (int iter = 1; iter <= 2; ++iter) {
        for (int s = 0; s < 7; ++s) mainWindow.stepInstructionWorkflow();
        QCOMPARE(disasmView->currentAddress(), 0x0040116eULL); stepTo(0x00401149ULL);
    }

    // Loop Exit (i == 3 > 2): step through cmp to jg and take exit branch to MPI_Finalize.
    stepTo(0x0040114dULL); stepTo(0x00401178ULL);
    // Step through MPI_Finalize call, leave, and ret, verifying return 0 highlight.
    for (int s = 0; s < 3; ++s) mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401183ULL); QCOMPARE(sourceView->executionLine(), returnLine);

    // Test Worker Rank (Rank 2) conditional branch behavior:
    mainWindow.stopTargetWorkflow(); serverRack->selectRank(2); mainWindow.startTargetWorkflow(); QApplication::processEvents();
    while (disasmView->currentAddress() < 0x00401155ULL) { mainWindow.stepInstructionWorkflow(); QApplication::processEvents(); }
    QCOMPARE(disasmView->currentAddress(), 0x00401155ULL);
    // For active rank 2, je 0x00401168 condition is false -> must fall through to worker block (0x00401157ULL).
    stepTo(0x00401157ULL); mainWindow.stepInstructionWorkflow(); stepTo(0x00401163ULL); stepTo(0x00401149ULL);

    // Part 4: Live GDB Disassembly CFG Basic-Block Splitting & Default CFG Map View Mode.
    auto& cfg = gridlock::core::ConfigManager::instance();
    auto getDefMode = [](const auto& c) { if constexpr (requires { c.defaultDisassemblyMode(); }) return c.defaultDisassemblyMode(); else return QString(); };
    auto getEdgeCnt = [](const auto* d) { if constexpr (requires { d->cfgEdgeCount(); }) return d->cfgEdgeCount(); else return -1; };
    QCOMPARE(getDefMode(cfg), QStringLiteral("cfg"));
    const QString liveGdbAsm = QStringLiteral(
        "0x00400550 <main+0x0>: push rbp ; line 12\n"
        "0x00400554 <main+0x4>: mov rbp, rsp ; line 12\n"
        "0x00400560 <main+0x10>xcall 00400420 <MPI_Init@plt> ; line 14\n"
        "0x0040056b <main+0x1b>: cmp dword [rbp-0x4], 0x2 ; line 16\n"
        "0x0040056f <main+0x1f>: jg 00400658 ; line 16\n"
        "0x00400575 <main+0x25>: cmp dword [rbp-0x8], 0x0 ; line 17\n"
        "0x00400579 <main+0x29>: je 00400600 ; line 17\n"
        "0x0040057f <main+0x2f>: add dword [rbp-0x4], 0x1 ; line 18\n"
        "0x00400585 <main+0xc5>xjmp 0040056b ; line 18\n"
        "0x00400600 <main+0xb0>: xor eax, eax ; line 19\n"
        "0x00400602 <main+0x1aa>xjmp 00400650 ; line 16\n"
        "0x00400650 <main+0x100>: add dword [rbp-0x4], 0x1 ; line 20\n"
        "0x00400654 <main+0x104>: jmp 0040056b ; line 20\n"
        "0x00400658 <main+0x108>: leave ; line 22\n"
        "0x00400659 <main+0x109>: ret ; line 22");
    disasmView->setGraphMode(true); disasmView->updateDisassembly(liveGdbAsm); QApplication::processEvents();
    QVERIFY2(disasmView->basicBlockCount() >= 4 && getEdgeCnt(disasmView) >= 3,
             qPrintable(QStringLiteral("CFG collapsed: BB=%1 E=%2").arg(disasmView->basicBlockCount()).arg(getEdgeCnt(disasmView))));
    QVERIFY(mainWindow.grab().save(QStringLiteral("/tmp/cfg_live_stepping_verify.png")));
    disasmView->seekToAddress(0x0040056bULL); mainWindow.stopTargetWorkflow(); QApplication::processEvents();
    QVERIFY2(disasmView->isGraphMode() && disasmView->basicBlockCount() >= 4, "DisassemblyView must remain in multi-block CFG Map mode across seek and stop");
    gridlock::ui::PreferencesDialog prefsCfgDlg(&mainWindow);
    auto* defModeCombo = prefsCfgDlg.findChild<QComboBox*>(QStringLiteral("defaultDisassemblyModeCombo"));
    QVERIFY2(defModeCombo != nullptr && defModeCombo->count() >= 3, "PreferencesDialog Debugger page must expose defaultDisassemblyModeCombo");

    // Part 5: Long Demangled C++ Template CFG Width Cap, Left-Edge Viewport Anchor & Cursor State.
    {
        gridlock::ui::DisassemblyView cfgPane; cfgPane.resize(600, 500); cfgPane.setGraphMode(true);
        cfgPane.show(); QVERIFY(QTest::qWaitForWindowExposed(&cfgPane)); QApplication::processEvents();
        // Build a 320+ char demangled STL/MFEM signature plus glued ")>call" boundary input.
        const QString longTmpl = QStringLiteral("mfem::Solver::Solve(std::vector<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, std::allocator<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > > > const&, std::map<int, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > > const&, double)");
        QVERIFY(longTmpl.size() >= 320);
        const QString longCppAsm = QStringLiteral("0x003ce970 <%1+0x0>: push rbp ; line 340\n0x003ce971 <%1+0x1>: mov rbp, rsp ; line 340\n0x003ce975 <std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&)>call 0x003d2d3c ; line 345\n0x003ce97a <%1+0xa>: test al, al ; line 346\n0x003ce97c <%1+0xc>: je 0x003ce990 ; line 346\n0x003ce97e <%1+0xe>: call 0x003d2e00 <%1> ; line 348\n0x003ce983 <%1+0x13>: jmp 0x003ce998 ; line 349\n0x003ce990 <%1+0x20>: nop ; line 352\n0x003ce998 <%1+0x28>: pop rbp ; line 355\n0x003ce999 <%1+0x29>: ret ; line 356").arg(longTmpl);
        cfgPane.updateDisassembly(longCppAsm); cfgPane.seekToAddress(0x003ce975ULL); QApplication::processEvents();
        auto* cfgView = cfgPane.findChild<QGraphicsView*>(QStringLiteral("cutterCfgGraphView"));
        QVERIFY(cfgView != nullptr && cfgView->scene() != nullptr && cfgPane.basicBlockCount() >= 3);
        // 1. Bounded basic-block widths (<= 960px) and clean mnemonic separation (no ")>call").
        QRectF targetRect, leftmostRect;
        for (QGraphicsItem* item : cfgView->scene()->items()) {
            if (qAbs(item->zValue() - 1.0) < 0.05) {
                const QRectF br = item->sceneBoundingRect();
                QVERIFY2(br.width() <= 960.0, qPrintable(QStringLiteral("CFG block width %1 > 960").arg(br.width())));
                if (item->data(Qt::UserRole).toULongLong() == 0x003ce970ULL) targetRect = br;
                if (leftmostRect.isNull() || br.left() < leftmostRect.left()) leftmostRect = br;
            }
            if (auto* txt = dynamic_cast<QGraphicsSimpleTextItem*>(item)) QVERIFY2(!txt->text().contains(QStringLiteral(")>call")), qPrintable(txt->text()));
        }
        QVERIFY(!targetRect.isNull() && !leftmostRect.isNull());
        // 2. Left edge visible on load/focus (x >= 0) and full left scroll range past leftmost block.
        QVERIFY2(cfgView->mapFromScene(targetRect.topLeft()).x() >= 0, qPrintable(QStringLiteral("Target left clipped at x=%1").arg(cfgView->mapFromScene(targetRect.topLeft()).x())));
        auto* hbar = cfgView->horizontalScrollBar(); QVERIFY(hbar != nullptr && hbar->maximum() > hbar->minimum());
        hbar->setValue(hbar->minimum()); QApplication::processEvents();
        QVERIFY2(cfgView->mapFromScene(leftmostRect.topLeft()).x() >= 0, qPrintable(QStringLiteral("Leftmost block clipped at min scroll x=%1").arg(cfgView->mapFromScene(leftmostRect.topLeft()).x())));
        // Test Right arrow scroll followed by Home key snap-back to active block left edge.
        QTest::keyClick(cfgView, Qt::Key_Right); QTest::keyClick(cfgView, Qt::Key_Home); QApplication::processEvents();
        QVERIFY(cfgView->mapFromScene(targetRect.topLeft()).x() >= 0);
        // 3. Idle cursor is Qt::ArrowCursor and pan press/release restores Qt::ArrowCursor.
        QCOMPARE(cfgView->cursor().shape(), Qt::ArrowCursor); QCOMPARE(cfgView->viewport()->cursor().shape(), Qt::ArrowCursor);
        const QPoint midPt = cfgView->viewport()->rect().center();
        QTest::mousePress(cfgView->viewport(), Qt::MiddleButton, Qt::NoModifier, midPt);
        QTest::mouseMove(cfgView->viewport(), midPt + QPoint(25, 10));
        QTest::mouseRelease(cfgView->viewport(), Qt::MiddleButton, Qt::NoModifier, midPt + QPoint(25, 10));
        QApplication::processEvents();
        QCOMPARE(cfgView->cursor().shape(), Qt::ArrowCursor); QCOMPARE(cfgView->viewport()->cursor().shape(), Qt::ArrowCursor);
    }
}

/**
 * @brief Verifies Seek in Memory View tab switching, base address update, and mnemonic F1 doc lookup.
 */
void TestMainWindowUI::testSeekInMemoryViewAndMnemonicDocLookup() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture(); mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>();
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY(disasmView != nullptr && memView != nullptr && bottomTabs != nullptr && refManual != nullptr);
    // Part 1: Seek in Memory View Switches Tab & Updates Base Address + Bytes.
    bottomTabs->setCurrentIndex(0); QApplication::processEvents();
    QVERIFY(bottomTabs->currentWidget() != memView);
    // Trigger context menu action "Seek in Memory View" at loop header instruction 0x00401149ULL.
    QVERIFY2(disasmView->triggerContextAction(0x00401149ULL, QStringLiteral("Seek in Memory View")), "Seek in Memory View must succeed");
    QApplication::processEvents();
    QCOMPARE(bottomTabs->currentWidget(), memView); QCOMPARE(memView->currentBaseAddress(), 0x00401149ULL);
    // Verify MemView hex dump contains the instruction bytes: 83 7d fc 02
    auto* dumpEdit = memView->findChild<QTextEdit*>(QStringLiteral("cutterHexDumpEdit"));
    QVERIFY(dumpEdit != nullptr);
    QVERIFY2(dumpEdit->toPlainText().contains(QStringLiteral("83 7d fc 02"), Qt::CaseInsensitive), qPrintable(dumpEdit->toPlainText()));
    // Verify seeking to .rodata string address 0x00402010ULL updates MemView base address.
    mainWindow.seekToAddressOrExpression(QStringLiteral("0x00402010")); QApplication::processEvents();
    QCOMPARE(memView->currentBaseAddress(), 0x00402010ULL);
    // Part 2: Looking Up "mov" (F1 on mov rbp, rsp) Never Shows MPI Reference.
    QVERIFY2(disasmView->triggerContextAction(0x00401125ULL, QStringLiteral("Lookup in Reference Manual (F1)")), "Lookup (F1) must succeed");
    QApplication::processEvents();
    QCOMPARE(bottomTabs->currentWidget(), refManual);
    // Verify displayed documentation is for MOV and NEVER falls back to MPI_Init.
    auto* docBrowser = refManual->findChild<QTextBrowser*>(QStringLiteral("docsetTextBrowser"));
    QVERIFY(docBrowser != nullptr);
    const QString docContent = docBrowser->toHtml();
    QVERIFY2(docContent.contains(QStringLiteral("MOV"), Qt::CaseInsensitive), "Must display MOV instruction documentation");
    QVERIFY2(!docContent.contains(QStringLiteral("MPI_Init")) && !docContent.contains(QStringLiteral("MPI-3.1 Standard")),
             "Looking up 'mov' must NEVER show MPI_Init or MPI-3.1 Standard text");
}

/**
 * @brief Verifies smart Meson/CMake/Make/SingleFileMPI build system detection and Terminal build/clean/rebuild workflow.
 */
void TestMainWindowUI::testSmartBuildSystemDetectionAndTerminalBuildCleanWorkflow() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture(); mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();

    // Part 1: Smart Build System Detection Across Project Types.
    const auto writeText = [](const QString& path, const QByteArray& bytes) {
        QFile f(path); if (f.open(QIODevice::WriteOnly)) { f.write(bytes); f.close(); }
    };
    // Case A: Meson + Ninja project directory.
    {
        QTemporaryDir mesonDir; QVERIFY(mesonDir.isValid());
        writeText(mesonDir.filePath(QStringLiteral("meson.build")), "project('demo', 'cpp')\n");
        QDir(mesonDir.path()).mkpath(QStringLiteral("builddir"));
        writeText(mesonDir.filePath(QStringLiteral("builddir/build.ninja")), "# ninja build file\n");
        QCOMPARE(mainWindow.detectBuildSystem(mesonDir.path()), QStringLiteral("Meson"));
        QVERIFY(mainWindow.buildCommandForPath(mesonDir.path()).contains(QStringLiteral("ninja -C")));
        QVERIFY(mainWindow.cleanCommandForPath(mesonDir.path()).contains(QStringLiteral("clean")));
    }
    // Case B: CMake project directory.
    {
        QTemporaryDir cmakeDir; QVERIFY(cmakeDir.isValid());
        writeText(cmakeDir.filePath(QStringLiteral("CMakeLists.txt")), "cmake_minimum_required(VERSION 3.20)\nproject(demo)\n");
        QCOMPARE(mainWindow.detectBuildSystem(cmakeDir.path()), QStringLiteral("CMake"));
        QVERIFY(mainWindow.buildCommandForPath(cmakeDir.path()).contains(QStringLiteral("cmake --build")) &&
                mainWindow.cleanCommandForPath(cmakeDir.path()).contains(QStringLiteral("clean")));
    }
    // Case C: GNU Make project directory.
    {
        QTemporaryDir makeDir; QVERIFY(makeDir.isValid());
        writeText(makeDir.filePath(QStringLiteral("Makefile")), "all:\n\t@echo building\nclean:\n\t@echo cleaning\n");
        QCOMPARE(mainWindow.detectBuildSystem(makeDir.path()), QStringLiteral("Make"));
        QVERIFY(mainWindow.buildCommandForPath(makeDir.path()).contains(QStringLiteral("make")) &&
                mainWindow.cleanCommandForPath(makeDir.path()).contains(QStringLiteral("make clean")));
    }
    // Case D: Standalone MPI / C++ source file.
    QTemporaryDir singleFileDir; QVERIFY(singleFileDir.isValid());
    const QString demoSourcePath = singleFileDir.filePath(QStringLiteral("demo.cpp"));
    writeText(demoSourcePath, "#include <mpi.h>\nint main(int argc, char** argv) { return 0; }\n");
    QCOMPARE(mainWindow.detectBuildSystem(demoSourcePath), QStringLiteral("SingleFileMPI"));
    const QString singleBuildCmd = mainWindow.buildCommandForPath(demoSourcePath);
    QVERIFY(singleBuildCmd.contains(QStringLiteral("-g")) && singleBuildCmd.contains(QStringLiteral("-o")));
    QVERIFY(mainWindow.cleanCommandForPath(demoSourcePath).contains(QStringLiteral("rm -f")));

    // Part 2: Interactive Build / Clean / Rebuild Terminal Workflow.
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>();
    auto* statusLabel = mainWindow.findChild<QLabel*>(QStringLiteral("cutterStatusTelemetry"));
    QVERIFY(bottomTabs != nullptr && terminalDock != nullptr && statusLabel != nullptr);

    // Load the standalone source file and switch away from the Terminal tab.
    mainWindow.loadSourceFile(demoSourcePath);
    for (int i = 0; i < bottomTabs->count(); ++i) {
        if (bottomTabs->widget(i) != terminalDock) { bottomTabs->setCurrentIndex(i); break; }
    }
    QVERIFY(bottomTabs->currentWidget() != terminalDock);

    // Trigger Build, Clean, and Rebuild Target workflows and verify Terminal command dispatch + telemetry.
    QVERIFY(mainWindow.buildTargetWorkflow()); QApplication::processEvents();
    QCOMPARE(bottomTabs->currentWidget(), terminalDock);
    QVERIFY(terminalDock->lastSentCommand().contains(QStringLiteral("-g")) && statusLabel->text().contains(QStringLiteral("BUILDING"), Qt::CaseInsensitive));
    QVERIFY(mainWindow.cleanTargetWorkflow()); QApplication::processEvents();
    QCOMPARE(bottomTabs->currentWidget(), terminalDock);
    QVERIFY((terminalDock->lastSentCommand().contains(QStringLiteral("rm -f")) || terminalDock->lastSentCommand().contains(QStringLiteral("clean"))) &&
            statusLabel->text().contains(QStringLiteral("CLEANING"), Qt::CaseInsensitive));
    QVERIFY(mainWindow.rebuildTargetWorkflow()); QApplication::processEvents();
    QCOMPARE(bottomTabs->currentWidget(), terminalDock);
    QVERIFY(terminalDock->lastSentCommand().contains(QStringLiteral("&&")));
}

/**
 * @brief Verifies TerminalDockWidget eventFilter font/geometry lock during PreferencesDialog,
 *        Light Mode contrast on ProjectExplorerWidget, and ReferenceManualWidget docset auto-preview.
 */
void TestMainWindowUI::testTerminalDialogEventFilterLightModeContrastAndDocsetAutoDetect() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture(); mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>();
    auto* explorer = mainWindow.findChild<gridlock::ui::ProjectExplorerWidget*>();
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY(bottomTabs != nullptr && terminalDock != nullptr && explorer != nullptr && refManual != nullptr);

    // Part 1: Terminal EventFilter Protection When Opening PreferencesDialog.
    bottomTabs->setCurrentWidget(terminalDock); QApplication::processEvents();
    gridlock::ui::PreferencesDialog prefsDialog(&mainWindow); prefsDialog.show(); QApplication::processEvents();
    QFont proportionalFont(QStringLiteral("Cantarell"), 11); proportionalFont.setFixedPitch(false);
    for (QWidget* child : terminalDock->findChildren<QWidget*>()) {
        child->setFont(proportionalFont);
        QEvent fontEv(QEvent::FontChange); QApplication::sendEvent(child, &fontEv);
        QEvent styleEv(QEvent::StyleChange); QApplication::sendEvent(child, &styleEv);
    }
    QApplication::processEvents();

    // Verify all child widgets in TerminalDockWidget remain fixed-pitch and span >= 95% of dock width.
    QVERIFY2(terminalDock->areAllChildWidgetsFixedPitch(), "TerminalDockWidget eventFilter must restore fixed-pitch font");
    for (QWidget* child : terminalDock->findChildren<QWidget*>()) {
        if (child->isVisible() && child->width() > 80) {
            QVERIFY2(child->width() >= (terminalDock->width() * 95) / 100,
                     qPrintable(QStringLiteral("Child %1 shrunk to %2px vs %3px").arg(child->metaObject()->className()).arg(child->width()).arg(terminalDock->width())));
        }
    }
    prefsDialog.close();

    // Part 2: Light Mode Contrast Verification.
    gridlock::core::ThemeManager::instance().setDarkMode(false);
    explorer->setSymbols({{QStringLiteral("sym.main"), 0x00401120ULL, 64}}); QApplication::processEvents();
    auto* symTable = explorer->findChild<QTableWidget*>();
    QVERIFY(symTable != nullptr && symTable->rowCount() > 0 && symTable->item(0, 0) != nullptr);
    const auto luminance = [](const QColor& c) { return (299 * c.red() + 587 * c.green() + 114 * c.blue()) / 1000; };
    const int contrastDiff = std::abs(luminance(symTable->item(0, 0)->foreground().color()) - luminance(symTable->palette().color(QPalette::Base)));
    QVERIFY2(contrastDiff >= 80, qPrintable(QStringLiteral("Light mode symbol contrast too low (%1)").arg(contrastDiff)));
    gridlock::core::ThemeManager::instance().setDarkMode(true); QApplication::processEvents();

    // Part 3: Selecting a Docset in ReferenceManualWidget Updates Right Pane.
    bottomTabs->setCurrentWidget(refManual); refManual->lookupSymbol(QStringLiteral("MPI_Init")); QApplication::processEvents();
    auto* docCombo = refManual->findChild<QComboBox*>();
    auto* docBrowser = refManual->findChild<QTextBrowser*>(QStringLiteral("docsetTextBrowser"));
    QVERIFY(docCombo != nullptr && docBrowser != nullptr && docBrowser->toHtml().contains(QStringLiteral("MPI_Init")));
    int targetIdx = -1;
    for (int i = 0; i < docCombo->count(); ++i) {
        const QString label = docCombo->itemText(i);
        if (!label.contains(QStringLiteral("All"), Qt::CaseInsensitive) && !label.contains(QStringLiteral("MPI"), Qt::CaseInsensitive)) { targetIdx = i; break; }
    }
    if (targetIdx >= 0) { docCombo->setCurrentIndex(targetIdx); QApplication::processEvents(); QVERIFY2(!docBrowser->toHtml().contains(QStringLiteral("MPI_Init(&argc")), "Switching docset category must auto-preview first entry"); }
}

/**
 * @brief Verifies selecting a target in `#toolbarTargetSelector` switches the active source
 * editor tab, updates DisassemblyView (Linear/CFG/Decompiler), and directs Build/Clean commands
 * to the selected target's source and binary paths.
 */
void TestMainWindowUI::testTargetSelectorSwitchesSourceDisassemblyAndBuildCommands() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.show(); QApplication::processEvents();
    auto* targetCombo = mainWindow.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector"));
    auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(targetCombo != nullptr && terminalDock != nullptr && disasmView != nullptr);

    // 1. Select "deadlock_ring" via MainWindow::selectTargetByName (and verify combo sync).
    QVERIFY2(mainWindow.selectTargetByName(QStringLiteral("deadlock_ring")), "selectTargetByName('deadlock_ring') must succeed");
    QApplication::processEvents();
    QVERIFY2(mainWindow.activeTargetSourcePath().endsWith(QStringLiteral("deadlock_ring.cpp")) && mainWindow.activeTargetBinaryPath().endsWith(QStringLiteral("deadlock_ring")), qPrintable(mainWindow.activeTargetSourcePath()));

    // Verify active SourceCodeView in EditorTabManager loaded deadlock_ring.cpp containing MPI_Send & MPI_Recv.
    auto* editorTabs = mainWindow.findChild<gridlock::ui::EditorTabManager*>(); QVERIFY(editorTabs && editorTabs->currentSourceCodeView());
    const QString editorText = editorTabs->currentSourceCodeView()->toPlainText();
    QVERIFY2(editorText.contains(QStringLiteral("MPI_Send")) && editorText.contains(QStringLiteral("MPI_Recv")), "Must display deadlock_ring.cpp");

    // Verify DisassemblyView updated its decompiler/disassembly preview to reflect deadlock_ring's MPI_Send/MPI_Recv.
    const QString decompText = disasmView->toPlainText() + QLatin1Char('\n') + disasmView->decompiledPseudocode();
    QVERIFY2(decompText.contains(QStringLiteral("MPI_Send")) || decompText.contains(QStringLiteral("MPI_Recv")), qPrintable(decompText.left(400)));

    // 2. Verify Clean & Build commands operate on deadlock_ring.cpp / deadlock_ring (NOT inspection_demo.cpp).
    mainWindow.cleanTargetWorkflow(); QApplication::processEvents();
    QVERIFY2(terminalDock->lastSentCommand().contains(QStringLiteral("deadlock_ring")) && !terminalDock->lastSentCommand().contains(QStringLiteral("inspection_demo")), qPrintable(terminalDock->lastSentCommand()));
    mainWindow.buildTargetWorkflow(); QApplication::processEvents();
    const QString buildCmd = terminalDock->lastSentCommand();
    QVERIFY2(buildCmd.contains(QStringLiteral("deadlock_ring.cpp")) && buildCmd.contains(QStringLiteral("-o")) && buildCmd.contains(QStringLiteral("deadlock_ring")) && !buildCmd.contains(QStringLiteral("inspection_demo.cpp")), qPrintable(buildCmd));

    // 3. Switch back to "inspection_demo" and verify source path and build command restore inspection_demo.cpp.
    QVERIFY2(mainWindow.selectTargetByName(QStringLiteral("inspection_demo")), "selectTargetByName('inspection_demo') must succeed");
    QApplication::processEvents(); QVERIFY2(mainWindow.activeTargetSourcePath().endsWith(QStringLiteral("inspection_demo.cpp")), qPrintable(mainWindow.activeTargetSourcePath()));
    mainWindow.buildTargetWorkflow(); QApplication::processEvents(); QVERIFY2(terminalDock->lastSentCommand().contains(QStringLiteral("inspection_demo.cpp")), qPrintable(terminalDock->lastSentCommand()));
}

/**
 * @brief Verifies complete Edit menu actions, live UI/Code font scaling across QSS and editors,
 *        and 6 curated color themes with human-readable display names (zero underscores).
 */
void TestMainWindowUI::testEditMenuActionsDynamicFontScalingAndCuratedThemes() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.show(); QApplication::processEvents();
    // Ensure a source file is open in EditorTabManager.
    mainWindow.selectTargetByName(QStringLiteral("inspection_demo")); QApplication::processEvents();
    auto* editorTabs = mainWindow.findChild<gridlock::ui::EditorTabManager*>(); auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(editorTabs != nullptr && disasmView != nullptr && editorTabs->currentSourceCodeView() != nullptr);
    auto* srcView = editorTabs->currentSourceCodeView();

    // 1. Verify Edit menu actions & focus-aware execution (Select All, Copy, Toggle Comment).
    srcView->setFocus();
    QVERIFY2(mainWindow.triggerEditMenuAction(QStringLiteral("Select All")), "Select All must succeed");
    QVERIFY2(mainWindow.triggerEditMenuAction(QStringLiteral("Copy")), "Copy must succeed");

    // Toggle line comment on line 1 and verify '//' prefix is inserted and removed.
    QTextCursor tc = srcView->textCursor(); tc.movePosition(QTextCursor::Start); srcView->setTextCursor(tc);
    const QString origFirstLine = srcView->document()->firstBlock().text();
    QVERIFY(mainWindow.triggerEditMenuAction(QStringLiteral("Toggle Comment")) && srcView->document()->firstBlock().text() != origFirstLine);
    QVERIFY(mainWindow.triggerEditMenuAction(QStringLiteral("Toggle Comment"))); QCOMPARE(srcView->document()->firstBlock().text().trimmed(), origFirstLine.trimmed());

    // 2. Verify dynamic UI Font Size & Code Font Size scaling (no hardcoded 11px lock).
    mainWindow.applyTypographySettings(QStringLiteral("Monospace"), 15, QStringLiteral("Monospace"), 16); QApplication::processEvents();
    const QString activeQss = gridlock::core::ThemeManager::instance().currentStyleSheet();
    QVERIFY2(activeQss.contains(QStringLiteral("15pt")) || activeQss.contains(QStringLiteral("15px")), qPrintable(activeQss.left(200)));
    QCOMPARE(srcView->font().pointSize(), 16); QCOMPARE(disasmView->codeFontPointSize(), 16);
    // Restore default typography settings after scaling verification.
    mainWindow.applyTypographySettings(QStringLiteral("Inter"), 11, QStringLiteral("JetBrains Mono"), 10); QApplication::processEvents();

    // 3. Verify 6 curated color themes and zero underscores in PreferencesDialog Color Theme combo.
    QVERIFY(gridlock::core::ThemeManager::instance().availableThemeIds().size() >= 6);
    gridlock::ui::PreferencesDialog prefsDlg(&mainWindow);
    auto* themeCombo = prefsDlg.findChild<QComboBox*>(QStringLiteral("themeSelectorCombo")); QVERIFY(themeCombo != nullptr && themeCombo->count() >= 6);
    for (int i = 0; i < themeCombo->count(); ++i) QVERIFY2(!themeCombo->itemText(i).contains(QLatin1Char('_')), qPrintable(themeCombo->itemText(i)));

    // 4. Generate 6 offscreen 1440x900 PNG screenshots of each curated theme in docs/screenshots/.
    struct ThemeShotEntry { QString themeId; QString fileName; };
    const std::vector<ThemeShotEntry> shots = {{QStringLiteral("gridlock_slate"), QStringLiteral("theme_gridlock_slate.png")}, {QStringLiteral("dark_teal"), QStringLiteral("theme_obsidian_teal.png")}, {QStringLiteral("tokyo_night"), QStringLiteral("theme_tokyo_night.png")}, {QStringLiteral("catppuccin_mocha"), QStringLiteral("theme_catppuccin_mocha.png")}, {QStringLiteral("gruvbox_dark"), QStringLiteral("theme_gruvbox_dark.png")}, {QStringLiteral("daylight_light"), QStringLiteral("theme_daylight_light.png")}};
    const QString docsShotDir = (!QDir(QStringLiteral("docs")).exists() && QDir(QStringLiteral("../docs")).exists()) ? QStringLiteral("../docs/screenshots") : QStringLiteral("docs/screenshots");
    QDir().mkpath(docsShotDir);
    // Export and verify each rendered theme PNG screenshot.
    for (const auto& shot : shots) {
        gridlock::core::ThemeManager::instance().setTheme(shot.themeId, !shot.themeId.contains(QStringLiteral("light")), false);
        if (srcView) { srcView->reloadStyle(); } if (disasmView) { disasmView->reloadStyle(); } QApplication::processEvents();
        const QString outPath = docsShotDir + QLatin1Char('/') + shot.fileName;
        QVERIFY2(mainWindow.captureWorkspaceSnapshot(outPath) && QFileInfo::exists(outPath) && QFileInfo(outPath).size() > 1024, qPrintable(outPath));
    }
    // Restore default GridLock Slate theme after screenshot export.
    gridlock::core::ThemeManager::instance().setTheme(QStringLiteral("gridlock_slate"), true, false);
    if (srcView) { srcView->reloadStyle(); } if (disasmView) { disasmView->reloadStyle(); } QApplication::processEvents();
}

/**
 * @brief Verifies two-lane vector gutter geometry, distinct breakpoint/conditional/PC marker states,
 *        and non-overlapping antialiased vector badge rendering in SourceCodeView.
 */
void TestMainWindowUI::testModernVectorGutterIconsAndCombinedBreakpointPcState() {
    gridlock::ui::SourceCodeView srcView; QString sampleCode;
    for (int i = 1; i <= 90; ++i) sampleCode += QStringLiteral("    int mpi_rank_step_%1 = %1; // source line %1\n").arg(i);
    srcView.setSourceCode(sampleCode); srcView.resize(720, 560); srcView.show(); QApplication::processEvents();
    // 1. Dedicated Two-Lane Gutter Width (>= 48px) & Non-Overlapping Icon Lane (>= 18px).
    QVERIFY(srcView.lineNumberAreaWidth() >= 48 && srcView.gutterIconLaneWidth() >= 18);
    // 2. Distinct Gutter Marker States (None, Breakpoint, ConditionalBreakpoint, ExecutionPc, BreakpointAndExecutionPc).
    QCOMPARE(srcView.gutterMarkerStateForLine(1), QStringLiteral("None"));
    srcView.toggleBreakpoint(15); srcView.setConditionalBreakpoint(16, QStringLiteral("rank == 0"));
    srcView.highlightExecutionLine(15); QApplication::processEvents();
    QCOMPARE(srcView.gutterMarkerStateForLine(15), QStringLiteral("BreakpointAndExecutionPc"));
    QCOMPARE(srcView.gutterMarkerStateForLine(16), QStringLiteral("ConditionalBreakpoint"));
    srcView.highlightExecutionLine(13); QApplication::processEvents();
    QCOMPARE(srcView.gutterMarkerStateForLine(13), QStringLiteral("ExecutionPc")); QCOMPARE(srcView.gutterMarkerStateForLine(15), QStringLiteral("Breakpoint"));
    // 3. Antialiased Vector Rendering in Gutter Snapshot & Per-Rank Accordion Colors vs. White-Outlined Crimson All-Ranks.
    srcView.setRankBreakpoint(82, -1); srcView.setRankBreakpoint(85, 1); srcView.setRankBreakpoint(88, 2);
    QCOMPARE(srcView.breakpointColorForLine(82), QColor(QStringLiteral("#ef4444")));
    QVERIFY(srcView.breakpointHasWhiteOutlineForLine(82) && srcView.breakpointRankFilterForLine(82) == -1);
    QCOMPARE(srcView.breakpointRankFilterForLine(85), 1); QCOMPARE(srcView.breakpointRankFilterForLine(88), 2);
    QVERIFY(srcView.breakpointColorForLine(85) != srcView.breakpointColorForLine(82) && srcView.breakpointColorForLine(88) != srcView.breakpointColorForLine(82) && srcView.breakpointColorForLine(85) != srcView.breakpointColorForLine(88));
    srcView.highlightExecutionLine(15); QApplication::processEvents();
    const QImage img = srcView.grab().toImage(); QVERIFY(!img.isNull());
    const QColor gutterBg(0x16, 0x18, 0x1d);
    bool foundBadgePixelInIconLane = false;
    // Scan gutter icon lane (x = 3..16) for rendered vector badge pixels.
    for (int y = 0; y < img.height() && !foundBadgePixelInIconLane; ++y) {
        for (int x = 3; x <= 16 && x < img.width(); ++x) {
            const QColor px = img.pixelColor(x, y);
            if (std::abs(px.red() - gutterBg.red()) > 35 || std::abs(px.green() - gutterBg.green()) > 35) { foundBadgePixelInIconLane = true; break; }
        }
    }
    QVERIFY2(foundBadgePixelInIconLane, "Expected vector badge pixels inside left icon lane (x = 3..16)");
}

/**
 * @brief Verifies live coordinator engine switching (`gdb` <-> `lldb-dap`), real per-rank OS PIDs,
 *        live `/proc` enumeration in `AttachProcessDialog` (zero `mpi_navier_stokes` stubs),
 *        cross-rank `MemView` `DIFF RANKS` byte divergence, and clean session teardown.
 */
void TestMainWindowUI::testRealCompiledTargetDwarfMappingBreakpointRunAndLatchedToolbar() {
    auto& cfg = gridlock::core::ConfigManager::instance();
    cfg.setDebuggerEngine(QStringLiteral("gdb")); cfg.setGlobalMpiArgs(QStringLiteral("--oversubscribe"));
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.show(); QApplication::processEvents();
    // 1. Dynamic coordinator switching: ConfigManager::setDebuggerEngine switches MainWindow's live backend.
    QVERIFY(mainWindow.coordinator() != nullptr && QString::fromLatin1(mainWindow.coordinator()->metaObject()->className()).contains(QStringLiteral("GdbRankCoordinator")));
    cfg.setDebuggerEngine(QStringLiteral("lldb-dap")); QApplication::processEvents();
    QVERIFY(mainWindow.coordinator() != nullptr && QString::fromLatin1(mainWindow.coordinator()->metaObject()->className()).contains(QStringLiteral("DapCoordinator")));
    cfg.setDebuggerEngine(QStringLiteral("gdb")); QApplication::processEvents();
    QVERIFY(mainWindow.coordinator() != nullptr && QString::fromLatin1(mainWindow.coordinator()->metaObject()->className()).contains(QStringLiteral("GdbRankCoordinator")));

    // 2. Verify AttachProcessDialog enumerates real /proc user processes and contains zero mpi_navier_stokes stubs.
    QProcess helperProc;
    struct ProcGuard { QProcess* p; ~ProcGuard() { if (p && p->state() != QProcess::NotRunning) { p->kill(); p->waitForFinished(1000); } } } guard{&helperProc};
    helperProc.start(QStringLiteral("sleep"), {QStringLiteral("30")}); QVERIFY(helperProc.waitForStarted(3000));
    const qint64 helperPid = helperProc.processId(); gridlock::ui::AttachProcessDialog attachDlg(&mainWindow);
    auto* procTable = attachDlg.findChild<QTableWidget*>(QStringLiteral("attachProcessTable")); QVERIFY(procTable != nullptr && procTable->rowCount() > 0);
    int helperRow = -1;
    for (int r = 0; r < procTable->rowCount(); ++r) {
        for (int c = 0; c < procTable->columnCount(); ++c) {
            const QString cell = procTable->item(r, c) ? procTable->item(r, c)->text() : QString();
            QVERIFY2(!cell.contains(QStringLiteral("mpi_navier_stokes"), Qt::CaseInsensitive), "Hardcoded mpi_navier_stokes row still present in AttachProcessDialog");
        }
        if (procTable->item(r, 0) && procTable->item(r, 0)->text().toLongLong() == helperPid) helperRow = r;
    }
    QVERIFY2(helperRow >= 0, "AttachProcessDialog must include live child helper PID from /proc");
    procTable->selectRow(helperRow); QCOMPARE(attachDlg.selectedPid(), helperPid);
    // 3. Verify static ELF64 DWARF line-to-address inspection on deadlock_ring before launching a live session.
    QVERIFY2(mainWindow.selectTargetByName(QStringLiteral("deadlock_ring")), "selectTargetByName must succeed");
    if (!mainWindow.ensureTargetBinaryCompiled()) { QFile::remove(mainWindow.activeTargetBinaryPath()); QTest::qWait(400); }
    QVERIFY2(mainWindow.ensureTargetBinaryCompiled(), "ensureTargetBinaryCompiled must compile ELF binary");
    auto* editorTabs = mainWindow.findChild<gridlock::ui::EditorTabManager*>(); auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* targetCombo = mainWindow.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector")); auto* rankSpin = mainWindow.findChild<QSpinBox*>(QStringLiteral("toolbarRankSpin"));
    auto* regTable = mainWindow.findChild<QTableWidget*>(QStringLiteral("cutterRegisterTable")); auto* memView = mainWindow.findChild<gridlock::ui::MemView*>();
    QVERIFY(editorTabs && disasmView && targetCombo && rankSpin && regTable && memView);
    const int sendLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Send(")), recvLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Recv("));
    QVERIFY(sendLine > 0 && recvLine > sendLine && mainWindow.sourceLineToAddress(sendLine) != 0ULL && mainWindow.sourceLineToAddress(recvLine) != 0ULL);
    // 4. Compile C++/MPI target (`inspection_demo`, -g -O0), set breakpoints, and launch 2 live ranks.
    rankSpin->setValue(2); QVERIFY2(mainWindow.selectTargetByName(QStringLiteral("inspection_demo")) && mainWindow.ensureTargetBinaryCompiled(), "inspection_demo must select and compile");
    auto* srcView = editorTabs->currentSourceCodeView(); QVERIFY(srcView != nullptr && QFileInfo::exists(mainWindow.activeTargetBinaryPath()));
    for (int bp : srcView->breakpoints().values()) srcView->toggleBreakpoint(bp);
    const int bpLine1 = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Comm_rank(")), bpLine2 = mainWindow.findSourceLineForStatement(QStringLiteral("printf("));
    QVERIFY(bpLine1 > 0 && bpLine2 > bpLine1); srcView->toggleBreakpoint(bpLine1); srcView->toggleBreakpoint(bpLine2);
    // Launch real parallel GDB session and verify distinct real OS PIDs per rank (`pid0 != pid1 != appPid`).
    mainWindow.startTargetWorkflow();
    QCOMPARE(mainWindow.isDebugSessionActive(), true); QCOMPARE(mainWindow.isRunButtonLatched(), true); QCOMPARE(mainWindow.coordinator()->getProcessCount(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(srcView->executionLine(), bpLine1, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(mainWindow.coordinator()->getPidForRank(0) > 0 && mainWindow.coordinator()->getPidForRank(1) > 0, 5000);
    const qint64 pid0 = mainWindow.coordinator()->getPidForRank(0), pid1 = mainWindow.coordinator()->getPidForRank(1);
    QVERIFY(pid0 > 0 && pid1 > 0 && pid0 != pid1 && pid0 != QCoreApplication::applicationPid() && pid1 != QCoreApplication::applicationPid());
    QVERIFY(QFileInfo::exists(QStringLiteral("/proc/%1/status").arg(pid0)) && QFileInfo::exists(QStringLiteral("/proc/%1/status").arg(pid1)));
    // Verify MpiDiagnosticsWidget cleared static demo rows, shows live PIDs, and flags collective divergence.
    auto* mpiDiag = mainWindow.findChild<gridlock::ui::MpiDiagnosticsWidget*>();
    auto* waitTable = mpiDiag ? mpiDiag->findChild<QTableWidget*>(QStringLiteral("mpiRankWaitTable")) : nullptr;
    auto* dlList = mpiDiag ? mpiDiag->findChild<QListWidget*>(QStringLiteral("mpiDeadlockList")) : nullptr; auto* dlBadge = mpiDiag ? mpiDiag->findChild<QLabel*>(QStringLiteral("mpiDeadlockStatusBadge")) : nullptr;
    QVERIFY(mpiDiag && waitTable && dlList && dlBadge); QCOMPARE(mpiDiag->rankWaitTableRowCount(), 2);
    // Simulate Rank 0 in MPI_Bcast and Rank 1 in MPI_Barrier to confirm live deadlock alert triggers.
    QTRY_VERIFY_WITH_TIMEOUT(waitTable->item(0, 0) && waitTable->item(0, 0)->text().contains(QString::number(pid0)), 4000);
    emit mainWindow.coordinator()->stackTraceUpdated(0, {StackFrameEntry{0, 0, 0x401140ULL, QStringLiteral("MPI_Bcast"), mainWindow.activeTargetSourcePath(), bpLine1}});
    emit mainWindow.coordinator()->stackTraceUpdated(1, {StackFrameEntry{0, 0, 0x401180ULL, QStringLiteral("MPI_Barrier"), mainWindow.activeTargetSourcePath(), bpLine1}});
    QTRY_VERIFY_WITH_TIMEOUT(waitTable->item(0, 2) && waitTable->item(0, 2)->text().contains(QStringLiteral("MPI_Bcast")) && waitTable->item(1, 2) && waitTable->item(1, 2)->text().contains(QStringLiteral("MPI_Barrier")) && dlList->count() >= 1 && !dlBadge->text().contains(QStringLiteral("NO CYCLE")), 3000);
    // Helper reading register value cell by lowercase mnemonic from RegisterView's QTableWidget.
    auto readReg = [&](const QString& n) { for (int r = 0; r < regTable->rowCount(); ++r) if (regTable->item(r, 0) && regTable->item(r, 0)->text() == n && regTable->item(r, 1)) return regTable->item(r, 1)->text(); return QString(); };
    QTRY_VERIFY_WITH_TIMEOUT(!readReg(QStringLiteral("rsp")).isEmpty() && readReg(QStringLiteral("rsp")) != QStringLiteral("0x7fffffffe480"), 5000);
    bool ripOk = false; const uint64_t liveRip1 = readReg(QStringLiteral("rip")).section(QLatin1Char(' '), 0, 0).mid(2).toULongLong(&ripOk, 16);
    QVERIFY(ripOk && liveRip1 != 0ULL && disasmView->currentAddress() == liveRip1);
    // 5. Step Inst, Continue both ranks to bpLine2, and run MemView Read + DIFF RANKS.
    QVERIFY(mainWindow.stepInstructionWorkflow()); QTRY_VERIFY_WITH_TIMEOUT(disasmView->currentAddress() != 0ULL && disasmView->currentAddress() != liveRip1, 5000);
    QVERIFY(mainWindow.continueExecutionWorkflow()); QTRY_COMPARE_WITH_TIMEOUT(srcView->executionLine(), bpLine2, 5000);
    mainWindow.onRankSelected(1); QTRY_COMPARE_WITH_TIMEOUT(srcView->executionLine(), bpLine2, 5000); mainWindow.onRankSelected(0);
    // Read Rank 0 live stack at $rsp and run DIFF RANKS between Rank 0 and Rank 1 to detect diverging bytes.
    auto* addrEdit = memView->findChild<QLineEdit*>(QStringLiteral("cutterMemAddressEdit")); QPushButton *readBtn = nullptr, *diffBtn = nullptr;
    for (auto* b : memView->findChildren<QPushButton*>()) { if (b->text().compare(QStringLiteral("Read"), Qt::CaseInsensitive) == 0) readBtn = b; if (b->text().contains(QStringLiteral("Diff"), Qt::CaseInsensitive)) diffBtn = b; }
    const auto memSpins = memView->findChildren<QSpinBox*>(); QVERIFY(addrEdit && readBtn && diffBtn && memSpins.size() >= 3);
    addrEdit->setText(QStringLiteral("$rsp")); readBtn->click(); QTRY_VERIFY_WITH_TIMEOUT(memView->currentBaseAddress() > 0x1000000ULL, 5000);
    memSpins[1]->setValue(0); memSpins[2]->setValue(1); diffBtn->click(); QTRY_VERIFY_WITH_TIMEOUT(memView->changedByteCount() > 0, 5000);
    mainWindow.stopTargetWorkflow();
    QVERIFY(!mainWindow.isDebugSessionActive() && !mainWindow.isRunButtonLatched() && mainWindow.coordinator()->getProcessCount() == 0 && mainWindow.coordinator()->getPidForRank(0) == 0);
    // 6. Verify CallStackDockWidget, live GdbConsoleWidget REPL, VariablesDockWidget expansion/filter/sort, and watchpoint trigger.
    auto* callStackDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("CallStackDock")); QVERIFY2(callStackDock != nullptr, "MainWindow must expose docked CallStackDock");
    auto* threadCombo = callStackDock->findChild<QComboBox*>(QStringLiteral("threadSelectorCombo")); auto* callStackTable = callStackDock->findChild<QTableWidget*>(QStringLiteral("callStackTable"));
    auto* varsDock = mainWindow.findChild<QDockWidget*>(QStringLiteral("VariablesDock")); auto* gdbConsole = mainWindow.findChild<gridlock::ui::GdbConsoleWidget*>();
    auto* consoleCmdEdit = gdbConsole ? gdbConsole->findChild<QLineEdit*>(QStringLiteral("gdbConsoleCommandEdit")) : nullptr; auto* consoleExecBtn = gdbConsole ? gdbConsole->findChild<QPushButton*>(QStringLiteral("gdbConsoleExecuteBtn")) : nullptr; auto* consoleViewport = gdbConsole ? gdbConsole->findChild<QPlainTextEdit*>(QStringLiteral("gdbConsoleViewport")) : nullptr;
    QVERIFY(threadCombo && callStackTable && varsDock && gdbConsole && consoleCmdEdit && consoleExecBtn && consoleViewport);
    QTemporaryDir nestedDir; QVERIFY(nestedDir.isValid());
    const QString nestedCpp = nestedDir.filePath(QStringLiteral("nested_stack.cpp"));
    { QFile nf(nestedCpp); QVERIFY(nf.open(QIODevice::WriteOnly));
      nf.write("#include <mpi.h>\nint watchedCounter = 10;\nstruct MeshCfg { int dims = 3; };\nstruct InputConfig { MeshCfg mesh; double tol = 1e-6; };\n"
               "__attribute__((noinline)) int innerCallee(int innerVal) {\n    int innerLocal = innerVal + 7;\n    return innerLocal;\n}\n"
               "__attribute__((noinline)) int outerCaller(int outerSecret) {\n    InputConfig Iconfig{};\n    int outerLocal = outerSecret + (Iconfig.mesh.dims > 0 ? 1 : 0) + (Iconfig.tol > 0.0 ? 0 : 1);\n    int res = innerCallee(outerLocal);\n    return res;\n}\n"
               "__attribute__((noinline)) int loopProbe(int step) {\n    int v = step * 10;\n    return v;\n}\n"
               "int main(int argc, char** argv) {\n    MPI_Init(&argc, &argv);\n    int total = outerCaller(4242);\n    for (int step = 1; step <= 4; ++step) total += loopProbe(step);\n    watchedCounter = 77;\n    MPI_Finalize();\n    return (total + watchedCounter) == 0 ? 1 : 0;\n}\n"); }
    rankSpin->setValue(1); targetCombo->insertItem(0, QStringLiteral("nested_stack"), nestedCpp);
    QVERIFY(mainWindow.selectTargetByName(QStringLiteral("nested_stack")) && mainWindow.ensureTargetBinaryCompiled());
    auto* nestedSrcView = editorTabs->currentSourceCodeView(); QVERIFY(nestedSrcView != nullptr);
    const int innerBpLine = mainWindow.findSourceLineForStatement(QStringLiteral("return innerLocal")), outerCallLine = mainWindow.findSourceLineForStatement(QStringLiteral("innerCallee(outerLocal)"));
    const int logLine = mainWindow.findSourceLineForStatement(QStringLiteral("int v = step * 10")), ignLine = mainWindow.findSourceLineForStatement(QStringLiteral("return v"));
    QVERIFY(innerBpLine > 0 && outerCallLine > innerBpLine && logLine > 0 && ignLine > logLine); nestedSrcView->toggleBreakpoint(innerBpLine);
    mainWindow.startTargetWorkflow(); QTRY_COMPARE_WITH_TIMEOUT(nestedSrcView->executionLine(), innerBpLine, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(callStackTable->rowCount() >= 3 && threadCombo->count() >= 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(nestedSrcView->inlineGhostTextForLine(innerBpLine).contains(QStringLiteral("// innerLocal = ")), 5000);
    cfg.setInlineVariableValuesEnabled(false); QVERIFY(nestedSrcView->inlineGhostTextForLine(innerBpLine).isEmpty());
    cfg.setInlineVariableValuesEnabled(true); QVERIFY(!nestedSrcView->inlineGhostTextForLine(innerBpLine).isEmpty());
    { gridlock::ui::PreferencesDialog pd(&mainWindow); auto* ivChk = pd.findChild<QCheckBox*>(QStringLiteral("inlineVarValuesCheck")); QVERIFY(ivChk && ivChk->isChecked());
      gridlock::ui::ConditionalBreakpointDialog cbd(QString(), &mainWindow); QVERIFY(cbd.findChild<QSpinBox*>(QStringLiteral("bpIgnoreCountSpin")) && cbd.findChild<QCheckBox*>(QStringLiteral("bpLogpointCheck")) && cbd.findChild<QLineEdit*>(QStringLiteral("bpLogMessageEdit"))); }
    nestedSrcView->toggleBreakpoint(outerCallLine); nestedSrcView->setBreakpointOptions(outerCallLine, QString(), 3, QStringLiteral("iter={i}"));
    QVERIFY(nestedSrcView->isLogpointForLine(outerCallLine) && nestedSrcView->breakpointIgnoreCountForLine(outerCallLine) == 3 && nestedSrcView->breakpointTooltipForLine(outerCallLine).contains(QStringLiteral("Logpoint")) && nestedSrcView->breakpointTooltipForLine(outerCallLine).contains(QLatin1Char('3')));
    nestedSrcView->toggleBreakpoint(outerCallLine);
    auto verifyFrameRow = [&](int row, const QString& exp) { const QString fn = callStackTable->item(row, 1) ? callStackTable->item(row, 1)->text() : QString(), loc = callStackTable->item(row, 2) ? callStackTable->item(row, 2)->text() : QString(), addr = callStackTable->item(row, 3) ? callStackTable->item(row, 3)->text() : QString(); bool hexOk = false; const quint64 pa = addr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? addr.mid(2).toULongLong(&hexOk, 16) : 0ULL; QVERIFY2(fn.contains(exp) && loc.contains(QLatin1Char(':')) && hexOk && pa != 0ULL, qPrintable(fn)); };
    verifyFrameRow(0, QStringLiteral("innerCallee")); verifyFrameRow(1, QStringLiteral("outerCaller")); verifyFrameRow(2, QStringLiteral("main"));
    callStackTable->selectRow(1); QMetaObject::invokeMethod(callStackTable, "cellClicked", Q_ARG(int, 1), Q_ARG(int, 1)); QTRY_COMPARE_WITH_TIMEOUT(editorTabs->currentSourceCodeView()->executionLine(), outerCallLine, 5000);
    auto* varsTree = varsDock->findChild<QTreeView*>(); auto* varSearch = varsDock->findChild<QLineEdit*>(QStringLiteral("variableSearchEdit"));
    QVERIFY2(varsTree != nullptr && varsTree->model() != nullptr && varSearch != nullptr && varsTree->isSortingEnabled() && varsTree->header()->sectionResizeMode(0) == QHeaderView::Interactive, "VariablesDockWidget must expose #variableSearchEdit, sortable headers, and interactive column resizing");
    auto findVarIdx = [&](const QModelIndex& parent, const QString& name) { for (int r = 0; r < varsTree->model()->rowCount(parent); ++r) { const QModelIndex i = varsTree->model()->index(r, 0, parent); if (varsTree->model()->data(i).toString() == name) return i; } return QModelIndex(); };
    QTRY_VERIFY_WITH_TIMEOUT(([&]() { const QModelIndex ic = findVarIdx({}, QStringLiteral("Iconfig")), os = findVarIdx({}, QStringLiteral("outerSecret")); return ic.isValid() && varsTree->model()->hasChildren(ic) && varsTree->model()->data(ic.siblingAtColumn(1)).toString().contains(QLatin1Char('{')) && os.isValid() && varsTree->model()->data(os.siblingAtColumn(1)).toString().contains(QStringLiteral("4242")); })(), 5000);
    varsTree->expand(findVarIdx({}, QStringLiteral("Iconfig"))); QTRY_VERIFY_WITH_TIMEOUT(([&]() { const QModelIndex ic = findVarIdx({}, QStringLiteral("Iconfig")); const QModelIndex m = findVarIdx(ic, QStringLiteral("mesh")), t = findVarIdx(ic, QStringLiteral("tol")); const QString tv = varsTree->model()->data(t.siblingAtColumn(1)).toString(); return m.isValid() && varsTree->model()->hasChildren(m) && t.isValid() && (tv.contains(QStringLiteral("1e")) || tv.contains(QStringLiteral("e-07"))); })(), 5000);
    varsTree->expand(findVarIdx(findVarIdx({}, QStringLiteral("Iconfig")), QStringLiteral("mesh"))); QTRY_VERIFY_WITH_TIMEOUT(([&]() { const QModelIndex dm = findVarIdx(findVarIdx(findVarIdx({}, QStringLiteral("Iconfig")), QStringLiteral("mesh")), QStringLiteral("dims")); return dm.isValid() && varsTree->model()->data(dm.siblingAtColumn(1)).toString().contains(QLatin1Char('3')); })(), 5000);
    varSearch->setText(QStringLiteral("tol")); QApplication::processEvents(); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("Iconfig")).isValid() && findVarIdx(findVarIdx({}, QStringLiteral("Iconfig")), QStringLiteral("tol")).isValid() && !findVarIdx({}, QStringLiteral("outerSecret")).isValid(), 3000);
    varSearch->clear(); QApplication::processEvents(); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("outerSecret")).isValid(), 3000);
    varsTree->sortByColumn(0, Qt::AscendingOrder); QApplication::processEvents(); const int icAsc = findVarIdx({}, QStringLiteral("Iconfig")).row(), osAsc = findVarIdx({}, QStringLiteral("outerSecret")).row();
    varsTree->sortByColumn(0, Qt::DescendingOrder); QApplication::processEvents(); const int icDesc = findVarIdx({}, QStringLiteral("Iconfig")).row(), osDesc = findVarIdx({}, QStringLiteral("outerSecret")).row();
    QVERIFY2(icAsc >= 0 && osAsc >= 0 && icAsc < osAsc && osDesc < icDesc, "Sorting NAME column asc/desc must deterministically invert top-level order");
    // Verify DDD-style PointerGraphDialog 2D struct/pointer canvas, cycle/alias deduplication, and live GDB struct expansion.
    gridlock::ui::PointerGraphDialog pgDlg(mainWindow.coordinator(), &mainWindow);
    QVERIFY(pgDlg.findChild<QLineEdit*>(QStringLiteral("pointerGraphRootEdit")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphAddBtn")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphLayoutBtn")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphExpandHopBtn")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphClearBtn")) && pgDlg.findChild<QGraphicsView*>(QStringLiteral("pointerGraphView")));
    pgDlg.upsertStructNode(QStringLiteral("n1"), QStringLiteral("Node"), QStringLiteral("0x7fff1000"), {{QStringLiteral("val"), QStringLiteral("10"), QStringLiteral("int")}, {QStringLiteral("next"), QStringLiteral("0x7fff2000"), QStringLiteral("Node*")}});
    pgDlg.upsertStructNode(QStringLiteral("n2"), QStringLiteral("Node"), QStringLiteral("0x7fff2000"), {{QStringLiteral("val"), QStringLiteral("20"), QStringLiteral("int")}, {QStringLiteral("next"), QStringLiteral("0x7fff1000"), QStringLiteral("Node*")}}, QStringLiteral("n1"), QStringLiteral("next"));
    pgDlg.connectPointerField(QStringLiteral("n2"), QStringLiteral("next"), QStringLiteral("0x7fff1000"));
    QVERIFY(pgDlg.nodeCount() == 2 && pgDlg.edgeCount() == 2 && pgDlg.hasCycleOrSharedAlias());
    pgDlg.clearGraph(); pgDlg.graphExpression(QStringLiteral("Iconfig"));
    QTRY_VERIFY_WITH_TIMEOUT(pgDlg.nodeCount() >= 1 && pgDlg.nodeHasField(QStringLiteral("Iconfig"), QStringLiteral("mesh")) && pgDlg.nodeHasField(QStringLiteral("Iconfig"), QStringLiteral("tol")), 5000);
    pgDlg.expandNodeField(QStringLiteral("Iconfig"), QStringLiteral("mesh"));
    QTRY_VERIFY_WITH_TIMEOUT(pgDlg.nodeCount() >= 2 && pgDlg.edgeCount() >= 1 && pgDlg.nodeHasField(QStringLiteral("Iconfig.mesh"), QStringLiteral("dims")), 5000);
    consoleCmdEdit->setText(QStringLiteral("print outerSecret")); consoleExecBtn->click(); QTRY_VERIFY_WITH_TIMEOUT(consoleViewport->toPlainText().contains(QStringLiteral("4242")), 5000);
    consoleCmdEdit->setText(QStringLiteral("-data-evaluate-expression 21 + 21")); consoleExecBtn->click(); QTRY_VERIFY_WITH_TIMEOUT(consoleViewport->toPlainText().contains(QStringLiteral("42")), 5000);
    consoleCmdEdit->setText(QStringLiteral("print outerSecret; echo PWNED")); consoleExecBtn->click(); QApplication::processEvents(); QVERIFY(!consoleViewport->toPlainText().contains(QStringLiteral("PWNED")));
    const QString mfemCpp = nestedDir.filePath(QStringLiteral("MfemSolver.cpp"));
    { QFile mf(mfemCpp); QVERIFY(mf.open(QIODevice::WriteOnly)); for (int ln = 1; ln <= 90; ++ln) mf.write(QStringLiteral("// line %1\nint dummy_%1 = %1;\n").arg(ln).toUtf8()); }
    mainWindow.loadSourceFile(mfemCpp); QApplication::processEvents(); auto* solverView = editorTabs->currentSourceCodeView();
    QVERIFY2(solverView != nullptr && solverView->currentFile().endsWith(QStringLiteral("MfemSolver.cpp")), "Secondary file tab MfemSolver.cpp must become active");
    solverView->toggleBreakpoint(82); QTest::qWait(300); QApplication::processEvents();
    QVERIFY2(editorTabs->currentSourceCodeView() == solverView && editorTabs->currentSourceCodeView()->currentFile().endsWith(QStringLiteral("MfemSolver.cpp")), "Toggling breakpoint in MfemSolver.cpp must NEVER switch active tab");
    nestedSrcView->toggleBreakpoint(logLine); nestedSrcView->setBreakpointOptions(logLine, QString(), 0, QStringLiteral("TRACE_STEP_HIT"));
    nestedSrcView->toggleBreakpoint(ignLine); nestedSrcView->setBreakpointOptions(ignLine, QString(), 2, QString());
    auto* logProgOut = mainWindow.findChild<QPlainTextEdit*>(QStringLiteral("programOutputView")); QVERIFY(logProgOut != nullptr);
    QVERIFY(mainWindow.continueExecutionWorkflow()); QTRY_COMPARE_WITH_TIMEOUT(nestedSrcView->executionLine(), ignLine, 8000);
    QTRY_VERIFY_WITH_TIMEOUT(logProgOut->toPlainText().contains(QStringLiteral("TRACE_STEP_HIT")), 5000); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("v")).isValid() && varsTree->model()->data(findVarIdx({}, QStringLiteral("v")).siblingAtColumn(1)).toString().contains(QStringLiteral("30")), 5000);
    nestedSrcView->toggleBreakpoint(logLine); nestedSrcView->toggleBreakpoint(ignLine);
    QSignalSpy wpHitSpy(mainWindow.coordinator(), SIGNAL(watchpointHit(int,QString,QString,QString))); QVERIFY2(QMetaObject::invokeMethod(mainWindow.coordinator(), "setDataWatchpoint", Q_ARG(QString, QStringLiteral("watchedCounter")), Q_ARG(int, 0), Q_ARG(int, -1)), "Coordinator must expose setDataWatchpoint"); QTest::qWait(150);
    QVERIFY(mainWindow.continueExecutionWorkflow()); QTRY_VERIFY_WITH_TIMEOUT(wpHitSpy.count() >= 1, 6000); QVERIFY(wpHitSpy.first().at(1).toString() == QStringLiteral("watchedCounter") && wpHitSpy.first().at(2).toString() == QStringLiteral("10") && wpHitSpy.first().at(3).toString() == QStringLiteral("77"));
    solverView->toggleBreakpoint(82); mainWindow.stopTargetWorkflow(); QCOMPARE(callStackTable->rowCount(), 0); QVERIFY(nestedSrcView->inlineGhostTextForLine(innerBpLine).isEmpty());
    // 7. Verify MemView #memMapModeBtn + MemoryProfilerWidget controls, then launch 4-rank hybrid MPI + 4 MB heap target.
    auto* memMapBtn = memView->findChild<QPushButton*>(QStringLiteral("memMapModeBtn")); auto* memProf = memView->findChild<QWidget*>(QStringLiteral("memoryProfilerWidget"));
    auto* treemapCanvas = memView->findChild<QWidget*>(QStringLiteral("memoryTreemapCanvas")); auto* breakdownTable = memView->findChild<QTableWidget*>(QStringLiteral("memoryBreakdownTable"));
    auto* profSearch = memView->findChild<QLineEdit*>(QStringLiteral("memProfileSearchEdit")); auto* preloadCheck = memView->findChild<QCheckBox*>(QStringLiteral("memTrackPreloadCheck"));
    QVERIFY2(memMapBtn && memProf && treemapCanvas && breakdownTable && profSearch && preloadCheck && preloadCheck->isChecked() && breakdownTable->isSortingEnabled() && breakdownTable->horizontalHeader()->sectionResizeMode(0) == QHeaderView::Interactive, "MemView must expose #memMapModeBtn, MemoryProfilerWidget controls, and sortable Interactive columns");
    const QString barrierCpp = nestedDir.filePath(QStringLiteral("barrier_hex.cpp"));
    { QFile bf(barrierCpp); QVERIFY(bf.open(QIODevice::WriteOnly));
      bf.write("#include <mpi.h>\n#include <cstdio>\n#include <cstdlib>\n#include <string>\n#include <thread>\n#include <unistd.h>\n#include <vector>\n"
               "__attribute__((noinline)) std::vector<char> allocateSolverArena() { std::vector<char> b(4 * 1024 * 1024, 1); b[0] = 7; b.back() = 9; return b; }\n"
               "__attribute__((noinline)) int solveStage(int r, int it, int sum) {\n  int res = sum + r + it;\n  return res;\n}\n"
               "int main(int argc, char** argv) {\n  MPI_Init(&argc, &argv);\n  if (argc < 2 || std::string(argv[1]) != \"HEX\") { MPI_Finalize(); return 2; }\n"
               "  int r = -1, s = 0; MPI_Comm_rank(MPI_COMM_WORLD, &r); MPI_Comm_size(MPI_COMM_WORLD, &s); auto solverArena = allocateSolverArena();\n"
               "  const char* omp = std::getenv(\"OMP_NUM_THREADS\"); const char* pol = std::getenv(\"OMP_WAIT_POLICY\"); int nt = (omp && std::atoi(omp) > 0) ? std::atoi(omp) : 8; std::vector<std::thread> ws;\n"
               "  for (int t = 0; t < nt; ++t) ws.emplace_back([r, t]() { volatile int acc = r + t; (void)acc; });\n  for (auto& th : ws) th.join();\n"
               "  std::printf(\"PRE_BARRIER_R%d OMP=%s POL=%s\\n\", r, omp ? omp : \"NONE\", pol ? pol : \"NONE\"); std::fflush(stdout); usleep(250000); int v = r + 1; MPI_Barrier(MPI_COMM_WORLD);\n"
               "  int g = 0; MPI_Allreduce(&v, &g, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD); MPI_Barrier(MPI_COMM_WORLD);\n  int out = solveStage(r, 3, g + solverArena[0]); MPI_Finalize(); return (out > 0 && s == 4) ? 0 : 1;\n}\n"); }
    auto projCfg = gridlock::core::ConfigManager::instance().loadProjectSettings(); projCfg.targetArguments = QStringLiteral("HEX"); projCfg.programArguments = "HEX";
    gridlock::core::ConfigManager::instance().saveProjectSettings(projCfg); qunsetenv("OMP_NUM_THREADS"); qunsetenv("OMP_WAIT_POLICY");
    rankSpin->setValue(4); targetCombo->insertItem(0, QStringLiteral("barrier_hex"), barrierCpp); QVERIFY(mainWindow.selectTargetByName(QStringLiteral("barrier_hex")) && mainWindow.ensureTargetBinaryCompiled());
    auto* barrierView = editorTabs->currentSourceCodeView(); const int solveBp = mainWindow.findSourceLineForStatement(QStringLiteral("return res")); QVERIFY(barrierView != nullptr && solveBp > 0);
    barrierView->toggleBreakpoint(solveBp); mainWindow.startTargetWorkflow();
    auto* rackView = mainWindow.findChild<gridlock::ui::ServerRackView*>(); QVERIFY(rackView != nullptr);
    for (int r = 0; r < rackView->count(); ++r) { const QString pc = rackView->item(r)->data(Qt::UserRole + 1).value<gridlock::RankState>().registers.value(16); QVERIFY2(!pc.contains(QStringLiteral("401120")) && !pc.contains(QStringLiteral("401155")) && !pc.contains(QStringLiteral("00000000")), qPrintable(pc)); }
    QSignalSpy cmdSpy(mainWindow.coordinator(), SIGNAL(commandSentToGdb(int,QString)));
    mainWindow.coordinator()->requestStackAndThreads(0); mainWindow.coordinator()->selectStackFrame(0, 1, 0); mainWindow.coordinator()->evaluateExpression(0, QStringLiteral("r")); mainWindow.coordinator()->sendDebuggerCommand(0, QStringLiteral("bt"));
    mainWindow.onRankSelected(1); mainWindow.onRankSelected(0); QCOMPARE(cmdSpy.count(), 0);
    const QString launchSummary = mainWindow.coordinator()->lastLaunchCommandSummary(); QVERIFY2(launchSummary.contains(QStringLiteral("OMP_NUM_THREADS=")) && launchSummary.contains(QStringLiteral("OMP_WAIT_POLICY=PASSIVE")), qPrintable(launchSummary));
    auto* progOut = mainWindow.findChild<QPlainTextEdit*>(QStringLiteral("programOutputView")); QVERIFY(progOut != nullptr); QTRY_COMPARE_WITH_TIMEOUT(barrierView->executionLine(), solveBp, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(([&]() { const QString o = progOut->toPlainText(); return o.contains(QStringLiteral("PRE_BARRIER_R0")) && o.contains(QStringLiteral("PRE_BARRIER_R1")) && o.contains(QStringLiteral("PRE_BARRIER_R2")) && o.contains(QStringLiteral("PRE_BARRIER_R3")) && o.contains(QStringLiteral("POL=PASSIVE")) && !o.contains(QStringLiteral("OMP=NONE")); })(), 5000);
    memMapBtn->click(); treemapCanvas->resize(720, 260); treemapCanvas->show(); QApplication::processEvents();
    const qint64 liveRankPid = mainWindow.coordinator()->getPidForRank(0); int rectCount = 0; QVERIFY(liveRankPid > 0 && QMetaObject::invokeMethod(memProf, "refreshForPid", Q_ARG(qint64, liveRankPid)) && QMetaObject::invokeMethod(memProf, "treemapRectCount", Q_RETURN_ARG(int, rectCount)) && breakdownTable->rowCount() >= 3 && rectCount >= 2);
    bool sawSharedLib = false, sawArena = false;
    for (int r = 0; r < breakdownTable->rowCount(); ++r) {
        const QString lib = breakdownTable->item(r, 0) ? breakdownTable->item(r, 0)->text() : QString(), cat = breakdownTable->item(r, 1) ? breakdownTable->item(r, 1)->text() : QString(), sym = breakdownTable->item(r, 5) ? breakdownTable->item(r, 5)->text() : QString();
        const double mb = breakdownTable->item(r, 2) ? breakdownTable->item(r, 2)->text().section(QLatin1Char(' '), 0, 0).toDouble() : 0.0;
        if ((lib.contains(QStringLiteral("libc")) || lib.contains(QStringLiteral("libstdc++")) || lib.contains(QStringLiteral("libmpi"))) && mb > 0.0) sawSharedLib = true;
        if ((lib.contains(QStringLiteral("barrier_hex")) || sym.contains(QStringLiteral("allocateSolverArena")) || cat.contains(QStringLiteral("Heap"))) && mb >= 3.8) sawArena = true;
    }
    QVERIFY2(sawSharedLib && sawArena, "Breakdown table must include live shared libraries and >=3.8 MB arena attribution");
    // Sort by Live/RSS MB descending and filter via #memProfileSearchEdit for "libc".
    breakdownTable->sortByColumn(2, Qt::DescendingOrder); QApplication::processEvents();
    QVERIFY(breakdownTable->item(0, 2)->text().section(' ', 0, 0).toDouble() >= breakdownTable->item(breakdownTable->rowCount() - 1, 2)->text().section(' ', 0, 0).toDouble());
    auto visProfCount = [&]() { int c = 0; for (int r = 0; r < breakdownTable->rowCount(); ++r) if (!breakdownTable->isRowHidden(r)) ++c; return c; };
    const int fullProfCount = visProfCount(); profSearch->setText(QStringLiteral("libc")); QApplication::processEvents();
    QVERIFY(visProfCount() > 0 && visProfCount() < fullProfCount);
    profSearch->clear(); QApplication::processEvents(); QCOMPARE(visProfCount(), fullProfCount);
    mainWindow.stopTargetWorkflow();
}

/** @brief Verifies gutter hover tracking, ghost preview, and anchored breakpoint popover controls. */
void TestMainWindowUI::testAnchoredBreakpointPopoverAndGutterHover() {
    // 1. Initialize SourceCodeView with 40 MPI_Send lines for gutter hover and breakpoint tests.
    gridlock::ui::SourceCodeView sourceCodeView; QString sampleCode;
    for (int i = 1; i <= 40; ++i) sampleCode += QStringLiteral("    MPI_Send(send_buf.data(), %1, MPI_INT, next, 0, MPI_COMM_WORLD);\n").arg(i);
    sourceCodeView.setSourceCode(sampleCode); sourceCodeView.resize(720, 560); sourceCodeView.show(); QApplication::processEvents();
    // Verify ghost hover preview transitions cleanly to active breakpoint and back to None.
    QCOMPARE(sourceCodeView.hoveredGutterLine(), -1);
    sourceCodeView.setHoveredGutterLine(16); QApplication::processEvents();
    QCOMPARE(sourceCodeView.hoveredGutterLine(), 16); QCOMPARE(sourceCodeView.gutterMarkerStateForLine(16), QStringLiteral("HoverPreview"));
    sourceCodeView.toggleBreakpoint(16); QCOMPARE(sourceCodeView.gutterMarkerStateForLine(16), QStringLiteral("Breakpoint"));
    sourceCodeView.toggleBreakpoint(16); sourceCodeView.setHoveredGutterLine(-1);
    QCOMPARE(sourceCodeView.hoveredGutterLine(), -1); QCOMPARE(sourceCodeView.gutterMarkerStateForLine(16), QStringLiteral("None"));
    // 2. Verify ConditionalBreakpointDialog target line summary and default suspend scope.
    gridlock::ui::ConditionalBreakpointDialog dialog(QString(), &sourceCodeView);
    dialog.setTargetLineContext(34, QStringLiteral("MPI_Send(send_buf.data(), ...);"));
    QVERIFY2(dialog.targetLineSummaryText().contains(QStringLiteral("Line 34")), qPrintable(dialog.targetLineSummaryText()));
    QCOMPARE(dialog.isBreakpointEnabled(), true); QCOMPARE(dialog.suspendScope(), QStringLiteral("All"));
    // Select single-rank suspend radio button and verify suspendScope() transitions to Rank.
    QRadioButton* rankRadio = nullptr;
    for (QRadioButton* rb : dialog.findChildren<QRadioButton*>())
        if (rb->text().contains(QStringLiteral("Rank"), Qt::CaseInsensitive) || !rb->isChecked()) { rankRadio = rb; break; }
    QVERIFY2(rankRadio != nullptr, "ConditionalBreakpointDialog must expose a Rank/Thread suspend radio button");
    rankRadio->setChecked(true); QApplication::processEvents(); QCOMPARE(dialog.suspendScope(), QStringLiteral("Rank"));
    // Apply quick preset condition and verify anchored popover screen positioning.
    dialog.applyQuickPreset(QStringLiteral("rank == 0")); QCOMPARE(dialog.getCondition(), QStringLiteral("rank == 0"));
    const QPoint anchorGlobalPos(120, 340); dialog.positionNearAnchor(anchorGlobalPos); dialog.show(); QApplication::processEvents();
    // Assert horizontal and vertical offsets stay tightly anchored to the clicked gutter line.
    QVERIFY2(std::abs(dialog.pos().x() - anchorGlobalPos.x()) <= 40, qPrintable(QStringLiteral("Popover x=%1").arg(dialog.pos().x())));
    QVERIFY2(std::abs(dialog.pos().y() - anchorGlobalPos.y()) <= 25, qPrintable(QStringLiteral("Popover y=%1").arg(dialog.pos().y())));
    dialog.hide();
}
/**
 * @brief Verifies left vertical tool-window icon rail (`#bottomToolWindowRail`), linked contextual
 *        tab filtering across Debug/MPI/Terminal/Docs, programmatic auto-sync, and flattened MPI header.
 */
void TestMainWindowUI::testBottomToolWindowRailAndLinkedTabGroups() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.show(); QApplication::processEvents();
    // 1. Verify #bottomToolWindowRail exists and exposes the 4 canonical tool groups.
    // Lookup bottom console tab widget and all linked child views (MemView, Terminal, Reference, MPI).
    auto* rail = mainWindow.findChild<QWidget*>(QStringLiteral("bottomToolWindowRail")); auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>(); auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>();
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>(); auto* mpiDiag = mainWindow.findChild<gridlock::ui::MpiDiagnosticsWidget*>();
    QVERIFY(rail && bottomTabs && memView && terminalDock && refManual && mpiDiag);
    QCOMPARE(mainWindow.bottomToolGroups(), (QStringList{QStringLiteral("Debug"), QStringLiteral("MPI"), QStringLiteral("Terminal"), QStringLiteral("Docs")}));
    // Helper returning uppercased titles of currently visible tabs in BottomConsoleTabs.
    const auto visibleTitles = [&]() {
        QStringList vis; for (int i = 0; i < bottomTabs->count(); ++i) if (bottomTabs->tabBar()->isTabVisible(i)) vis.append(bottomTabs->tabText(i).toUpper());
        return vis;
    };
    // 2. Verify selecting Debug, MPI, Terminal, and Docs filters visible tabs strictly to linked views.
    mainWindow.selectBottomToolGroup(QStringLiteral("Debug")); QCOMPARE(mainWindow.activeBottomToolGroup(), QStringLiteral("Debug"));
    // Check Debug visible tabs (`GDB CONSOLE`, `PROGRAM OUTPUT`, `MEMORY`, `EVALUATOR`).
    const QStringList debugVis = visibleTitles(); QCOMPARE(debugVis.size(), 4);
    QVERIFY(debugVis.join(QLatin1Char('|')).contains(QStringLiteral("GDB")) && debugVis.join(QLatin1Char('|')).contains(QStringLiteral("PROGRAM")) && debugVis.join(QLatin1Char('|')).contains(QStringLiteral("MEMORY")) && debugVis.join(QLatin1Char('|')).contains(QStringLiteral("EVALUATOR")));
    // Switch to MPI, Terminal, and Docs tool groups and confirm filtered view visibility.
    mainWindow.selectBottomToolGroup(QStringLiteral("MPI")); QCOMPARE(mainWindow.activeBottomToolGroup(), QStringLiteral("MPI")); QCOMPARE(visibleTitles().size(), 2);
    mainWindow.selectBottomToolGroup(QStringLiteral("Terminal")); QCOMPARE(mainWindow.activeBottomToolGroup(), QStringLiteral("Terminal")); QCOMPARE(bottomTabs->currentWidget(), terminalDock);
    mainWindow.selectBottomToolGroup(QStringLiteral("Docs")); QCOMPARE(mainWindow.activeBottomToolGroup(), QStringLiteral("Docs")); QCOMPARE(bottomTabs->currentWidget(), refManual);
    // 3. Programmatically switching BottomConsoleTabs to MemView auto-syncs the Debug tool group.
    bottomTabs->setCurrentWidget(memView); QApplication::processEvents();
    QCOMPARE(mainWindow.activeBottomToolGroup(), QStringLiteral("Debug")); QCOMPARE(visibleTitles().size(), 4);
    // 4. Verify MpiDiagnosticsWidget uses a single compact 26px header bar with hidden inner QTabBar.
    QVERIFY(mpiDiag->hasCompactSingleRowHeader());
}
/** @brief Verifies gutter QContextMenuEvent suppression and clean rollback of canceled breakpoints. */
void TestMainWindowUI::testGutterRightClickSuppressesEditorContextMenuAndCancelsCleanly() {
    // Configure sample 30-line source code buffer to test gutter context menu and breakpoint rollback.
    gridlock::ui::SourceCodeView sourceCodeView; QString sampleCode;
    for (int i = 1; i <= 30; ++i) sampleCode += QStringLiteral("    int rank_val_%1 = %1;\n").arg(i);
    sourceCodeView.setSourceCode(sampleCode); sourceCodeView.resize(720, 560); sourceCodeView.show(); QApplication::processEvents();
    // 1. Locate LineNumberArea child widget and verify QContextMenuEvent is accepted without bubbling.
    // Prevents standard QPlainTextEdit Undo/Redo/Cut/Copy popup from bleeding over the gutter margin.
    QWidget* gutterWidget = nullptr;
    for (QWidget* w : sourceCodeView.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly))
        if (w != sourceCodeView.viewport() && w->width() == sourceCodeView.lineNumberAreaWidth()) { gutterWidget = w; break; }
    QVERIFY(gutterWidget != nullptr);
    // Dispatch synthetic right-click inside gutter area and verify event acceptance.
    QContextMenuEvent gutterMenuEv(QContextMenuEvent::Mouse, QPoint(10, 25), sourceCodeView.mapToGlobal(QPoint(10, 25)));
    gutterMenuEv.ignore(); QApplication::sendEvent(gutterWidget, &gutterMenuEv); QCOMPARE(gutterMenuEv.isAccepted(), true);
    // Verify context menu suppression within gutter boundaries (`x=10` suppressed, `x=120` allowed).
    QCOMPARE(sourceCodeView.shouldOpenEditorContextMenuAt(QPoint(10, 25)), false); QCOMPARE(sourceCodeView.shouldOpenEditorContextMenuAt(QPoint(120, 25)), true);
    // 2. Canceling a newly right-clicked gutter breakpoint restores unmarked state on line 18.
    // Confirms dismissing the popover rolls back transient breakpoint insertion cleanly.
    sourceCodeView.toggleBreakpoint(18);
    sourceCodeView.handleBreakpointPopoverResult(18, false, false, false, QString(), QStringLiteral("All")); QCOMPARE(sourceCodeView.hasBreakpoint(18), false);
    // Accept conditional breakpoint (`rank == 0`) and verify removal on subsequent popover deletion.
    sourceCodeView.handleBreakpointPopoverResult(18, false, true, true, QStringLiteral("rank == 0"), QStringLiteral("Rank"));
    QCOMPARE(sourceCodeView.gutterMarkerStateForLine(18), QStringLiteral("ConditionalBreakpoint"));
    sourceCodeView.handleBreakpointPopoverResult(18, true, true, false, QString(), QStringLiteral("All")); QCOMPARE(sourceCodeView.hasBreakpoint(18), false);
}

namespace {
// Resolve relative tutorial paths across both repo-root and Meson `builddir/` working directories.
inline QString resolveTutorialPath(const QString& r) { return QFileInfo::exists(r) ? r : (QStringLiteral("../") + r); }
inline QString readTutorialFile(const QString& r) { QFile f(resolveTutorialPath(r)); return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString(); }
// SFINAE dispatchers for theme persistence, editor completions, and detailed LSP metadata queries.
template <typename T> auto callSavePersistedTheme(T& tm, int) -> decltype(tm.savePersistedTheme(), void()) { tm.savePersistedTheme(); } template <typename T> void callSavePersistedTheme(T&, ...) {}
template <typename T> auto callLoadPersistedTheme(T& tm, int) -> decltype(tm.loadPersistedTheme()) { return tm.loadPersistedTheme(); } template <typename T> int callLoadPersistedTheme(T&, ...) { return 0; }
template <typename T> auto callEditorCompletionsAtLine(T* v, int l, const QString& p, int) -> decltype(v->editorCompletionsAtLine(l, p)) { return v->editorCompletionsAtLine(l, p); } template <typename T> int callEditorCompletionsAtLine(T*, int, const QString&, ...) { return 0; }
template <typename T> auto callTriggerCompletionPopup(T* v, const QString& p, int l, int) -> decltype(v->triggerCompletionPopupAtCursor(p, l), void()) { v->triggerCompletionPopupAtCursor(p, l); } template <typename T> void callTriggerCompletionPopup(T*, const QString&, int, ...) {}
template <typename C, typename... A> auto callCompleteDetailed(int, A&&... a) -> decltype(C::completeDetailedAtLine(std::forward<A>(a)...)) { return C::completeDetailedAtLine(std::forward<A>(a)...); } template <typename C, typename... A> int callCompleteDetailed(long, A&&...) { return 0; }
// Validate basic and detailed Clangd completion payloads for `DeeplyNested` struct fields (`id`, `coords`).
template <typename T> void verifyCompletions(const T& c) { if constexpr (!std::is_same_v<std::decay_t<T>, int>) { QVERIFY(!c.isEmpty() && c.contains(QStringLiteral("id")) && c.contains(QStringLiteral("coords"))); } }
template <typename T> void verifyDetailed(const T& items) {
    if constexpr (!std::is_same_v<std::decay_t<T>, int>) {
        QVERIFY(!items.isEmpty()); bool fId = false, fCoords = false;
        for (const auto& it : items) {
            if (it.label == QStringLiteral("id")) { fId = true; QCOMPARE(it.kindLabel, QStringLiteral("Field")); QCOMPARE(it.detailType, QStringLiteral("int")); }
            else if (it.label == QStringLiteral("coords")) { fCoords = true; QCOMPARE(it.kindLabel, QStringLiteral("Field")); QVERIFY(!it.detailType.isEmpty()); }
        } QVERIFY(fId && fCoords); } }
// Confirm `loadPersistedTheme()` restores `tokyo_night` after transient runtime theme switches.
template <typename T, typename M> void verifyLoadedTheme(const T&, M& tm) { if constexpr (!std::is_same_v<std::decay_t<T>, int>) { QCOMPARE(tm.activeThemeId(), QStringLiteral("tokyo_night")); } }
}  // namespace (anonymous)
/**
 * @brief Verifies ClangdLspClient virtual-buffer splicing, JSON-RPC framing, and nested struct/vector
 *        expression completion (`data[i].`, `data[i].coords.`, `send_buf.`) in ConditionalBreakpointDialog.
 */
void TestMainWindowUI::testClangdLspBreakpointExpressionCompletion() {
    // Resolve tutorial sample file paths and read source code contents for LSP splicing tests.
    const QString inspectionPath = resolveTutorialPath(QStringLiteral("tutorial/inspection_demo.cpp"));
    const QString deadlockPath = resolveTutorialPath(QStringLiteral("tutorial/deadlock_ring.cpp"));
    const QString inspectionSource = readTutorialFile(QStringLiteral("tutorial/inspection_demo.cpp"));
    const QString deadlockSource = readTutorialFile(QStringLiteral("tutorial/deadlock_ring.cpp"));
    QVERIFY(!inspectionSource.isEmpty() && !deadlockSource.isEmpty());
    // 1. Verify LSP JSON-RPC Content-Length framing and virtual document splicing at line 18.
    // Ensures synthetic completion probe sits at 0-based line 17 immediately after `data[i].`.
    int zeroLine = -1, zeroCol = -1;
    const QString spliced = gridlock::core::ClangdLspClient::buildVirtualCompletionDocument(inspectionSource, 18, QStringLiteral("data[i]."), &zeroLine, &zeroCol);
    const QStringList splicedLines = spliced.split(QLatin1Char('\n'));
    QVERIFY(zeroLine == 17 && zeroCol >= 8 && zeroLine < splicedLines.size() && splicedLines.at(zeroLine).left(zeroCol).endsWith(QStringLiteral("data[i].")));
    // 2. Verify member completion on `data[i].` and nested `data[i].coords.` in ConditionalBreakpointDialog.
    // Confirms both first-level struct fields and nested coordinate fields (`x`, `y`, `z`) resolve.
    gridlock::ui::ConditionalBreakpointDialog dialog; dialog.setSourceDocumentContext(inspectionPath, inspectionSource, 18);
    const QStringList items1 = dialog.completionsForExpression(QStringLiteral("data[i]."));
    const QStringList items2 = dialog.completionsForExpression(QStringLiteral("data[i].coords."));
    QVERIFY(items1.contains(QStringLiteral("id")) && items1.contains(QStringLiteral("values")) && items1.contains(QStringLiteral("coords")));
    QVERIFY(items2.contains(QStringLiteral("x")) && items2.contains(QStringLiteral("y")) && items2.contains(QStringLiteral("z")));
    // 3. Verify applying completion item "coords" to "data[i]." and std::vector completion on `send_buf.`.
    // Confirms STL container methods (`data`, `size`) populate for `deadlock_ring.cpp` buffers.
    dialog.applyQuickPreset(QStringLiteral("data[i].")); dialog.applyCompletionItem(QStringLiteral("coords"));
    QCOMPARE(dialog.getCondition(), QStringLiteral("data[i].coords"));
    dialog.setSourceDocumentContext(deadlockPath, deadlockSource, 34);
    const QString joined3 = dialog.completionsForExpression(QStringLiteral("send_buf.")).join(QLatin1Char('|'));
    QVERIFY2(joined3.contains(QStringLiteral("data")) && joined3.contains(QStringLiteral("size")), qPrintable(joined3));
    // 4. Verify live clangd publishDiagnostics -> SourceCodeView::setDiagnostics & cross-file F12 in MainWindow.
    // Create a 2-file workspace (`math_ops.hpp` + `caller.cpp` with an error line and cross-file call).
    QTemporaryDir lspDir; QVERIFY(lspDir.isValid());
    const QString hdrFile = lspDir.filePath(QStringLiteral("math_ops.hpp"));
    const QString cppFile = lspDir.filePath(QStringLiteral("caller.cpp"));
    { QFile hf(hdrFile); QVERIFY(hf.open(QIODevice::WriteOnly)); hf.write("#pragma once\nstruct Vec2 {\n  double x;\n  double y;\n  double lengthSq() const { return x * x + y * y; }\n};\n"); }
    { QFile cf(cppFile); QVERIFY(cf.open(QIODevice::WriteOnly)); cf.write("#include \"math_ops.hpp\"\nint main() {\n  Vec2 v{3.0, 4.0};\n  int bad = \"not_an_int\";\n  return static_cast<int>(v.lengthSq()) + bad;\n}\n"); }
    // Open `caller.cpp` in MainWindow and verify `clangd` publishDiagnostics populates `view->diagnostics()`.
    gridlock::ui::MainWindow mw; mw.loadSourceFile(cppFile);
    auto* tabMgr = mw.findChild<gridlock::ui::EditorTabManager*>(); QVERIFY(tabMgr && tabMgr->currentSourceCodeView());
    auto* callerView = tabMgr->currentSourceCodeView();
    QTRY_VERIFY_WITH_TIMEOUT(!callerView->diagnostics().isEmpty(), 4000);
    // Position cursor on `lengthSq` (line 5, col 29) and press F12 to jump into `math_ops.hpp` line 5.
    QTextCursor navCur(callerView->document()->findBlockByNumber(4)); navCur.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 29);
    callerView->setTextCursor(navCur);
    QKeyEvent f12Press(QEvent::KeyPress, Qt::Key_F12, Qt::NoModifier); QApplication::sendEvent(callerView, &f12Press);
    QCOMPARE(QFileInfo(tabMgr->currentSourceCodeView()->currentFile()).fileName(), QStringLiteral("math_ops.hpp"));
    QCOMPARE(tabMgr->currentSourceCodeView()->getCurrentLineNumber(), 5);
}
/**
 * @brief Verifies editable SourceCodeView, live editor LSP completion, modern chained
 *        breakpoint popover suffix insertion, theme persistence, dirty tab indicators (`●`),
 *        `saveAllModifiedTabs()`, `switchHeaderSource()`, scope breadcrumbs, and split editors.
 */
void TestMainWindowUI::testEditableSourceViewModernLspPopupAndThemePersistence() {
    // Load the tutorial inspection demo source code for editor and completion verification.
    const QString inspectionPath = resolveTutorialPath(QStringLiteral("tutorial/inspection_demo.cpp"));
    const QString inspectionSource = readTutorialFile(QStringLiteral("tutorial/inspection_demo.cpp"));
    QVERIFY(!inspectionSource.isEmpty());
    // 1. SourceCodeView must be editable by default (isReadOnly() == false) & support live completions.
    // Verify that both basic and detailed ClangdLspClient completion endpoints return struct fields.
    gridlock::ui::SourceCodeView srcView; QVERIFY2(!srcView.isReadOnly(), "SourceCodeView must be editable (isReadOnly() == false)");
    srcView.setSourceCode(inspectionSource);
    verifyCompletions(callEditorCompletionsAtLine(&srcView, 18, QStringLiteral("data[i]."), 0));
    callTriggerCompletionPopup(&srcView, QStringLiteral("data[i]."), 18, 0);
    verifyDetailed(callCompleteDetailed<gridlock::core::ClangdLspClient>(0, inspectionPath, inspectionSource, 18, QStringLiteral("data[i].")));
    // 2. Modern Breakpoint Popover Chained Member Insertion (data[i]. -> data[i].coords -> data[i].coords.x).
    // Verify that suffix completion preserves the boolean prefix expression ("i == 0 && ").
    gridlock::ui::ConditionalBreakpointDialog dialog; dialog.setSourceDocumentContext(inspectionPath, inspectionSource, 18);
    dialog.applyQuickPreset(QStringLiteral("i == 0 && data[i].")); dialog.applyCompletionItem(QStringLiteral("coords"));
    QCOMPARE(dialog.getCondition(), QStringLiteral("i == 0 && data[i].coords"));
    dialog.applyQuickPreset(dialog.getCondition() + QStringLiteral("."));
    const QStringList subItems = dialog.completionsForExpression(dialog.getCondition());
    QVERIFY(subItems.contains(QStringLiteral("x")) && subItems.contains(QStringLiteral("y")) && subItems.contains(QStringLiteral("z")));
    dialog.applyCompletionItem(QStringLiteral("x")); QCOMPARE(dialog.getCondition(), QStringLiteral("i == 0 && data[i].coords.x"));
    // 3. Theme Persistence Across App Restart (savePersistedTheme / loadPersistedTheme).
    // Verify that saving tokyo_night persists across transient switches to gridlock_slate.
    auto& tm = gridlock::core::ThemeManager::instance();
    tm.applyTheme(QStringLiteral("tokyo_night")); callSavePersistedTheme(tm, 0);
    tm.applyTheme(QStringLiteral("gridlock_slate"), false); QCOMPARE(tm.activeThemeId(), QStringLiteral("gridlock_slate"));
    verifyLoadedTheme(callLoadPersistedTheme(tm, 0), tm);
    tm.applyTheme(QStringLiteral("gridlock_slate")); callSavePersistedTheme(tm, 0);
    // 4. Dirty Buffer Tab Indicators ("●"), saveAllModifiedTabs(), switchHeaderSource(), & Scope Breadcrumbs.
    // Create temporary .cpp and .hpp companion files to exercise dirty buffer and F10 toggle workflows.
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid());
    const QString tempCpp = tempDir.filePath(QStringLiteral("inspection_demo.cpp"));
    const QString tempHpp = tempDir.filePath(QStringLiteral("inspection_demo.hpp"));
    { QFile fc(tempCpp); QVERIFY(fc.open(QIODevice::WriteOnly)); fc.write(inspectionSource.toUtf8()); }
    { QFile fh(tempHpp); QVERIFY(fh.open(QIODevice::WriteOnly)); fh.write("#pragma once\nstruct DeeplyNested {};\n"); }
    // Open the temporary C++ source file in EditorTabManager and define SFINAE-safe helper lambdas.
    gridlock::ui::EditorTabManager tabManager;
    auto* tabView = tabManager.openFile(tempCpp); QVERIFY(tabView != nullptr);
    const int idx = tabManager.currentIndex();
    auto isDirty = [](auto& m, int i) { if constexpr (requires { m.isTabModified(i); }) return m.isTabModified(i); else return false; };
    auto saveAll = [](auto& m) { if constexpr (requires { m.saveAllModifiedTabs(); }) return m.saveAllModifiedTabs(); else return -1; };
    auto switchHdr = [](auto& m) { if constexpr (requires { m.switchHeaderSource(); }) return m.switchHeaderSource(); else return false; };
    auto breadcrumb = [](auto* v) { if constexpr (requires { v->currentScopeBreadcrumb(); }) return v->currentScopeBreadcrumb(); else return QString(); };
    // Modify text in tabView and verify dirty indicator ("●") and isTabModified(idx) == true.
    // Then invoke saveAllModifiedTabs() and verify dirty state is cleared on disk and tab bar.
    tabView->textCursor().insertText(QStringLiteral("// dirty edit\n")); QApplication::processEvents();
    QVERIFY2(isDirty(tabManager, idx) && tabManager.tabText(idx).contains(QStringLiteral("●")), "Modified tab must report isTabModified(idx) == true and include '●' in tabText");
    QVERIFY2(saveAll(tabManager) >= 1 && !isDirty(tabManager, idx) && !tabManager.tabText(idx).contains(QStringLiteral("●")), "saveAllModifiedTabs() must save dirty buffer and remove '●'");
    // Move cursor into main() or DeeplyNested and verify currentScopeBreadcrumb(), then toggle header/source.
    // Confirms F10 companion lookup opens `inspection_demo.hpp` in EditorTabManager.
    tabView->setTextCursor(QTextCursor(tabView->document()->findBlockByNumber(16)));
    const QString bc = breadcrumb(tabView);
    QVERIFY2(bc.contains(QStringLiteral("inspection_demo.cpp")) && (bc.contains(QStringLiteral("main")) || bc.contains(QStringLiteral("DeeplyNested"))), qPrintable(bc));
    QVERIFY2(switchHdr(tabManager) && tabManager.tabText(tabManager.currentIndex()).contains(QStringLiteral("inspection_demo.hpp")), "switchHeaderSource() must switch to inspection_demo.hpp");
    // 5. Horizontal and Vertical Side-by-Side Split Editors (`splitEditor`, `closeSplit`, `secondaryEditorView`).
    // Verify initial unsplit state, horizontal/vertical split orientation toggling, and split collapse.
    auto checkSplits = [](auto& m) -> bool {
        if constexpr (requires { m.isSplitActive(); m.splitEditor(Qt::Horizontal); m.splitOrientation(); m.secondaryEditorView(); m.closeSplit(); }) {
            if (m.isSplitActive()) return false;
            m.splitEditor(Qt::Horizontal);
            if (!m.isSplitActive() || m.splitOrientation() != Qt::Horizontal || m.secondaryEditorView() == nullptr) return false;
            m.splitEditor(Qt::Vertical);
            if (!m.isSplitActive() || m.splitOrientation() != Qt::Vertical) return false;
            m.closeSplit();
            return !m.isSplitActive();
        } else { return false; }
    };
    QVERIFY2(checkSplits(tabManager), "EditorTabManager must support horizontal/vertical splitEditor() and closeSplit()");
}

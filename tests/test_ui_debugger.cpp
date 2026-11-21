/**
 * @file test_ui_debugger.cpp
 * @brief Integration tests for dynamic statement mapping, branch/loop stepping, and rank context.
 *
 * Verifies that source lines are resolved dynamically without hardcoded constants,
 * loop back-edges (jmp) and branch conditions (jg, je) are accurately evaluated per MPI rank,
 * and the active stepping rank context stays synchronized across views.
 */

#include "test_ui.hpp"

#include <QDir>
#include <QtWidgets>
#include <QtTest/QtTest>
#include <cmath>
#include <unistd.h>

#include "../src/core/hpc/GdbRankCoordinator.hpp"
#include "../src/core/hpc/IBackendCoordinator.hpp"
#include "../src/core/managers/ConfigManager.hpp"
#include "../src/core/managers/ThemeManager.hpp"
#include "../src/core/utils/ClangdLspClient.hpp"
#include "../src/ui/EditorTabManager.hpp"
#include "../src/ui/GdbConsoleWidget.hpp"
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/dialogs/ArrayPlotDialog.hpp"
#include "../src/ui/dialogs/ProjectSettingsDialog.hpp"
#include "../src/ui/dialogs/AttachProcessDialog.hpp"
#include "../src/ui/dialogs/CheckpointDialog.hpp"
#include "../src/ui/dialogs/CommandPaletteDialog.hpp"
#include "../src/ui/dialogs/ConditionalBreakpointDialog.hpp"
#include "../src/ui/dialogs/PointerGraphDialog.hpp"
#include "../src/ui/dialogs/PreferencesDialog.hpp"
#include "../src/ui/dialogs/ProgrammerCalculatorDialog.hpp"
#include "../src/ui/dialogs/SignalCatchpointDialog.hpp"
#include "../src/ui/docks/TerminalDockWidget.hpp"
#include "../src/ui/docks/VariablesDockWidget.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/views/MemView.hpp"
#include "../src/ui/views/RegisterView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
#include "../src/ui/views/SourceCodeView.hpp"
#include "../src/ui/widgets/HpcConsoleWidget.hpp"
#include "../src/ui/widgets/MpiDiagnosticsWidget.hpp"
#include "../src/ui/widgets/ProjectExplorerWidget.hpp"
#include "../src/ui/widgets/ReferenceManualWidget.hpp"
/** @brief Verifies dynamic statement mapping, loop back-edge/branch control-flow stepping, active rank context sync, live GDB disassembly CFG parsing, and default CFG Map mode. */
void TestMainWindowUI::testDynamicSourceMappingLoopBranchSteppingAndRankContext() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture();
    // Ensure tutorial/inspection_demo.cpp is loaded in SourceCodeView.
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp")) ? QStringLiteral("tutorial/inspection_demo.cpp") : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>(); auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>(); auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    QVERIFY(sourceView != nullptr && disasmView != nullptr && serverRack != nullptr);
    // Part 1: Dynamic Statement-Based Source Line Resolution (No Hardcoding).
    // Prepend 5 comment lines to shift all subsequent statement line numbers by 5.
    const QString originalCode = sourceView->toPlainText(), shiftedCode = QStringLiteral("// 1\n// 2\n// 3\n// 4\n// 5\n") + originalCode;
    sourceView->setPlainText(shiftedCode); mainWindow.rebuildDynamicSourceLineMap();
    const int initLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Init")), finalizeLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Finalize")), returnLine = mainWindow.findSourceLineForStatement(QStringLiteral("return 0"));
    // Statements must be dynamically located at shifted positions.
    QVERIFY2(initLine > 5 && finalizeLine > initLine && returnLine >= finalizeLine, "Dynamic scanner must locate shifted statements");
    // Dynamic bidirectional mapping must match instructions to shifted source lines.
    auto checkLine = [&](int l, uint64_t a) { QCOMPARE(mainWindow.sourceLineToAddress(l), a); QCOMPARE(mainWindow.addressToSourceLine(a), l); };
    checkLine(initLine, 0x0040113dULL); checkLine(finalizeLine, 0x00401178ULL); checkLine(returnLine, 0x00401183ULL);
    // Part 2 & 3: Active Rank Stepping Context and True Control-Flow Loop & Branch Stepping.
    QCOMPARE(mainWindow.activeSteppingRank(), 0);
    serverRack->selectRank(2); QApplication::processEvents(); QCOMPARE(mainWindow.activeSteppingRank(), 2);
    serverRack->selectRank(0); QApplication::processEvents(); QCOMPARE(mainWindow.activeSteppingRank(), 0);
    sourceView->setBreakpoints(QSet<int>()); mainWindow.stopTargetWorkflow(); mainWindow.startTargetWorkflow();
    QApplication::processEvents(); QCOMPARE(disasmView->currentAddress(), 0x00401120ULL);
    while (disasmView->currentAddress() < 0x00401149ULL) { mainWindow.stepInstructionWorkflow(); QApplication::processEvents(); }
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);
    auto stepTo = [&](uint64_t addr) { mainWindow.stepInstructionWorkflow(); QCOMPARE(disasmView->currentAddress(), addr); };
    stepTo(0x0040114dULL); stepTo(0x0040114fULL); stepTo(0x00401152ULL); stepTo(0x00401155ULL);
    stepTo(0x00401168ULL); stepTo(0x0040116aULL); stepTo(0x0040116eULL); stepTo(0x00401149ULL);
    for (int iter = 1; iter <= 2; ++iter) { for (int s = 0; s < 7; ++s) mainWindow.stepInstructionWorkflow(); QCOMPARE(disasmView->currentAddress(), 0x0040116eULL); stepTo(0x00401149ULL); }
    stepTo(0x0040114dULL); stepTo(0x00401178ULL);
    for (int s = 0; s < 3; ++s) mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401183ULL); QCOMPARE(sourceView->executionLine(), returnLine);
    mainWindow.stopTargetWorkflow(); serverRack->selectRank(2); mainWindow.startTargetWorkflow(); QApplication::processEvents();
    while (disasmView->currentAddress() < 0x00401155ULL) { mainWindow.stepInstructionWorkflow(); QApplication::processEvents(); }
    QCOMPARE(disasmView->currentAddress(), 0x00401155ULL);
    stepTo(0x00401157ULL); mainWindow.stepInstructionWorkflow(); stepTo(0x00401163ULL); stepTo(0x00401149ULL);
    // Part 4: Live GDB Disassembly CFG Basic-Block Splitting & Default CFG Map View Mode.
    auto& cfg = gridlock::core::ConfigManager::instance(); QCOMPARE(cfg.defaultDisassemblyMode(), QStringLiteral("cfg"));
    const QString liveGdbAsm = QStringLiteral("0x00400550 <main+0x0>: push rbp ; line 12\n0x00400554 <main+0x4>: mov rbp, rsp ; line 12\n0x00400560 <main+0x10>xcall 00400420 <MPI_Init@plt> ; line 14\n0x0040056b <main+0x1b>: cmp dword [rbp-0x4], 0x2 ; line 16\n0x0040056f <main+0x1f>: jg 00400658 ; line 16\n0x00400575 <main+0x25>: cmp dword [rbp-0x8], 0x0 ; line 17\n0x00400579 <main+0x29>: je 00400600 ; line 17\n0x0040057f <main+0x2f>: add dword [rbp-0x4], 0x1 ; line 18\n0x00400585 <main+0xc5>xjmp 0040056b ; line 18\n0x00400600 <main+0xb0>: xor eax, eax ; line 19\n0x00400602 <main+0x1aa>xjmp 00400650 ; line 16\n0x00400650 <main+0x100>: add dword [rbp-0x4], 0x1 ; line 20\n0x00400654 <main+0x104>: jmp 0040056b ; line 20\n0x00400658 <main+0x108>: leave ; line 22\n0x00400659 <main+0x109>: ret ; line 22");
    disasmView->setGraphMode(true); disasmView->updateDisassembly(liveGdbAsm); QApplication::processEvents();
    QVERIFY2(disasmView->basicBlockCount() >= 4 && disasmView->cfgEdgeCount() >= 3, qPrintable(QStringLiteral("CFG collapsed: BB=%1 E=%2").arg(disasmView->basicBlockCount()).arg(disasmView->cfgEdgeCount())));
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
        const QString longTmpl = QStringLiteral("gridlock::Solver::Solve(std::vector<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> >, std::allocator<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > > > const&, std::map<int, std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > > const&, double)");
        QVERIFY(longTmpl.size() >= 320);
        const QString longCppAsm = QStringLiteral("0x003ce970 <%1+0x0>: push rbp ; line 340\n0x003ce971 <%1+0x1>: mov rbp, rsp ; line 340\n0x003ce975 <std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char> > const&)>call 0x003d2d3c ; line 345\n0x003ce97a <%1+0xa>: test al, al ; line 346\n0x003ce97c <%1+0xc>: je 0x003ce990 ; line 346\n0x003ce97e <%1+0xe>: call 0x003d2e00 <%1> ; line 348\n0x003ce983 <%1+0x13>: jmp 0x003ce998 ; line 349\n0x003ce990 <%1+0x20>: nop ; line 352\n0x003ce998 <%1+0x28>: pop rbp ; line 355\n0x003ce999 <%1+0x29>: ret ; line 356").arg(longTmpl);
        cfgPane.updateDisassembly(longCppAsm); cfgPane.seekToAddress(0x003ce975ULL); QApplication::processEvents();
        auto* cfgView = cfgPane.findChild<QGraphicsView*>(QStringLiteral("cutterCfgGraphView")); QVERIFY(cfgView && cfgView->scene() && cfgPane.basicBlockCount() >= 3);
        QRectF targetRect, leftmostRect;
        for (QGraphicsItem* item : cfgView->scene()->items()) {
            if (qAbs(item->zValue() - 1.0) < 0.05) { const QRectF br = item->sceneBoundingRect(); QVERIFY2(br.width() <= 960.0, qPrintable(QStringLiteral("CFG block width %1 > 960").arg(br.width()))); if (item->data(Qt::UserRole).toULongLong() == 0x003ce970ULL) targetRect = br; if (leftmostRect.isNull() || br.left() < leftmostRect.left()) leftmostRect = br; }
            if (auto* txt = dynamic_cast<QGraphicsSimpleTextItem*>(item)) QVERIFY2(!txt->text().contains(QStringLiteral(")>call")), qPrintable(txt->text()));
        }
        QVERIFY(!targetRect.isNull() && !leftmostRect.isNull() && cfgView->mapFromScene(targetRect.topLeft()).x() >= 0);
        auto* hbar = cfgView->horizontalScrollBar(); QVERIFY(hbar && hbar->maximum() > hbar->minimum()); hbar->setValue(hbar->minimum()); QApplication::processEvents();
        QVERIFY(cfgView->mapFromScene(leftmostRect.topLeft()).x() >= 0);
        QTest::keyClick(cfgView, Qt::Key_Right); QTest::keyClick(cfgView, Qt::Key_Home); QApplication::processEvents(); QVERIFY(cfgView->mapFromScene(targetRect.topLeft()).x() >= 0);
        QCOMPARE(cfgView->cursor().shape(), Qt::ArrowCursor); QCOMPARE(cfgView->viewport()->cursor().shape(), Qt::ArrowCursor);
        const QPoint midPt = cfgView->viewport()->rect().center();
        QTest::mousePress(cfgView->viewport(), Qt::MiddleButton, Qt::NoModifier, midPt); QTest::mouseMove(cfgView->viewport(), midPt + QPoint(25, 10)); QTest::mouseRelease(cfgView->viewport(), Qt::MiddleButton, Qt::NoModifier, midPt + QPoint(25, 10));
        QApplication::processEvents(); QCOMPARE(cfgView->cursor().shape(), Qt::ArrowCursor); QCOMPARE(cfgView->viewport()->cursor().shape(), Qt::ArrowCursor);
    }
}

/** @brief Verifies Seek in Memory View tab switching, base address update, and mnemonic F1 doc lookup. */
void TestMainWindowUI::testSeekInMemoryViewAndMnemonicDocLookup() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture(); mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>(); auto* memView = mainWindow.findChild<gridlock::ui::MemView*>(); auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs")); auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY(disasmView && memView && bottomTabs && refManual); bottomTabs->setCurrentIndex(0); QApplication::processEvents(); QVERIFY(bottomTabs->currentWidget() != memView);
    QVERIFY(disasmView->triggerContextAction(0x00401149ULL, QStringLiteral("Seek in Memory View"))); QApplication::processEvents(); QVERIFY(bottomTabs->currentWidget() == memView && memView->currentBaseAddress() == 0x00401149ULL);
    auto* dumpEdit = memView->findChild<QTextEdit*>(QStringLiteral("cutterHexDumpEdit")); QVERIFY(dumpEdit && dumpEdit->toPlainText().contains(QStringLiteral("83 7d fc 02"), Qt::CaseInsensitive));
    mainWindow.seekToAddressOrExpression(QStringLiteral("0x00402010")); QApplication::processEvents(); QCOMPARE(memView->currentBaseAddress(), 0x00402010ULL);
    QVERIFY(disasmView->triggerContextAction(0x00401125ULL, QStringLiteral("Lookup in Reference Manual (F1)"))); QApplication::processEvents(); QCOMPARE(bottomTabs->currentWidget(), refManual);
    auto* docBrowser = refManual->findChild<QTextBrowser*>(QStringLiteral("docsetTextBrowser")); QVERIFY(docBrowser != nullptr); const QString docContent = docBrowser->toHtml();
    QVERIFY2(docContent.contains(QStringLiteral("MOV"), Qt::CaseInsensitive) && !docContent.contains(QStringLiteral("MPI_Init")) && !docContent.contains(QStringLiteral("MPI-3.1 Standard")), "Must display MOV instruction documentation");
}
/** @brief Verifies smart Meson/CMake/Make/SingleFileMPI build system detection and Terminal build/clean/rebuild workflow. */
void TestMainWindowUI::testSmartBuildSystemDetectionAndTerminalBuildCleanWorkflow() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture(); mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    const auto writeText = [](const QString& path, const QByteArray& bytes) { QFile f(path); if (f.open(QIODevice::WriteOnly)) { f.write(bytes); f.close(); } };
    { QTemporaryDir d; QVERIFY(d.isValid()); writeText(d.filePath(QStringLiteral("meson.build")), "project('demo', 'cpp')\n"); QDir(d.path()).mkpath(QStringLiteral("builddir")); writeText(d.filePath(QStringLiteral("builddir/build.ninja")), "# ninja\n"); QVERIFY(mainWindow.detectBuildSystem(d.path()) == QStringLiteral("Meson") && mainWindow.buildCommandForPath(d.path()).contains(QStringLiteral("ninja -C")) && mainWindow.cleanCommandForPath(d.path()).contains(QStringLiteral("clean"))); }
    { QTemporaryDir d; QVERIFY(d.isValid()); writeText(d.filePath(QStringLiteral("CMakeLists.txt")), "cmake_minimum_required(VERSION 3.20)\nproject(demo)\n"); QVERIFY(mainWindow.detectBuildSystem(d.path()) == QStringLiteral("CMake") && mainWindow.buildCommandForPath(d.path()).contains(QStringLiteral("cmake --build")) && mainWindow.cleanCommandForPath(d.path()).contains(QStringLiteral("clean"))); }
    { QTemporaryDir d; QVERIFY(d.isValid()); writeText(d.filePath(QStringLiteral("Makefile")), "all:\n\t@echo building\nclean:\n\t@echo cleaning\n"); QVERIFY(mainWindow.detectBuildSystem(d.path()) == QStringLiteral("Make") && mainWindow.buildCommandForPath(d.path()).contains(QStringLiteral("make")) && mainWindow.cleanCommandForPath(d.path()).contains(QStringLiteral("make clean"))); }
    QTemporaryDir singleFileDir; QVERIFY(singleFileDir.isValid()); const QString demoSourcePath = singleFileDir.filePath(QStringLiteral("demo.cpp"));
    writeText(demoSourcePath, "#include <mpi.h>\nint main(int argc, char** argv) { return 0; }\n");
    QVERIFY(mainWindow.detectBuildSystem(demoSourcePath) == QStringLiteral("SingleFileMPI") && mainWindow.buildCommandForPath(demoSourcePath).contains(QStringLiteral("-g")) && mainWindow.buildCommandForPath(demoSourcePath).contains(QStringLiteral("-o")) && mainWindow.cleanCommandForPath(demoSourcePath).contains(QStringLiteral("rm -f")));
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs")); auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>(); auto* statusLabel = mainWindow.findChild<QLabel*>(QStringLiteral("cutterStatusTelemetry"));
    QVERIFY(bottomTabs && terminalDock && statusLabel); mainWindow.loadSourceFile(demoSourcePath);
    for (int i = 0; i < bottomTabs->count(); ++i) if (bottomTabs->widget(i) != terminalDock) { bottomTabs->setCurrentIndex(i); break; }
    QVERIFY(bottomTabs->currentWidget() != terminalDock && mainWindow.buildTargetWorkflow()); QApplication::processEvents();
    QVERIFY(bottomTabs->currentWidget() == terminalDock && terminalDock->lastSentCommand().contains(QStringLiteral("-g")) && statusLabel->text().contains(QStringLiteral("BUILDING"), Qt::CaseInsensitive));
    QVERIFY(mainWindow.cleanTargetWorkflow()); QApplication::processEvents(); QVERIFY(bottomTabs->currentWidget() == terminalDock && (terminalDock->lastSentCommand().contains(QStringLiteral("rm -f")) || terminalDock->lastSentCommand().contains(QStringLiteral("clean"))) && statusLabel->text().contains(QStringLiteral("CLEANING"), Qt::CaseInsensitive));
    QVERIFY(mainWindow.rebuildTargetWorkflow()); QApplication::processEvents(); QVERIFY(bottomTabs->currentWidget() == terminalDock && terminalDock->lastSentCommand().contains(QStringLiteral("&&")));
}
/** @brief Verifies TerminalDockWidget eventFilter font/geometry lock during PreferencesDialog, Light Mode contrast, and ReferenceManualWidget docset auto-preview. */
void TestMainWindowUI::testTerminalDialogEventFilterLightModeContrastAndDocsetAutoDetect() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.loadPreviewFixture(); mainWindow.show(); QVERIFY(QTest::qWaitForWindowExposed(&mainWindow)); QApplication::processEvents();
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs")); auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>(); auto* explorer = mainWindow.findChild<gridlock::ui::ProjectExplorerWidget*>(); auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY(bottomTabs && terminalDock && explorer && refManual); bottomTabs->setCurrentWidget(terminalDock); QApplication::processEvents();
    gridlock::ui::PreferencesDialog prefsDialog(&mainWindow); prefsDialog.show(); QApplication::processEvents();
    QFont proportionalFont(QStringLiteral("Cantarell"), 11); proportionalFont.setFixedPitch(false);
    for (QWidget* child : terminalDock->findChildren<QWidget*>()) { child->setFont(proportionalFont); QEvent fe(QEvent::FontChange); QApplication::sendEvent(child, &fe); QEvent se(QEvent::StyleChange); QApplication::sendEvent(child, &se); }
    QApplication::processEvents(); QVERIFY2(terminalDock->areAllChildWidgetsFixedPitch(), "TerminalDockWidget eventFilter must restore fixed-pitch font");
    for (QWidget* child : terminalDock->findChildren<QWidget*>()) if (child->isVisible() && child->width() > 80) QVERIFY2(child->width() >= (terminalDock->width() * 95) / 100, qPrintable(QStringLiteral("Child %1 shrunk to %2px vs %3px").arg(child->metaObject()->className()).arg(child->width()).arg(terminalDock->width())));
    prefsDialog.close(); gridlock::core::ThemeManager::instance().setDarkMode(false); explorer->setSymbols({{QStringLiteral("sym.main"), 0x00401120ULL, 64}}); QApplication::processEvents();
    auto* symTable = explorer->findChild<QTableWidget*>(); QVERIFY(symTable && symTable->rowCount() > 0 && symTable->item(0, 0));
    const auto luminance = [](const QColor& c) { return (299 * c.red() + 587 * c.green() + 114 * c.blue()) / 1000; };
    QVERIFY(std::abs(luminance(symTable->item(0, 0)->foreground().color()) - luminance(symTable->palette().color(QPalette::Base))) >= 80);
    gridlock::core::ThemeManager::instance().setDarkMode(true); QApplication::processEvents();
    bottomTabs->setCurrentWidget(refManual); refManual->lookupSymbol(QStringLiteral("MPI_Init")); QApplication::processEvents();
    auto* docCombo = refManual->findChild<QComboBox*>(); auto* docBrowser = refManual->findChild<QTextBrowser*>(QStringLiteral("docsetTextBrowser"));
    QVERIFY(docCombo && docBrowser && docBrowser->toHtml().contains(QStringLiteral("MPI_Init")));
    int targetIdx = -1; for (int i = 0; i < docCombo->count(); ++i) { const QString l = docCombo->itemText(i); if (!l.contains(QStringLiteral("All"), Qt::CaseInsensitive) && !l.contains(QStringLiteral("MPI"), Qt::CaseInsensitive)) { targetIdx = i; break; } }
    if (targetIdx >= 0) { docCombo->setCurrentIndex(targetIdx); QApplication::processEvents(); QVERIFY2(!docBrowser->toHtml().contains(QStringLiteral("MPI_Init(&argc")), "Switching docset category must auto-preview first entry"); }
}

/** @brief Verifies selecting a target in `#toolbarTargetSelector` switches SourceCodeView, DisassemblyView, and Build/Clean target paths. */
void TestMainWindowUI::testTargetSelectorSwitchesSourceDisassemblyAndBuildCommands() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.show(); QApplication::processEvents();
    auto* targetCombo = mainWindow.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector")); auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>(); auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(targetCombo && terminalDock && disasmView && mainWindow.selectTargetByName(QStringLiteral("deadlock_ring"))); QApplication::processEvents();
    QVERIFY(mainWindow.activeTargetSourcePath().endsWith(QStringLiteral("deadlock_ring.cpp")) && mainWindow.activeTargetBinaryPath().endsWith(QStringLiteral("deadlock_ring")));
    auto* editorTabs = mainWindow.findChild<gridlock::ui::EditorTabManager*>(); QVERIFY(editorTabs && editorTabs->currentSourceCodeView());
    const QString editorText = editorTabs->currentSourceCodeView()->toPlainText(), decompText = disasmView->toPlainText() + QLatin1Char('\n') + disasmView->decompiledPseudocode();
    QVERIFY(editorText.contains(QStringLiteral("MPI_Send")) && editorText.contains(QStringLiteral("MPI_Recv")) && (decompText.contains(QStringLiteral("MPI_Send")) || decompText.contains(QStringLiteral("MPI_Recv"))));
    mainWindow.cleanTargetWorkflow(); QApplication::processEvents();
    QVERIFY(terminalDock->lastSentCommand().contains(QStringLiteral("deadlock_ring")) && !terminalDock->lastSentCommand().contains(QStringLiteral("inspection_demo")));
    mainWindow.buildTargetWorkflow(); QApplication::processEvents(); const QString buildCmd = terminalDock->lastSentCommand();
    QVERIFY(buildCmd.contains(QStringLiteral("deadlock_ring.cpp")) && buildCmd.contains(QStringLiteral("-o")) && buildCmd.contains(QStringLiteral("deadlock_ring")) && !buildCmd.contains(QStringLiteral("inspection_demo.cpp")));
    QVERIFY(mainWindow.selectTargetByName(QStringLiteral("inspection_demo"))); QApplication::processEvents();
    QVERIFY(mainWindow.activeTargetSourcePath().endsWith(QStringLiteral("inspection_demo.cpp")));
    mainWindow.buildTargetWorkflow(); QApplication::processEvents(); QVERIFY(terminalDock->lastSentCommand().contains(QStringLiteral("inspection_demo.cpp")));
}
/** @brief Verifies Edit menu actions, dynamic UI/Code font scaling across QSS and editors, and 6 curated themes with human-readable titles. */
void TestMainWindowUI::testEditMenuActionsDynamicFontScalingAndCuratedThemes() {
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.show(); QApplication::processEvents(); mainWindow.selectTargetByName(QStringLiteral("inspection_demo")); QApplication::processEvents();
    auto* editorTabs = mainWindow.findChild<gridlock::ui::EditorTabManager*>(); auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(editorTabs && disasmView && editorTabs->currentSourceCodeView()); auto* srcView = editorTabs->currentSourceCodeView();
    srcView->setFocus(); QVERIFY(mainWindow.triggerEditMenuAction(QStringLiteral("Select All")) && mainWindow.triggerEditMenuAction(QStringLiteral("Copy")));
    QTextCursor tc = srcView->textCursor(); tc.movePosition(QTextCursor::Start); srcView->setTextCursor(tc); const QString origFirstLine = srcView->document()->firstBlock().text();
    QVERIFY(mainWindow.triggerEditMenuAction(QStringLiteral("Toggle Comment")) && srcView->document()->firstBlock().text() != origFirstLine && mainWindow.triggerEditMenuAction(QStringLiteral("Toggle Comment")) && srcView->document()->firstBlock().text().trimmed() == origFirstLine.trimmed());
    mainWindow.applyTypographySettings(QStringLiteral("Monospace"), 15, QStringLiteral("Monospace"), 16); QApplication::processEvents();
    const QString activeQss = gridlock::core::ThemeManager::instance().currentStyleSheet();
    QVERIFY((activeQss.contains(QStringLiteral("15pt")) || activeQss.contains(QStringLiteral("15px"))) && srcView->font().pointSize() == 16 && disasmView->codeFontPointSize() == 16);
    mainWindow.applyTypographySettings(QStringLiteral("Inter"), 11, QStringLiteral("JetBrains Mono"), 10); QApplication::processEvents();
    QVERIFY(gridlock::core::ThemeManager::instance().availableThemeIds().size() >= 6); gridlock::ui::PreferencesDialog prefsDlg(&mainWindow);
    auto* themeCombo = prefsDlg.findChild<QComboBox*>(QStringLiteral("themeSelectorCombo")); QVERIFY(themeCombo && themeCombo->count() >= 6);
    for (int i = 0; i < themeCombo->count(); ++i) QVERIFY2(!themeCombo->itemText(i).contains(QLatin1Char('_')), qPrintable(themeCombo->itemText(i)));
    struct ThemeShotEntry { QString themeId; QString fileName; };
    const std::vector<ThemeShotEntry> shots = {{QStringLiteral("gridlock_slate"), QStringLiteral("theme_gridlock_slate.png")}, {QStringLiteral("dark_teal"), QStringLiteral("theme_obsidian_teal.png")}, {QStringLiteral("tokyo_night"), QStringLiteral("theme_tokyo_night.png")}, {QStringLiteral("catppuccin_mocha"), QStringLiteral("theme_catppuccin_mocha.png")}, {QStringLiteral("gruvbox_dark"), QStringLiteral("theme_gruvbox_dark.png")}, {QStringLiteral("daylight_light"), QStringLiteral("theme_daylight_light.png")}};
    QTemporaryDir tempThemeShotDir; QVERIFY(tempThemeShotDir.isValid());
    for (const auto& shot : shots) {
        gridlock::core::ThemeManager::instance().setTheme(shot.themeId, !shot.themeId.contains(QStringLiteral("light")), false);
        srcView->reloadStyle(); disasmView->reloadStyle(); QApplication::processEvents();
        const QString outPath = tempThemeShotDir.path() + QLatin1Char('/') + shot.fileName;
        QVERIFY2(mainWindow.captureWorkspaceSnapshot(outPath) && QFileInfo::exists(outPath) && QFileInfo(outPath).size() > 1024, qPrintable(outPath));
    }
    gridlock::core::ThemeManager::instance().setTheme(QStringLiteral("gridlock_slate"), true, false); srcView->reloadStyle(); disasmView->reloadStyle(); QApplication::processEvents();
}
/** @brief Verifies two-lane vector gutter geometry, distinct breakpoint/conditional/PC marker states, and non-overlapping antialiased vector badge rendering in SourceCodeView. */
void TestMainWindowUI::testModernVectorGutterIconsAndCombinedBreakpointPcState() {
    gridlock::ui::SourceCodeView srcView; QString sampleCode;
    for (int i = 1; i <= 90; ++i) sampleCode += QStringLiteral("    int mpi_rank_step_%1 = %1; // source line %1\n").arg(i);
    srcView.setSourceCode(sampleCode); srcView.resize(720, 560); srcView.show(); QApplication::processEvents();
    QVERIFY(srcView.lineNumberAreaWidth() >= 48 && srcView.gutterIconLaneWidth() >= 18 && srcView.gutterMarkerStateForLine(1) == QStringLiteral("None"));
    srcView.toggleBreakpoint(15); srcView.setConditionalBreakpoint(16, QStringLiteral("rank == 0")); srcView.highlightExecutionLine(15); QApplication::processEvents();
    QVERIFY(srcView.gutterMarkerStateForLine(15) == QStringLiteral("BreakpointAndExecutionPc") && srcView.gutterMarkerStateForLine(16) == QStringLiteral("ConditionalBreakpoint"));
    srcView.highlightExecutionLine(13); QApplication::processEvents();
    QVERIFY(srcView.gutterMarkerStateForLine(13) == QStringLiteral("ExecutionPc") && srcView.gutterMarkerStateForLine(15) == QStringLiteral("Breakpoint"));
    srcView.setRankBreakpoint(82, -1); srcView.setRankBreakpoint(85, 1); srcView.setRankBreakpoint(88, 2);
    QVERIFY(srcView.breakpointColorForLine(82) == QColor(QStringLiteral("#ef4444")) && srcView.breakpointHasWhiteOutlineForLine(82) && srcView.breakpointRankFilterForLine(82) == -1 && srcView.breakpointRankFilterForLine(85) == 1 && srcView.breakpointRankFilterForLine(88) == 2);
    QVERIFY(srcView.breakpointColorForLine(85) != srcView.breakpointColorForLine(82) && srcView.breakpointColorForLine(88) != srcView.breakpointColorForLine(82) && srcView.breakpointColorForLine(85) != srcView.breakpointColorForLine(88));
    srcView.highlightExecutionLine(15); QApplication::processEvents(); const QImage img = srcView.grab().toImage(); QVERIFY(!img.isNull());
    const QColor gutterBg(0x16, 0x18, 0x1d); bool foundBadgePixelInIconLane = false;
    for (int y = 0; y < img.height() && !foundBadgePixelInIconLane; ++y)
        for (int x = 3; x <= 16 && x < img.width(); ++x) { const QColor px = img.pixelColor(x, y); if (std::abs(px.red() - gutterBg.red()) > 35 || std::abs(px.green() - gutterBg.green()) > 35) { foundBadgePixelInIconLane = true; break; } }
    QVERIFY2(foundBadgePixelInIconLane, "Expected vector badge pixels inside left icon lane (x = 3..16)");
}

/** @brief Verifies live coordinator engine switching, real per-rank OS PIDs, live /proc enumeration, cross-rank MemView DIFF RANKS, and clean session teardown. */
void TestMainWindowUI::testRealCompiledTargetDwarfMappingBreakpointRunAndLatchedToolbar() {
    auto& cfg = gridlock::core::ConfigManager::instance();
    cfg.setDebuggerEngine(QStringLiteral("gdb")); cfg.setGlobalMpiArgs(QStringLiteral("--oversubscribe"));
    gridlock::ui::MainWindow mainWindow; mainWindow.resize(1440, 900); mainWindow.show(); QApplication::processEvents();
    // 1. Dynamic coordinator switching: ConfigManager::setDebuggerEngine switches MainWindow's live backend.
    QVERIFY(mainWindow.coordinator() && QString::fromLatin1(mainWindow.coordinator()->metaObject()->className()).contains(QStringLiteral("GdbRankCoordinator")));
    cfg.setDebuggerEngine(QStringLiteral("lldb-dap")); QApplication::processEvents();
    QVERIFY(mainWindow.coordinator() && QString::fromLatin1(mainWindow.coordinator()->metaObject()->className()).contains(QStringLiteral("DapCoordinator")));
    cfg.setDebuggerEngine(QStringLiteral("gdb")); QApplication::processEvents();
    QVERIFY(mainWindow.coordinator() && QString::fromLatin1(mainWindow.coordinator()->metaObject()->className()).contains(QStringLiteral("GdbRankCoordinator")));
    // 2. Verify File -> Attach to Process... (Ctrl+Alt+P) and AttachProcessDialog /proc + MPI job grouping.
    auto* attachAct = mainWindow.findChild<QAction*>(QStringLiteral("actionAttachProcess")); QVERIFY2(attachAct && attachAct->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P), "MainWindow must expose #actionAttachProcess with Ctrl+Alt+P shortcut");
    QProcess helperProc; struct ProcGuard { QProcess* p; ~ProcGuard() { if (p && p->state() != QProcess::NotRunning) { p->kill(); p->waitForFinished(1000); } } } guard{&helperProc};
    helperProc.start(QStringLiteral("sleep"), {QStringLiteral("30")}); QVERIFY(helperProc.waitForStarted(3000));
    const qint64 helperPid = helperProc.processId(); gridlock::ui::AttachProcessDialog attachDlg(&mainWindow);
    auto* procTable = attachDlg.findChild<QTableWidget*>(QStringLiteral("attachProcessTable")); auto* procTree = attachDlg.findChild<QTreeWidget*>(QStringLiteral("attachProcessTree"));
    auto* filterEd = attachDlg.findChild<QLineEdit*>(QStringLiteral("attachProcessFilterEdit")); auto* myProcChk = attachDlg.findChild<QCheckBox*>(QStringLiteral("attachMyProcessesOnlyCheck"));
    auto* ptraceBanner = attachDlg.findChild<QWidget*>(QStringLiteral("attachPtraceWarningBanner")); auto* attachBtn = attachDlg.findChild<QPushButton*>(QStringLiteral("attachSelectedBtn"));
    QVERIFY(procTable && procTable->rowCount() > 0 && procTree && filterEd && myProcChk && myProcChk->isChecked() && ptraceBanner && attachBtn);
    int helperRow = -1;
    for (int r = 0; r < procTable->rowCount(); ++r) {
        for (int c = 0; c < procTable->columnCount(); ++c) QVERIFY2(!(procTable->item(r, c) && procTable->item(r, c)->text().contains(QStringLiteral("mpi_navier_stokes"), Qt::CaseInsensitive)), "Hardcoded mpi_navier_stokes row still present in AttachProcessDialog");
        if (procTable->item(r, 0) && procTable->item(r, 0)->text().toLongLong() == helperPid) helperRow = r;
    }
    QVERIFY2(helperRow >= 0, "AttachProcessDialog must include live child helper PID from /proc");
    procTable->selectRow(helperRow); QCOMPARE(attachDlg.selectedPid(), helperPid);
    QTemporaryDir fakeProcDir; QVERIFY(fakeProcDir.isValid()); const uint curUid = static_cast<uint>(::geteuid());
    auto mkFakePid = [&](int pid, int rank) {
        const QString d = fakeProcDir.filePath(QString::number(pid)); QVERIFY(QDir().mkpath(d));
        { QFile f(d + "/comm"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("deadlock_ring\n"); }
        { QFile f(d + "/status"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QStringLiteral("Name:\tdeadlock_ring\nPPid:\t4000\nUid:\t%1\t%1\t%1\t%1\n").arg(curUid).toUtf8()); }
        { QFile f(d + "/cmdline"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("./deadlock_ring"); }
        if (rank >= 0) { QFile f(d + "/environ"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(QStringLiteral("OMPI_COMM_WORLD_RANK=%1\nOMPI_COMM_WORLD_SIZE=2\nOMPI_MCA_ess_base_jobid=job42\n").arg(rank).toUtf8()); }
    };
    mkFakePid(4102, 1); mkFakePid(4101, 0); mkFakePid(4999, -1);
    const QString fakeSysctl = fakeProcDir.filePath(QStringLiteral("ptrace_scope")); { QFile f(fakeSysctl); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("1\n"); }
    attachDlg.setProcPaths(fakeProcDir.path(), fakeSysctl); QCOMPARE(attachDlg.mpiJobGroupCount(), 1); QCOMPARE(attachDlg.ptraceScope(), 1); QVERIFY(!ptraceBanner->isHidden());
    procTree->setCurrentItem(procTree->topLevelItem(0)); QCOMPARE(attachDlg.selectedPids(), (QList<qint64>{4101, 4102}));
    filterEd->setText(QStringLiteral("4999")); QVERIFY(procTree->topLevelItem(0)->isHidden() && !procTree->topLevelItem(1)->isHidden());
    auto* tabW = attachDlg.findChild<QTabWidget*>(QStringLiteral("attachModeTabWidget")); auto* remEpEd = attachDlg.findChild<QLineEdit*>(QStringLiteral("remoteEndpointEdit"));
    auto* remSymEd = attachDlg.findChild<QLineEdit*>(QStringLiteral("remoteSymbolBinaryEdit")); auto* remSymBtn = attachDlg.findChild<QPushButton*>(QStringLiteral("remoteSymbolBrowseBtn"));
    auto* remGdbCombo = attachDlg.findChild<QComboBox*>(QStringLiteral("remoteGdbBinaryCombo")); auto* remProtoCombo = attachDlg.findChild<QComboBox*>(QStringLiteral("remoteProtocolModeCombo"));
    auto* remHintLbl = attachDlg.findChild<QLabel*>(QStringLiteral("remoteCommandHintLabel"));
    QVERIFY(tabW && tabW->count() == 2 && remEpEd && remSymEd && remSymBtn && remGdbCombo && remProtoCombo && remHintLbl);
    attachDlg.setRemoteTargetTabActive(true); QVERIFY(attachDlg.isRemoteTargetTabActive());
    remEpEd->setText(QStringLiteral("localhost:1234..1237")); remSymEd->setText(QStringLiteral("/tmp/qemu_guest.elf")); remProtoCombo->setCurrentIndex(1);
    const auto remTargets = attachDlg.selectedRemoteTargets(); QVERIFY(remTargets.size() == 4 && remTargets[0].endpoint == QStringLiteral("localhost:1234") && remTargets[3].endpoint == QStringLiteral("localhost:1237") && remTargets[0].extendedRemote && attachBtn->isEnabled());
    remEpEd->setText(QStringLiteral("localhost:99999")); QVERIFY(attachDlg.selectedRemoteTargets().isEmpty() && !attachBtn->isEnabled()); QVERIFY(mainWindow.attachToRemoteTargets({remTargets[0]})); mainWindow.stopTargetWorkflow();
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
      nf.write("#include <mpi.h>\nint watchedCounter = 10;\nclass CellMesh { protected: int protDim = 9; public: int dims = 3; };\nstruct SubdomainBlock { CellMesh mesh; double tol = 1e-6; };\n"
               "__attribute__((noinline)) int innerCallee(int innerVal) {\n    int innerLocal = innerVal + 7;\n    int stateVal = 111;\n    stateVal = 999;\n    return innerLocal + stateVal;\n}\n"
               "__attribute__((noinline)) int outerCaller(int outerSecret) {\n    SubdomainBlock domain_cfg{};\n    int outerLocal = outerSecret + (domain_cfg.mesh.dims > 0 ? 1 : 0) + (domain_cfg.tol > 0.0 ? 0 : 1);\n    int res = innerCallee(outerLocal);\n    return res;\n}\n"
               "__attribute__((noinline)) int loopProbe(int step) {\n    int v = step * 10;\n    return v;\n}\n"
               "int main(int argc, char** argv) {\n    MPI_Init(&argc, &argv);\n    int total = outerCaller(4242);\n    for (int step = 1; step <= 4; ++step) total += loopProbe(step);\n    watchedCounter = 77;\n    MPI_Finalize();\n    return (total + watchedCounter) == 0 ? 1 : 0;\n}\n"); }
    rankSpin->setValue(1); targetCombo->insertItem(0, QStringLiteral("nested_stack"), nestedCpp);
    QVERIFY(mainWindow.selectTargetByName(QStringLiteral("nested_stack")) && mainWindow.ensureTargetBinaryCompiled());
    auto* nestedSrcView = editorTabs->currentSourceCodeView(); QVERIFY(nestedSrcView != nullptr);
    const int revMutLine = mainWindow.findSourceLineForStatement(QStringLiteral("stateVal = 999")), innerBpLine = mainWindow.findSourceLineForStatement(QStringLiteral("return innerLocal")), outerCallLine = mainWindow.findSourceLineForStatement(QStringLiteral("innerCallee(outerLocal)")), cfgLine = mainWindow.findSourceLineForStatement(QStringLiteral("domain_cfg.mesh.dims"));
    const int logLine = mainWindow.findSourceLineForStatement(QStringLiteral("int v = step * 10")), ignLine = mainWindow.findSourceLineForStatement(QStringLiteral("return v"));
    QVERIFY(revMutLine > 0 && innerBpLine > revMutLine && outerCallLine > innerBpLine && cfgLine > 0 && logLine > 0 && ignLine > logLine); nestedSrcView->toggleBreakpoint(revMutLine);
    mainWindow.startTargetWorkflow(); QTRY_COMPARE_WITH_TIMEOUT(nestedSrcView->executionLine(), revMutLine, 8000);
    auto* varsTree = varsDock->findChild<QTreeView*>(); auto* varSearch = varsDock->findChild<QLineEdit*>(QStringLiteral("variableSearchEdit"));
    QVERIFY2(varsTree != nullptr && varsTree->model() != nullptr && varSearch != nullptr && varsTree->isSortingEnabled() && varsTree->header()->sectionResizeMode(0) == QHeaderView::Interactive, "VariablesDockWidget must expose #variableSearchEdit, sortable headers, and interactive column resizing");
    auto findVarIdx = [&](const QModelIndex& parent, const QString& name) { for (int r = 0; r < varsTree->model()->rowCount(parent); ++r) { const QModelIndex i = varsTree->model()->index(r, 0, parent); if (varsTree->model()->data(i).toString() == name) return i; } return QModelIndex(); };
    // Verify Reverse Debugging toolbar controls (#toolbarRecordHistoryBtn, #toolbarStepBackBtn, #toolbarReverseContinueBtn) and live GDB time-travel.
    auto* recBtn = mainWindow.findChild<QPushButton*>(QStringLiteral("toolbarRecordHistoryBtn")), *stepBackBtn = mainWindow.findChild<QPushButton*>(QStringLiteral("toolbarStepBackBtn")), *revContBtn = mainWindow.findChild<QPushButton*>(QStringLiteral("toolbarReverseContinueBtn"));
    QVERIFY(recBtn && stepBackBtn && revContBtn); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("stateVal")).isValid() && varsTree->model()->data(findVarIdx({}, QStringLiteral("stateVal")).siblingAtColumn(1)).toString().contains(QStringLiteral("111")), 5000);
    recBtn->click(); QVERIFY(mainWindow.coordinator()->isReverseRecordingEnabled(0)); QTest::qWait(150);
    QVERIFY(mainWindow.stepOverWorkflow()); QTRY_COMPARE_WITH_TIMEOUT(nestedSrcView->executionLine(), innerBpLine, 5000); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("stateVal")).isValid() && varsTree->model()->data(findVarIdx({}, QStringLiteral("stateVal")).siblingAtColumn(1)).toString().contains(QStringLiteral("999")), 5000);
    stepBackBtn->click(); QTRY_COMPARE_WITH_TIMEOUT(nestedSrcView->executionLine(), revMutLine, 5000); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("stateVal")).isValid() && varsTree->model()->data(findVarIdx({}, QStringLiteral("stateVal")).siblingAtColumn(1)).toString().contains(QStringLiteral("111")), 5000);
    mainWindow.coordinator()->setReverseRecordingEnabled(0, false); QTest::qWait(100); QVERIFY(mainWindow.stepOverWorkflow()); QTRY_COMPARE_WITH_TIMEOUT(nestedSrcView->executionLine(), innerBpLine, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(callStackTable->rowCount() >= 3 && threadCombo->count() >= 1, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(nestedSrcView->inlineGhostTextForLine(innerBpLine).contains(QStringLiteral("// innerLocal = ")), 5000);
    cfg.setInlineVariableValuesEnabled(false); QVERIFY(nestedSrcView->inlineGhostTextForLine(innerBpLine).isEmpty());
    cfg.setInlineVariableValuesEnabled(true); QVERIFY(!nestedSrcView->inlineGhostTextForLine(innerBpLine).isEmpty());
    { gridlock::ui::PreferencesDialog pd(&mainWindow); auto* ivChk = pd.findChild<QCheckBox*>(QStringLiteral("inlineVarValuesCheck")); QVERIFY(ivChk && ivChk->isChecked());
      gridlock::ui::ConditionalBreakpointDialog cbd(QString(), &mainWindow); QVERIFY(cbd.findChild<QSpinBox*>(QStringLiteral("bpIgnoreCountSpin")) && cbd.findChild<QCheckBox*>(QStringLiteral("bpLogpointCheck")) && cbd.findChild<QLineEdit*>(QStringLiteral("bpLogMessageEdit"))); }
    nestedSrcView->toggleBreakpoint(outerCallLine); nestedSrcView->setBreakpointOptions(outerCallLine, QString(), 3, QStringLiteral("iter={i}"));
    QVERIFY(nestedSrcView->isLogpointForLine(outerCallLine) && nestedSrcView->breakpointIgnoreCountForLine(outerCallLine) == 3 && nestedSrcView->breakpointTooltipForLine(outerCallLine).contains(QStringLiteral("Logpoint")) && nestedSrcView->breakpointTooltipForLine(outerCallLine).contains(QLatin1Char('3')));
    nestedSrcView->toggleBreakpoint(outerCallLine); nestedSrcView->toggleBreakpoint(revMutLine);
    auto verifyFrameRow = [&](int row, const QString& exp) { const QString fn = callStackTable->item(row, 1) ? callStackTable->item(row, 1)->text() : QString(), loc = callStackTable->item(row, 2) ? callStackTable->item(row, 2)->text() : QString(), addr = callStackTable->item(row, 3) ? callStackTable->item(row, 3)->text() : QString(); bool hexOk = false; const quint64 pa = addr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? addr.mid(2).toULongLong(&hexOk, 16) : 0ULL; QVERIFY2(fn.contains(exp) && loc.contains(QLatin1Char(':')) && hexOk && pa != 0ULL, qPrintable(fn)); };
    verifyFrameRow(0, QStringLiteral("innerCallee")); verifyFrameRow(1, QStringLiteral("outerCaller")); verifyFrameRow(2, QStringLiteral("main"));
    callStackTable->selectRow(1); QMetaObject::invokeMethod(callStackTable, "cellClicked", Q_ARG(int, 1), Q_ARG(int, 1)); QTRY_COMPARE_WITH_TIMEOUT(editorTabs->currentSourceCodeView()->executionLine(), outerCallLine, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(([&]() { const QModelIndex ic = findVarIdx({}, QStringLiteral("domain_cfg")), os = findVarIdx({}, QStringLiteral("outerSecret")); return ic.isValid() && varsTree->model()->hasChildren(ic) && varsTree->model()->data(ic.siblingAtColumn(1)).toString().contains(QLatin1Char('{')) && os.isValid() && varsTree->model()->data(os.siblingAtColumn(1)).toString().contains(QStringLiteral("4242")); })(), 5000);
    { QSet<QString> seenTop; for (int r = 0; r < varsTree->model()->rowCount(); ++r) { const QString vn = varsTree->model()->data(varsTree->model()->index(r, 0)).toString(); QVERIFY2(!vn.endsWith(QStringLiteral("@entry")) && !seenTop.contains(vn), qPrintable(vn)); seenTop.insert(vn); } }
    varsTree->expand(findVarIdx({}, QStringLiteral("domain_cfg"))); QTRY_VERIFY_WITH_TIMEOUT(([&]() { const QModelIndex ic = findVarIdx({}, QStringLiteral("domain_cfg")); const QModelIndex m = findVarIdx(ic, QStringLiteral("mesh")), t = findVarIdx(ic, QStringLiteral("tol")); const QString tv = varsTree->model()->data(t.siblingAtColumn(1)).toString(); return m.isValid() && varsTree->model()->hasChildren(m) && t.isValid() && (tv.contains(QStringLiteral("1e")) || tv.contains(QStringLiteral("e-07"))); })(), 5000);
    varsTree->expand(findVarIdx(findVarIdx({}, QStringLiteral("domain_cfg")), QStringLiteral("mesh"))); QTRY_VERIFY_WITH_TIMEOUT(([&]() { const QModelIndex m = findVarIdx(findVarIdx({}, QStringLiteral("domain_cfg")), QStringLiteral("mesh")); const QModelIndex dm = findVarIdx(m, QStringLiteral("dims")), pm = findVarIdx(m, QStringLiteral("protDim")); return dm.isValid() && varsTree->model()->data(dm.siblingAtColumn(1)).toString().contains(QLatin1Char('3')) && pm.isValid() && !findVarIdx(m, QStringLiteral("protected")).isValid() && !findVarIdx(m, QStringLiteral("public")).isValid() && !findVarIdx(m, QStringLiteral("private")).isValid(); })(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(([&]() { const QString g = nestedSrcView->inlineGhostTextForLine(cfgLine); return !g.contains(QStringLiteral("{...}")) && g.contains(QStringLiteral("domain_cfg.mesh.dims = 3")) && g.contains(QStringLiteral("domain_cfg.tol = ")); })(), 5000);
    varSearch->setText(QStringLiteral("tol")); QApplication::processEvents(); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("domain_cfg")).isValid() && findVarIdx(findVarIdx({}, QStringLiteral("domain_cfg")), QStringLiteral("tol")).isValid() && !findVarIdx({}, QStringLiteral("outerSecret")).isValid(), 3000);
    varSearch->clear(); QApplication::processEvents(); QTRY_VERIFY_WITH_TIMEOUT(findVarIdx({}, QStringLiteral("outerSecret")).isValid(), 3000);
    varsTree->sortByColumn(0, Qt::AscendingOrder); QApplication::processEvents(); const int icAsc = findVarIdx({}, QStringLiteral("domain_cfg")).row(), osAsc = findVarIdx({}, QStringLiteral("outerSecret")).row();
    varsTree->sortByColumn(0, Qt::DescendingOrder); QApplication::processEvents(); const int icDesc = findVarIdx({}, QStringLiteral("domain_cfg")).row(), osDesc = findVarIdx({}, QStringLiteral("outerSecret")).row();
    QVERIFY2(icAsc >= 0 && osAsc >= 0 && icAsc < osAsc && osDesc < icDesc, "Sorting NAME column asc/desc must deterministically invert top-level order");
    // Verify PointerGraphDialog incremental multi-root pinning, 1-hop auto-expand, and cycle/alias deduplication.
    mainWindow.openPointerGraphVisualizer(QStringLiteral("outerLocal"), QStringLiteral("4243"), QStringLiteral("int"));
    auto* livePg = mainWindow.findChild<gridlock::ui::PointerGraphDialog*>(); auto* pgCloseBtn = livePg ? livePg->findChild<QPushButton*>(QStringLiteral("pointerGraphCloseBtn")) : nullptr; QVERIFY2(livePg && pgCloseBtn, "PointerGraphDialog must expose #pointerGraphCloseBtn"); QTRY_VERIFY_WITH_TIMEOUT(livePg->nodeCount() == 1, 3000);
    mainWindow.openPointerGraphVisualizer(QStringLiteral("domain_cfg"), QStringLiteral("{...}"), QStringLiteral("SubdomainBlock"));
    QTRY_VERIFY_WITH_TIMEOUT(livePg->nodeCount() >= 3 && livePg->pinnedRootExpressions().contains(QStringLiteral("outerLocal")) && livePg->pinnedRootExpressions().contains(QStringLiteral("domain_cfg")) && livePg->edgeCount() >= 1 && livePg->nodeHasField(QStringLiteral("domain_cfg"), QStringLiteral("mesh")) && livePg->nodeHasField(QStringLiteral("domain_cfg.mesh"), QStringLiteral("dims")), 5000);
    pgCloseBtn->click(); QApplication::processEvents(); QVERIFY(!livePg->isVisible());
    gridlock::ui::PointerGraphDialog pgDlg(mainWindow.coordinator(), &mainWindow); QVERIFY(pgDlg.findChild<QLineEdit*>(QStringLiteral("pointerGraphRootEdit")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphAddBtn")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphLayoutBtn")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphExpandHopBtn")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphClearBtn")) && pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphCloseBtn")) && pgDlg.findChild<QGraphicsView*>(QStringLiteral("pointerGraphView")));
    pgDlg.upsertStructNode(QStringLiteral("n1"), QStringLiteral("Node"), QStringLiteral("0x7fff1000"), {{QStringLiteral("val"), QStringLiteral("10"), QStringLiteral("int")}, {QStringLiteral("next"), QStringLiteral("0x7fff2000"), QStringLiteral("Node*")}, {QStringLiteral("ghost_guard"), QStringLiteral("0x0"), QStringLiteral("HaloExchangeBuf *")}});
    pgDlg.upsertStructNode(QStringLiteral("n2"), QStringLiteral("Node"), QStringLiteral("0x7fff2000"), {{QStringLiteral("val"), QStringLiteral("20"), QStringLiteral("int")}, {QStringLiteral("next"), QStringLiteral("0x7fff1000"), QStringLiteral("Node*")}}, QStringLiteral("n1"), QStringLiteral("next")); pgDlg.connectPointerField(QStringLiteral("n2"), QStringLiteral("next"), QStringLiteral("0x7fff1000")); pgDlg.expandAllPointersOneHop(); QVERIFY(pgDlg.nodeCount() == 2 && pgDlg.edgeCount() == 2 && pgDlg.hasCycleOrSharedAlias() && !pgDlg.nodeHasField(QStringLiteral("n1.ghost_guard"), QStringLiteral("val")));
    { auto* srcVm = qobject_cast<QAbstractProxyModel*>(varsTree->model())->sourceModel(); QVERIFY(srcVm != nullptr); const QJsonArray initArr{QJsonObject{{"name", "A"}, {"value", "{...}"}, {"type", "HaloExchangeBuf"}}, QJsonObject{{"name", "assemblyTimeSec"}, {"value", "1.114"}, {"type", "double"}}, QJsonObject{{"name", "assemblyTimeSec@entry"}, {"value", "1.114"}, {"type", "double"}}};
      QMetaObject::invokeMethod(srcVm, "onLocalsUpdated", Q_ARG(int, 0), Q_ARG(int, 0), Q_ARG(QJsonArray, initArr)); QMetaObject::invokeMethod(srcVm, "handleGdbOutput", Q_ARG(int, 0), Q_ARG(QString, QStringLiteral("^done,name=\"var_99_A\",numchild=\"1\",value=\"{...} @ 0x7fffffffabd0\",type=\"HaloExchangeBuf &\""))); QMetaObject::invokeMethod(srcVm, "handleGdbOutput", Q_ARG(int, 0), Q_ARG(QString, QStringLiteral("^done,name=\"var_99_assemblyTimeSec\",numchild=\"0\",value=\"1.114\",type=\"double\""))); QMetaObject::invokeMethod(srcVm, "handleGdbOutput", Q_ARG(int, 0), Q_ARG(QString, QStringLiteral("^done,name=\"var_99_assemblyTimeSec@entry\",numchild=\"0\",value=\"1.114\",type=\"double\""))); QCOMPARE(srcVm->rowCount(), 2); QVERIFY(srcVm->data(srcVm->index(0, 1)).toString().contains(QStringLiteral("0x7fffffffabd0"))); }
    consoleCmdEdit->setText(QStringLiteral("print outerSecret")); consoleExecBtn->click(); QTRY_VERIFY_WITH_TIMEOUT(consoleViewport->toPlainText().contains(QStringLiteral("4242")), 5000); consoleCmdEdit->setText(QStringLiteral("-data-evaluate-expression 21 + 21")); consoleExecBtn->click(); QTRY_VERIFY_WITH_TIMEOUT(consoleViewport->toPlainText().contains(QStringLiteral("42")), 5000); consoleCmdEdit->setText(QStringLiteral("print outerSecret; echo PWNED")); consoleExecBtn->click(); QApplication::processEvents(); QVERIFY(!consoleViewport->toPlainText().contains(QStringLiteral("PWNED")));
    const QString solverCpp = nestedDir.filePath(QStringLiteral("SolverDemo.cpp"));
    { QFile mf(solverCpp); QVERIFY(mf.open(QIODevice::WriteOnly)); for (int ln = 1; ln <= 90; ++ln) mf.write(QStringLiteral("// line %1\nint dummy_%1 = %1;\n").arg(ln).toUtf8()); }
    mainWindow.loadSourceFile(solverCpp); QApplication::processEvents(); auto* solverView = editorTabs->currentSourceCodeView();
    QVERIFY2(solverView != nullptr && solverView->currentFile().endsWith(QStringLiteral("SolverDemo.cpp")), "Secondary file tab SolverDemo.cpp must become active");
    solverView->toggleBreakpoint(82); QTest::qWait(300); QApplication::processEvents();
    QVERIFY2(editorTabs->currentSourceCodeView() == solverView && editorTabs->currentSourceCodeView()->currentFile().endsWith(QStringLiteral("SolverDemo.cpp")), "Toggling breakpoint in SolverDemo.cpp must NEVER switch active tab");
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
    gridlock::ui::SourceCodeView sourceCodeView; QString sampleCode;
    for (int i = 1; i <= 40; ++i) sampleCode += QStringLiteral("    MPI_Send(send_buf.data(), %1, MPI_INT, next, 0, MPI_COMM_WORLD);\n").arg(i);
    sourceCodeView.setSourceCode(sampleCode); sourceCodeView.resize(720, 560); sourceCodeView.show(); QApplication::processEvents();
    QCOMPARE(sourceCodeView.hoveredGutterLine(), -1); sourceCodeView.setHoveredGutterLine(16); QApplication::processEvents();
    QVERIFY(sourceCodeView.hoveredGutterLine() == 16 && sourceCodeView.gutterMarkerStateForLine(16) == QStringLiteral("HoverPreview"));
    sourceCodeView.toggleBreakpoint(16); QCOMPARE(sourceCodeView.gutterMarkerStateForLine(16), QStringLiteral("Breakpoint"));
    sourceCodeView.toggleBreakpoint(16); sourceCodeView.setHoveredGutterLine(-1);
    QVERIFY(sourceCodeView.hoveredGutterLine() == -1 && sourceCodeView.gutterMarkerStateForLine(16) == QStringLiteral("None"));
    gridlock::ui::ConditionalBreakpointDialog dialog(QString(), &sourceCodeView); dialog.setTargetLineContext(34, QStringLiteral("MPI_Send(send_buf.data(), ...);"));
    QVERIFY2(dialog.targetLineSummaryText().contains(QStringLiteral("Line 34")) && dialog.isBreakpointEnabled() && dialog.suspendScope() == QStringLiteral("All"), qPrintable(dialog.targetLineSummaryText()));
    QRadioButton* rankRadio = nullptr; for (QRadioButton* rb : dialog.findChildren<QRadioButton*>()) if (rb->text().contains(QStringLiteral("Rank"), Qt::CaseInsensitive) || !rb->isChecked()) { rankRadio = rb; break; }
    QVERIFY(rankRadio != nullptr); rankRadio->setChecked(true); QApplication::processEvents(); QCOMPARE(dialog.suspendScope(), QStringLiteral("Rank"));
    dialog.applyQuickPreset(QStringLiteral("rank == 0")); QCOMPARE(dialog.getCondition(), QStringLiteral("rank == 0"));
    const QPoint anchorGlobalPos(120, 340); dialog.positionNearAnchor(anchorGlobalPos); dialog.show(); QApplication::processEvents();
    QVERIFY(std::abs(dialog.pos().x() - anchorGlobalPos.x()) <= 40 && std::abs(dialog.pos().y() - anchorGlobalPos.y()) <= 25); dialog.hide();
}
/** @brief Verifies gutter QContextMenuEvent suppression and clean rollback of canceled breakpoints. */
void TestMainWindowUI::testGutterRightClickSuppressesEditorContextMenuAndCancelsCleanly() {
    gridlock::ui::SourceCodeView sourceCodeView; QString sampleCode;
    for (int i = 1; i <= 30; ++i) sampleCode += QStringLiteral("    int rank_val_%1 = %1;\n").arg(i);
    sourceCodeView.setSourceCode(sampleCode); sourceCodeView.resize(720, 560); sourceCodeView.show(); QApplication::processEvents();
    QWidget* gutterWidget = nullptr; for (QWidget* w : sourceCodeView.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) if (w != sourceCodeView.viewport() && w->width() == sourceCodeView.lineNumberAreaWidth()) { gutterWidget = w; break; }
    QVERIFY(gutterWidget != nullptr); QContextMenuEvent gutterMenuEv(QContextMenuEvent::Mouse, QPoint(10, 25), sourceCodeView.mapToGlobal(QPoint(10, 25)));
    gutterMenuEv.ignore(); QApplication::sendEvent(gutterWidget, &gutterMenuEv);
    QVERIFY(gutterMenuEv.isAccepted() && !sourceCodeView.shouldOpenEditorContextMenuAt(QPoint(10, 25)) && sourceCodeView.shouldOpenEditorContextMenuAt(QPoint(120, 25)));
    sourceCodeView.toggleBreakpoint(18); sourceCodeView.handleBreakpointPopoverResult(18, false, false, false, QString(), QStringLiteral("All")); QCOMPARE(sourceCodeView.hasBreakpoint(18), false);
    sourceCodeView.handleBreakpointPopoverResult(18, false, true, true, QStringLiteral("rank == 0"), QStringLiteral("Rank")); QCOMPARE(sourceCodeView.gutterMarkerStateForLine(18), QStringLiteral("ConditionalBreakpoint"));
    sourceCodeView.handleBreakpointPopoverResult(18, true, true, false, QString(), QStringLiteral("All")); QCOMPARE(sourceCodeView.hasBreakpoint(18), false);
}
namespace {
inline QString resolveTutorialPath(const QString& r) { return QFileInfo::exists(r) ? r : (QStringLiteral("../") + r); }
inline QString readTutorialFile(const QString& r) { QFile f(resolveTutorialPath(r)); return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString(); }
template <typename T> auto callSavePersistedTheme(T& tm, int) -> decltype(tm.savePersistedTheme(), void()) { tm.savePersistedTheme(); } template <typename T> void callSavePersistedTheme(T&, ...) {}
template <typename T> auto callLoadPersistedTheme(T& tm, int) -> decltype(tm.loadPersistedTheme()) { return tm.loadPersistedTheme(); } template <typename T> int callLoadPersistedTheme(T&, ...) { return 0; }
template <typename T> auto callEditorCompletionsAtLine(T* v, int l, const QString& p, int) -> decltype(v->editorCompletionsAtLine(l, p)) { return v->editorCompletionsAtLine(l, p); } template <typename T> int callEditorCompletionsAtLine(T*, int, const QString&, ...) { return 0; }
template <typename T> auto callTriggerCompletionPopup(T* v, const QString& p, int l, int) -> decltype(v->triggerCompletionPopupAtCursor(p, l), void()) { v->triggerCompletionPopupAtCursor(p, l); } template <typename T> void callTriggerCompletionPopup(T*, const QString&, int, ...) {}
template <typename C, typename... A> auto callCompleteDetailed(int, A&&... a) -> decltype(C::completeDetailedAtLine(std::forward<A>(a)...)) { return C::completeDetailedAtLine(std::forward<A>(a)...); } template <typename C, typename... A> int callCompleteDetailed(long, A&&...) { return 0; }
template <typename T> void verifyCompletions(const T& c) { if constexpr (!std::is_same_v<std::decay_t<T>, int>) { QVERIFY(!c.isEmpty() && c.contains(QStringLiteral("id")) && c.contains(QStringLiteral("coords"))); } }
template <typename T> void verifyDetailed(const T& items) { if constexpr (!std::is_same_v<std::decay_t<T>, int>) { QVERIFY(!items.isEmpty()); bool fId = false, fCoords = false; for (const auto& it : items) { if (it.label == QStringLiteral("id")) { fId = true; QCOMPARE(it.kindLabel, QStringLiteral("Field")); QCOMPARE(it.detailType, QStringLiteral("int")); } else if (it.label == QStringLiteral("coords")) { fCoords = true; QCOMPARE(it.kindLabel, QStringLiteral("Field")); QVERIFY(!it.detailType.isEmpty()); } } QVERIFY(fId && fCoords); } }
template <typename T, typename M> void verifyLoadedTheme(const T&, M& tm) { if constexpr (!std::is_same_v<std::decay_t<T>, int>) { QCOMPARE(tm.activeThemeId(), QStringLiteral("tokyo_night")); } }
}  // namespace (anonymous)
/** @brief Verifies ClangdLspClient virtual-buffer splicing, JSON-RPC framing, and nested struct/vector expression completion in ConditionalBreakpointDialog. */
void TestMainWindowUI::testClangdLspBreakpointExpressionCompletion() {
    const QString inspectionPath = resolveTutorialPath(QStringLiteral("tutorial/inspection_demo.cpp")), deadlockPath = resolveTutorialPath(QStringLiteral("tutorial/deadlock_ring.cpp"));
    const QString inspectionSource = readTutorialFile(QStringLiteral("tutorial/inspection_demo.cpp")), deadlockSource = readTutorialFile(QStringLiteral("tutorial/deadlock_ring.cpp"));
    QVERIFY(!inspectionSource.isEmpty() && !deadlockSource.isEmpty());
    int zeroLine = -1, zeroCol = -1; const QString spliced = gridlock::core::ClangdLspClient::buildVirtualCompletionDocument(inspectionSource, 18, QStringLiteral("data[i]."), &zeroLine, &zeroCol);
    const QStringList splicedLines = spliced.split(QLatin1Char('\n')); QVERIFY(zeroLine == 17 && zeroCol >= 8 && zeroLine < splicedLines.size() && splicedLines.at(zeroLine).left(zeroCol).endsWith(QStringLiteral("data[i].")));
    gridlock::ui::ConditionalBreakpointDialog dialog; dialog.setSourceDocumentContext(inspectionPath, inspectionSource, 18);
    const QStringList items1 = dialog.completionsForExpression(QStringLiteral("data[i].")), items2 = dialog.completionsForExpression(QStringLiteral("data[i].coords."));
    QVERIFY(items1.contains(QStringLiteral("id")) && items1.contains(QStringLiteral("values")) && items1.contains(QStringLiteral("coords")) && items2.contains(QStringLiteral("x")) && items2.contains(QStringLiteral("y")) && items2.contains(QStringLiteral("z")));
    dialog.applyQuickPreset(QStringLiteral("data[i].")); dialog.applyCompletionItem(QStringLiteral("coords")); QCOMPARE(dialog.getCondition(), QStringLiteral("data[i].coords"));
    dialog.setSourceDocumentContext(deadlockPath, deadlockSource, 34); const QString joined3 = dialog.completionsForExpression(QStringLiteral("send_buf.")).join(QLatin1Char('|'));
    QVERIFY2(joined3.contains(QStringLiteral("data")) && joined3.contains(QStringLiteral("size")), qPrintable(joined3));
    QTemporaryDir lspDir; QVERIFY(lspDir.isValid()); const QString hdrFile = lspDir.filePath(QStringLiteral("math_ops.hpp")), cppFile = lspDir.filePath(QStringLiteral("caller.cpp"));
    { QFile hf(hdrFile); QVERIFY(hf.open(QIODevice::WriteOnly)); hf.write("#pragma once\nstruct Vec2 {\n  double x;\n  double y;\n  double lengthSq() const { return x * x + y * y; }\n};\n"); }
    { QFile cf(cppFile); QVERIFY(cf.open(QIODevice::WriteOnly)); cf.write("#include \"math_ops.hpp\"\nint main() {\n  Vec2 v{3.0, 4.0};\n  int bad = \"not_an_int\";\n  return static_cast<int>(v.lengthSq()) + bad;\n}\n"); }
    gridlock::ui::MainWindow mw; mw.loadSourceFile(cppFile); auto* tabMgr = mw.findChild<gridlock::ui::EditorTabManager*>(); QVERIFY(tabMgr && tabMgr->currentSourceCodeView());
    auto* callerView = tabMgr->currentSourceCodeView(); QTRY_VERIFY_WITH_TIMEOUT(!callerView->diagnostics().isEmpty(), 4000);
    QTextCursor navCur(callerView->document()->findBlockByNumber(4)); navCur.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, 29);
    callerView->setTextCursor(navCur); QKeyEvent f12Press(QEvent::KeyPress, Qt::Key_F12, Qt::NoModifier); QApplication::sendEvent(callerView, &f12Press);
    QVERIFY(QFileInfo(tabMgr->currentSourceCodeView()->currentFile()).fileName() == QStringLiteral("math_ops.hpp") && tabMgr->currentSourceCodeView()->getCurrentLineNumber() == 5);
}
/** @brief Verifies editable SourceCodeView, live editor LSP completion, chained breakpoint popover suffix insertion, theme persistence, dirty tab indicators (`●`), `saveAllModifiedTabs()`, `switchHeaderSource()`, scope breadcrumbs, and split editors. */
void TestMainWindowUI::testEditableSourceViewModernLspPopupAndThemePersistence() {
    const QString inspectionPath = resolveTutorialPath(QStringLiteral("tutorial/inspection_demo.cpp")), inspectionSource = readTutorialFile(QStringLiteral("tutorial/inspection_demo.cpp"));
    QVERIFY(!inspectionSource.isEmpty());
    gridlock::ui::SourceCodeView srcView; QVERIFY2(!srcView.isReadOnly(), "SourceCodeView must be editable (isReadOnly() == false)"); srcView.setSourceCode(inspectionSource);
    verifyCompletions(callEditorCompletionsAtLine(&srcView, 18, QStringLiteral("data[i]."), 0)); callTriggerCompletionPopup(&srcView, QStringLiteral("data[i]."), 18, 0);
    verifyDetailed(callCompleteDetailed<gridlock::core::ClangdLspClient>(0, inspectionPath, inspectionSource, 18, QStringLiteral("data[i].")));
    gridlock::ui::ConditionalBreakpointDialog dialog; dialog.setSourceDocumentContext(inspectionPath, inspectionSource, 18);
    dialog.applyQuickPreset(QStringLiteral("i == 0 && data[i].")); dialog.applyCompletionItem(QStringLiteral("coords")); QCOMPARE(dialog.getCondition(), QStringLiteral("i == 0 && data[i].coords"));
    dialog.applyQuickPreset(dialog.getCondition() + QStringLiteral(".")); const QStringList subItems = dialog.completionsForExpression(dialog.getCondition());
    QVERIFY(subItems.contains(QStringLiteral("x")) && subItems.contains(QStringLiteral("y")) && subItems.contains(QStringLiteral("z")));
    dialog.applyCompletionItem(QStringLiteral("x")); QCOMPARE(dialog.getCondition(), QStringLiteral("i == 0 && data[i].coords.x"));
    auto& tm = gridlock::core::ThemeManager::instance(); tm.applyTheme(QStringLiteral("tokyo_night")); callSavePersistedTheme(tm, 0);
    tm.applyTheme(QStringLiteral("gridlock_slate"), false); QCOMPARE(tm.activeThemeId(), QStringLiteral("gridlock_slate"));
    verifyLoadedTheme(callLoadPersistedTheme(tm, 0), tm); tm.applyTheme(QStringLiteral("gridlock_slate")); callSavePersistedTheme(tm, 0);
    QTemporaryDir tempDir; QVERIFY(tempDir.isValid()); const QString tempCpp = tempDir.filePath(QStringLiteral("inspection_demo.cpp")), tempHpp = tempDir.filePath(QStringLiteral("inspection_demo.hpp"));
    { QFile fc(tempCpp); QVERIFY(fc.open(QIODevice::WriteOnly)); fc.write(inspectionSource.toUtf8()); QFile fh(tempHpp); QVERIFY(fh.open(QIODevice::WriteOnly)); fh.write("#pragma once\nstruct DeeplyNested {};\n"); }
    gridlock::ui::EditorTabManager tabManager; auto* tabView = tabManager.openFile(tempCpp); QVERIFY(tabView != nullptr); const int idx = tabManager.currentIndex();
    tabView->textCursor().insertText(QStringLiteral("// dirty edit\n")); QApplication::processEvents();
    QVERIFY2(tabManager.isTabModified(idx) && tabManager.tabText(idx).contains(QStringLiteral("●")), "Modified tab must report isTabModified(idx) == true and include '●' in tabText");
    QVERIFY2(tabManager.saveAllModifiedTabs() >= 1 && !tabManager.isTabModified(idx) && !tabManager.tabText(idx).contains(QStringLiteral("●")), "saveAllModifiedTabs() must save dirty buffer and remove '●'");
    tabView->setTextCursor(QTextCursor(tabView->document()->findBlockByNumber(16))); const QString bc = tabView->currentScopeBreadcrumb();
    QVERIFY2(bc.contains(QStringLiteral("inspection_demo.cpp")) && (bc.contains(QStringLiteral("main")) || bc.contains(QStringLiteral("DeeplyNested"))), qPrintable(bc));
    QVERIFY2(tabManager.switchHeaderSource() && tabManager.tabText(tabManager.currentIndex()).contains(QStringLiteral("inspection_demo.hpp")), "switchHeaderSource() must switch to inspection_demo.hpp");
    auto checkSplits = [](gridlock::ui::EditorTabManager& m) -> bool { if (m.isSplitActive()) return false; m.splitEditor(Qt::Horizontal); if (!m.isSplitActive() || m.splitOrientation() != Qt::Horizontal || m.secondaryEditorView() == nullptr) return false; m.splitEditor(Qt::Vertical); if (!m.isSplitActive() || m.splitOrientation() != Qt::Vertical) return false; m.closeSplit(); return !m.isSplitActive(); };
    QVERIFY2(checkSplits(tabManager), "EditorTabManager must support horizontal/vertical splitEditor() and closeSplit()");
}

/** @brief Verifies CommandPaletteDialog subsequence fuzzy scoring, prefix filtering (>, @, #), wrap-around keyboard navigation (Down, Up, Return, Escape), and MainWindow shortcuts. */
void TestMainWindowUI::testCommandPaletteFuzzyAndNavigation() {
    using gridlock::ui::CommandPaletteDialog; using gridlock::ui::PaletteItem;
    // 1. Subsequence fuzzy scoring: boundary matches outrank loose matches and non-matches return -1.
    QVector<int> dtpIdx, drIdx, badIdx;
    const int dtpExact = CommandPaletteDialog::fuzzyScore(QStringLiteral("dtp"), QStringLiteral("Debug: Attach to Process..."), &dtpIdx), dtpLoose = CommandPaletteDialog::fuzzyScore(QStringLiteral("dtp"), QStringLiteral("Edit: Copy desktop path"));
    const int drExact = CommandPaletteDialog::fuzzyScore(QStringLiteral("dr.cpp"), QStringLiteral("tutorial/deadlock_ring.cpp"), &drIdx), drLoose = CommandPaletteDialog::fuzzyScore(QStringLiteral("dr.cpp"), QStringLiteral("src/core/hpc/DeadlockAnalyzer.cpp"));
    QVERIFY(dtpExact > 0 && dtpExact > dtpLoose && dtpIdx.size() == 3 && drExact > 0 && drExact > drLoose && drIdx.size() == 6 && CommandPaletteDialog::fuzzyScore(QStringLiteral("zqx_nomatch"), QStringLiteral("tutorial/deadlock_ring.cpp"), &badIdx) == -1 && badIdx.isEmpty());
    int firedId = -1; CommandPaletteDialog palette;
    palette.setItems({{PaletteItem::Category::Action, QStringLiteral("Debug: Attach to Process..."), QStringLiteral("Menu -> File"), QStringLiteral("Ctrl+Alt+P"), [&]() { firedId = 1; }}, {PaletteItem::Category::Action, QStringLiteral("Tools: Build Target"), QStringLiteral("Menu -> Tools"), QStringLiteral("Ctrl+B"), [&]() { firedId = 2; }}, {PaletteItem::Category::File, QStringLiteral("tutorial/deadlock_ring.cpp"), QStringLiteral("tutorial"), QStringLiteral("C/C++"), [&]() { firedId = 3; }}, {PaletteItem::Category::Symbol, QStringLiteral("DeeplyNested"), QStringLiteral("inspection_demo.cpp:6"), QStringLiteral("Struct"), [&]() { firedId = 4; }}, {PaletteItem::Category::Rank, QStringLiteral("Rank 2"), QStringLiteral("Focus MPI Rank 2 (PAUSED)"), QStringLiteral("PAUSED"), [&]() { firedId = 5; }}, {PaletteItem::Category::Theme, QStringLiteral("Theme: Solarized Dark"), QStringLiteral("Switch palette"), QStringLiteral("Dark"), [&]() { firedId = 6; }}});
    palette.openWithPrefix(QStringLiteral(">")); QVERIFY(palette.resultCount() == 2 && palette.resultItemAt(0).category == PaletteItem::Category::Action);
    palette.setFilterText(QStringLiteral("@")); QVERIFY(palette.resultCount() == 1 && palette.resultItemAt(0).category == PaletteItem::Category::Symbol);
    palette.setFilterText(QStringLiteral("#")); QVERIFY(palette.resultCount() == 1 && palette.resultItemAt(0).category == PaletteItem::Category::Rank);
    palette.setFilterText(QStringLiteral(">")); QCOMPARE(palette.selectedIndex(), 0);
    QTest::keyClick(palette.inputEdit(), Qt::Key_Down); QCOMPARE(palette.selectedIndex(), 1); QTest::keyClick(palette.inputEdit(), Qt::Key_Down); QCOMPARE(palette.selectedIndex(), 0);
    QTest::keyClick(palette.inputEdit(), Qt::Key_Up); QCOMPARE(palette.selectedIndex(), 1); QTest::keyClick(palette.inputEdit(), Qt::Key_Escape); QVERIFY(!palette.isVisible() && firedId == -1);
    palette.openWithPrefix(QStringLiteral(">")); QTest::keyClick(palette.inputEdit(), Qt::Key_Down); QTest::keyClick(palette.inputEdit(), Qt::Key_Return); QVERIFY(!palette.isVisible() && firedId == 2);
    static QStringList s_capturedWarnings; s_capturedWarnings.clear();
    const QtMessageHandler prevHandler = qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& msg) { if (type == QtWarningMsg) s_capturedWarnings.append(msg); });
    struct MsgGuard { QtMessageHandler prev; ~MsgGuard() { qInstallMessageHandler(prev); } } msgGuard{prevHandler};
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.loadPreviewFixture(); mw.show(); QApplication::processEvents();
    auto* actCmd = mw.findChild<QAction*>(QStringLiteral("actionCommandPalette")); auto* actQuick = mw.findChild<QAction*>(QStringLiteral("actionQuickOpen")); auto* actAttach = mw.findChild<QAction*>(QStringLiteral("actionAttachProcess"));
    QVERIFY(actCmd && actCmd->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P) && actQuick && actQuick->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_P) && actAttach && actAttach->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P));
    int countCtrlShiftP = 0, countCtrlP = 0, countCtrlAltP = 0;
    for (QAction* a : mw.findChildren<QAction*>()) { if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P)) ++countCtrlShiftP; if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::Key_P)) ++countCtrlP; if (a->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P)) ++countCtrlAltP; }
    for (QShortcut* s : mw.findChildren<QShortcut*>()) { if (s->key() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P)) ++countCtrlShiftP; if (s->key() == QKeySequence(Qt::CTRL | Qt::Key_P)) ++countCtrlP; if (s->key() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P)) ++countCtrlAltP; }
    QVERIFY(countCtrlShiftP == 1 && countCtrlP == 1 && countCtrlAltP == 1);
    if (auto* dv = mw.disassemblyView()) dv->setFocus();
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier); QApplication::processEvents();
    auto* mwPalette = mw.findChild<CommandPaletteDialog*>(QStringLiteral("CommandPaletteDialog")); QVERIFY(mwPalette && mwPalette->isVisible() && mwPalette->filterText() == QStringLiteral(">") && mwPalette->resultCount() > 0);
    for (int i = 0; i < mwPalette->resultCount(); ++i) QCOMPARE(mwPalette->resultItemAt(i).category, PaletteItem::Category::Action);
    QTest::keyClick(mwPalette->inputEdit(), Qt::Key_Escape); QVERIFY(!mwPalette->isVisible());
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier); QApplication::processEvents(); QVERIFY(mwPalette->isVisible() && mwPalette->filterText().isEmpty() && mwPalette->resultCount() > 0);
    mwPalette->setFilterText(QStringLiteral("dr.cpp")); QVERIFY(mwPalette->resultCount() > 0 && mwPalette->resultItemAt(0).title.contains(QStringLiteral("deadlock_ring.cpp")));
    QTest::keyClick(mwPalette->inputEdit(), Qt::Key_Return); QApplication::processEvents(); QVERIFY(!mwPalette->isVisible() && mw.sourceCodeView() && mw.sourceCodeView()->currentFile().endsWith(QStringLiteral("deadlock_ring.cpp")));
    bool attachOpened = false; QTimer::singleShot(80, [&]() { if (auto* dlg = dynamic_cast<gridlock::ui::AttachProcessDialog*>(QApplication::activeModalWidget())) { attachOpened = dlg->isVisible(); dlg->reject(); } });
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier | Qt::AltModifier); QApplication::processEvents(); QVERIFY(attachOpened);
    for (const QString& w : s_capturedWarnings) QVERIFY2(!w.contains(QStringLiteral("Ambiguous shortcut overload"), Qt::CaseInsensitive), qPrintable(w));
}
/** @brief Verifies ProgrammerCalculatorDialog base conversion, width masking, bit grid, BSWAP, expressions, Preferences, and Command Palette launch. */
void TestMainWindowUI::testProgrammerCalculatorBasesAndBits() {
    using gridlock::ui::CommandPaletteDialog; using gridlock::ui::ProgrammerCalculatorDialog;
    ProgrammerCalculatorDialog calc; calc.setWordWidth(32); calc.setSigned(false); calc.hexEdit()->setText(QStringLiteral("0xDEADBEEF")); QApplication::processEvents();
    QVERIFY(calc.value() == 0xDEADBEEFULL && calc.decText() == QStringLiteral("3735928559") && calc.octText() == QStringLiteral("33653337357") && calc.binText().startsWith(QStringLiteral("1101 1110 1010 1101")) && calc.binText().endsWith(QStringLiteral("1110 1111")));
    calc.setSigned(true); QCOMPARE(calc.decText(), QStringLiteral("-559038737"));
    calc.setSigned(false); calc.setWordWidth(64); calc.setValue(0x11223344DEADBEEFULL);
    calc.setWordWidth(16); QVERIFY(calc.value() == 0xBEEFULL && !calc.bitButton(16)->isEnabled() && calc.bitButton(15)->isEnabled());
    calc.setWordWidth(8); QVERIFY(calc.value() == 0xEFULL && !calc.bitButton(8)->isEnabled() && calc.bitButton(0)->isEnabled());
    calc.bitButton(0)->click(); QVERIFY(calc.value() == 0xEEULL && calc.hexText() == QStringLiteral("0xEE"));
    calc.setWordWidth(32); QVERIFY(calc.evaluateAndApplyExpression(QStringLiteral("(0x10 << 4) | 0x05")) && calc.value() == 0x105ULL && calc.hexText() == QStringLiteral("0x105"));
    calc.setValue(0x12345678ULL); calc.byteSwap(); QCOMPARE(calc.value(), 0x78563412ULL);
    auto* acBtn = calc.findChild<QPushButton*>(QStringLiteral("calcOpClearBtn")), *addBtn = calc.findChild<QPushButton*>(QStringLiteral("calcOpAddBtn")), *shlBtn = calc.findChild<QPushButton*>(QStringLiteral("calcOpShlBtn")), *eqBtn = calc.findChild<QPushButton*>(QStringLiteral("calcExprEvalBtn")), *hexRadixBtn = calc.findChild<QPushButton*>(QStringLiteral("calcRadixHexBtn")), *binRadixBtn = calc.findChild<QPushButton*>(QStringLiteral("calcRadixBinBtn"));
    QVERIFY(acBtn && addBtn && shlBtn && eqBtn && hexRadixBtn && binRadixBtn && !calc.keypadDigitButton(10)->isEnabled());
    acBtn->click(); calc.keypadDigitButton(1)->click(); calc.keypadDigitButton(0)->click(); addBtn->click(); calc.keypadDigitButton(5)->click(); eqBtn->click();
    QVERIFY(calc.value() == 15ULL && calc.hexText() == QStringLiteral("0xF") && calc.decText() == QStringLiteral("15") && calc.octText() == QStringLiteral("17") && calc.bitButton(3)->text() == QStringLiteral("1"));
    hexRadixBtn->click(); QVERIFY(calc.keypadDigitButton(13)->isEnabled() && calc.inputRadix() == 16);
    calc.keypadDigitButton(13)->click(); calc.keypadDigitButton(14)->click(); calc.keypadDigitButton(10)->click(); calc.keypadDigitButton(13)->click(); eqBtn->click(); QVERIFY(calc.value() == 0xDEADULL && calc.decText() == QStringLiteral("57005"));
    calc.expressionEdit()->clear(); shlBtn->click(); calc.keypadDigitButton(4)->click(); eqBtn->click(); QCOMPARE(calc.value(), 0xDEAD0ULL);
    binRadixBtn->click(); QVERIFY(calc.keypadDigitButton(1)->isEnabled() && !calc.keypadDigitButton(2)->isEnabled());
    auto* statusLbl = calc.findChild<QLabel*>(QStringLiteral("calcExprStatusLabel")); QVERIFY(statusLbl && calc.findChild<QPushButton*>(QStringLiteral("calcKeyDot")));
    calc.setInitialTextOrValue(QStringLiteral("3.3f")); QVERIFY(calc.expressionEdit()->text() == QStringLiteral("3.3") && calc.decText() == QStringLiteral("3") && calc.hexText() == QStringLiteral("0x3") && calc.hasFloatValue() && std::abs(calc.floatValue() - 3.3) < 1e-9 && calc.floatReadoutText().contains(QStringLiteral("0x40533333")) && !statusLbl->text().contains(QStringLiteral("Error")));
    QVERIFY(calc.evaluateAndApplyExpression(QStringLiteral("3.3 * 2")) && std::abs(calc.floatValue() - 6.6) < 1e-9 && calc.decText() == QStringLiteral("6") && calc.hexText() == QStringLiteral("0x6"));
    calc.setInitialTextOrValue(QStringLiteral("0x4011a6")); QVERIFY(calc.hexText() == QStringLiteral("0x4011A6") && calc.expressionEdit()->text() == QStringLiteral("0x4011A6"));
    calc.setWordWidth(64); calc.setSigned(false); calc.setInitialTextOrValue(QStringLiteral("0xdeadbeefULL")); QVERIFY(calc.value() == 0xDEADBEEFULL && calc.hexText() == QStringLiteral("0xDEADBEEF"));
    calc.setInitialTextOrValue(QStringLiteral("-42")); QCOMPARE(calc.decText(), QStringLiteral("-42"));
    calc.setSigned(false); calc.setInitialTextOrValue(QStringLiteral("(1 << 8) + 7")); QVERIFY(calc.value() == 263ULL && calc.hexText() == QStringLiteral("0x107"));
    static QStringList s_calcWarnings; s_calcWarnings.clear();
    const QtMessageHandler prevH = qInstallMessageHandler([](QtMsgType t, const QMessageLogContext&, const QString& m) { if (t == QtWarningMsg) s_calcWarnings.append(m); });
    struct Guard { QtMessageHandler p; ~Guard() { qInstallMessageHandler(p); } } g{prevH};
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.show(); QApplication::processEvents();
    gridlock::ui::ProjectSettingsDialog psDlg(&mw); QVERIFY(psDlg.findChild<QComboBox*>(QStringLiteral("calcDefaultWidthCombo")) && psDlg.findChild<QCheckBox*>(QStringLiteral("calcDefaultSignedCheck")) && psDlg.findChild<QPushButton*>(QStringLiteral("openProgrammerCalculatorBtn")));
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier); QApplication::processEvents();
    auto* pal = mw.findChild<CommandPaletteDialog*>(QStringLiteral("CommandPaletteDialog")); QVERIFY(pal && pal->isVisible());
    pal->setFilterText(QStringLiteral("> Programmer")); QVERIFY(pal->resultCount() > 0 && pal->resultItemAt(0).title.contains(QStringLiteral("Programmer's Calculator")));
    QTest::keyClick(pal->inputEdit(), Qt::Key_Return); QApplication::processEvents();
    auto* floatingCalc = mw.programmerCalculatorDialog(); QVERIFY(floatingCalc && floatingCalc->isVisible() && !floatingCalc->isModal() && (floatingCalc->windowFlags() & Qt::Window) && floatingCalc->keypadDigitButton(0)->height() >= 26);
    floatingCalc->close(); QTest::keyClick(&mw, Qt::Key_C, Qt::ControlModifier | Qt::AltModifier); QApplication::processEvents(); QVERIFY(floatingCalc->isVisible()); floatingCalc->close();
    QVERIFY(mw.sourceCodeView() && mw.variablesDockWidget() && mw.registerView() && mw.memView() && mw.disassemblyView());
    emit mw.sourceCodeView()->sendToCalculatorRequested(QStringLiteral("0x4011a6")); QApplication::processEvents();
    QVERIFY(floatingCalc->isVisible() && floatingCalc->value() == 0x4011A6ULL && floatingCalc->hexText() == QStringLiteral("0x4011A6") && floatingCalc->expressionEdit()->text() == QStringLiteral("0x4011A6"));
    emit mw.variablesDockWidget()->sendToCalculatorRequested(QStringLiteral("0x7fffffffe5a8")); QCOMPARE(floatingCalc->value(), 0x7FFFFFFFE5A8ULL); floatingCalc->close();
    for (const QString& w : s_calcWarnings) QVERIFY2(!w.contains(QStringLiteral("Ambiguous shortcut overload"), Qt::CaseInsensitive), qPrintable(w));
}
/** @brief Verifies PointerGraphDialog layered DAG layout, manual drag preservation, incremental multi-root pinning, and dynamic alias splitting/merging. */
static void testPointerGraphDagLayoutAndAliasSplitting() {
    using gridlock::ui::PointerGraphDialog; using F = PointerGraphDialog::StructFieldEntry;
    PointerGraphDialog pg; auto* layoutBtn = pg.findChild<QPushButton*>(QStringLiteral("pointerGraphLayoutBtn")); auto* clearBtn = pg.findChild<QPushButton*>(QStringLiteral("pointerGraphClearBtn")); auto* pruneChk = pg.findChild<QCheckBox*>(QStringLiteral("pointerGraphPruneCheck"));
    QVERIFY(layoutBtn && clearBtn && pruneChk && pruneChk->isChecked());
    pg.upsertStructNode(QStringLiteral("root"), QStringLiteral("Tree"), QStringLiteral("0x100"), {F{QStringLiteral("left"), QStringLiteral("0x200"), QStringLiteral("Node*")}, F{QStringLiteral("right"), QStringLiteral("0x210"), QStringLiteral("Node*")}});
    pg.upsertStructNode(QStringLiteral("child"), QStringLiteral("Node"), QStringLiteral("0x200"), {F{QStringLiteral("next"), QStringLiteral("0x300"), QStringLiteral("Node*")}}, QStringLiteral("root"), QStringLiteral("left"));
    pg.upsertStructNode(QStringLiteral("altChild"), QStringLiteral("Node"), QStringLiteral("0x210"), {F{QStringLiteral("val"), QStringLiteral("7"), QStringLiteral("int")}}, QStringLiteral("root"), QStringLiteral("right"));
    pg.upsertStructNode(QStringLiteral("grandchild"), QStringLiteral("Node"), QStringLiteral("0x300"), {F{QStringLiteral("val"), QStringLiteral("9"), QStringLiteral("int")}}, QStringLiteral("child"), QStringLiteral("next"));
    pg.autoLayoutNodes();
    QVERIFY(pg.nodeLayer(QStringLiteral("root")) == 0 && pg.nodeLayer(QStringLiteral("child")) == 1 && pg.nodeLayer(QStringLiteral("grandchild")) == 2 && pg.nodePosition(QStringLiteral("root")).x() < pg.nodePosition(QStringLiteral("child")).x() && pg.nodePosition(QStringLiteral("child")).x() < pg.nodePosition(QStringLiteral("grandchild")).x() && pg.nodePosition(QStringLiteral("child")).y() < pg.nodePosition(QStringLiteral("altChild")).y());
    const QPointF manualPos(555.0, 444.0); QVERIFY(pg.setNodePosition(QStringLiteral("child"), manualPos) && pg.isNodeManuallyPositioned(QStringLiteral("child")));
    pg.onTargetStopped(0); pg.upsertStructNode(QStringLiteral("child"), QStringLiteral("Node"), QStringLiteral("0x200"), {F{QStringLiteral("next"), QStringLiteral("0x300"), QStringLiteral("Node*")}, F{QStringLiteral("tag"), QStringLiteral("1"), QStringLiteral("int")}});
    pg.upsertStructNode(QStringLiteral("extra"), QStringLiteral("Node"), QStringLiteral("0x310"), {F{QStringLiteral("val"), QStringLiteral("3"), QStringLiteral("int")}}, QStringLiteral("child"), QStringLiteral("tag"));
    pg.autoLayoutNodes(false); QCOMPARE(pg.nodePosition(QStringLiteral("child")), manualPos);
    layoutBtn->click(); QVERIFY(!pg.isNodeManuallyPositioned(QStringLiteral("child")) && pg.nodePosition(QStringLiteral("root")).x() < pg.nodePosition(QStringLiteral("child")).x() && pg.nodePosition(QStringLiteral("child")).x() < pg.nodePosition(QStringLiteral("grandchild")).x());
    clearBtn->click(); QVERIFY(pg.nodeCount() == 0 && pg.edgeCount() == 0 && pg.pinnedRootExpressions().isEmpty());
    pg.addAndEvaluateRootExpression(QStringLiteral("headA"), QStringLiteral("Holder*"), QStringLiteral("0x100")); pg.upsertStructNode(QStringLiteral("headA"), QStringLiteral("Holder"), QStringLiteral("0x100"), {F{QStringLiteral("ptr"), QStringLiteral("0x1000"), QStringLiteral("Node*")}});
    pg.addAndEvaluateRootExpression(QStringLiteral("headB"), QStringLiteral("Holder*"), QStringLiteral("0x200")); pg.upsertStructNode(QStringLiteral("headB"), QStringLiteral("Holder"), QStringLiteral("0x200"), {F{QStringLiteral("ptr"), QStringLiteral("0x1000"), QStringLiteral("Node*")}});
    QVERIFY(pg.nodeCount() == 2 && pg.pinnedRootExpressions() == (QStringList{QStringLiteral("headA"), QStringLiteral("headB")}) && pg.nodePosition(QStringLiteral("headA")).y() != pg.nodePosition(QStringLiteral("headB")).y());
    QVERIFY(pg.unpinRootExpression(QStringLiteral("headB")) && pg.nodeCount() == 1 && pg.pinnedRootExpressions() == (QStringList{QStringLiteral("headA")}));
    pg.addAndEvaluateRootExpression(QStringLiteral("headB"), QStringLiteral("Holder*"), QStringLiteral("0x200")); pg.upsertStructNode(QStringLiteral("headB"), QStringLiteral("Holder"), QStringLiteral("0x200"), {F{QStringLiteral("ptr"), QStringLiteral("0x1000"), QStringLiteral("Node*")}});
    pg.upsertStructNode(QStringLiteral("n1"), QStringLiteral("Node"), QStringLiteral("0x1000"), {F{QStringLiteral("val"), QStringLiteral("10"), QStringLiteral("int")}}, QStringLiteral("headA"), QStringLiteral("ptr"));
    QVERIFY(pg.connectPointerField(QStringLiteral("headB"), QStringLiteral("ptr"), QStringLiteral("0x1000")) && pg.nodeCount() == 3 && pg.edgeCount() == 2 && pg.hasCycleOrSharedAlias());
    pg.onTargetStopped(0); pg.upsertStructNode(QStringLiteral("headB"), QStringLiteral("Holder"), QStringLiteral("0x200"), {F{QStringLiteral("ptr"), QStringLiteral("0x2000"), QStringLiteral("Node*")}});
    pg.upsertStructNode(QStringLiteral("n2"), QStringLiteral("Node"), QStringLiteral("0x2000"), {F{QStringLiteral("val"), QStringLiteral("20"), QStringLiteral("int")}}, QStringLiteral("headB"), QStringLiteral("ptr"));
    QVERIFY(pg.nodeCount() == 4 && pg.edgeCount() == 2 && pg.edgeTargetAddressOrExpr(QStringLiteral("headA"), QStringLiteral("ptr")) == QStringLiteral("0x1000") && pg.edgeTargetAddressOrExpr(QStringLiteral("headB"), QStringLiteral("ptr")) == QStringLiteral("0x2000") && !pg.hasCycleOrSharedAlias() && pg.fieldMutationSummary(QStringLiteral("headB"), QStringLiteral("ptr")) == QStringLiteral("0x1000 -> 0x2000"));
    pg.onTargetStopped(0); pg.upsertStructNode(QStringLiteral("headA"), QStringLiteral("Holder"), QStringLiteral("0x100"), {F{QStringLiteral("ptr"), QStringLiteral("0x2000"), QStringLiteral("Node*")}});
    QVERIFY(pg.nodeCount() == 3 && pg.edgeCount() == 2 && pg.edgeTargetAddressOrExpr(QStringLiteral("headA"), QStringLiteral("ptr")) == QStringLiteral("0x2000") && pg.hasCycleOrSharedAlias());
    clearBtn->click(); QVERIFY(pg.nodeCount() == 0 && pg.edgeCount() == 0);
}
/** @brief Verifies PointerGraphDialog wheel zoom, corner minimap, field suppression, horizontal orientation, /x /d /b overrides, and SVG/PNG export. */
static void testPointerGraphControlsAndExport() {
    using gridlock::ui::PointerGraphDialog; using F = PointerGraphDialog::StructFieldEntry;
    PointerGraphDialog pg; pg.resize(960, 620); pg.show(); QApplication::processEvents();
    QVERIFY(pg.findChild<QPushButton*>(QStringLiteral("pointerGraphExportSvgBtn")) && pg.findChild<QPushButton*>(QStringLiteral("pointerGraphExportPngBtn")) && pg.findChild<QPushButton*>(QStringLiteral("pointerGraphResetZoomBtn")) && pg.findChild<QPushButton*>(QStringLiteral("pointerGraphFitBtn")));
    pg.upsertStructNode(QStringLiteral("pkt"), QStringLiteral("Packet"), QStringLiteral("0x5000"), {F{QStringLiteral("magic"), QStringLiteral("42"), QStringLiteral("int")}, F{QStringLiteral("seq"), QStringLiteral("7"), QStringLiteral("int")}, F{QStringLiteral("crc"), QStringLiteral("255"), QStringLiteral("uint32_t")}, F{QStringLiteral("next"), QStringLiteral("0x5100"), QStringLiteral("Packet*")}});
    pg.upsertStructNode(QStringLiteral("peer"), QStringLiteral("Packet"), QStringLiteral("0x5100"), {F{QStringLiteral("magic"), QStringLiteral("99"), QStringLiteral("int")}}, QStringLiteral("pkt"), QStringLiteral("next"));
    QApplication::processEvents();
    QVERIFY(pg.visibleFieldCount(QStringLiteral("pkt")) == 4 && pg.hiddenFieldCount(QStringLiteral("pkt")) == 0 && pg.setNodeFieldHidden(QStringLiteral("pkt"), QStringLiteral("crc"), true) && pg.visibleFieldCount(QStringLiteral("pkt")) == 3 && pg.hiddenFieldCount(QStringLiteral("pkt")) == 1 && pg.hiddenFieldsBadgeText(QStringLiteral("pkt")).contains(QStringLiteral("(+1 hidden)")));
    pg.onTargetStopped(0); pg.upsertStructNode(QStringLiteral("pkt"), QStringLiteral("Packet"), QStringLiteral("0x5000"), {F{QStringLiteral("magic"), QStringLiteral("42"), QStringLiteral("int")}, F{QStringLiteral("seq"), QStringLiteral("8"), QStringLiteral("int")}, F{QStringLiteral("crc"), QStringLiteral("255"), QStringLiteral("uint32_t")}, F{QStringLiteral("next"), QStringLiteral("0x5100"), QStringLiteral("Packet*")}});
    QVERIFY(pg.hiddenFieldCount(QStringLiteral("pkt")) == 1 && pg.restoreAllHiddenFields(QStringLiteral("pkt")) && pg.visibleFieldCount(QStringLiteral("pkt")) == 4 && pg.hiddenFieldCount(QStringLiteral("pkt")) == 0);
    QVERIFY(!pg.isNodeHorizontal(QStringLiteral("pkt")) && pg.toggleNodeOrientation(QStringLiteral("pkt")) && pg.isNodeHorizontal(QStringLiteral("pkt")) && pg.nodeCardSize(QStringLiteral("pkt")).width() > pg.nodeCardSize(QStringLiteral("pkt")).height());
    QVERIFY(pg.setNodeFormatOverride(QStringLiteral("pkt"), QStringLiteral("/x")) && pg.fieldValue(QStringLiteral("pkt"), QStringLiteral("magic")) == QStringLiteral("0x2a") && pg.setNodeFormatOverride(QStringLiteral("pkt"), QStringLiteral("/b")) && pg.fieldValue(QStringLiteral("pkt"), QStringLiteral("magic")) == QStringLiteral("0b101010") && pg.setNodeFormatOverride(QStringLiteral("pkt"), QStringLiteral("/d")) && pg.fieldValue(QStringLiteral("pkt"), QStringLiteral("magic")) == QStringLiteral("42"));
    auto* gv = pg.findChild<QGraphicsView*>(QStringLiteral("pointerGraphView")); QVERIFY(gv != nullptr && !pg.minimapViewportRect().isEmpty());
    QWheelEvent wheelIn(QPointF(100, 100), gv->mapToGlobal(QPointF(100, 100)), QPoint(0, 0), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(gv->viewport(), &wheelIn); QVERIFY(pg.zoomFactor() > 1.05);
    pg.setZoomFactor(9.0); QVERIFY(std::abs(pg.zoomFactor() - 2.5) < 1e-3); pg.setZoomFactor(0.05); QVERIFY(std::abs(pg.zoomFactor() - 0.25) < 1e-3); pg.resetZoom(); QVERIFY(std::abs(pg.zoomFactor() - 1.0) < 1e-3);
    QTest::mouseClick(gv->viewport(), Qt::LeftButton, Qt::NoModifier, pg.minimapViewportRect().center().toPoint());
    QTemporaryDir expDir; QVERIFY(expDir.isValid()); const QString pngOut = expDir.filePath(QStringLiteral("graph.png")), svgOut = expDir.filePath(QStringLiteral("graph.svg"));
    QVERIFY(pg.exportGraphToPng(pngOut) && pg.exportGraphToSvg(svgOut) && QImage(pngOut).width() >= 400);
    QFile sf(svgOut); QVERIFY(sf.open(QIODevice::ReadOnly) && QString::fromUtf8(sf.readAll()).contains(QStringLiteral("<svg"))); pg.close();
}
/** @brief Verifies ArrayPlotDialog v0.8.8 multi-rank 1D overlay, start:end:stride slicing, Log10 scale, drag-box zoom, and Scalar History mode. */
static void testArrayPlotterScientificExtensions() {
    using gridlock::ui::ArrayPlotDialog; using gridlock::ui::parseArraySliceExpression; using gridlock::ui::parseRankSubset;
    const auto s1 = parseArraySliceExpression(QStringLiteral("arr[2:10:2]")), s2 = parseArraySliceExpression(QStringLiteral("u[16:128:2]")), s3 = parseArraySliceExpression(QStringLiteral("pressure_field[0..63]")), s4 = parseArraySliceExpression(QStringLiteral("&vec[0]"));
    QVERIFY(s1.hasSlice && s1.baseExpression == QStringLiteral("arr") && s1.offset == 2 && s1.count == 4 && s1.stride == 2 && s2.hasSlice && s2.offset == 16 && s2.count == 56 && s2.stride == 2 && s3.hasSlice && s3.offset == 0 && s3.count == 64 && s3.stride == 1 && !s4.hasSlice && s4.baseExpression == QStringLiteral("&vec[0]"));
    QCOMPARE(parseRankSubset(QStringLiteral("0..3"), 8), (QVector<int>{0, 1, 2, 3})); QCOMPARE(parseRankSubset(QStringLiteral("0, 2, 3"), 4), (QVector<int>{0, 2, 3}));
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(4); ArrayPlotDialog dlg(&coord); dlg.resize(980, 600); dlg.show(); QApplication::processEvents();
    auto* logChk = dlg.findChild<QCheckBox*>(QStringLiteral("arrayPlotLogScaleCheck")); auto* overlayChk = dlg.findChild<QCheckBox*>(QStringLiteral("arrayPlotOverlayAllRanksCheck")); auto* resetZoomBtn = dlg.findChild<QPushButton*>(QStringLiteral("arrayPlotResetZoomBtn")); auto* clearHistBtn = dlg.findChild<QPushButton*>(QStringLiteral("arrayPlotClearHistoryBtn"));
    QVERIFY(logChk && overlayChk && resetZoomBtn && clearHistBtn && dlg.findChild<QSpinBox*>(QStringLiteral("arrayPlotOffsetSpin")) && dlg.findChild<QSpinBox*>(QStringLiteral("arrayPlotStrideSpin")) && dlg.findChild<QLineEdit*>(QStringLiteral("arrayPlotRankSubsetEdit")));
    QVector<double> buf12(12); for (int i = 0; i < 12; ++i) buf12[i] = static_cast<double>(i); dlg.setExpression(QStringLiteral("arr[2:10:2]"));
    QVERIFY(dlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(buf12.constData()), buf12.size() * sizeof(double)), ArrayPlotDialog::ElementType::Float64) && dlg.currentSamples() == (QVector<double>{2.0, 4.0, 6.0, 8.0}) && dlg.sliceOffset() == 2 && dlg.sliceStride() == 2);
    QSignalSpy multiSpy(&dlg, &ArrayPlotDialog::multiRankPlotRequested); dlg.setExpression(QStringLiteral("halo[0:4]")); dlg.setRankSubsetSpec(QStringLiteral("0..3"));
    overlayChk->setChecked(true); QApplication::processEvents(); QVERIFY(multiSpy.count() >= 1 && multiSpy.first().at(0).toString() == QStringLiteral("halo") && multiSpy.first().at(1).toInt() == 32);
    for (int r = 0; r < 4; ++r) { const double vals[4] = {r * 10.0 + 1.0, r * 10.0 + 2.0, r * 10.0 + 3.0, r * 10.0 + 4.0}; emit coord.memoryRead(r, QStringLiteral("&(halo)[0]"), QByteArray(reinterpret_cast<const char*>(vals), sizeof(vals))); }
    QVERIFY(dlg.rankOverlays().size() == 4 && dlg.rankOverlays().value(3).first() == 31.0 && !dlg.grab().isNull()); overlayChk->setChecked(false); QVERIFY(dlg.rankOverlays().isEmpty());
    dlg.setSliceOffset(0); dlg.setSliceStride(1); dlg.setSamples({1e-4, 1e-2, 1.0, 0.0, -5.0}, 1, 5); logChk->setChecked(true); QApplication::processEvents(); QVERIFY(dlg.isLogScale() && !dlg.grab().isNull());
    dlg.setSamples({1e-3, 1.0, 0.0, -2.0}, 2, 2); QApplication::processEvents(); QVERIFY(!dlg.grab().isNull()); logChk->setChecked(false);
    QVector<double> wave(64); for (int i = 0; i < 64; ++i) wave[i] = std::sin(i * 0.15) + 2.0; dlg.setPlotMode(ArrayPlotDialog::PlotMode::Line1D); dlg.setSamples(wave, 1, 64); QApplication::processEvents();
    auto* canvas = dlg.canvas(); QVERIFY(canvas != nullptr && !dlg.isZoomed() && !resetZoomBtn->isEnabled());
    const QRect pr = canvas->plotAreaRect(); const QPoint p0(pr.left() + pr.width() / 4, pr.top() + pr.height() / 4), p1(pr.left() + (pr.width() * 3) / 4, pr.top() + (pr.height() * 3) / 4);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, p0); QTest::mouseMove(canvas, p1); QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, p1); QApplication::processEvents();
    QVERIFY(dlg.isZoomed() && resetZoomBtn->isEnabled() && dlg.visibleXRange().first > 5 && dlg.visibleXRange().second < 58); resetZoomBtn->click(); QVERIFY(!dlg.isZoomed() && !resetZoomBtn->isEnabled() && dlg.visibleXRange() == (QPair<int, int>{0, 63}));
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, p0); QTest::mouseMove(canvas, p1); QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, p1); QVERIFY(dlg.isZoomed()); QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier, pr.center()); QVERIFY(!dlg.isZoomed());
    dlg.setPlotMode(ArrayPlotDialog::PlotMode::ScalarHistory); dlg.setExpression(QStringLiteral("residual"));
    for (const QString& rv : {QStringLiteral("$1 = 1.0"), QStringLiteral("$2 = 0.1"), QStringLiteral("$3 = 0.01"), QStringLiteral("$4 = 0.001")}) { dlg.onTargetStopped(0); emit coord.expressionEvaluated(0, QStringLiteral("residual"), rv); }
    QVERIFY(dlg.scalarHistory().size() == 4 && dlg.currentSamples().size() == 4 && std::abs(dlg.scalarHistory().constLast() - 0.001) < 1e-12 && !dlg.grab().isNull());
    clearHistBtn->click(); QVERIFY(dlg.scalarHistory().isEmpty() && dlg.currentSamples().isEmpty()); dlg.close();
}
/** @brief Verifies Complex64/Complex128 decoding, 2D splot/pm3d gnuplot script generation, and virtualized C/Fortran 2D/3D TensorSliceTableModel. */
static void testComplexAndTensorTableExtensions() {
    using gridlock::ui::ArrayPlotDialog; using gridlock::ui::TensorSliceTableModel;
    ArrayPlotDialog dlg; const float c64[2] = {3.0f, 4.0f}; const double c128[4] = {-1.0, 0.0, 1.0, std::numeric_limits<double>::quiet_NaN()};
    QVERIFY(dlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(c64), sizeof(c64)), ArrayPlotDialog::ElementType::Complex64, 1, 1) && std::abs(dlg.currentSamples().first() - 5.0) < 1e-6);
    dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Phase); QVERIFY(std::abs(dlg.currentSamples().first() - std::atan2(4.0, 3.0)) < 1e-6);
    dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Real); QCOMPARE(dlg.currentSamples().first(), 3.0); dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Imaginary); QCOMPARE(dlg.currentSamples().first(), 4.0);
    dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Magnitude);
    QVERIFY(dlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(c128), sizeof(c128)), ArrayPlotDialog::ElementType::Complex128, 1, 2) && std::abs(dlg.currentSamples().first() - 1.0) < 1e-12 && dlg.nanCount() == 1);
    dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Phase); QVERIFY(std::abs(dlg.currentSamples().first() - std::acos(-1.0)) < 1e-12 && dlg.nanCount() == 1);
    dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Real); QCOMPARE(dlg.currentSamples().first(), -1.0); dlg.setComplexComponent(ArrayPlotDialog::ComplexComponent::Imaginary); QCOMPARE(dlg.currentSamples().first(), 0.0);
    QVector<double> m32(32, 1.5); dlg.setSamples(m32, 4, 8); const QString gp2d = dlg.buildGnuplotScript(QStringLiteral("/tmp/m.dat"));
    QVERIFY(gp2d.contains(QStringLiteral("splot")) && gp2d.contains(QStringLiteral("with pm3d")) && gp2d.contains(QStringLiteral("matrix")));
    dlg.setSamples({1.0, 2.0, 3.0, 4.0}, 1, 4); const QString gp1d = dlg.buildGnuplotScript(QStringLiteral("/tmp/l.dat")); QVERIFY(gp1d.contains(QStringLiteral("plot '")) && !gp1d.contains(QStringLiteral("splot")));
    QVector<double> t12(12); for (int i = 0; i < 12; ++i) t12[i] = static_cast<double>(i);
    dlg.setTensorDimensions(3, 2, 2); dlg.setSamples(t12, 2, 3); dlg.setTensorTableActive(true); auto* tm = dlg.tensorTableModel(); QVERIFY(dlg.isTensorTableActive() && tm && tm->rowCount() == 2 && tm->columnCount() == 3);
    dlg.setMemoryOrder(TensorSliceTableModel::MemoryOrder::CRowMajor); dlg.setZSlice(0); QVERIFY(tm->cellValue(0, 2) == 2.0 && tm->cellValue(1, 0) == 3.0 && tm->data(tm->index(1, 0), Qt::ToolTipRole).toString().contains(QStringLiteral("[3]")));
    dlg.setZSlice(1); QVERIFY(tm->cellValue(0, 0) == 6.0 && tm->cellValue(1, 2) == 11.0);
    dlg.setMemoryOrder(TensorSliceTableModel::MemoryOrder::FortranColMajor); dlg.setZSlice(0); QVERIFY(tm->cellValue(1, 0) == 1.0 && tm->cellValue(0, 1) == 2.0 && tm->cellValue(1, 2) == 5.0);
    dlg.setZSlice(1); QVERIFY(tm->cellValue(0, 1) == 8.0 && tm->cellValue(1, 2) == 11.0);
    QVector<double> big(120000); for (int i = 0; i < big.size(); ++i) big[i] = static_cast<double>(i);
    dlg.setTensorDimensions(400, 300, 1); dlg.setMemoryOrder(TensorSliceTableModel::MemoryOrder::CRowMajor); dlg.setSamples(big, 300, 400);
    QVERIFY(tm->rowCount() == 300 && tm->columnCount() == 400 && tm->cellValue(299, 399) == 119999.0);
}
/** @brief Verifies ArrayPlotDialog 1D/2D buffer decoding, stats, stop mutation diffing, CSV export, PointerGraphDialog persistent auto-updating roots, QSS disk cache, and lazy bottom tabs. */
void TestMainWindowUI::testArrayPlotterAndPersistentPointerGraph() {
    using gridlock::ui::ArrayPlotDialog; using gridlock::ui::PointerGraphDialog;
    ArrayPlotDialog plotDlg; QVERIFY(plotDlg.findChild<QLineEdit*>(QStringLiteral("arrayPlotExprEdit")) && plotDlg.findChild<QCheckBox*>(QStringLiteral("arrayPlotAutoRefreshCheck")) && plotDlg.isAutoRefreshEnabled());
    const double d0[4] = {1.0, 2.0, 3.0, 4.0}, d1[4] = {1.0, 9.0, 3.0, std::numeric_limits<double>::quiet_NaN()};
    QVERIFY(plotDlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(d0), sizeof(d0)), ArrayPlotDialog::ElementType::Float64, 1, 4) && plotDlg.currentSamples().size() == 4 && plotDlg.minValue() == 1.0 && plotDlg.maxValue() == 4.0 && plotDlg.meanValue() == 2.5 && std::abs(plotDlg.l2Norm() - std::sqrt(30.0)) < 1e-9 && plotDlg.nanInfCount() == 0);
    QVERIFY(plotDlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(d1), sizeof(d1)), ArrayPlotDialog::ElementType::Float64, 2, 2) && plotDlg.plotMode() == ArrayPlotDialog::PlotMode::Heatmap2D && plotDlg.nanCount() == 1 && plotDlg.changedElementCount() == 2);
    const float f0[3] = {-2.5f, 0.0f, 5.5f}; const int32_t i0[4] = {10, 20, 30, 40};
    QVERIFY(plotDlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(f0), sizeof(f0)), QStringLiteral("float"), 1, 3) && plotDlg.minValue() == -2.5 && plotDlg.maxValue() == 5.5 && plotDlg.loadFromRawBytes(QByteArray(reinterpret_cast<const char*>(i0), sizeof(i0)), QStringLiteral("int32_t"), 2, 2) && plotDlg.meanValue() == 25.0);
    QTemporaryDir tmpDir; QVERIFY(tmpDir.isValid()); const QString csvPath = tmpDir.filePath(QStringLiteral("field.csv")), pngPath = tmpDir.filePath(QStringLiteral("field.png"));
    QVERIFY(plotDlg.exportCsv(csvPath) && plotDlg.savePng(pngPath) && QFileInfo::exists(pngPath));
    QFile cf(csvPath); QVERIFY(cf.open(QIODevice::ReadOnly)); const QString csvText = QString::fromUtf8(cf.readAll()); QVERIFY(csvText.contains(QStringLiteral("10")) && csvText.contains(QStringLiteral("40")));
    PointerGraphDialog pgDlg; QVERIFY(pgDlg.findChild<QPushButton*>(QStringLiteral("pointerGraphRefreshBtn")) && pgDlg.findChild<QCheckBox*>(QStringLiteral("pointerGraphAutoRefreshCheck")) && pgDlg.isAutoRefreshEnabled());
    pgDlg.pinRootExpression(QStringLiteral("head")); pgDlg.upsertStructNode(QStringLiteral("head"), QStringLiteral("ListNode"), QStringLiteral("0x7fff1000"), {{QStringLiteral("val"), QStringLiteral("42"), QStringLiteral("int")}, {QStringLiteral("next"), QStringLiteral("0x7fff2000"), QStringLiteral("ListNode*")}});
    QVERIFY(pgDlg.pinnedRootExpressions() == QStringList{QStringLiteral("head")} && pgDlg.mutatedFieldCount() == 0);
    pgDlg.onTargetStopped(0); pgDlg.upsertStructNode(QStringLiteral("head"), QStringLiteral("ListNode"), QStringLiteral("0x7fff1000"), {{QStringLiteral("val"), QStringLiteral("99"), QStringLiteral("int")}, {QStringLiteral("next"), QStringLiteral("0x7fff2000"), QStringLiteral("ListNode*")}});
    QVERIFY(pgDlg.pinnedRootExpressions() == QStringList{QStringLiteral("head")} && pgDlg.isFieldMutated(QStringLiteral("head"), QStringLiteral("val")) && !pgDlg.isFieldMutated(QStringLiteral("head"), QStringLiteral("next")) && pgDlg.fieldMutationSummary(QStringLiteral("head"), QStringLiteral("val")) == QStringLiteral("42 -> 99") && pgDlg.mutatedFieldCount() == 1);
    testPointerGraphDagLayoutAndAliasSplitting(); testPointerGraphControlsAndExport(); testArrayPlotterScientificExtensions(); testComplexAndTensorTableExtensions();
    auto& tm = gridlock::core::ThemeManager::instance(); tm.applyTheme(QStringLiteral("gridlock_slate"), true);
    const QString qssCache = tm.cachedQssFilePath(QStringLiteral("gridlock_slate")); QVERIFY(QFileInfo::exists(qssCache) && QFileInfo(qssCache).size() > 100);
    QElapsedTimer startupTimer; startupTimer.start(); tm.initialize(); gridlock::ui::MainWindow mw; mw.show();
    const qint64 startupMs = startupTimer.elapsed(); QVERIFY2(startupMs < 450, qPrintable(QStringLiteral("Startup took %1 ms").arg(startupMs)));
    QApplication::processEvents(); auto* bottomTabs = mw.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    auto* pm = static_cast<gridlock::ui::HpcConsoleWidget*>(mw.findChild<QWidget*>(QStringLiteral("HpcConsoleDock")));
    auto* term = mw.findChild<gridlock::ui::TerminalDockWidget*>(); auto* ref = mw.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY(bottomTabs && pm && term && ref && !pm->isPackagesLoaded() && !term->isShellStarted() && !ref->isDocsetsInitialized());
    bottomTabs->setCurrentWidget(term); QApplication::processEvents(); bottomTabs->setCurrentWidget(ref); QApplication::processEvents(); bottomTabs->setCurrentWidget(pm); QApplication::processEvents();
    QVERIFY(term->isShellStarted() && ref->isDocsetsInitialized() && pm->isPackagesLoaded());
}
/** @brief Verifies GdbRankCoordinator signal disposition/catchpoint MI commands and SignalCatchpointDialog UI integration. */
void TestMainWindowUI::testSignalDispositionAndCatchpoints() {
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(2);
    coord.processGdbOutput(0, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401000\",func=\"main\",fullname=\"/tmp/main.cpp\",line=\"10\",args=[]}\n"));
    coord.processGdbOutput(1, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401000\",func=\"main\",fullname=\"/tmp/main.cpp\",line=\"10\",args=[]}\n"));
    QStringList sentCmds; connect(&coord, &gridlock::GdbRankCoordinator::commandSentToGdb, [&](int r, const QString& cmd) { if (r == 0) sentCmds.append(cmd.trimmed()); });
    coord.setSignalDisposition(QStringLiteral("SIGUSR1"), false, false, true); QVERIFY(sentCmds.contains(QStringLiteral("-interpreter-exec console \"handle SIGUSR1 nostop noprint pass\"")));
    coord.insertCatchpoint({CatchpointSpec::Kind::CppThrow, QStringLiteral("std::runtime_error"), -1, true}); coord.insertCatchpoint({CatchpointSpec::Kind::Syscall, QStringLiteral("mmap, openat, clone3, ioctl"), -1, true}); coord.insertCatchpoint({CatchpointSpec::Kind::LoadLib, QStringLiteral("libmpi"), -1, true});
    QVERIFY(sentCmds.contains(QStringLiteral("-catch-throw -r std::runtime_error")) && sentCmds.contains(QStringLiteral("-interpreter-exec console \"catch syscall mmap openat clone3 ioctl\"")) && sentCmds.contains(QStringLiteral("-catch-load -r libmpi")));
    gridlock::ui::SignalCatchpointDialog dlg(&coord); QCOMPARE(dlg.catchpoints().size(), 3);
    dlg.setSignalDispositionRow(QStringLiteral("SIGFPE"), true, true, false); QVERIFY(dlg.dispositionForSignal(QStringLiteral("SIGFPE")).stop && !dlg.dispositionForSignal(QStringLiteral("SIGFPE")).pass);
    dlg.removeCatchpointById(dlg.catchpoints().first().gdbId); QVERIFY(dlg.catchpoints().size() == 2 && sentCmds.contains(QStringLiteral("-break-delete 1")));
    gridlock::ui::MainWindow mw; mw.show(); QApplication::processEvents();
    auto* sigAct = mw.findChild<QAction*>(QStringLiteral("actionSignalsAndCatchpoints")); QVERIFY(sigAct && sigAct->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    sigAct->trigger(); QApplication::processEvents(); auto* mwSigDlg = mw.findChild<gridlock::ui::SignalCatchpointDialog*>(QStringLiteral("SignalCatchpointDialog")); QVERIFY(mwSigDlg && mwSigDlg->isVisible()); mwSigDlg->close();
}
/** @brief Verifies GdbConsoleWidget persistent gdb_history, Up/Down draft preservation, Ctrl+R reverse search, and Tab completion popup. */
void TestMainWindowUI::testGdbConsoleReadlineErgonomics() {
    QTemporaryDir tmpDir; QVERIFY(tmpDir.isValid()); gridlock::core::ConfigManager::instance().setGdbHistoryFilePathOverride(tmpDir.filePath(QStringLiteral("gdb_history")));
    struct HistGuard { ~HistGuard() { gridlock::core::ConfigManager::instance().setGdbHistoryFilePathOverride(QString()); } } guard;
    gridlock::ui::GdbConsoleWidget console; console.resize(640, 320); console.show(); QApplication::processEvents();
    auto* edit = console.commandEdit(); QVERIFY(edit != nullptr);
    for (const QString& cmd : {QStringLiteral("info registers"), QStringLiteral("break main"), QStringLiteral("x/16gx $rsp"), QStringLiteral("break main")}) { edit->setText(cmd); QTest::keyClick(edit, Qt::Key_Return); }
    QCOMPARE(console.commandHistory(), (QStringList{QStringLiteral("info registers"), QStringLiteral("x/16gx $rsp"), QStringLiteral("break main")}));
    edit->setText(QStringLiteral("print draft_val")); QTest::keyClick(edit, Qt::Key_Up); QCOMPARE(edit->text(), QStringLiteral("break main")); QTest::keyClick(edit, Qt::Key_Up); QCOMPARE(edit->text(), QStringLiteral("x/16gx $rsp"));
    QTest::keyClick(edit, Qt::Key_Down); QCOMPARE(edit->text(), QStringLiteral("break main")); QTest::keyClick(edit, Qt::Key_Down); QCOMPARE(edit->text(), QStringLiteral("print draft_val"));
    gridlock::ui::GdbConsoleWidget reloadedConsole; QCOMPARE(reloadedConsole.commandHistory(), console.commandHistory());
    edit->setText(QStringLiteral("initial_input")); QTest::keyClick(edit, Qt::Key_R, Qt::ControlModifier); QVERIFY(console.isReverseSearchActive() && console.reverseSearchLabel() && console.reverseSearchLabel()->isVisible());
    QTest::keyClicks(edit, QStringLiteral("in")); QVERIFY(console.reverseSearchQuery() == QStringLiteral("in") && edit->text() == QStringLiteral("break main"));
    QTest::keyClick(edit, Qt::Key_R, Qt::ControlModifier); QCOMPARE(edit->text(), QStringLiteral("info registers")); QTest::keyClick(edit, Qt::Key_Escape); QVERIFY(!console.isReverseSearchActive() && !console.reverseSearchLabel()->isVisible() && edit->text() == QStringLiteral("initial_input"));
    edit->setText(QStringLiteral("disas")); edit->setCursorPosition(5); QTest::keyClick(edit, Qt::Key_Tab); QCOMPARE(edit->text(), QStringLiteral("disassemble"));
    edit->setText(QStringLiteral("info ")); edit->setCursorPosition(5); QTest::keyClick(edit, Qt::Key_Tab); QVERIFY(console.completionPopup() && console.completionPopup()->isVisible() && console.completionPopup()->count() >= 4); QTest::keyClick(edit, Qt::Key_Escape); QVERIFY(!console.completionPopup()->isVisible());
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(1); coord.processGdbOutput(0, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401000\",func=\"main\",fullname=\"/tmp/main.cpp\",line=\"10\",args=[]}\n"));
    connect(&console, &gridlock::ui::GdbConsoleWidget::completionRequested, &coord, &gridlock::GdbRankCoordinator::requestConsoleCompletions); connect(&coord, &IBackendCoordinator::consoleCompletionsReady, &console, &gridlock::ui::GdbConsoleWidget::onConsoleCompletionsReady);
    edit->setText(QStringLiteral("b ma")); edit->setCursorPosition(4); QTest::keyClick(edit, Qt::Key_Tab); coord.processGdbOutput(0, QStringLiteral("~\"b main\\n\"\n~\"b malloc\\n\"\n211^done\n"));
    QVERIFY(console.completionPopup()->isVisible() && console.completionPopup()->count() == 2); QTest::keyClick(edit, Qt::Key_Down); QTest::keyClick(edit, Qt::Key_Return); QVERIFY(!console.completionPopup()->isVisible() && edit->text() == QStringLiteral("b malloc"));
}
/** @brief Verifies nexti, Ctrl+F10 Run to Cursor, draggable gutter PC jump, lexical scope guard, Force Early Return, and shortcut uniqueness. */
void TestMainWindowUI::testNonLinearExecutionControl() {
    // 1. Verify GdbRankCoordinator emits exact MI3 commands for nexti, until, jump, and return.
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(2);
    coord.processGdbOutput(0, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401120\",func=\"solve\",fullname=\"/tmp/solver.cpp\",line=\"4\",args=[]}\n"));
    coord.processGdbOutput(1, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401120\",func=\"solve\",fullname=\"/tmp/solver.cpp\",line=\"4\",args=[]}\n"));
    QStringList sent; connect(&coord, &gridlock::GdbRankCoordinator::commandSentToGdb, [&](int r, const QString& c) { if (r == 0) sent.append(c.trimmed()); });
    coord.nextInstruction(0); coord.runToCursor(QStringLiteral("/tmp/solver.cpp"), 12, 0); coord.runToAddress(0x401180ULL, 0);
    coord.jumpToLocation(QStringLiteral("/tmp/solver.cpp"), 15, 0); coord.jumpToAddress(0x401190ULL, 0); coord.forceEarlyReturn(QString(), 0); coord.forceEarlyReturn(QStringLiteral("42"), 0);
    QVERIFY(sent.contains(QStringLiteral("-exec-next-instruction")) && sent.contains(QStringLiteral("-exec-until \"/tmp/solver.cpp:12\"")) && sent.contains(QStringLiteral("-exec-until *0x401180")));
    QVERIFY(sent.contains(QStringLiteral("-exec-jump \"/tmp/solver.cpp:15\"")) && sent.contains(QStringLiteral("-exec-jump *0x401190")) && sent.contains(QStringLiteral("-exec-return")) && sent.contains(QStringLiteral("-exec-return 42")));
    // 2. Verify lexical scope safety guard distinguishes safe intra-block jumps from declaration, block, and cross-function hazards.
    const QString sampleCpp = QStringLiteral("int solve(int x) {\n  int a = 1;\n  a += x;\n  a *= 2;\n  int b = 7;\n  if (a > 0) {\n    a += b;\n  }\n  return a;\n}\nint helper() {\n  return 0;\n}\n");
    QVERIFY(gridlock::ui::MainWindow::checkJumpLexicalScopeHazard(sampleCpp, 3, 4, QStringLiteral("solve")).isEmpty());
    QVERIFY(!gridlock::ui::MainWindow::checkJumpLexicalScopeHazard(sampleCpp, 4, 6, QStringLiteral("solve")).isEmpty() && !gridlock::ui::MainWindow::checkJumpLexicalScopeHazard(sampleCpp, 3, 7, QStringLiteral("solve")).isEmpty() && !gridlock::ui::MainWindow::checkJumpLexicalScopeHazard(sampleCpp, 3, 12, QStringLiteral("solve")).isEmpty());
    // 3. Verify draggable gutter PC arrow emits jumpToLineRequested and MainWindow routes Ctrl+F10, nexti, DisassemblyView, & Force Early Return with zero shortcut collisions.
    static QStringList s_execWarnings; s_execWarnings.clear();
    const QtMessageHandler prevH = qInstallMessageHandler([](QtMsgType t, const QMessageLogContext&, const QString& m) { if (t == QtWarningMsg) s_execWarnings.append(m); });
    struct WarnGuard { QtMessageHandler p; ~WarnGuard() { qInstallMessageHandler(p); } } wg{prevH};
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.show(); QApplication::processEvents(); mw.setCoordinator(&coord); sent.clear();
    auto* sv = mw.sourceCodeView(); auto* dv = mw.disassemblyView(); QVERIFY(sv && dv);
    sv->setCurrentFile(QStringLiteral("/tmp/solver.cpp")); sv->setSourceCode(sampleCpp); sv->highlightExecutionLine(3); QApplication::processEvents();
    QWidget* gutter = nullptr; for (QWidget* w : sv->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) if (w != sv->viewport() && w->width() == sv->lineNumberAreaWidth()) { gutter = w; break; }
    QVERIFY(gutter != nullptr); QSignalSpy jumpSpy(sv, &gridlock::ui::SourceCodeView::jumpToLineRequested);
    const int rowH = qMax(14, sv->fontMetrics().lineSpacing()); const QPoint pStart(10, 2 * rowH + rowH / 2), pEnd(10, 3 * rowH + rowH / 2);
    QTest::mousePress(gutter, Qt::LeftButton, Qt::NoModifier, pStart); QTest::mouseMove(gutter, pEnd); QApplication::processEvents();
    QVERIFY(sv->isDraggingExecutionPc() && sv->dragTargetLine() == 4);
    QTest::mouseRelease(gutter, Qt::LeftButton, Qt::NoModifier, pEnd); QApplication::processEvents();
    QCOMPARE(jumpSpy.count(), 1); QCOMPARE(jumpSpy.first().at(1).toInt(), 4); QVERIFY(sent.contains(QStringLiteral("-exec-jump \"/tmp/solver.cpp:4\"")));
    sv->setTextCursor(QTextCursor(sv->document()->findBlockByNumber(8))); sv->setFocus();
    QTest::keyClick(&mw, Qt::Key_F10, Qt::ControlModifier); QApplication::processEvents(); QVERIFY(sent.contains(QStringLiteral("-exec-until \"/tmp/solver.cpp:9\"")));
    QTest::keyClick(&mw, Qt::Key_F10, Qt::ControlModifier | Qt::ShiftModifier); QApplication::processEvents(); QVERIFY(sent.contains(QStringLiteral("-exec-next-instruction")));
    auto* nextiBtn = dv->findChild<QPushButton*>(QStringLiteral("disasmNextiBtn")); QVERIFY(nextiBtn && dv->findChild<QPushButton*>(QStringLiteral("disasmStepiBtn")));
    sent.clear(); nextiBtn->click(); QVERIFY(sent.contains(QStringLiteral("-exec-next-instruction")));
    emit dv->runToAddressRequested(0x401180ULL); emit dv->jumpToAddressRequested(0x401190ULL); QVERIFY(sent.contains(QStringLiteral("-exec-until *0x401180")) && sent.contains(QStringLiteral("-exec-jump *0x401190")));
    QVERIFY(mw.forceEarlyReturnWorkflow(QStringLiteral("7"), false) && sent.contains(QStringLiteral("-exec-return 7")));
    QVERIFY(mw.findChild<QAction*>(QStringLiteral("actionNextInstruction")) && mw.findChild<QAction*>(QStringLiteral("actionRunToCursor")) && mw.findChild<QAction*>(QStringLiteral("actionJumpToLine")) && mw.findChild<QAction*>(QStringLiteral("actionForceEarlyReturn")));
    for (const QString& w : s_execWarnings) QVERIFY2(!w.contains(QStringLiteral("Ambiguous shortcut overload"), Qt::CaseInsensitive), qPrintable(w));
    mw.setCoordinator(nullptr);
}
/** @brief Verifies non-destructive breakpoint enable/disable hollow markers, tbreak, rbreak, and GDB breakpoint command lists. */
void TestMainWindowUI::testBreakpointLifecycleAndCommands() {
    // 1. Verify GdbRankCoordinator emits exact MI3 commands for disable/enable, tbreak, rbreak, and -break-commands.
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(2);
    coord.processGdbOutput(0, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401120\",func=\"solve\",fullname=\"/tmp/solver.cpp\",line=\"4\",args=[]}\n"));
    coord.processGdbOutput(1, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401120\",func=\"solve\",fullname=\"/tmp/solver.cpp\",line=\"4\",args=[]}\n"));
    QStringList sent; connect(&coord, &gridlock::GdbRankCoordinator::commandSentToGdb, [&](int r, const QString& c) { if (r == 0) sent.append(c.trimmed()); });
    QSignalSpy createdSpy(&coord, &IBackendCoordinator::breakpointRecordCreated); QSignalSpy tempHitSpy(&coord, &IBackendCoordinator::temporaryBreakpointHit);
    coord.setBreakpoint(QStringLiteral("/tmp/solver.cpp"), 10, QStringLiteral("rank == 0"), -1);
    coord.processGdbOutput(0, QStringLiteral("^done,bkpt={number=\"3\",type=\"breakpoint\",disp=\"keep\",enabled=\"y\",fullname=\"/tmp/solver.cpp\",line=\"10\"}\n"));
    coord.setBreakpointEnabled(QStringLiteral("/tmp/solver.cpp"), 10, false); coord.setBreakpointEnabled(QStringLiteral("/tmp/solver.cpp"), 10, true);
    coord.setBreakpointCommands(QStringLiteral("/tmp/solver.cpp"), 10, {QStringLiteral("silent"), QStringLiteral("printf \"rank=%d\\n\", rank"), QStringLiteral("continue")});
    coord.setBreakpointCommands(QStringLiteral("/tmp/solver.cpp"), 10, {});
    coord.insertTemporaryBreakpoint(QStringLiteral("/tmp/solver.cpp"), 14);
    coord.processGdbOutput(0, QStringLiteral("^done,bkpt={number=\"4\",type=\"breakpoint\",disp=\"del\",enabled=\"y\",fullname=\"/tmp/solver.cpp\",line=\"14\"}\n=breakpoint-deleted,id=\"4\"\n"));
    coord.insertRegexBreakpoint(QStringLiteral("MPI_.*|Solver::step.*"));
    coord.processGdbOutput(0, QStringLiteral("=breakpoint-created,bkpt={number=\"5\",type=\"breakpoint\",disp=\"keep\",enabled=\"y\",func=\"MPI_Send\",fullname=\"/tmp/solver.cpp\",line=\"20\"}\n"));
    QVERIFY(sent.contains(QStringLiteral("-break-disable 3")) && sent.contains(QStringLiteral("-break-enable 3")) && sent.contains(QStringLiteral("-break-commands 3")));
    QVERIFY(sent.contains(QStringLiteral("-break-commands 3 \"silent\" \"printf \\\"rank=%d\\\\n\\\", rank\" \"continue\"")) && sent.contains(QStringLiteral("-break-insert -t -f /tmp/solver.cpp:14")) && sent.contains(QStringLiteral("-interpreter-exec console \"rbreak MPI_.*|Solver::step.*\"")));
    QVERIFY(tempHitSpy.count() == 1 && tempHitSpy.first().at(2).toInt() == 14 && createdSpy.count() >= 1 && !coord.regexCreatedBreakpoints().isEmpty() && coord.regexCreatedBreakpoints().last().gdbId == 5 && coord.regexCreatedBreakpoints().last().line == 20);
    // 2. Verify SourceCodeView non-destructive disable retains metadata & renders DisabledBreakpoint, and TemporaryBreakpoint badge state.
    gridlock::ui::SourceCodeView srcView; QString sampleCode;
    for (int i = 1; i <= 25; ++i) sampleCode += QStringLiteral("    int step_%1 = %1;\n").arg(i);
    srcView.setCurrentFile(QStringLiteral("/tmp/solver.cpp")); srcView.setSourceCode(sampleCode); srcView.resize(640, 420); srcView.show(); QApplication::processEvents();
    srcView.toggleBreakpoint(10); srcView.setBreakpointOptions(10, QStringLiteral("rank == 0"), 2, QString()); srcView.setBreakpointCommands(10, {QStringLiteral("silent"), QStringLiteral("continue")});
    srcView.handleBreakpointPopoverResult(10, true, true, false, QStringLiteral("rank == 0"), QStringLiteral("All"), false);
    QVERIFY(srcView.hasBreakpoint(10) && !srcView.isBreakpointEnabledForLine(10) && srcView.gutterMarkerStateForLine(10) == QStringLiteral("DisabledBreakpoint"));
    QVERIFY(srcView.breakpointCondition(10) == QStringLiteral("rank == 0") && srcView.breakpointIgnoreCountForLine(10) == 2 && srcView.breakpointCommandsForLine(10) == (QStringList{QStringLiteral("silent"), QStringLiteral("continue")}));
    QVERIFY(srcView.breakpointTooltipForLine(10).contains(QStringLiteral("[DISABLED]")) && srcView.breakpointTooltipForLine(10).contains(QStringLiteral("GDB Commands: 2")));
    srcView.setBreakpointEnabled(10, true); QVERIFY(srcView.isBreakpointEnabledForLine(10) && srcView.gutterMarkerStateForLine(10) == QStringLiteral("ConditionalBreakpoint"));
    srcView.setTemporaryBreakpoint(14, true); QVERIFY(srcView.hasBreakpoint(14) && srcView.isTemporaryBreakpointForLine(14) && srcView.gutterMarkerStateForLine(14) == QStringLiteral("TemporaryBreakpoint"));
    srcView.removeBreakpointQuietly(14); QVERIFY(!srcView.hasBreakpoint(14));
    // 3. Verify ConditionalBreakpointDialog non-destructive Enabled toggle, Temporary checkbox, and Commands editor.
    gridlock::ui::ConditionalBreakpointDialog bpDlg(QString(), &srcView); bpDlg.setSourceDocumentContext(QStringLiteral("/tmp/solver.cpp"), sampleCode, 10); bpDlg.setTargetLineContext(10, QStringLiteral("int step_10 = 10;"));
    QSignalSpy enSpy(&bpDlg, &gridlock::ui::ConditionalBreakpointDialog::breakpointEnabledToggled);
    auto* enChk = bpDlg.findChild<QCheckBox*>(QStringLiteral("bpEnabledCheck")); auto* tmpChk = bpDlg.findChild<QCheckBox*>(QStringLiteral("bpTemporaryCheck")); auto* cmdEd = bpDlg.findChild<QPlainTextEdit*>(QStringLiteral("bpCommandsEdit"));
    QVERIFY(enChk && tmpChk && cmdEd); enChk->setChecked(false); tmpChk->setChecked(true); cmdEd->setPlainText(QStringLiteral("silent\nbt 2\ncontinue"));
    QVERIFY(enSpy.count() == 1 && !enSpy.first().at(2).toBool() && !bpDlg.isRemoveRequested() && bpDlg.isTemporary() && bpDlg.commands() == (QStringList{QStringLiteral("silent"), QStringLiteral("bt 2"), QStringLiteral("continue")}));
    // 4. Verify MainWindow Debug menu rbreak action, coordinator wiring, temporary hit auto-clear, and Command Palette entry.
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.show(); QApplication::processEvents(); mw.setCoordinator(&coord); sent.clear();
    QVERIFY(mw.findChild<QAction*>(QStringLiteral("actionRegexBreakpoint")) != nullptr && mw.insertRegexBreakpointWorkflow(QStringLiteral("Solver::.*"), false) && sent.contains(QStringLiteral("-interpreter-exec console \"rbreak Solver::.*\"")));
    auto* mwSv = mw.sourceCodeView(); QVERIFY(mwSv != nullptr); mwSv->setCurrentFile(QStringLiteral("/tmp/solver.cpp")); mwSv->setSourceCode(sampleCode);
    mwSv->setTemporaryBreakpoint(14, true); QVERIFY(mwSv->hasBreakpoint(14));
    emit coord.temporaryBreakpointHit(0, QStringLiteral("/tmp/solver.cpp"), 14); QApplication::processEvents(); QVERIFY(!mwSv->hasBreakpoint(14));
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier); QApplication::processEvents();
    auto* pal = mw.findChild<gridlock::ui::CommandPaletteDialog*>(QStringLiteral("CommandPaletteDialog")); QVERIFY(pal && pal->isVisible());
    pal->setFilterText(QStringLiteral("> rbreak")); QVERIFY(pal->resultCount() > 0 && pal->resultItemAt(0).title.contains(QStringLiteral("rbreak"), Qt::CaseInsensitive));
    QTest::keyClick(pal->inputEdit(), Qt::Key_Escape); mw.setCoordinator(nullptr);
}
/** @brief Verifies Copy-on-Write GDB checkpoints (checkpoint/restart/info/delete), /proc fd/maps safety checks, and CheckpointDialog integration. */
void TestMainWindowUI::testCowCheckpointsAndProcSafety() {
    using gridlock::ui::CheckpointDialog; using gridlock::ui::SafetyLevel;
    QTemporaryDir fakeProc; QVERIFY(fakeProc.isValid());
    auto mkProc = [&](int pid, const QMap<QString, QString>& fds, const QByteArray& maps) {
        const QString pDir = fakeProc.filePath(QString::number(pid)), fdDir = pDir + QStringLiteral("/fd"); QVERIFY(QDir().mkpath(fdDir));
        for (auto it = fds.constBegin(); it != fds.constEnd(); ++it) QCOMPARE(::symlink(it.value().toUtf8().constData(), (fdDir + QLatin1Char('/') + it.key()).toUtf8().constData()), 0);
        QFile mf(pDir + QStringLiteral("/maps")); QVERIFY(mf.open(QIODevice::WriteOnly)); mf.write(maps);
    };
    mkProc(1001, {{QStringLiteral("0"), QStringLiteral("/dev/pts/0")}, {QStringLiteral("1"), QStringLiteral("/dev/pts/0")}}, "555555554000-555555558000 r-xp 00000000 08:01 1 /usr/bin/solver\n7ffff7a00000-7ffff7b00000 rw-p 00000000 00:00 0 [heap]\n");
    mkProc(1002, {{QStringLiteral("3"), QStringLiteral("socket:[12345]")}, {QStringLiteral("4"), QStringLiteral("pipe:[67890]")}}, "7ffff6000000-7ffff6400000 rw-s 00000000 00:18 2 /dev/shm/openmpi_vader_segment\n");
    mkProc(1003, {{QStringLiteral("5"), QStringLiteral("/dev/infiniband/uverbs0")}}, "7ffff5000000-7ffff5100000 rw-s 00000000 00:06 3 /dev/infiniband/uverbs0\n");
    const auto repSafe = gridlock::ui::inspectProcessCheckpointSafety(1001, fakeProc.path()), repWarn = gridlock::ui::inspectProcessCheckpointSafety(1002, fakeProc.path()), repUnsafe = gridlock::ui::inspectProcessCheckpointSafety(1003, fakeProc.path());
    QVERIFY(repSafe.level == SafetyLevel::Safe && repSafe.warnings.isEmpty() && repSafe.blockers.isEmpty() && repSafe.badgeText == QStringLiteral("SAFE"));
    QVERIFY(repWarn.level == SafetyLevel::Warning && repWarn.blockers.isEmpty() && repWarn.warnings.size() >= 3 && repWarn.badgeText.contains(QStringLiteral("WARNING")));
    QVERIFY(repUnsafe.level == SafetyLevel::BlockedOrUnsafe && !repUnsafe.blockers.isEmpty() && repUnsafe.badgeText.contains(QStringLiteral("UNSAFE")));
    gridlock::GdbRankCoordinator coord; coord.initializeMockSession(2);
    coord.processGdbOutput(0, QStringLiteral("*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x401120\",func=\"solve\",fullname=\"/tmp/solver.cpp\",line=\"28\",args=[]}\n"));
    QStringList sent; connect(&coord, &gridlock::GdbRankCoordinator::commandSentToGdb, [&](int r, const QString& c) { if (r == 0) sent.append(c.trimmed()); });
    QSignalSpy cpSpy(&coord, &IBackendCoordinator::checkpointsReady); coord.createCheckpoint(0, QStringLiteral("before halo")); QVERIFY(sent.contains(QStringLiteral("212-interpreter-exec console \"checkpoint\"")));
    coord.processGdbOutput(0, QStringLiteral("~\"checkpoint 1: fork returned pid 4242.\\n\"\n212^done\n"));
    QVERIFY(cpSpy.count() >= 1 && coord.checkpointsForRank(0).size() == 1 && coord.checkpointsForRank(0).first().id == 1 && coord.checkpointsForRank(0).first().forkPid == 4242 && coord.checkpointsForRank(0).first().note == QStringLiteral("before halo") && sent.contains(QStringLiteral("213-interpreter-exec console \"info checkpoints\"")));
    coord.processGdbOutput(0, QStringLiteral("~\"  1 process 4242 at 0x401120, file solver.cpp, line 28\\n\"\n~\"  0 process 4200 (main process) at 0x401100\\n\"\n213^done\n"));
    QVERIFY(coord.checkpointsForRank(0).size() == 2 && coord.checkpointsForRank(0).first().note == QStringLiteral("before halo") && coord.checkpointsForRank(0).first().location.contains(QStringLiteral("solver.cpp")));
    coord.restoreCheckpoint(0, 1); coord.deleteCheckpoint(0, 1);
    QVERIFY(sent.contains(QStringLiteral("-interpreter-exec console \"restart 1\"")) && sent.contains(QStringLiteral("-interpreter-exec console \"delete checkpoint 1\"")) && coord.checkpointsForRank(0).size() == 1);
    CheckpointDialog cpDlg(&coord); cpDlg.setProcRootForTesting(fakeProc.path()); cpDlg.show(); QApplication::processEvents();
    QVERIFY(cpDlg.findChild<QPushButton*>(QStringLiteral("checkpointCloseBtn")) && cpDlg.findChild<QPushButton*>(QStringLiteral("createCheckpointBtn")) && cpDlg.checkpointTable() && cpDlg.checkpointTable()->rowCount() == 1);
    sent.clear(); QVERIFY(cpDlg.createCheckpointNow(true) && sent.contains(QStringLiteral("212-interpreter-exec console \"checkpoint\""))); cpDlg.close();
    gridlock::ui::MainWindow mw; mw.resize(1440, 900); mw.show(); QApplication::processEvents();
    auto* cpAct = mw.findChild<QAction*>(QStringLiteral("actionProcessCheckpoints")); QVERIFY(cpAct && cpAct->shortcut() == QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_K));
    QTest::keyClick(&mw, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier); QApplication::processEvents(); QVERIFY(mw.checkpointDialog() && mw.checkpointDialog()->isVisible()); mw.checkpointDialog()->close();
    QTest::keyClick(&mw, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier); QApplication::processEvents();
    auto* pal = mw.findChild<gridlock::ui::CommandPaletteDialog*>(QStringLiteral("CommandPaletteDialog")); QVERIFY(pal && pal->isVisible());
    pal->setFilterText(QStringLiteral("> Process Checkpoints")); QVERIFY(pal->resultCount() > 0 && pal->resultItemAt(0).title.contains(QStringLiteral("Process Checkpoints")));
    QTest::keyClick(pal->inputEdit(), Qt::Key_Escape);
}


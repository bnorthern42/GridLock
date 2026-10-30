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
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QRadioButton>
#include <QString>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextEdit>
#include <QtTest/QtTest>
#include <cmath>

#include "../src/core/managers/ThemeManager.hpp"
#include "../src/ui/EditorTabManager.hpp"
#include "../src/ui/MainWindow.hpp"
#include "../src/ui/dialogs/ConditionalBreakpointDialog.hpp"
#include "../src/ui/dialogs/PreferencesDialog.hpp"
#include "../src/ui/docks/TerminalDockWidget.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/views/MemView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
#include "../src/ui/views/SourceCodeView.hpp"
#include "../src/ui/widgets/MpiDiagnosticsWidget.hpp"
#include "../src/ui/widgets/ProjectExplorerWidget.hpp"
#include "../src/ui/widgets/ReferenceManualWidget.hpp"

/**
 * @brief Verifies dynamic statement mapping, loop back-edge/branch control-flow stepping, and rank context.
 *
 * Test workflow:
 * 1. Inserts 5 comment lines at top of SourceCodeView, calls rebuildDynamicSourceLineMap(),
 *    and verifies dynamic resolution of MPI_Init, MPI_Finalize, and return 0 statements.
 * 2. Starts debugging workflow without breakpoints, steps through rank-conditional branches
 *    and loop iterations, validating that jmp jumps back to loop header 0x00401149ULL
 *    while i < 2, jg branches to MPI_Finalize (0x00401178ULL) after 3 passes, and ret
 *    highlights return 0;.
 * 3. Validates active rank tracking between ServerRackView and MainWindow, confirming
 *    that je branches to rank 0 coordinator block or falls through to worker block.
 */
void TestMainWindowUI::testDynamicSourceMappingLoopBranchSteppingAndRankContext() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();

    // Ensure tutorial/inspection_demo.cpp is loaded in SourceCodeView.
    if (!mainWindow.findChild<gridlock::ui::SourceCodeView*>()) {
        const QString demoPath = QFileInfo::exists(QStringLiteral("tutorial/inspection_demo.cpp"))
                                     ? QStringLiteral("tutorial/inspection_demo.cpp")
                                     : QStringLiteral("../tutorial/inspection_demo.cpp");
        mainWindow.loadSourceFile(demoPath);
    }
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    auto* sourceView = mainWindow.findChild<gridlock::ui::SourceCodeView*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* serverRack = mainWindow.findChild<gridlock::ui::ServerRackView*>();
    QVERIFY(sourceView != nullptr && disasmView != nullptr && serverRack != nullptr);

    // =========================================================================
    // Part 1: Dynamic Statement-Based Source Line Resolution (No Hardcoding)
    // =========================================================================
    // Prepend 5 comment lines to shift all subsequent statement line numbers by 5.
    const QString originalCode = sourceView->toPlainText();
    const QString shiftedCode = QStringLiteral(
        "// Comment banner line 1\n"
        "// Comment banner line 2\n"
        "// Comment banner line 3\n"
        "// Comment banner line 4\n"
        "// Comment banner line 5\n") + originalCode;
    sourceView->setPlainText(shiftedCode);
    mainWindow.rebuildDynamicSourceLineMap();

    const int initLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Init"));
    const int finalizeLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Finalize"));
    const int returnLine = mainWindow.findSourceLineForStatement(QStringLiteral("return 0"));

    // Statements must be dynamically located at shifted positions.
    QVERIFY2(initLine > 5, "Dynamic scanner must find MPI_Init below inserted comments");
    QVERIFY2(finalizeLine > initLine, "Dynamic scanner must locate MPI_Finalize below MPI_Init");
    QVERIFY2(returnLine >= finalizeLine, "Dynamic scanner must locate return 0 below MPI_Finalize");

    // Dynamic bidirectional mapping must match instructions to shifted source lines.
    QCOMPARE(mainWindow.sourceLineToAddress(initLine), 0x0040113dULL);
    QCOMPARE(mainWindow.addressToSourceLine(0x0040113dULL), initLine);
    QCOMPARE(mainWindow.sourceLineToAddress(finalizeLine), 0x00401178ULL);
    QCOMPARE(mainWindow.addressToSourceLine(0x00401178ULL), finalizeLine);
    QCOMPARE(mainWindow.sourceLineToAddress(returnLine), 0x00401183ULL);
    QCOMPARE(mainWindow.addressToSourceLine(0x00401183ULL), returnLine);

    // =========================================================================
    // Part 2: Active Rank Stepping Context
    // =========================================================================
    // By default, active stepping rank should be 0.
    QCOMPARE(mainWindow.activeSteppingRank(), 0);

    // Selecting rank 2 in ServerRackView must update activeSteppingRank.
    serverRack->selectRank(2);
    QApplication::processEvents();
    QCOMPARE(mainWindow.activeSteppingRank(), 2);

    // Reset selection to rank 0 for initial control flow stepping.
    serverRack->selectRank(0);
    QApplication::processEvents();
    QCOMPARE(mainWindow.activeSteppingRank(), 0);

    // =========================================================================
    // Part 3: True Control-Flow Loop & Rank-Conditional Branch Stepping
    // =========================================================================
    sourceView->setBreakpoints(QSet<int>());
    mainWindow.stopTargetWorkflow();
    mainWindow.startTargetWorkflow();
    QApplication::processEvents();

    // Verify initial pause at function entry (0x00401120ULL).
    QCOMPARE(disasmView->currentAddress(), 0x00401120ULL);

    // Step through function prologue up to loop header.
    while (disasmView->currentAddress() < 0x00401149ULL) {
        mainWindow.stepInstructionWorkflow();
        QApplication::processEvents();
    }
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);

    // Loop Iteration 0:
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x0040114dULL);
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x0040114fULL);

    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401152ULL);
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401155ULL);

    // Stepping at 0x00401155 with rank 0 MUST branch to 0x00401168.
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401168ULL);

    // 0x401168 (xor) -> 0x40116a (add i, 1) -> 0x40116e (jmp 0x00401149)
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x0040116aULL);
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x0040116eULL);

    // Stepping at 0x0040116e MUST jump back to loop header 0x00401149ULL.
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);

    // Loop Iteration 1 & 2: execute 7 single-steps through coordinator body to jmp back-edge.
    for (int iter = 1; iter <= 2; ++iter) {
        for (int s = 0; s < 7; ++s) mainWindow.stepInstructionWorkflow();
        QCOMPARE(disasmView->currentAddress(), 0x0040116eULL);
        // Stepping at 0x0040116e MUST jump back to loop header 0x00401149ULL.
        mainWindow.stepInstructionWorkflow();
        QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);
    }

    // Loop Exit (i == 3 > 2): step through cmp to jg and take exit branch to MPI_Finalize.
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x0040114dULL);
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401178ULL);

    // Step through MPI_Finalize call, leave, and ret, verifying return 0 highlight.
    for (int s = 0; s < 3; ++s) mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401183ULL);
    QCOMPARE(sourceView->executionLine(), returnLine);

    // Test Worker Rank (Rank 2) conditional branch behavior:
    mainWindow.stopTargetWorkflow();
    serverRack->selectRank(2);
    mainWindow.startTargetWorkflow();
    QApplication::processEvents();

    while (disasmView->currentAddress() < 0x00401155ULL) {
        mainWindow.stepInstructionWorkflow();
        QApplication::processEvents();
    }
    QCOMPARE(disasmView->currentAddress(), 0x00401155ULL);

    // For active rank 2, je 0x00401168 condition is false -> must fall through to worker block (0x00401157ULL)!
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401157ULL);

    // Step through worker block: 0x401157 -> 0x40115f -> 0x401163 (jmp 0x00401149)
    mainWindow.stepInstructionWorkflow(); // -> 0x40115f
    mainWindow.stepInstructionWorkflow(); // -> 0x401163
    QCOMPARE(disasmView->currentAddress(), 0x00401163ULL);

    // Stepping at 0x00401163 MUST jump back to loop header 0x00401149ULL.
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);
}

/**
 * @brief Verifies Seek in Memory View tab switching, base address update, and mnemonic F1 doc lookup.
 *
 * Test workflow:
 * 1. Switches bottom console tabs away from MEMORY VIEW (e.g. to Terminal / index 0).
 * 2. Triggers "Seek in Memory View" on instruction 0x00401149ULL in DisassemblyView.
 * 3. Verifies that BottomConsoleTabs automatically switches to MEMORY VIEW, updates
 *    currentBaseAddress() to 0x00401149ULL, and populates hex dump bytes starting with
 *    instruction opcode bytes 83 7d fc 02 (cmp dword [rbp-0x4], 0x2).
 * 4. Verifies seeking to .rodata string address 0x00402010ULL updates currentBaseAddress()
 *    to 0x00402010ULL and displays string bytes.
 * 5. Triggers "Lookup in Reference Manual (F1)" on 0x00401125ULL (mov rbp, rsp).
 * 6. Verifies ReferenceManualWidget is shown, displays MOV instruction reference documentation,
 *    and does NOT contain "MPI_Init" or "MPI-3.1 Standard".
 */
void TestMainWindowUI::testSeekInMemoryViewAndMnemonicDocLookup() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>();
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY(disasmView != nullptr && memView != nullptr && bottomTabs != nullptr && refManual != nullptr);

    // =========================================================================
    // Part 1: Seek in Memory View Switches Tab & Updates Base Address + Bytes
    // =========================================================================
    // Start by selecting a different tab (e.g. index 0: Terminal).
    bottomTabs->setCurrentIndex(0);
    QApplication::processEvents();
    QVERIFY(bottomTabs->currentWidget() != memView);

    // Trigger context menu action "Seek in Memory View" at loop header instruction 0x00401149ULL.
    const bool seekTriggered = disasmView->triggerContextAction(0x00401149ULL, QStringLiteral("Seek in Memory View"));
    QVERIFY2(seekTriggered, "triggerContextAction for 'Seek in Memory View' must succeed");
    QApplication::processEvents();

    // Verify MainWindow switched bottom console tabs to MEMORY VIEW.
    QCOMPARE(bottomTabs->currentWidget(), memView);
    // Verify MemView base address updated to requested virtual address.
    QCOMPARE(memView->currentBaseAddress(), 0x00401149ULL);

    // Verify MemView hex dump contains the instruction bytes: 83 7d fc 02
    auto* dumpEdit = memView->findChild<QTextEdit*>(QStringLiteral("cutterHexDumpEdit"));
    QVERIFY(dumpEdit != nullptr);
    const QString hexDumpText = dumpEdit->toPlainText();
    QVERIFY2(hexDumpText.contains(QStringLiteral("83 7d fc 02"), Qt::CaseInsensitive),
             qPrintable(QStringLiteral("Expected 83 7d fc 02 in hex dump, got: %1").arg(hexDumpText)));

    // Verify seeking to .rodata string address 0x00402010ULL updates MemView base address.
    mainWindow.seekToAddressOrExpression(QStringLiteral("0x00402010"));
    QApplication::processEvents();
    QCOMPARE(memView->currentBaseAddress(), 0x00402010ULL);

    // =========================================================================
    // Part 2: Looking Up "mov" (F1 on mov rbp, rsp) Never Shows MPI Reference
    // =========================================================================
    // Trigger "Lookup in Reference Manual (F1)" on 0x00401125ULL (mov rbp, rsp).
    const bool docTriggered = disasmView->triggerContextAction(0x00401125ULL, QStringLiteral("Lookup in Reference Manual (F1)"));
    QVERIFY2(docTriggered, "triggerContextAction for 'Lookup in Reference Manual (F1)' must succeed");
    QApplication::processEvents();

    // Verify Reference Manual tab is activated in bottom console tabs.
    QCOMPARE(bottomTabs->currentWidget(), refManual);

    // Verify displayed documentation is for MOV and NEVER falls back to MPI_Init.
    auto* docBrowser = refManual->findChild<QTextBrowser*>(QStringLiteral("docsetTextBrowser"));
    QVERIFY(docBrowser != nullptr);
    const QString docContent = docBrowser->toHtml();
    QVERIFY2(docContent.contains(QStringLiteral("MOV"), Qt::CaseInsensitive),
             "Reference Manual must display MOV instruction documentation");
    QVERIFY2(!docContent.contains(QStringLiteral("MPI_Init")),
             "Looking up 'mov' must NEVER show MPI_Init reference");
    QVERIFY2(!docContent.contains(QStringLiteral("MPI-3.1 Standard")),
             "Looking up 'mov' must NEVER display MPI-3.1 Standard text");
}

/**
 * @brief Verifies smart Meson/CMake/Make/SingleFileMPI build system detection and Terminal build/clean/rebuild workflow.
 *
 * Test workflow:
 * 1. Creates isolated temporary directories representing Meson, CMake, Make, and standalone MPI C++ projects.
 * 2. Verifies detectBuildSystem(), buildCommandForPath(), and cleanCommandForPath() for each project type.
 * 3. Loads a standalone source file in MainWindow, switches BottomConsoleTabs away from Terminal,
 *    and invokes buildTargetWorkflow(), cleanTargetWorkflow(), and rebuildTargetWorkflow().
 * 4. Verifies that BottomConsoleTabs switches to TerminalDockWidget, dispatches the build/clean
 *    commands via sendCommand(), and updates #cutterStatusTelemetry.
 */
void TestMainWindowUI::testSmartBuildSystemDetectionAndTerminalBuildCleanWorkflow() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    // =========================================================================
    // Part 1: Smart Build System Detection Across Project Types
    // =========================================================================
    const auto writeText = [](const QString& path, const QByteArray& bytes) {
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) { f.write(bytes); f.close(); }
    };
    // Case A: Meson + Ninja project directory
    {
        QTemporaryDir mesonDir;
        QVERIFY(mesonDir.isValid());
        writeText(mesonDir.filePath(QStringLiteral("meson.build")), "project('demo', 'cpp')\n");
        QDir(mesonDir.path()).mkpath(QStringLiteral("builddir"));
        writeText(mesonDir.filePath(QStringLiteral("builddir/build.ninja")), "# ninja build file\n");
        QCOMPARE(mainWindow.detectBuildSystem(mesonDir.path()), QStringLiteral("Meson"));
        QVERIFY(mainWindow.buildCommandForPath(mesonDir.path()).contains(QStringLiteral("ninja -C")));
        QVERIFY(mainWindow.cleanCommandForPath(mesonDir.path()).contains(QStringLiteral("clean")));
    }
    // Case B: CMake project directory
    {
        QTemporaryDir cmakeDir;
        QVERIFY(cmakeDir.isValid());
        writeText(cmakeDir.filePath(QStringLiteral("CMakeLists.txt")), "cmake_minimum_required(VERSION 3.20)\nproject(demo)\n");
        QCOMPARE(mainWindow.detectBuildSystem(cmakeDir.path()), QStringLiteral("CMake"));
        QVERIFY(mainWindow.buildCommandForPath(cmakeDir.path()).contains(QStringLiteral("cmake --build")));
        QVERIFY(mainWindow.cleanCommandForPath(cmakeDir.path()).contains(QStringLiteral("clean")));
    }
    // Case C: GNU Make project directory
    {
        QTemporaryDir makeDir;
        QVERIFY(makeDir.isValid());
        writeText(makeDir.filePath(QStringLiteral("Makefile")), "all:\n\t@echo building\nclean:\n\t@echo cleaning\n");
        QCOMPARE(mainWindow.detectBuildSystem(makeDir.path()), QStringLiteral("Make"));
        QVERIFY(mainWindow.buildCommandForPath(makeDir.path()).contains(QStringLiteral("make")));
        QVERIFY(mainWindow.cleanCommandForPath(makeDir.path()).contains(QStringLiteral("make clean")));
    }
    // Case D: Standalone MPI / C++ source file
    QTemporaryDir singleFileDir;
    QVERIFY(singleFileDir.isValid());
    const QString demoSourcePath = singleFileDir.filePath(QStringLiteral("demo.cpp"));
    writeText(demoSourcePath, "#include <mpi.h>\nint main(int argc, char** argv) { return 0; }\n");
    QCOMPARE(mainWindow.detectBuildSystem(demoSourcePath), QStringLiteral("SingleFileMPI"));
    const QString singleBuildCmd = mainWindow.buildCommandForPath(demoSourcePath);
    QVERIFY(singleBuildCmd.contains(QStringLiteral("-g")) && singleBuildCmd.contains(QStringLiteral("-o")));
    QVERIFY(mainWindow.cleanCommandForPath(demoSourcePath).contains(QStringLiteral("rm -f")));

    // =========================================================================
    // Part 2: Interactive Build / Clean / Rebuild Terminal Workflow
    // =========================================================================
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>();
    auto* statusLabel = mainWindow.findChild<QLabel*>(QStringLiteral("cutterStatusTelemetry"));
    QVERIFY(bottomTabs != nullptr && terminalDock != nullptr && statusLabel != nullptr);

    // Load the standalone source file and switch away from the Terminal tab.
    mainWindow.loadSourceFile(demoSourcePath);
    for (int i = 0; i < bottomTabs->count(); ++i) {
        if (bottomTabs->widget(i) != terminalDock) {
            bottomTabs->setCurrentIndex(i);
            break;
        }
    }
    QVERIFY(bottomTabs->currentWidget() != terminalDock);

    // Trigger Build Target workflow: must focus Terminal tab, send compile command, and update telemetry.
    QVERIFY(mainWindow.buildTargetWorkflow());
    QApplication::processEvents();
    QCOMPARE(bottomTabs->currentWidget(), terminalDock);
    QVERIFY(!terminalDock->lastSentCommand().isEmpty());
    QVERIFY(terminalDock->lastSentCommand().contains(QStringLiteral("-g")));
    QVERIFY(statusLabel->text().contains(QStringLiteral("BUILDING"), Qt::CaseInsensitive));

    // Trigger Clean Target workflow: must focus Terminal tab, send clean command, and update telemetry.
    QVERIFY(mainWindow.cleanTargetWorkflow());
    QApplication::processEvents();
    QCOMPARE(bottomTabs->currentWidget(), terminalDock);
    QVERIFY(terminalDock->lastSentCommand().contains(QStringLiteral("rm -f")) ||
            terminalDock->lastSentCommand().contains(QStringLiteral("clean")));
    QVERIFY(statusLabel->text().contains(QStringLiteral("CLEANING"), Qt::CaseInsensitive));

    // Trigger Rebuild Target workflow: must chain clean and build commands in Terminal.
    QVERIFY(mainWindow.rebuildTargetWorkflow());
    QApplication::processEvents();
    QCOMPARE(bottomTabs->currentWidget(), terminalDock);
    QVERIFY(terminalDock->lastSentCommand().contains(QStringLiteral("&&")));
}

/**
 * @brief Verifies TerminalDockWidget eventFilter font/geometry lock during PreferencesDialog,
 *        Light Mode contrast on ProjectExplorerWidget, and ReferenceManualWidget docset auto-preview.
 */
void TestMainWindowUI::testTerminalDialogEventFilterLightModeContrastAndDocsetAutoDetect() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.loadPreviewFixture();
    mainWindow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&mainWindow));
    QApplication::processEvents();

    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>();
    auto* explorer = mainWindow.findChild<gridlock::ui::ProjectExplorerWidget*>();
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    QVERIFY(bottomTabs != nullptr && terminalDock != nullptr && explorer != nullptr && refManual != nullptr);

    // =========================================================================
    // Part 1: Terminal EventFilter Protection When Opening PreferencesDialog
    // =========================================================================
    bottomTabs->setCurrentWidget(terminalDock);
    QApplication::processEvents();

    // Open PreferencesDialog and simulate proportional FontChange / StyleChange events on terminal children.
    gridlock::ui::PreferencesDialog prefsDialog(&mainWindow);
    prefsDialog.show();
    QApplication::processEvents();

    QFont proportionalFont(QStringLiteral("Cantarell"), 11);
    proportionalFont.setFixedPitch(false);
    for (QWidget* child : terminalDock->findChildren<QWidget*>()) {
        child->setFont(proportionalFont);
        QEvent fontEv(QEvent::FontChange);
        QApplication::sendEvent(child, &fontEv);
        QEvent styleEv(QEvent::StyleChange);
        QApplication::sendEvent(child, &styleEv);
    }
    QApplication::processEvents();

    // Verify all child widgets in TerminalDockWidget remain fixed-pitch and span >= 95% of dock width.
    QVERIFY2(terminalDock->areAllChildWidgetsFixedPitch(),
             "TerminalDockWidget eventFilter must restore fixed-pitch font across all child widgets");
    for (QWidget* child : terminalDock->findChildren<QWidget*>()) {
        if (child->isVisible() && child->width() > 80) {
            QVERIFY2(child->width() >= (terminalDock->width() * 95) / 100,
                     qPrintable(QStringLiteral("Child widget %1 shrunk to %2px vs dock %3px")
                                    .arg(child->metaObject()->className())
                                    .arg(child->width())
                                    .arg(terminalDock->width())));
        }
    }
    prefsDialog.close();

    // =========================================================================
    // Part 2: Light Mode Contrast Verification
    // =========================================================================
    gridlock::core::ThemeManager::instance().setDarkMode(false);
    explorer->setSymbols({{QStringLiteral("sym.main"), 0x00401120ULL, 64}});
    QApplication::processEvents();

    auto* symTable = explorer->findChild<QTableWidget*>();
    QVERIFY(symTable != nullptr && symTable->rowCount() > 0);
    QTableWidgetItem* symItem = symTable->item(0, 0);
    QVERIFY(symItem != nullptr);

    const QColor fgColor = symItem->foreground().color();
    const QColor bgColor = symTable->palette().color(QPalette::Base);
    const auto luminance = [](const QColor& c) -> int {
        return (299 * c.red() + 587 * c.green() + 114 * c.blue()) / 1000;
    };
    const int contrastDiff = std::abs(luminance(fgColor) - luminance(bgColor));
    QVERIFY2(contrastDiff >= 80,
             qPrintable(QStringLiteral("Light mode symbol contrast too low (%1): fg=%2 bg=%3")
                            .arg(contrastDiff)
                            .arg(fgColor.name())
                            .arg(bgColor.name())));

    // Restore Dark Mode.
    gridlock::core::ThemeManager::instance().setDarkMode(true);
    QApplication::processEvents();

    // =========================================================================
    // Part 3: Selecting a Docset in ReferenceManualWidget Updates Right Pane
    // =========================================================================
    bottomTabs->setCurrentWidget(refManual);
    refManual->lookupSymbol(QStringLiteral("MPI_Init"));
    QApplication::processEvents();

    auto* docCombo = refManual->findChild<QComboBox*>();
    auto* docBrowser = refManual->findChild<QTextBrowser*>(QStringLiteral("docsetTextBrowser"));
    QVERIFY(docCombo != nullptr && docBrowser != nullptr);
    QVERIFY(docBrowser->toHtml().contains(QStringLiteral("MPI_Init")));

    // Switch docset combo to a non-MPI category (e.g. "Intel x86_64 ISA" or another loaded docset).
    int targetIdx = -1;
    for (int i = 0; i < docCombo->count(); ++i) {
        const QString label = docCombo->itemText(i);
        if (!label.contains(QStringLiteral("All"), Qt::CaseInsensitive) &&
            !label.contains(QStringLiteral("MPI"), Qt::CaseInsensitive)) {
            targetIdx = i;
            break;
        }
    }
    if (targetIdx >= 0) {
        docCombo->setCurrentIndex(targetIdx);
        QApplication::processEvents();
        QVERIFY2(!docBrowser->toHtml().contains(QStringLiteral("MPI_Init(&argc")),
                 "Switching docset category must auto-preview first entry in that docset instead of keeping MPI_Init");
    }
}

/**
 * @brief Verifies selecting a target in `#toolbarTargetSelector` switches the active source
 * editor tab, updates DisassemblyView (Linear/CFG/Decompiler), and directs Build/Clean commands
 * to the selected target's source and binary paths.
 */
void TestMainWindowUI::testTargetSelectorSwitchesSourceDisassemblyAndBuildCommands() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.show();
    QApplication::processEvents();

    auto* targetCombo = mainWindow.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector"));
    auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(targetCombo != nullptr && terminalDock != nullptr && disasmView != nullptr);

    // 1. Select "deadlock_ring" via MainWindow::selectTargetByName (and verify combo sync).
    QVERIFY2(mainWindow.selectTargetByName(QStringLiteral("deadlock_ring")),
             "selectTargetByName('deadlock_ring') must succeed and switch active target");
    QApplication::processEvents();
    QVERIFY2(mainWindow.activeTargetSourcePath().endsWith(QStringLiteral("deadlock_ring.cpp")),
             qPrintable(mainWindow.activeTargetSourcePath()));
    QVERIFY2(mainWindow.activeTargetBinaryPath().endsWith(QStringLiteral("deadlock_ring")),
             qPrintable(mainWindow.activeTargetBinaryPath()));

    // Verify active SourceCodeView in EditorTabManager loaded deadlock_ring.cpp containing MPI_Send & MPI_Recv.
    auto* editorTabs = mainWindow.findChild<gridlock::ui::EditorTabManager*>();
    QVERIFY(editorTabs != nullptr && editorTabs->currentSourceCodeView() != nullptr);
    const QString editorText = editorTabs->currentSourceCodeView()->toPlainText();
    QVERIFY2(editorText.contains(QStringLiteral("MPI_Send")) && editorText.contains(QStringLiteral("MPI_Recv")),
             "Active SourceCodeView must display deadlock_ring.cpp with MPI_Send and MPI_Recv");

    // Verify DisassemblyView updated its decompiler/disassembly preview to reflect deadlock_ring's MPI_Send/MPI_Recv.
    const QString decompText = disasmView->toPlainText() + QLatin1Char('\n') + disasmView->decompiledPseudocode();
    QVERIFY2(decompText.contains(QStringLiteral("MPI_Send")) || decompText.contains(QStringLiteral("MPI_Recv")),
             qPrintable(decompText.left(400)));

    // 2. Verify Build & Clean commands operate on deadlock_ring.cpp / deadlock_ring (NOT inspection_demo.cpp).
    mainWindow.buildTargetWorkflow();
    QApplication::processEvents();
    const QString buildCmd = terminalDock->lastSentCommand();
    QVERIFY2(buildCmd.contains(QStringLiteral("deadlock_ring.cpp")) && buildCmd.contains(QStringLiteral("-o")) &&
                 buildCmd.contains(QStringLiteral("deadlock_ring")) && !buildCmd.contains(QStringLiteral("inspection_demo.cpp")),
             qPrintable(buildCmd));

    mainWindow.cleanTargetWorkflow();
    QApplication::processEvents();
    const QString cleanCmd = terminalDock->lastSentCommand();
    QVERIFY2(cleanCmd.contains(QStringLiteral("deadlock_ring")) && !cleanCmd.contains(QStringLiteral("inspection_demo")),
             qPrintable(cleanCmd));

    // 3. Switch back to "inspection_demo" and verify source path and build command restore inspection_demo.cpp.
    QVERIFY2(mainWindow.selectTargetByName(QStringLiteral("inspection_demo")),
             "selectTargetByName('inspection_demo') must succeed");
    QApplication::processEvents();
    QVERIFY2(mainWindow.activeTargetSourcePath().endsWith(QStringLiteral("inspection_demo.cpp")),
             qPrintable(mainWindow.activeTargetSourcePath()));
    mainWindow.buildTargetWorkflow();
    QApplication::processEvents();
    QVERIFY2(terminalDock->lastSentCommand().contains(QStringLiteral("inspection_demo.cpp")),
             qPrintable(terminalDock->lastSentCommand()));
}

/**
 * @brief Verifies complete Edit menu actions, live UI/Code font scaling across QSS and editors,
 *        and 6 curated color themes with human-readable display names (zero underscores).
 */
void TestMainWindowUI::testEditMenuActionsDynamicFontScalingAndCuratedThemes() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.show();
    QApplication::processEvents();

    // Ensure a source file is open in EditorTabManager.
    mainWindow.selectTargetByName(QStringLiteral("inspection_demo"));
    QApplication::processEvents();

    auto* editorTabs = mainWindow.findChild<gridlock::ui::EditorTabManager*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    QVERIFY(editorTabs != nullptr && disasmView != nullptr);
    auto* srcView = editorTabs->currentSourceCodeView();
    QVERIFY(srcView != nullptr);

    // 1. Verify Edit menu actions & focus-aware execution (Select All, Copy, Toggle Comment).
    srcView->setFocus();
    QVERIFY2(mainWindow.triggerEditMenuAction(QStringLiteral("Select All")), "Select All must succeed");
    QVERIFY2(mainWindow.triggerEditMenuAction(QStringLiteral("Copy")), "Copy must succeed");

    // Toggle line comment on line 1 and verify '//' prefix is inserted and removed.
    QTextCursor tc = srcView->textCursor();
    tc.movePosition(QTextCursor::Start);
    srcView->setTextCursor(tc);
    const QString origFirstLine = srcView->document()->firstBlock().text();
    QVERIFY2(mainWindow.triggerEditMenuAction(QStringLiteral("Toggle Comment")), "Toggle Comment must succeed");
    QVERIFY2(srcView->document()->firstBlock().text() != origFirstLine, "Toggle Comment must modify active line");
    QVERIFY2(mainWindow.triggerEditMenuAction(QStringLiteral("Toggle Comment")), "Second Toggle Comment must restore");
    QCOMPARE(srcView->document()->firstBlock().text().trimmed(), origFirstLine.trimmed());

    // 2. Verify dynamic UI Font Size & Code Font Size scaling (no hardcoded 11px lock).
    mainWindow.applyTypographySettings(QStringLiteral("Monospace"), 15, QStringLiteral("Monospace"), 16);
    QApplication::processEvents();
    const QString activeQss = gridlock::core::ThemeManager::instance().currentStyleSheet();
    QVERIFY2(activeQss.contains(QStringLiteral("15pt")) || activeQss.contains(QStringLiteral("15px")),
             qPrintable(activeQss.left(200)));
    QCOMPARE(srcView->font().pointSize(), 16);
    QCOMPARE(disasmView->codeFontPointSize(), 16);

    // Restore default typography.
    mainWindow.applyTypographySettings(QStringLiteral("Inter"), 11, QStringLiteral("JetBrains Mono"), 10);
    QApplication::processEvents();

    // 3. Verify 6 curated color themes and zero underscores in PreferencesDialog Color Theme combo.
    QVERIFY(gridlock::core::ThemeManager::instance().availableThemeIds().size() >= 6);
    gridlock::ui::PreferencesDialog prefsDlg(&mainWindow);
    auto* themeCombo = prefsDlg.findChild<QComboBox*>(QStringLiteral("themeSelectorCombo"));
    QVERIFY(themeCombo != nullptr && themeCombo->count() >= 6);
    for (int i = 0; i < themeCombo->count(); ++i) {
        QVERIFY2(!themeCombo->itemText(i).contains(QLatin1Char('_')), qPrintable(themeCombo->itemText(i)));
    }

    // 4. Generate 6 offscreen 1440x900 PNG screenshots of each curated theme in docs/screenshots/.
    struct ThemeShotEntry { QString themeId; QString fileName; };
    const std::vector<ThemeShotEntry> shots = {
        {QStringLiteral("gridlock_slate"), QStringLiteral("theme_gridlock_slate.png")},
        {QStringLiteral("dark_teal"), QStringLiteral("theme_obsidian_teal.png")},
        {QStringLiteral("tokyo_night"), QStringLiteral("theme_tokyo_night.png")},
        {QStringLiteral("catppuccin_mocha"), QStringLiteral("theme_catppuccin_mocha.png")},
        {QStringLiteral("gruvbox_dark"), QStringLiteral("theme_gruvbox_dark.png")},
        {QStringLiteral("daylight_light"), QStringLiteral("theme_daylight_light.png")},
    };
    const QString docsShotDir = (!QDir(QStringLiteral("docs")).exists() && QDir(QStringLiteral("../docs")).exists())
                                    ? QStringLiteral("../docs/screenshots")
                                    : QStringLiteral("docs/screenshots");
    QDir().mkpath(docsShotDir);

    for (const auto& shot : shots) {
        gridlock::core::ThemeManager::instance().setTheme(shot.themeId, !shot.themeId.contains(QStringLiteral("light")));
        if (srcView) srcView->reloadStyle();
        if (disasmView) disasmView->reloadStyle();
        QApplication::processEvents();
        const QString outPath = docsShotDir + QLatin1Char('/') + shot.fileName;
        QVERIFY2(mainWindow.captureWorkspaceSnapshot(outPath), qPrintable(outPath));
        QVERIFY2(QFileInfo::exists(outPath) && QFileInfo(outPath).size() > 1024, qPrintable(outPath));
    }

    // Restore default GridLock Slate theme after screenshot export.
    gridlock::core::ThemeManager::instance().setTheme(QStringLiteral("gridlock_slate"), true);
    if (srcView) srcView->reloadStyle();
    if (disasmView) disasmView->reloadStyle();
    QApplication::processEvents();
}

/**
 * @brief Verifies two-lane vector gutter geometry, distinct breakpoint/conditional/PC marker states,
 *        and non-overlapping antialiased vector badge rendering in SourceCodeView.
 */
void TestMainWindowUI::testModernVectorGutterIconsAndCombinedBreakpointPcState() {
    gridlock::ui::SourceCodeView srcView;
    QString sampleCode;
    for (int i = 1; i <= 30; ++i) {
        sampleCode += QStringLiteral("    int mpi_rank_step_%1 = %1; // source line %1\n").arg(i);
    }
    srcView.setSourceCode(sampleCode);
    srcView.resize(720, 560);
    srcView.show();
    QApplication::processEvents();

    // 1. Dedicated Two-Lane Gutter Width (>= 48px) & Non-Overlapping Icon Lane (>= 18px).
    QVERIFY2(srcView.lineNumberAreaWidth() >= 48, qPrintable(QString::number(srcView.lineNumberAreaWidth())));
    QVERIFY2(srcView.gutterIconLaneWidth() >= 18, qPrintable(QString::number(srcView.gutterIconLaneWidth())));

    // 2. Distinct Gutter Marker States (None, Breakpoint, ConditionalBreakpoint, ExecutionPc, BreakpointAndExecutionPc).
    QCOMPARE(srcView.gutterMarkerStateForLine(1), QStringLiteral("None"));
    srcView.toggleBreakpoint(15);
    srcView.setConditionalBreakpoint(16, QStringLiteral("rank == 0"));
    srcView.highlightExecutionLine(15);
    QApplication::processEvents();
    QCOMPARE(srcView.gutterMarkerStateForLine(15), QStringLiteral("BreakpointAndExecutionPc"));
    QCOMPARE(srcView.gutterMarkerStateForLine(16), QStringLiteral("ConditionalBreakpoint"));

    srcView.highlightExecutionLine(13);
    QApplication::processEvents();
    QCOMPARE(srcView.gutterMarkerStateForLine(13), QStringLiteral("ExecutionPc"));
    QCOMPARE(srcView.gutterMarkerStateForLine(15), QStringLiteral("Breakpoint"));

    // 3. Antialiased Vector Rendering in Gutter Snapshot with zero overlap against line numbers.
    srcView.highlightExecutionLine(15);
    QApplication::processEvents();
    const QImage img = srcView.grab().toImage();
    QVERIFY(!img.isNull());

    const QColor gutterBg(0x16, 0x18, 0x1d);
    bool foundBadgePixelInIconLane = false;
    for (int y = 0; y < img.height() && !foundBadgePixelInIconLane; ++y) {
        for (int x = 3; x <= 16 && x < img.width(); ++x) {
            const QColor px = img.pixelColor(x, y);
            // Detect crimson (#e06c75), amber (#e5c07b), or emerald (#4ec9b0) vector badge pixels.
            if (std::abs(px.red() - gutterBg.red()) > 35 || std::abs(px.green() - gutterBg.green()) > 35) {
                foundBadgePixelInIconLane = true;
                break;
            }
        }
    }
    QVERIFY2(foundBadgePixelInIconLane, "Expected vector badge pixels inside left icon lane (x = 3..16)");
}

/**
 * @brief Verifies on-demand ELF binary compilation, genuine DWARF source-to-instruction line
 *        mapping for `deadlock_ring`, breakpoint-first Run/Continue execution, latched Run Target
 *        toolbar visual state, and non-italic toolbar selector typography.
 */
void TestMainWindowUI::testRealCompiledTargetDwarfMappingBreakpointRunAndLatchedToolbar() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.show();
    QApplication::processEvents();

    // 1. Select deadlock_ring and compile its real ELF64 binary with DWARF debug info on-demand.
    QVERIFY2(mainWindow.selectTargetByName(QStringLiteral("deadlock_ring")), "selectTargetByName must succeed");
    QVERIFY2(mainWindow.ensureTargetBinaryCompiled(), "ensureTargetBinaryCompiled must compile ELF binary");
    QApplication::processEvents();
    QVERIFY2(QFileInfo::exists(mainWindow.activeTargetBinaryPath()), qPrintable(mainWindow.activeTargetBinaryPath()));

    auto* editorTabs = mainWindow.findChild<gridlock::ui::EditorTabManager*>();
    auto* disasmView = mainWindow.findChild<gridlock::ui::DisassemblyView*>();
    auto* targetCombo = mainWindow.findChild<QComboBox*>(QStringLiteral("toolbarTargetSelector"));
    QVERIFY(editorTabs != nullptr && disasmView != nullptr && targetCombo != nullptr);
    auto* srcView = editorTabs->currentSourceCodeView();
    QVERIFY(srcView != nullptr);

    // Verify genuine compiler-emitted assembly (calls MPI_Send & MPI_Recv; zero hardcoded DeeplyNested).
    const QString asmText = disasmView->toPlainText();
    QVERIFY2(asmText.contains(QStringLiteral("MPI_Send")) && asmText.contains(QStringLiteral("MPI_Recv")),
             qPrintable(asmText.left(400)));
    QVERIFY2(!asmText.contains(QStringLiteral("DeeplyNested")), "Must NOT contain hardcoded DeeplyNested preview");

    // Resolve DWARF instruction addresses for MPI_Send and MPI_Recv lines in deadlock_ring.cpp.
    const int sendLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Send("));
    const int recvLine = mainWindow.findSourceLineForStatement(QStringLiteral("MPI_Recv("));
    QVERIFY(sendLine > 0 && recvLine > sendLine);
    const uint64_t sendAddr = mainWindow.sourceLineToAddress(sendLine);
    const uint64_t recvAddr = mainWindow.sourceLineToAddress(recvLine);
    QVERIFY(sendAddr != 0ULL && recvAddr != 0ULL && sendAddr != recvAddr);
    QVERIFY(disasmView->lineForAddress(sendAddr) >= 0 && disasmView->lineForAddress(recvAddr) >= 0);

    // 2. Toggle breakpoints on sendLine and recvLine; Run Target must stop directly on sendLine!
    for (int bp : srcView->breakpoints().values()) srcView->toggleBreakpoint(bp);
    srcView->toggleBreakpoint(sendLine);
    srcView->toggleBreakpoint(recvLine);
    mainWindow.startTargetWorkflow();
    QApplication::processEvents();

    QCOMPARE(mainWindow.isDebugSessionActive(), true);
    QCOMPARE(mainWindow.isRunButtonLatched(), true);
    QCOMPARE(srcView->executionLine(), sendLine);
    QCOMPARE(disasmView->currentAddress(), sendAddr);

    // Continue must step past sendAddr and stop directly at the second user breakpoint (recvLine).
    QVERIFY(mainWindow.continueExecutionWorkflow());
    QApplication::processEvents();
    QCOMPARE(srcView->executionLine(), recvLine);
    QCOMPARE(disasmView->currentAddress(), recvAddr);

    // 3. Stop session and verify Run Target unlatches and toolbar combo is non-italic.
    mainWindow.stopTargetWorkflow();
    QApplication::processEvents();
    QCOMPARE(mainWindow.isDebugSessionActive(), false);
    QCOMPARE(mainWindow.isRunButtonLatched(), false);
    QCOMPARE(targetCombo->font().italic(), false);

    // Zero-breakpoint Run Target on deadlock_ring must pause at MPI_Send circular deadlock.
    srcView->toggleBreakpoint(sendLine);
    srcView->toggleBreakpoint(recvLine);
    mainWindow.startTargetWorkflow();
    QApplication::processEvents();
    QCOMPARE(srcView->executionLine(), sendLine);
    mainWindow.stopTargetWorkflow();
}

/**
 * @brief Verifies gutter hover line tracking, ghost breakpoint preview state, cursor-anchored
 *        breakpoint popover controls (Enabled, Suspend scope, Condition presets), and positioning.
 */
void TestMainWindowUI::testAnchoredBreakpointPopoverAndGutterHover() {
    gridlock::ui::SourceCodeView sourceCodeView;
    QString sampleCode;
    for (int i = 1; i <= 40; ++i) {
        sampleCode += QStringLiteral("    MPI_Send(send_buf.data(), %1, MPI_INT, next, 0, MPI_COMM_WORLD);\n").arg(i);
    }
    sourceCodeView.setSourceCode(sampleCode);
    sourceCodeView.resize(720, 560);
    sourceCodeView.show();
    QApplication::processEvents();

    // 1. Gutter Hover Line Tracking & Ghost Preview State.
    QCOMPARE(sourceCodeView.hoveredGutterLine(), -1);
    sourceCodeView.setHoveredGutterLine(16);
    QApplication::processEvents();
    QCOMPARE(sourceCodeView.hoveredGutterLine(), 16);
    QCOMPARE(sourceCodeView.gutterMarkerStateForLine(16), QStringLiteral("HoverPreview"));

    // Active breakpoint supersedes ghost hover preview; clearing hover restores None on unmarked lines.
    sourceCodeView.toggleBreakpoint(16);
    QCOMPARE(sourceCodeView.gutterMarkerStateForLine(16), QStringLiteral("Breakpoint"));
    sourceCodeView.toggleBreakpoint(16);
    sourceCodeView.setHoveredGutterLine(-1);
    QCOMPARE(sourceCodeView.hoveredGutterLine(), -1);
    QCOMPARE(sourceCodeView.gutterMarkerStateForLine(16), QStringLiteral("None"));

    // 2. Breakpoint Popover Controls (Enabled, Suspend: All / Rank, Condition Presets).
    gridlock::ui::ConditionalBreakpointDialog dialog(QString(), &sourceCodeView);
    dialog.setTargetLineContext(34, QStringLiteral("MPI_Send(send_buf.data(), ...);"));
    QVERIFY2(dialog.targetLineSummaryText().contains(QStringLiteral("Line 34")),
             qPrintable(dialog.targetLineSummaryText()));
    QCOMPARE(dialog.isBreakpointEnabled(), true);
    QCOMPARE(dialog.suspendScope(), QStringLiteral("All"));

    // Toggle the single-rank/thread radio button and verify suspendScope() transitions to "Rank".
    QRadioButton* rankRadio = nullptr;
    for (QRadioButton* rb : dialog.findChildren<QRadioButton*>()) {
        if (rb->text().contains(QStringLiteral("Rank"), Qt::CaseInsensitive) || !rb->isChecked()) {
            rankRadio = rb;
            break;
        }
    }
    QVERIFY2(rankRadio != nullptr, "ConditionalBreakpointDialog must expose a Rank/Thread suspend radio button");
    rankRadio->setChecked(true);
    QApplication::processEvents();
    QCOMPARE(dialog.suspendScope(), QStringLiteral("Rank"));

    // Apply quick preset expression and verify condition accessor updates immediately.
    dialog.applyQuickPreset(QStringLiteral("rank == 0"));
    QCOMPARE(dialog.getCondition(), QStringLiteral("rank == 0"));

    // 3. Cursor-Anchored Positioning (directly beneath the gutter dot, not screen-centered).
    const QPoint anchorGlobalPos(120, 340);
    dialog.positionNearAnchor(anchorGlobalPos);
    dialog.show();
    QApplication::processEvents();
    QVERIFY2(std::abs(dialog.pos().x() - anchorGlobalPos.x()) <= 40,
             qPrintable(QStringLiteral("Popover x=%1 too far from anchor x=120").arg(dialog.pos().x())));
    QVERIFY2(std::abs(dialog.pos().y() - anchorGlobalPos.y()) <= 25,
             qPrintable(QStringLiteral("Popover y=%1 too far from anchor y=340").arg(dialog.pos().y())));
    dialog.hide();
}

/**
 * @brief Verifies left vertical tool-window icon rail (`#bottomToolWindowRail`), linked contextual
 *        tab filtering across Debug/MPI/Terminal/Docs, programmatic auto-sync, and flattened MPI header.
 */
void TestMainWindowUI::testBottomToolWindowRailAndLinkedTabGroups() {
    gridlock::ui::MainWindow mainWindow;
    mainWindow.resize(1440, 900);
    mainWindow.show();
    QApplication::processEvents();

    // 1. Verify #bottomToolWindowRail exists and exposes the 4 canonical tool groups.
    auto* rail = mainWindow.findChild<QWidget*>(QStringLiteral("bottomToolWindowRail"));
    auto* bottomTabs = mainWindow.findChild<QTabWidget*>(QStringLiteral("BottomConsoleTabs"));
    auto* memView = mainWindow.findChild<gridlock::ui::MemView*>();
    auto* terminalDock = mainWindow.findChild<gridlock::ui::TerminalDockWidget*>();
    auto* refManual = mainWindow.findChild<gridlock::ui::ReferenceManualWidget*>();
    auto* mpiDiag = mainWindow.findChild<gridlock::ui::MpiDiagnosticsWidget*>();
    QVERIFY(rail != nullptr && bottomTabs != nullptr && memView != nullptr);
    QVERIFY(terminalDock != nullptr && refManual != nullptr && mpiDiag != nullptr);
    QCOMPARE(mainWindow.bottomToolGroups(), (QStringList{QStringLiteral("Debug"), QStringLiteral("MPI"), QStringLiteral("Terminal"), QStringLiteral("Docs")}));

    // Helper returning uppercased titles of currently visible tabs in BottomConsoleTabs.
    const auto visibleTitles = [&]() {
        QStringList vis;
        for (int i = 0; i < bottomTabs->count(); ++i) {
            if (bottomTabs->tabBar()->isTabVisible(i)) vis.append(bottomTabs->tabText(i).toUpper());
        }
        return vis;
    };

    // 2. Verify selecting Debug, MPI, Terminal, and Docs filters visible tabs strictly to linked views.
    mainWindow.selectBottomToolGroup(QStringLiteral("Debug"));
    QCOMPARE(mainWindow.activeBottomToolGroup(), QStringLiteral("Debug"));
    const QStringList debugVis = visibleTitles();
    QCOMPARE(debugVis.size(), 3);
    QVERIFY(debugVis.join(QLatin1Char('|')).contains(QStringLiteral("GDB")) && debugVis.join(QLatin1Char('|')).contains(QStringLiteral("MEMORY")) && debugVis.join(QLatin1Char('|')).contains(QStringLiteral("EVALUATOR")));

    mainWindow.selectBottomToolGroup(QStringLiteral("MPI"));
    QCOMPARE(mainWindow.activeBottomToolGroup(), QStringLiteral("MPI"));
    QCOMPARE(visibleTitles().size(), 2);

    mainWindow.selectBottomToolGroup(QStringLiteral("Terminal"));
    QCOMPARE(mainWindow.activeBottomToolGroup(), QStringLiteral("Terminal"));
    QCOMPARE(bottomTabs->currentWidget(), terminalDock);

    mainWindow.selectBottomToolGroup(QStringLiteral("Docs"));
    QCOMPARE(mainWindow.activeBottomToolGroup(), QStringLiteral("Docs"));
    QCOMPARE(bottomTabs->currentWidget(), refManual);

    // 3. Programmatically switching BottomConsoleTabs to MemView auto-syncs the Debug tool group.
    bottomTabs->setCurrentWidget(memView);
    QApplication::processEvents();
    QCOMPARE(mainWindow.activeBottomToolGroup(), QStringLiteral("Debug"));
    QCOMPARE(visibleTitles().size(), 3);

    // 4. Verify MpiDiagnosticsWidget uses a single compact 26px header bar with hidden inner QTabBar.
    QVERIFY(mpiDiag->hasCompactSingleRowHeader());
}

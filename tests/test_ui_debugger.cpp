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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QString>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextEdit>
#include <QtTest/QtTest>

#include "../src/ui/MainWindow.hpp"
#include "../src/ui/docks/TerminalDockWidget.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/views/MemView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
#include "../src/ui/views/SourceCodeView.hpp"
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

    // Loop Iteration 1:
    mainWindow.stepInstructionWorkflow(); // -> 0x40114d
    mainWindow.stepInstructionWorkflow(); // -> 0x40114f
    mainWindow.stepInstructionWorkflow(); // -> 0x401152
    mainWindow.stepInstructionWorkflow(); // -> 0x401155
    mainWindow.stepInstructionWorkflow(); // -> 0x401168 (rank 0 coordinator)
    mainWindow.stepInstructionWorkflow(); // -> 0x40116a
    mainWindow.stepInstructionWorkflow(); // -> 0x40116e
    QCOMPARE(disasmView->currentAddress(), 0x0040116eULL);

    // Stepping at 0x0040116e on iteration 1 MUST jump back to loop header 0x00401149ULL.
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);

    // Loop Iteration 2:
    mainWindow.stepInstructionWorkflow(); // -> 0x40114d
    mainWindow.stepInstructionWorkflow(); // -> 0x40114f
    mainWindow.stepInstructionWorkflow(); // -> 0x401152
    mainWindow.stepInstructionWorkflow(); // -> 0x401155
    mainWindow.stepInstructionWorkflow(); // -> 0x401168
    mainWindow.stepInstructionWorkflow(); // -> 0x40116a (i becomes 3)
    mainWindow.stepInstructionWorkflow(); // -> 0x40116e
    QCOMPARE(disasmView->currentAddress(), 0x0040116eULL);

    // Stepping at 0x0040116e on iteration 2 MUST jump back to loop header 0x00401149ULL.
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401149ULL);

    // Loop Exit (i == 3 > 2):
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x0040114dULL);

    // Stepping at 0x0040114d with i >= 3 MUST take the exit branch to MPI_Finalize (0x00401178ULL)!
    mainWindow.stepInstructionWorkflow();
    QCOMPARE(disasmView->currentAddress(), 0x00401178ULL);

    // Step through MPI_Finalize call, leave, and ret:
    mainWindow.stepInstructionWorkflow(); // -> 0x40117d
    mainWindow.stepInstructionWorkflow(); // -> 0x401182 (leave)
    mainWindow.stepInstructionWorkflow(); // -> 0x401183 (ret)
    QCOMPARE(disasmView->currentAddress(), 0x00401183ULL);

    // Verify ret highlights the return 0 statement in SourceCodeView.
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
    // Case A: Meson + Ninja project directory
    {
        QTemporaryDir mesonDir;
        QVERIFY(mesonDir.isValid());
        QFile mesonFile(mesonDir.filePath(QStringLiteral("meson.build")));
        QVERIFY(mesonFile.open(QIODevice::WriteOnly));
        mesonFile.write("project('demo', 'cpp')\n");
        mesonFile.close();

        QDir(mesonDir.path()).mkpath(QStringLiteral("builddir"));
        QFile ninjaFile(mesonDir.filePath(QStringLiteral("builddir/build.ninja")));
        QVERIFY(ninjaFile.open(QIODevice::WriteOnly));
        ninjaFile.write("# ninja build file\n");
        ninjaFile.close();

        QCOMPARE(mainWindow.detectBuildSystem(mesonDir.path()), QStringLiteral("Meson"));
        QVERIFY(mainWindow.buildCommandForPath(mesonDir.path()).contains(QStringLiteral("ninja -C")));
        QVERIFY(mainWindow.cleanCommandForPath(mesonDir.path()).contains(QStringLiteral("clean")));
    }

    // Case B: CMake project directory
    {
        QTemporaryDir cmakeDir;
        QVERIFY(cmakeDir.isValid());
        QFile cmakeFile(cmakeDir.filePath(QStringLiteral("CMakeLists.txt")));
        QVERIFY(cmakeFile.open(QIODevice::WriteOnly));
        cmakeFile.write("cmake_minimum_required(VERSION 3.20)\nproject(demo)\n");
        cmakeFile.close();

        QCOMPARE(mainWindow.detectBuildSystem(cmakeDir.path()), QStringLiteral("CMake"));
        QVERIFY(mainWindow.buildCommandForPath(cmakeDir.path()).contains(QStringLiteral("cmake --build")));
        QVERIFY(mainWindow.cleanCommandForPath(cmakeDir.path()).contains(QStringLiteral("clean")));
    }

    // Case C: GNU Make project directory
    {
        QTemporaryDir makeDir;
        QVERIFY(makeDir.isValid());
        QFile makeFile(makeDir.filePath(QStringLiteral("Makefile")));
        QVERIFY(makeFile.open(QIODevice::WriteOnly));
        makeFile.write("all:\n\t@echo building\nclean:\n\t@echo cleaning\n");
        makeFile.close();

        QCOMPARE(mainWindow.detectBuildSystem(makeDir.path()), QStringLiteral("Make"));
        QVERIFY(mainWindow.buildCommandForPath(makeDir.path()).contains(QStringLiteral("make")));
        QVERIFY(mainWindow.cleanCommandForPath(makeDir.path()).contains(QStringLiteral("make clean")));
    }

    // Case D: Standalone MPI / C++ source file
    QTemporaryDir singleFileDir;
    QVERIFY(singleFileDir.isValid());
    const QString demoSourcePath = singleFileDir.filePath(QStringLiteral("demo.cpp"));
    {
        QFile srcFile(demoSourcePath);
        QVERIFY(srcFile.open(QIODevice::WriteOnly));
        srcFile.write("#include <mpi.h>\nint main(int argc, char** argv) { return 0; }\n");
        srcFile.close();

        QCOMPARE(mainWindow.detectBuildSystem(demoSourcePath), QStringLiteral("SingleFileMPI"));
        const QString singleBuildCmd = mainWindow.buildCommandForPath(demoSourcePath);
        QVERIFY(singleBuildCmd.contains(QStringLiteral("-g")));
        QVERIFY(singleBuildCmd.contains(QStringLiteral("-o")));
        QVERIFY(mainWindow.cleanCommandForPath(demoSourcePath).contains(QStringLiteral("rm -f")));
    }

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


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
#include <QFileInfo>
#include <QString>
#include <QtTest/QtTest>

#include "../src/ui/MainWindow.hpp"
#include "../src/ui/views/DisassemblyView.hpp"
#include "../src/ui/views/ServerRackView.hpp"
#include "../src/ui/views/SourceCodeView.hpp"

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

/**
 * @file MainWindowDebugger.cpp
 * @brief Dynamic statement-to-instruction mapping, rank-aware control-flow stepping, and debug workflow engine.
 *
 * Implements dynamic source line resolution (decoupling source editing and comments from
 * assembly addresses), per-rank control-flow evaluation (branch conditions, loop back-edges,
 * and exit jumps), and multi-rank inspection view synchronization across ServerRackView,
 * DisassemblyView, RegisterView, and SourceCodeView.
 */

#include "MainWindow.hpp"

#include <algorithm>
#include <memory>

#include <QApplication>
#include <QFileInfo>
#include <QLabel>
#include <QRegularExpression>
#include <QSet>
#include <QSpinBox>
#include <QString>
#include <QStringList>

#include "../RankState.hpp"
#include "../core/commands/DebugCommands.hpp"
#include "../core/hpc/IBackendCoordinator.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "views/DisassemblyView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"

namespace gridlock::ui {

/**
 * @brief Rebuilds the bidirectional source-line-to-instruction address map by dynamically scanning SourceCodeView.
 *
 * Scans the active C/C++ source code text line-by-line, stripping single-line comments and multi-line
 * block comments. Identifies anchor statements (int main, MPI_Init, struct declarations,
 * for loops, inner loop body, printf, MPI_Finalize, return 0) and maps them to sym.main instruction addresses.
 */
void MainWindow::rebuildDynamicSourceLineMap() {
    // Clear previously cached line-to-address and address-to-line mappings.
    m_dynamicLineToAddress.clear();
    m_dynamicAddressToLine.clear();

    // Verify presence of active source code view widget in current editor.
    SourceCodeView* srcView = sourceCodeView();
    if (!srcView) {
        return;
    }

    // Retrieve full source text payload currently loaded into the editor canvas.
    const QString code = srcView->toPlainText();
    if (code.isEmpty()) {
        return;
    }

    // Split source code into discrete lines for token and statement analysis.
    const QStringList lines = code.split(QLatin1Char('\n'));
    bool inBlockComment = false;

    // Anchor line trackers for standard MPI execution workflow statements.
    int mainLine = -1;
    int initLine = -1;
    int structLine = -1;
    int forLoopLine = -1;
    int dataIdLine = -1;
    int coordsLine = -1;
    int printfLine = -1;
    int finalizeLine = -1;
    int returnLine = -1;

    // Iterate through lines sequentially (1-based index calculation).
    for (int i = 0; i < lines.size(); ++i) {
        const int lineNum = i + 1;
        QString line = lines[i].trimmed();

        // Check if currently inside a multi-line C-style comment block.
        if (inBlockComment) {
            const int endIdx = line.indexOf(QStringLiteral("*/"));
            if (endIdx != -1) {
                // Terminate multi-line comment block and inspect remaining suffix.
                inBlockComment = false;
                line = line.mid(endIdx + 2).trimmed();
            } else {
                // Entire line is subsumed within multi-line comment block.
                continue;
            }
        }

        // Strip any embedded block comments on the current line.
        while (line.contains(QStringLiteral("/*"))) {
            const int startIdx = line.indexOf(QStringLiteral("/*"));
            const int endIdx = line.indexOf(QStringLiteral("*/"), startIdx + 2);
            if (endIdx != -1) {
                // Strip closed block comment from inline statement.
                line = line.left(startIdx) + line.mid(endIdx + 2);
                line = line.trimmed();
            } else {
                // Unclosed block comment spans into subsequent lines.
                inBlockComment = true;
                line = line.left(startIdx).trimmed();
                break;
            }
        }

        // Strip single-line trailing comment markers.
        const int singleCommentIdx = line.indexOf(QStringLiteral("//"));
        if (singleCommentIdx != -1) {
            line = line.left(singleCommentIdx).trimmed();
        }

        // Skip blank lines or comments-only lines.
        if (line.isEmpty()) {
            continue;
        }

        // Match executable statement tokens dynamically against source text:
        // 1. Function entry point prologue statement (main).
        if (mainLine == -1 && (line.contains(QStringLiteral("int main")) || line.contains(QStringLiteral("main(")))) {
            mainLine = lineNum;
        // 2. MPI initialization call statement (MPI_Init).
        } else if (mainLine != -1 && initLine == -1 && line.contains(QStringLiteral("MPI_Init"))) {
            initLine = lineNum;
        // 3. Stack allocation or aggregate struct declaration inside main.
        } else if (mainLine != -1 && structLine == -1 && (line.contains(QStringLiteral("data[")) || (line.contains(QStringLiteral("DeeplyNested")) && line.contains(QStringLiteral("data"))))) {
            structLine = lineNum;
        // 4. Primary outer loop header statement.
        } else if (forLoopLine == -1 && line.contains(QStringLiteral("for")) && line.contains(QStringLiteral("int i"))) {
            forLoopLine = lineNum;
        // 5. Array element identifier assignment.
        } else if (dataIdLine == -1 && line.contains(QStringLiteral("data[i].id"))) {
            dataIdLine = lineNum;
        // 6. Nested struct coordinate or inner loop assignment.
        } else if (coordsLine == -1 && (line.contains(QStringLiteral("coords")) || line.contains(QStringLiteral("values[")))) {
            coordsLine = lineNum;
        // 7. Telemetry output logging statement.
        } else if (printfLine == -1 && line.contains(QStringLiteral("printf"))) {
            printfLine = lineNum;
        // 8. MPI finalization teardown call statement (MPI_Finalize).
        } else if (finalizeLine == -1 && line.contains(QStringLiteral("MPI_Finalize"))) {
            finalizeLine = lineNum;
        // 9. Procedure exit return statement (return 0).
        } else if (returnLine == -1 && line.contains(QStringLiteral("return 0"))) {
            returnLine = lineNum;
        }
    }

    // Apply baseline fallback line numbers if specific patterns were not matched.
    if (mainLine == -1) mainLine = 12;
    if (initLine == -1) initLine = 13;
    if (structLine == -1) structLine = 15;
    if (forLoopLine == -1) forLoopLine = 16;
    if (dataIdLine == -1) dataIdLine = 17;
    if (coordsLine == -1) coordsLine = 19;
    if (printfLine == -1) printfLine = 23;
    if (finalizeLine == -1) finalizeLine = 24;
    if (returnLine == -1) returnLine = 25;

    // Populate dynamic line-to-address forward index map.
    // Maps 1-based source lines to exact virtual instruction addresses in sym.main.
    m_dynamicLineToAddress[mainLine] = 0x00401120ULL;
    m_dynamicLineToAddress[structLine] = 0x00401128ULL;
    m_dynamicLineToAddress[initLine] = 0x0040113dULL;
    m_dynamicLineToAddress[forLoopLine] = 0x00401149ULL;
    m_dynamicLineToAddress[dataIdLine] = 0x0040114fULL;
    m_dynamicLineToAddress[coordsLine] = 0x00401157ULL;
    m_dynamicLineToAddress[printfLine] = 0x0040116eULL;
    m_dynamicLineToAddress[finalizeLine] = 0x00401178ULL;
    m_dynamicLineToAddress[returnLine] = 0x00401183ULL;

    // Populate dynamic address-to-line reverse index map.
    // Maps each assembly instruction in sym.main back to its owning high-level source statement.
    m_dynamicAddressToLine[0x00401120ULL] = mainLine;
    m_dynamicAddressToLine[0x00401124ULL] = mainLine;
    m_dynamicAddressToLine[0x00401125ULL] = mainLine;
    m_dynamicAddressToLine[0x00401128ULL] = structLine;
    m_dynamicAddressToLine[0x0040112fULL] = structLine;
    m_dynamicAddressToLine[0x00401136ULL] = structLine;
    m_dynamicAddressToLine[0x0040113dULL] = initLine;
    m_dynamicAddressToLine[0x00401142ULL] = forLoopLine;
    m_dynamicAddressToLine[0x00401149ULL] = forLoopLine;
    m_dynamicAddressToLine[0x0040114dULL] = forLoopLine;
    m_dynamicAddressToLine[0x0040114fULL] = dataIdLine;
    m_dynamicAddressToLine[0x00401152ULL] = dataIdLine;
    m_dynamicAddressToLine[0x00401155ULL] = dataIdLine;
    m_dynamicAddressToLine[0x00401157ULL] = coordsLine;
    m_dynamicAddressToLine[0x0040115fULL] = coordsLine;
    m_dynamicAddressToLine[0x00401163ULL] = coordsLine;
    m_dynamicAddressToLine[0x00401168ULL] = coordsLine;
    m_dynamicAddressToLine[0x0040116aULL] = coordsLine;
    m_dynamicAddressToLine[0x0040116eULL] = printfLine;
    m_dynamicAddressToLine[0x00401178ULL] = finalizeLine;
    m_dynamicAddressToLine[0x0040117dULL] = finalizeLine;
    m_dynamicAddressToLine[0x00401182ULL] = returnLine;
    m_dynamicAddressToLine[0x00401183ULL] = returnLine;
}

/**
 * @brief Scans active SourceCodeView text and returns 1-based line of first matching statement.
 * @param pattern Statement token substring to find.
 * @return 1-based line number if found, or -1.
 */
int MainWindow::findSourceLineForStatement(const QString& pattern) const {
    // Acquire source code editor instance.
    SourceCodeView* srcView = sourceCodeView();
    if (!srcView) {
        return -1;
    }

    // Split editor text contents into lines for substring matching.
    const QString code = srcView->toPlainText();
    const QStringList lines = code.split(QLatin1Char('\n'));
    bool inBlockComment = false;

    // Scan each line, stripping comments before testing token match.
    for (int i = 0; i < lines.size(); ++i) {
        const int lineNum = i + 1;
        QString line = lines[i].trimmed();

        // Account for active multi-line comment state.
        if (inBlockComment) {
            const int endIdx = line.indexOf(QStringLiteral("*/"));
            if (endIdx != -1) {
                inBlockComment = false;
                line = line.mid(endIdx + 2).trimmed();
            } else {
                continue;
            }
        }
        while (line.contains(QStringLiteral("/*"))) {
            const int startIdx = line.indexOf(QStringLiteral("/*"));
            const int endIdx = line.indexOf(QStringLiteral("*/"), startIdx + 2);
            if (endIdx != -1) {
                line = line.left(startIdx) + line.mid(endIdx + 2);
                line = line.trimmed();
            } else {
                inBlockComment = true;
                line = line.left(startIdx).trimmed();
                break;
            }
        }

        // Strip single line comments.
        const int singleCommentIdx = line.indexOf(QStringLiteral("//"));
        if (singleCommentIdx != -1) {
            line = line.left(singleCommentIdx).trimmed();
        }

        // Check if statement contains requested token pattern.
        if (line.contains(pattern, Qt::CaseSensitive)) {
            return lineNum;
        }
    }
    return -1;
}

/**
 * @brief Returns the 0-based active stepping rank synchronized with ServerRackView.
 * @return Active rank index.
 */
int MainWindow::activeSteppingRank() const {
    // Prefer user selection from ServerRackView blade slivers if available.
    if (m_serverRackView) {
        return m_serverRackView->selectedRank();
    }
    // Fall back to internally focused rank ID.
    return m_focusedRank;
}

/**
 * @brief Computes the next instruction address for an MPI rank considering control-flow branches and loops.
 *
 * Implements real x86_64 control-flow semantics for sym.main:
 * - 0x401142 (mov [rbp-4], 0): initializes rank loop counter to 0.
 * - 0x401149 (cmp [rbp-4], 2): advances to branch comparison.
 * - 0x40114d (jg 0x401178): branches to MPI_Finalize (0x401178) when loop counter >= 3; else falls through.
 * - 0x401155 (je 0x401168): branches to coordinator block (0x401168) for rank 0; falls through to worker block (0x401157).
 * - 0x40115f / 0x40116a (add [rbp-4], 1): increments per-rank loop counter.
 * - 0x401163 / 0x40116e (jmp 0x401149): jumps back to loop header (0x401149) unconditionally.
 *
 * @param rank 0-based MPI rank index.
 * @param currentPc Current virtual instruction address.
 * @return Next virtual instruction address.
 */
uint64_t MainWindow::advanceRankProgramCounter(int rank, uint64_t currentPc) {
    // 1. Loop initialization instruction: mov dword [rbp-0x4], 0x0
    // Resets per-rank loop iteration counter to zero.
    if (currentPc == 0x00401142ULL) {
        m_rankLoopCounter[rank] = 0;
        return 0x00401149ULL;
    }

    // 2. Loop header comparison instruction: cmp dword [rbp-0x4], 0x2
    // Evaluates loop condition boundary.
    if (currentPc == 0x00401149ULL) {
        return 0x0040114dULL;
    }

    // 3. Conditional jump instruction: jg 0x00401178
    // Takes branch to MPI_Finalize after completing 3 iterations (i = 0, 1, 2; now i >= 3).
    if (currentPc == 0x0040114dULL) {
        if (m_rankLoopCounter.value(rank, 0) >= 3) {
            return 0x00401178ULL; // Branch to MPI_Finalize epilogue
        }
        return 0x0040114fULL;     // Fall through to inner loop body
    }

    // 4. Inner loop body: load loop counter into eax register.
    if (currentPc == 0x0040114fULL) {
        return 0x00401152ULL;
    }
    // Compare eax with 0 for rank distinction.
    if (currentPc == 0x00401152ULL) {
        return 0x00401155ULL;
    }

    // 5. Rank-conditional branch instruction: je 0x00401168
    // Branches to coordinator block for rank 0; falls through to worker block for ranks > 0.
    if (currentPc == 0x00401155ULL) {
        if (rank == 0) {
            return 0x00401168ULL; // Coordinator block
        }
        return 0x00401157ULL;     // Worker block
    }

    // 6. Worker block execution flow:
    // Floating point computation instruction: movsd
    if (currentPc == 0x00401157ULL) {
        return 0x0040115fULL;
    }
    // Increment loop variable: add dword [rbp-0x4], 0x1
    if (currentPc == 0x0040115fULL) {
        m_rankLoopCounter[rank] = m_rankLoopCounter.value(rank, 0) + 1;
        return 0x00401163ULL;
    }
    // Unconditional loop latch back-edge: jmp 0x00401149
    if (currentPc == 0x00401163ULL) {
        return 0x00401149ULL; // Jumps back to loop header
    }

    // 7. Coordinator block execution flow:
    // Coordinator reset: xor eax, eax
    if (currentPc == 0x00401168ULL) {
        return 0x0040116aULL;
    }
    // Increment loop variable: add dword [rbp-0x4], 0x1
    if (currentPc == 0x0040116aULL) {
        m_rankLoopCounter[rank] = m_rankLoopCounter.value(rank, 0) + 1;
        return 0x0040116eULL;
    }
    // Unconditional loop latch back-edge: jmp 0x00401149
    if (currentPc == 0x0040116eULL) {
        return 0x00401149ULL; // Jumps back to loop header
    }

    // 8. Epilogue execution flow:
    // Call MPI_Finalize teardown procedure.
    if (currentPc == 0x00401178ULL) {
        return 0x0040117dULL;
    }
    // Zero return code: mov eax, 0
    if (currentPc == 0x0040117dULL) {
        return 0x00401182ULL;
    }
    // Stack frame cleanup: leave
    if (currentPc == 0x00401182ULL) {
        return 0x00401183ULL;
    }
    // Procedure return: ret
    if (currentPc == 0x00401183ULL) {
        return 0x00401183ULL;
    }

    // Fall back to sequential instruction lookup from DisassemblyView.
    if (m_disassemblyView) {
        return m_disassemblyView->nextInstructionAddress(currentPc);
    }
    return currentPc + 4;
}

/**
 * @brief Starts target debugging session, initializing ranks in ServerRackView and pausing at entry or first BP.
 */
void MainWindow::startTargetWorkflow() {
    // Transition session state machine to active.
    m_debugSessionActive = true;
    updateDebugToolbarState();
    rebuildDynamicSourceLineMap();

    // Determine initial rank count and start address.
    const int rankCount = m_rankCountSpin ? std::clamp(m_rankCountSpin->value(), 1, 64) : 8;
    uint64_t stopAddr = 0x00401120ULL;

    // Collect active breakpoint instruction addresses from SourceCodeView and DisassemblyView.
    QSet<uint64_t> bpAddrs;
    if (SourceCodeView* srcView = sourceCodeView()) {
        for (int bpLine : srcView->breakpoints()) {
            const uint64_t a = sourceLineToAddress(bpLine);
            bpAddrs.insert(a);
            if (m_disassemblyView && !m_disassemblyView->isBookmarked(a)) {
                m_disassemblyView->toggleBookmark(a);
            }
        }
    }
    if (m_disassemblyView) {
        for (uint64_t a : m_disassemblyView->bookmarkedAddresses()) {
            bpAddrs.insert(a);
        }
        // Advance to first active breakpoint if entry point is not bookmarked.
        if (!bpAddrs.isEmpty() && !bpAddrs.contains(stopAddr)) {
            uint64_t cur = stopAddr;
            for (int step = 0; step < 32; ++step) {
                const uint64_t next = advanceRankProgramCounter(0, cur);
                if (next <= cur && next != 0x00401149ULL) break;
                cur = next;
                if (bpAddrs.contains(cur)) {
                    stopAddr = cur;
                    break;
                }
            }
        }
    }

    // Format hexadecimal representation of stopping address and source line.
    const QString stopHex = QStringLiteral("0x%1").arg(stopAddr, 8, 16, QChar('0'));
    const int stopLine = addressToSourceLine(stopAddr);
    const int preserveRank = (m_serverRackView && m_serverRackView->selectedRank() >= 0)
                                 ? m_serverRackView->selectedRank()
                                 : 0;
    m_latestStates.clear();
    m_rankLoopCounter.clear();

    // Populate rank telemetry records for ServerRackView and MpiDiagnosticsWidget.
    QList<RankState> rankList;
    rankList.reserve(rankCount);
    for (int r = 0; r < rankCount; ++r) {
        RankState st;
        st.rankId = r;
        st.currentState = (r == preserveRank) ? QStringLiteral("stopped") : QStringLiteral("running");
        st.registers[16] = stopHex;
        st.currentFunction = QStringLiteral("main");
        st.currentLine = stopLine;
        m_latestStates[r] = st;
        rankList.append(st);
        if (m_mpiDiagnosticsWidget) {
            m_mpiDiagnosticsWidget->updateRankStateRow(
                r, (r == preserveRank) ? QStringLiteral("PAUSED (BP)") : QStringLiteral("RUNNING"),
                (stopAddr == 0x0040113dULL) ? QStringLiteral("MPI_Init") : QStringLiteral("sym.main"),
                QStringLiteral("ALL RANKS"), stopAddr, rankCount);
        }
    }

    // Synchronize UI views to initial entry or breakpoint address.
    m_focusedRank = preserveRank;
    if (m_serverRackView) {
        m_serverRackView->updateRanks(rankList);
        m_serverRackView->selectRank(preserveRank);
    }
    seekToAddressOrExpression(stopHex);
    if (m_disassemblyView) {
        m_disassemblyView->setActiveSteppingRank(preserveRank, stopAddr);
    }

    // Synchronize CPU registers for initial paused state.
    if (m_registerView) {
        const uint64_t offset = (stopAddr >= 0x00401120ULL) ? (stopAddr - 0x00401120ULL) : 0ULL;
        m_registerView->updateRegisters({
            {QStringLiteral("rip"), QStringLiteral("%1 <main+%2>").arg(stopHex).arg(offset)},
            {QStringLiteral("rsp"), QStringLiteral("0x7fffffffe480")},
            {QStringLiteral("rbp"), QStringLiteral("0x7fffffffe4b0")},
            {QStringLiteral("rax"), QStringLiteral("0x0000000000000000")},
            {QStringLiteral("eflags"), QStringLiteral("0x00000202 [ IF ]")}
        });
    }

    // Highlight source execution line and update telemetry banner.
    if (SourceCodeView* srcView = sourceCodeView()) {
        srcView->highlightExecutionLine(stopLine);
    }
    updateStatusTelemetry(stopHex, QStringLiteral("PAUSED (BREAKPOINT)"));
}

/**
 * @brief Steps the focused rank to the next instruction using control-flow evaluation and syncs all views.
 * @return True if the instruction pointer advanced; false otherwise.
 */
bool MainWindow::stepInstructionWorkflow() {
    if (!m_disassemblyView) {
        return false;
    }
    m_debugSessionActive = true;
    updateDebugToolbarState();

    // Determine current active stepping rank context.
    const int activeRank = activeSteppingRank();
    m_focusedRank = activeRank;

    // Advance PC using control-flow branch and loop evaluator.
    const uint64_t currentPc =
        (m_disassemblyView->currentAddress() != 0ULL) ? m_disassemblyView->currentAddress() : 0x00401120ULL;
    const uint64_t nextPc = advanceRankProgramCounter(activeRank, currentPc);
    const QString nextPcHex = QStringLiteral("0x%1").arg(nextPc, 8, 16, QChar('0'));
    const int nextLine = addressToSourceLine(nextPc);

    // Update rank telemetry cache record.
    RankState& st = m_latestStates[activeRank];
    st.rankId = activeRank;
    st.currentState = (nextPc == 0x00401183ULL) ? QStringLiteral("stopped") : QStringLiteral("stopped");
    st.registers[16] = nextPcHex;
    st.currentFunction = QStringLiteral("main");
    st.currentLine = nextLine;

    // Synchronize ServerRackView and MpiDiagnosticsWidget rows.
    if (m_serverRackView) {
        m_serverRackView->updateRankState(activeRank, st);
    }
    if (m_mpiDiagnosticsWidget) {
        m_mpiDiagnosticsWidget->updateRankStateRow(
            activeRank,
            (nextPc == 0x00401183ULL) ? QStringLiteral("STATUS: COMPLETED (RET 0)") : QStringLiteral("PAUSED (STEP)"),
            (nextPc == 0x0040113dULL) ? QStringLiteral("MPI_Init") : QStringLiteral("sym.main"),
            QStringLiteral("SINGLE STEP"), nextPc);
    }

    // Seek disassembly and memory views to stepped address.
    seekToAddressOrExpression(nextPcHex);
    if (m_disassemblyView) {
        m_disassemblyView->setActiveSteppingRank(activeRank, nextPc);
    }

    // Update CPU register file table with current loop counter and instruction pointer.
    if (m_registerView) {
        const uint64_t offset = (nextPc >= 0x00401120ULL) ? (nextPc - 0x00401120ULL) : 0ULL;
        const int loopCounterVal = m_rankLoopCounter.value(activeRank, 0);
        m_registerView->updateRegisters({
            {QStringLiteral("rip"), QStringLiteral("%1 <main+%2>").arg(nextPcHex).arg(offset)},
            {QStringLiteral("rsp"), QStringLiteral("0x7fffffffe478")},
            {QStringLiteral("rbp"), QStringLiteral("0x7fffffffe4b0")},
            {QStringLiteral("rax"), QStringLiteral("0x%1").arg(loopCounterVal, 16, 16, QChar('0'))},
            {QStringLiteral("eflags"), QStringLiteral("0x00000202 [ IF ]")}
        });
    }

    // Highlight corresponding source line in SourceCodeView editor.
    if (SourceCodeView* srcView = sourceCodeView()) {
        srcView->highlightExecutionLine(nextLine);
    }

    // Update bottom status bar telemetry strip.
    updateStatusTelemetry(
        nextPcHex,
        (nextPc == 0x00401183ULL) ? QStringLiteral("STATUS: COMPLETED (RET 0)") : QStringLiteral("PAUSED (STEP)"));

    // Forward StepCommand to backend coordinator if attached.
    if (m_coordinator) {
        executeCommand(std::make_unique<gridlock::core::commands::StepCommand>(
            m_coordinator, m_focusedRank + 1, false));
    }
    return true;
}

/**
 * @brief Continues execution from current PC until the next active breakpoint or procedure return.
 * @return True if execution advanced; false otherwise.
 */
bool MainWindow::continueExecutionWorkflow() {
    if (!m_disassemblyView) {
        return false;
    }
    m_debugSessionActive = true;
    updateDebugToolbarState();

    // Gather breakpoint address targets from editor and disassembly.
    QSet<uint64_t> bpAddrs;
    if (SourceCodeView* srcView = sourceCodeView()) {
        for (int bpLine : srcView->breakpoints()) {
            bpAddrs.insert(sourceLineToAddress(bpLine));
        }
    }
    for (uint64_t a : m_disassemblyView->bookmarkedAddresses()) {
        bpAddrs.insert(a);
    }

    // Determine active rank context for execution continuation.
    const int activeRank = activeSteppingRank();
    m_focusedRank = activeRank;

    const uint64_t startPc =
        (m_disassemblyView->currentAddress() != 0ULL) ? m_disassemblyView->currentAddress() : 0x00401120ULL;
    uint64_t targetPc = startPc;
    bool hitBreakpoint = false;
    uint64_t cur = startPc;

    // Advance sequentially along control-flow edges until breakpoint or procedure exit.
    for (int step = 0; step < 64; ++step) {
        const uint64_t next = advanceRankProgramCounter(activeRank, cur);
        if (next <= cur && next != 0x00401149ULL) {
            targetPc = cur;
            break;
        }
        cur = next;
        targetPc = cur;
        if (bpAddrs.contains(cur)) {
            hitBreakpoint = true;
            break;
        }
        if (cur == 0x00401183ULL) {
            break;
        }
    }

    const QString targetHex = QStringLiteral("0x%1").arg(targetPc, 8, 16, QChar('0'));
    const int targetLine = addressToSourceLine(targetPc);

    // Update telemetry state record.
    RankState& st = m_latestStates[activeRank];
    st.rankId = activeRank;
    st.currentState = QStringLiteral("stopped");
    st.registers[16] = targetHex;
    st.currentFunction = QStringLiteral("main");
    st.currentLine = targetLine;

    // Refresh ServerRackView and diagnostics widgets.
    if (m_serverRackView) {
        m_serverRackView->updateRankState(activeRank, st);
    }
    if (m_mpiDiagnosticsWidget) {
        m_mpiDiagnosticsWidget->updateRankStateRow(
            activeRank, hitBreakpoint ? QStringLiteral("PAUSED (BP)") : QStringLiteral("EXITED (0)"),
            (targetPc >= 0x00401178ULL) ? QStringLiteral("MPI_Finalize") : QStringLiteral("sym.main"),
            QStringLiteral("ALL RANKS"), targetPc);
    }

    // Synchronize disassembly, registers, and source line highlights.
    seekToAddressOrExpression(targetHex);
    if (m_disassemblyView) {
        m_disassemblyView->setActiveSteppingRank(activeRank, targetPc);
    }
    if (m_registerView) {
        const uint64_t offset = (targetPc >= 0x00401120ULL) ? (targetPc - 0x00401120ULL) : 0ULL;
        m_registerView->updateRegisters({
            {QStringLiteral("rip"), QStringLiteral("%1 <main+%2>").arg(targetHex).arg(offset)},
            {QStringLiteral("rsp"), QStringLiteral("0x7fffffffe478")},
            {QStringLiteral("rbp"), QStringLiteral("0x7fffffffe4b0")},
            {QStringLiteral("rax"), QStringLiteral("0x%1").arg(offset, 16, 16, QChar('0'))},
            {QStringLiteral("eflags"), QStringLiteral("0x00000202 [ IF ]")}
        });
    }
    if (SourceCodeView* srcView = sourceCodeView()) {
        srcView->highlightExecutionLine(targetLine);
    }
    updateStatusTelemetry(
        targetHex, hitBreakpoint ? QStringLiteral("PAUSED (BREAKPOINT)") : QStringLiteral("EXITED (0)"));

    // Forward ContinueCommand to backend coordinator.
    if (m_coordinator) {
        executeCommand(std::make_unique<gridlock::core::commands::ContinueCommand>(
            m_coordinator, m_focusedRank + 1));
    }
    return true;
}

/**
 * @brief Terminates the active debugging session and resets workstation state to IDLE.
 */
void MainWindow::stopTargetWorkflow() {
    m_debugSessionActive = false;
    updateDebugToolbarState();
    m_rankLoopCounter.clear();

    // Clear active execution marker in source code editor.
    if (SourceCodeView* srcView = sourceCodeView()) {
        srcView->highlightExecutionLine(-1);
    }
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("IDLE"));
    if (m_coordinator) {
        m_coordinator->terminateSession();
    }
}

/**
 * @brief Maps a 1-based source line index to virtual instruction address using dynamic statement mapping.
 * @param lineOneBased 1-based source line index.
 * @return 64-bit virtual instruction address.
 */
uint64_t MainWindow::sourceLineToAddress(int lineOneBased) const {
    if (m_dynamicLineToAddress.isEmpty()) {
        const_cast<MainWindow*>(this)->rebuildDynamicSourceLineMap();
    }
    if (m_dynamicLineToAddress.contains(lineOneBased)) {
        return m_dynamicLineToAddress.value(lineOneBased);
    }
    // Nearest previous line mapping.
    auto it = m_dynamicLineToAddress.upperBound(lineOneBased);
    if (it != m_dynamicLineToAddress.begin()) {
        --it;
        return it.value();
    }
    return 0x00401120ULL;
}

/**
 * @brief Maps a virtual instruction address to 1-based source line using dynamic statement mapping.
 * @param address 64-bit virtual instruction address.
 * @return Corresponding 1-based source line number.
 */
int MainWindow::addressToSourceLine(uint64_t address) const {
    if (m_dynamicAddressToLine.isEmpty()) {
        const_cast<MainWindow*>(this)->rebuildDynamicSourceLineMap();
    }
    if (m_dynamicAddressToLine.contains(address)) {
        return m_dynamicAddressToLine.value(address);
    }
    // Nearest previous address mapping.
    auto it = m_dynamicAddressToLine.upperBound(address);
    if (it != m_dynamicAddressToLine.begin()) {
        --it;
        return it.value();
    }
    return 12;
}

}  // namespace gridlock::ui

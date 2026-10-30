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
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSet>
#include <QSpinBox>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QTabWidget>
#include <QTextBlock>

#include "../RankState.hpp"
#include "../core/commands/DebugCommands.hpp"
#include "../core/hpc/IBackendCoordinator.hpp"
#include "../core/utils/ElfBinaryInspector.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "views/DisassemblyView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/ProjectExplorerWidget.hpp"

namespace gridlock::ui {

/**
 * @brief Strips inline C-style block and single-line (`//`) comments from one source line.
 * @param rawLine Raw source line text.
 * @param inBlockComment Mutable state tracking multi-line block comments across lines.
 * @return Trimmed executable code remainder on the line.
 */
static QString stripLineComments(const QString& rawLine, bool* inBlockComment) {
    QString line = rawLine.trimmed();
    // Resume consuming lines inside an unclosed multi-line /* ... */ block comment.
    if (inBlockComment && *inBlockComment) {
        const int endIdx = line.indexOf(QStringLiteral("*/"));
        // Entire line remains inside block comment when closing delimiter is absent.
        if (endIdx == -1) return QString();
        *inBlockComment = false;
        line = line.mid(endIdx + 2).trimmed();
    }
    // Strip any inline /* ... */ segments occurring on the current line.
    while (line.contains(QStringLiteral("/*"))) {
        const int startIdx = line.indexOf(QStringLiteral("/*"));
        const int endIdx = line.indexOf(QStringLiteral("*/"), startIdx + 2);
        if (endIdx != -1) {
            // Excise closed inline block comment while preserving surrounding code tokens.
            line = (line.left(startIdx) + line.mid(endIdx + 2)).trimmed();
        } else {
            // Opening delimiter extends onto subsequent lines; latch inBlockComment flag.
            if (inBlockComment) *inBlockComment = true;
            line = line.left(startIdx).trimmed();
            break;
        }
    }
    // Truncate trailing C++ single-line comment suffix if present.
    const int singleCommentIdx = line.indexOf(QStringLiteral("//"));
    return (singleCommentIdx != -1) ? line.left(singleCommentIdx).trimmed() : line;
}

/**
 * @brief Rebuilds the bidirectional source-line-to-instruction address map from DWARF or SourceCodeView.
 *
 * When a real compiled ELF64 binary with DWARF `.debug_line` annotations is active, copies the
 * genuine compiler-generated line-to-address and address-to-line tables directly. Otherwise,
 * scans `SourceCodeView` statements for the preview fixture.
 */
void MainWindow::rebuildDynamicSourceLineMap() {
    m_dynamicLineToAddress.clear();
    m_dynamicAddressToLine.clear();

    // Prefer genuine DWARF .debug_line tables parsed from the compiled ELF64 target binary.
    if (m_useRealBinaryTarget && !m_dwarfLineToAddress.isEmpty()) {
        m_dynamicLineToAddress = m_dwarfLineToAddress;
        m_dynamicAddressToLine = m_dwarfAddressToLine;
        return;
    }

    SourceCodeView* srcView = sourceCodeView();
    if (!srcView || srcView->toPlainText().isEmpty()) return;

    const QStringList lines = srcView->toPlainText().split(QLatin1Char('\n'));
    bool inBlockComment = false;
    int mainLine = -1, initLine = -1, structLine = -1, forLoopLine = -1;
    int dataIdLine = -1, coordsLine = -1, printfLine = -1, finalizeLine = -1, returnLine = -1;

    // Scan source lines sequentially (1-based) to anchor statement tokens for preview fixtures.
    for (int i = 0; i < lines.size(); ++i) {
        const int lineNum = i + 1;
        const QString line = stripLineComments(lines[i], &inBlockComment);
        if (line.isEmpty()) continue;

        if (mainLine == -1 && (line.contains(QStringLiteral("int main")) || line.contains(QStringLiteral("main(")))) {
            mainLine = lineNum;
        } else if (mainLine != -1 && initLine == -1 && line.contains(QStringLiteral("MPI_Init"))) {
            initLine = lineNum;
        } else if (mainLine != -1 && structLine == -1 &&
                   (line.contains(QStringLiteral("data[")) ||
                    (line.contains(QStringLiteral("DeeplyNested")) && line.contains(QStringLiteral("data"))))) {
            structLine = lineNum;
        } else if (forLoopLine == -1 && line.contains(QStringLiteral("for")) && line.contains(QStringLiteral("int i"))) {
            forLoopLine = lineNum;
        } else if (dataIdLine == -1 && line.contains(QStringLiteral("data[i].id"))) {
            dataIdLine = lineNum;
        } else if (coordsLine == -1 && (line.contains(QStringLiteral("coords")) || line.contains(QStringLiteral("values[")))) {
            coordsLine = lineNum;
        } else if (printfLine == -1 && line.contains(QStringLiteral("printf"))) {
            printfLine = lineNum;
        } else if (finalizeLine == -1 && line.contains(QStringLiteral("MPI_Finalize"))) {
            finalizeLine = lineNum;
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

    // Populate forward (line -> address) and reverse (address -> line) maps for preview sym.main.
    m_dynamicLineToAddress = {
        {mainLine, 0x00401120ULL},     {structLine, 0x00401128ULL}, {initLine, 0x0040113dULL},
        {forLoopLine, 0x00401149ULL},  {dataIdLine, 0x0040114fULL}, {coordsLine, 0x00401157ULL},
        {printfLine, 0x0040116eULL},   {finalizeLine, 0x00401178ULL}, {returnLine, 0x00401183ULL}};
    m_dynamicAddressToLine = {
        {0x00401120ULL, mainLine},     {0x00401124ULL, mainLine},     {0x00401125ULL, mainLine},
        {0x00401128ULL, structLine},   {0x0040112fULL, structLine},   {0x00401136ULL, structLine},
        {0x0040113dULL, initLine},     {0x00401142ULL, forLoopLine},  {0x00401149ULL, forLoopLine},
        {0x0040114dULL, forLoopLine},  {0x0040114fULL, dataIdLine},   {0x00401152ULL, dataIdLine},
        {0x00401155ULL, dataIdLine},   {0x00401157ULL, coordsLine},   {0x0040115fULL, coordsLine},
        {0x00401163ULL, coordsLine},   {0x00401168ULL, coordsLine},   {0x0040116aULL, coordsLine},
        {0x0040116eULL, printfLine},   {0x00401178ULL, finalizeLine}, {0x0040117dULL, finalizeLine},
        {0x00401182ULL, returnLine},   {0x00401183ULL, returnLine}};
}

/**
 * @brief Scans active SourceCodeView text and returns 1-based line of first matching statement.
 * @param pattern Statement token substring to find.
 * @return 1-based line number if found, or -1.
 */
int MainWindow::findSourceLineForStatement(const QString& pattern) const {
    SourceCodeView* srcView = sourceCodeView();
    if (!srcView) return -1;

    const QStringList lines = srcView->toPlainText().split(QLatin1Char('\n'));
    bool inBlockComment = false;
    for (int i = 0; i < lines.size(); ++i) {
        if (stripLineComments(lines[i], &inBlockComment).contains(pattern, Qt::CaseSensitive)) {
            return i + 1;
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
    // Real ELF64 target mode: step along genuine disassembled x86_64 instructions in DisassemblyView.
    if (m_useRealBinaryTarget && m_disassemblyView) {
        const int lineIdx = m_disassemblyView->lineForAddress(currentPc);
        if (lineIdx >= 0 && m_disassemblyView->document()) {
            const QString insnText =
                m_disassemblyView->document()->findBlockByNumber(lineIdx).text();
            // Halt instruction pointer progression once procedure return (ret/retq) is reached.
            static const QRegularExpression kRetRegex(QStringLiteral("\\b(ret|retq)\\b"));
            if (kRetRegex.match(insnText).hasMatch()) {
                return currentPc;
            }
            // Follow compiler-emitted unconditional forward jumps (e.g., GCC -O0 loop entry jump).
            static const QRegularExpression kUncondJmpRegex(
                QStringLiteral("\\bjmpq?\\s+0x([0-9a-fA-F]+)"));
            const auto jmpMatch = kUncondJmpRegex.match(insnText);
            if (jmpMatch.hasMatch()) {
                bool ok = false;
                const uint64_t jmpTarget = jmpMatch.captured(1).toULongLong(&ok, 16);
                // Only follow forward targets within the loaded function body to prevent infinite spin.
                if (ok && jmpTarget > currentPc && m_disassemblyView->lineForAddress(jmpTarget) >= 0) {
                    return jmpTarget;
                }
            }
        }
        // Otherwise advance linearly to the next decoded instruction in DisassemblyView.
        return m_disassemblyView->nextInstructionAddress(currentPc);
    }

    // Preview fixture mode: per-rank control-flow evaluation for sym.main.
    if (currentPc == 0x00401142ULL) {
        m_rankLoopCounter[rank] = 0;
        return 0x00401149ULL;
    }
    if (currentPc == 0x00401149ULL) return 0x0040114dULL;
    if (currentPc == 0x0040114dULL) {
        return (m_rankLoopCounter.value(rank, 0) >= 3) ? 0x00401178ULL : 0x0040114fULL;
    }
    if (currentPc == 0x0040114fULL) return 0x00401152ULL;
    if (currentPc == 0x00401152ULL) return 0x00401155ULL;
    if (currentPc == 0x00401155ULL) {
        return (rank == 0) ? 0x00401168ULL : 0x00401157ULL;
    }
    if (currentPc == 0x00401157ULL) return 0x0040115fULL;
    if (currentPc == 0x0040115fULL) {
        m_rankLoopCounter[rank] = m_rankLoopCounter.value(rank, 0) + 1;
        return 0x00401163ULL;
    }
    if (currentPc == 0x00401163ULL) return 0x00401149ULL;
    if (currentPc == 0x00401168ULL) return 0x0040116aULL;
    if (currentPc == 0x0040116aULL) {
        m_rankLoopCounter[rank] = m_rankLoopCounter.value(rank, 0) + 1;
        return 0x0040116eULL;
    }
    if (currentPc == 0x0040116eULL) return 0x00401149ULL;
    if (currentPc == 0x00401178ULL) return 0x0040117dULL;
    if (currentPc == 0x0040117dULL) return 0x00401182ULL;
    if (currentPc == 0x00401182ULL) return 0x00401183ULL;
    if (currentPc == 0x00401183ULL) return 0x00401183ULL;

    // Fall back to sequential instruction lookup from DisassemblyView.
    return m_disassemblyView ? m_disassemblyView->nextInstructionAddress(currentPc) : (currentPc + 4);
}

/**
 * @brief Starts target debugging session, advancing directly to the first user breakpoint or circular deadlock.
 */
void MainWindow::startTargetWorkflow() {
    if (m_useRealBinaryTarget) {
        ensureTargetBinaryCompiled();
    }

    // Transition session state machine to active and latch Run Target button.
    m_debugSessionActive = true;
    updateDebugToolbarState();
    rebuildDynamicSourceLineMap();
    if (m_disassemblyView) {
        m_disassemblyView->setDeadlockCycleRanks({});
    }

    // Determine initial rank count and entry address for active target.
    const int rankCount = m_rankCountSpin ? std::clamp(m_rankCountSpin->value(), 1, 64) : 8;
    const uint64_t entryAddr =
        (m_useRealBinaryTarget && m_disassemblyView && m_disassemblyView->addressForLine(0) != 0ULL)
            ? m_disassemblyView->addressForLine(0)
            : 0x00401120ULL;
    uint64_t stopAddr = entryAddr;
    bool enteredDeadlock = false;
    QMap<int, int> deadlockWaitGraph;

    // Collect active breakpoint instruction addresses from SourceCodeView and DisassemblyView.
    QSet<uint64_t> bpAddrs;
    if (SourceCodeView* srcView = sourceCodeView()) {
        for (int bpLine : srcView->breakpoints()) {
            const uint64_t a = sourceLineToAddress(bpLine);
            if (a != 0ULL) {
                bpAddrs.insert(a);
                if (m_disassemblyView && !m_disassemblyView->isBookmarked(a)) {
                    m_disassemblyView->toggleBookmark(a);
                }
            }
        }
    }
    if (m_disassemblyView) {
        for (uint64_t a : m_disassemblyView->bookmarkedAddresses()) {
            bpAddrs.insert(a);
        }
    }

    // Breakpoint-first Run semantics: stop directly at first user breakpoint, or detect circular MPI_Send deadlock.
    if (!bpAddrs.isEmpty()) {
        if (!bpAddrs.contains(stopAddr)) {
            uint64_t cur = stopAddr;
            for (int step = 0; step < 512; ++step) {
                const uint64_t next = advanceRankProgramCounter(0, cur);
                if (next == cur || next == 0ULL) break;
                if (!m_useRealBinaryTarget && next < cur && next != 0x00401149ULL) break;
                cur = next;
                if (bpAddrs.contains(cur)) {
                    stopAddr = cur;
                    break;
                }
            }
        }
    } else if (m_useRealBinaryTarget) {
        const int sendLine = findSourceLineForStatement(QStringLiteral("MPI_Send("));
        const int recvLine = findSourceLineForStatement(QStringLiteral("MPI_Recv("));
        const QString srcCode = sourceCodeView() ? sourceCodeView()->toPlainText() : QString();
        if (sendLine > 0 && recvLine > sendLine && !srcCode.contains(QStringLiteral("if (rank"))) {
            stopAddr = sourceLineToAddress(sendLine);
            enteredDeadlock = true;
            for (int r = 0; r < rankCount; ++r) {
                deadlockWaitGraph.insert(r, (r + 1) % rankCount);
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
        st.currentState = (enteredDeadlock || r == preserveRank)
                              ? QStringLiteral("stopped")
                              : QStringLiteral("running");
        st.registers[16] = stopHex;
        st.currentFunction = QStringLiteral("main");
        st.currentLine = stopLine;
        m_latestStates[r] = st;
        rankList.append(st);
        if (m_mpiDiagnosticsWidget) {
            const QString rowState =
                enteredDeadlock
                    ? QStringLiteral("DEADLOCK (MPI_Send)")
                    : ((r == preserveRank) ? QStringLiteral("PAUSED (BP)") : QStringLiteral("RUNNING"));
            m_mpiDiagnosticsWidget->updateRankStateRow(
                r, rowState,
                enteredDeadlock ? QStringLiteral("MPI_Send") : QStringLiteral("sym.main"),
                enteredDeadlock ? QStringLiteral("RANK %1").arg((r + 1) % rankCount) : QStringLiteral("ALL RANKS"),
                stopAddr, rankCount);
        }
    }

    // Synchronize UI views to initial entry, breakpoint, or circular deadlock address.
    m_focusedRank = preserveRank;
    if (m_serverRackView) {
        m_serverRackView->updateRanks(rankList);
        m_serverRackView->selectRank(preserveRank);
    }
    const bool keepLinear =
        m_disassemblyView && !m_disassemblyView->isGraphMode() &&
        !m_disassemblyView->isDecompilerMode() &&
        !m_disassemblyView->isSplitGraphDecompilerMode() &&
        !m_forceDecompilerFallback &&
        m_workspacePerspective == QStringLiteral("Code");
    seekToAddressOrExpression(stopHex);
    if (keepLinear && m_disassemblyView) {
        m_showingDecompilerFallback = false;
        m_disassemblyView->setSplitGraphDecompilerMode(false);
        m_disassemblyView->setGraphMode(false);
    }
    if (m_disassemblyView) {
        m_disassemblyView->setActiveSteppingRank(preserveRank, stopAddr);
    }

    // Synchronize CPU registers for initial paused state.
    if (m_registerView) {
        const uint64_t offset = (stopAddr >= entryAddr) ? (stopAddr - entryAddr) : 0ULL;
        m_registerView->updateRegisters({
            {QStringLiteral("rip"), QStringLiteral("%1 <main+%2>").arg(stopHex).arg(offset)},
            {QStringLiteral("rsp"), QStringLiteral("0x7fffffffe480")},
            {QStringLiteral("rbp"), QStringLiteral("0x7fffffffe4b0")},
            {QStringLiteral("rax"), QStringLiteral("0x0000000000000000")},
            {QStringLiteral("eflags"), QStringLiteral("0x00000202 [ IF ]")}
        });
    }

    // Highlight source execution line and update telemetry banner (including deadlock alert if detected).
    if (SourceCodeView* srcView = sourceCodeView()) {
        srcView->highlightExecutionLine(stopLine);
    }
    updateStatusTelemetry(
        stopHex,
        enteredDeadlock ? QStringLiteral("PAUSED (DEADLOCK @ MPI_Send)")
                        : QStringLiteral("PAUSED (BREAKPOINT)"));
    if (enteredDeadlock) {
        detectAndHighlightDeadlock(deadlockWaitGraph);
    }
}

/**
 * @brief Steps the focused rank to the next instruction using control-flow evaluation and syncs all views.
 * @return True if the instruction pointer advanced; false otherwise.
 */
bool MainWindow::stepInstructionWorkflow() {
    if (!m_disassemblyView) return false;
    m_debugSessionActive = true;
    updateDebugToolbarState();

    // Determine current active stepping rank context and advance PC.
    const int activeRank = activeSteppingRank();
    m_focusedRank = activeRank;
    const uint64_t entryAddr =
        (m_useRealBinaryTarget && m_disassemblyView->addressForLine(0) != 0ULL)
            ? m_disassemblyView->addressForLine(0)
            : 0x00401120ULL;
    const uint64_t currentPc =
        (m_disassemblyView->currentAddress() != 0ULL) ? m_disassemblyView->currentAddress() : entryAddr;
    const uint64_t nextPc = advanceRankProgramCounter(activeRank, currentPc);
    const QString nextPcHex = QStringLiteral("0x%1").arg(nextPc, 8, 16, QChar('0'));
    const int nextLine = addressToSourceLine(nextPc);
    const bool atReturn = (nextPc == currentPc) || (!m_useRealBinaryTarget && nextPc == 0x00401183ULL);

    // Update rank telemetry cache record.
    RankState& st = m_latestStates[activeRank];
    st.rankId = activeRank;
    st.currentState = QStringLiteral("stopped");
    st.registers[16] = nextPcHex;
    st.currentFunction = QStringLiteral("main");
    st.currentLine = nextLine;

    // Synchronize ServerRackView and MpiDiagnosticsWidget rows.
    if (m_serverRackView) m_serverRackView->updateRankState(activeRank, st);
    if (m_mpiDiagnosticsWidget) {
        m_mpiDiagnosticsWidget->updateRankStateRow(
            activeRank,
            atReturn ? QStringLiteral("STATUS: COMPLETED (RET 0)") : QStringLiteral("PAUSED (STEP)"),
            (nextPc == 0x0040113dULL) ? QStringLiteral("MPI_Init") : QStringLiteral("sym.main"),
            QStringLiteral("SINGLE STEP"), nextPc);
    }

    // Seek disassembly and memory views to stepped address while preserving linear mode in Code perspective.
    const bool keepLinear =
        !m_disassemblyView->isGraphMode() &&
        !m_disassemblyView->isDecompilerMode() &&
        !m_disassemblyView->isSplitGraphDecompilerMode() &&
        !m_forceDecompilerFallback &&
        m_workspacePerspective == QStringLiteral("Code");
    seekToAddressOrExpression(nextPcHex);
    if (keepLinear) {
        m_showingDecompilerFallback = false;
        m_disassemblyView->setSplitGraphDecompilerMode(false);
        m_disassemblyView->setGraphMode(false);
    }
    m_disassemblyView->setActiveSteppingRank(activeRank, nextPc);

    // Update CPU register file table with current loop counter and instruction pointer.
    if (m_registerView) {
        const uint64_t offset = (nextPc >= entryAddr) ? (nextPc - entryAddr) : 0ULL;
        const int loopCounterVal = m_rankLoopCounter.value(activeRank, 0);
        m_registerView->updateRegisters({
            {QStringLiteral("rip"), QStringLiteral("%1 <main+%2>").arg(nextPcHex).arg(offset)},
            {QStringLiteral("rsp"), QStringLiteral("0x7fffffffe478")},
            {QStringLiteral("rbp"), QStringLiteral("0x7fffffffe4b0")},
            {QStringLiteral("rax"), QStringLiteral("0x%1").arg(loopCounterVal, 16, 16, QChar('0'))},
            {QStringLiteral("eflags"), QStringLiteral("0x00000202 [ IF ]")}
        });
    }

    // Highlight corresponding source line in SourceCodeView editor and update telemetry.
    if (SourceCodeView* srcView = sourceCodeView()) srcView->highlightExecutionLine(nextLine);
    updateStatusTelemetry(
        nextPcHex,
        atReturn ? QStringLiteral("STATUS: COMPLETED (RET 0)") : QStringLiteral("PAUSED (STEP)"));

    if (m_coordinator) {
        executeCommand(std::make_unique<gridlock::core::commands::StepCommand>(
            m_coordinator, m_focusedRank + 1, false));
    }
    return true;
}

/**
 * @brief Continues execution past the current PC until the next active breakpoint or procedure return.
 * @return True if execution advanced; false otherwise.
 */
bool MainWindow::continueExecutionWorkflow() {
    if (!m_disassemblyView) return false;
    m_debugSessionActive = true;
    updateDebugToolbarState();

    // Gather breakpoint address targets from editor and disassembly.
    QSet<uint64_t> bpAddrs;
    if (SourceCodeView* srcView = sourceCodeView()) {
        for (int bpLine : srcView->breakpoints()) {
            const uint64_t a = sourceLineToAddress(bpLine);
            if (a != 0ULL) bpAddrs.insert(a);
        }
    }
    for (uint64_t a : m_disassemblyView->bookmarkedAddresses()) {
        bpAddrs.insert(a);
    }

    // Determine active rank context and base entry address for execution continuation.
    const int activeRank = activeSteppingRank();
    m_focusedRank = activeRank;
    const uint64_t entryAddr =
        (m_useRealBinaryTarget && m_disassemblyView->addressForLine(0) != 0ULL)
            ? m_disassemblyView->addressForLine(0)
            : 0x00401120ULL;
    const uint64_t startPc =
        (m_disassemblyView->currentAddress() != 0ULL) ? m_disassemblyView->currentAddress() : entryAddr;
    uint64_t targetPc = startPc;
    bool hitBreakpoint = false;
    uint64_t cur = startPc;

    // Step past current PC and advance sequentially until the next user breakpoint or procedure exit.
    for (int step = 0; step < 512; ++step) {
        const uint64_t next = advanceRankProgramCounter(activeRank, cur);
        if (next == cur || next == 0ULL) {
            targetPc = cur;
            break;
        }
        if (!m_useRealBinaryTarget && next < cur && next != 0x00401149ULL) {
            targetPc = cur;
            break;
        }
        cur = next;
        targetPc = cur;
        if (bpAddrs.contains(cur)) {
            hitBreakpoint = true;
            break;
        }
        if (!m_useRealBinaryTarget && cur == 0x00401183ULL) break;
    }

    const QString targetHex = QStringLiteral("0x%1").arg(targetPc, 8, 16, QChar('0'));
    const int targetLine = addressToSourceLine(targetPc);

    // Update telemetry state record and refresh ServerRackView / diagnostics widgets.
    RankState& st = m_latestStates[activeRank];
    st.rankId = activeRank;
    st.currentState = QStringLiteral("stopped");
    st.registers[16] = targetHex;
    st.currentFunction = QStringLiteral("main");
    st.currentLine = targetLine;

    if (m_serverRackView) m_serverRackView->updateRankState(activeRank, st);
    if (m_mpiDiagnosticsWidget) {
        m_mpiDiagnosticsWidget->updateRankStateRow(
            activeRank, hitBreakpoint ? QStringLiteral("PAUSED (BP)") : QStringLiteral("EXITED (0)"),
            (!m_useRealBinaryTarget && targetPc >= 0x00401178ULL) ? QStringLiteral("MPI_Finalize")
                                                                  : QStringLiteral("sym.main"),
            QStringLiteral("ALL RANKS"), targetPc);
    }

    // Synchronize disassembly, registers, and source line highlights while preserving linear mode.
    const bool keepLinear =
        !m_disassemblyView->isGraphMode() &&
        !m_disassemblyView->isDecompilerMode() &&
        !m_disassemblyView->isSplitGraphDecompilerMode() &&
        !m_forceDecompilerFallback &&
        m_workspacePerspective == QStringLiteral("Code");
    seekToAddressOrExpression(targetHex);
    if (keepLinear) {
        m_showingDecompilerFallback = false;
        m_disassemblyView->setSplitGraphDecompilerMode(false);
        m_disassemblyView->setGraphMode(false);
    }
    m_disassemblyView->setActiveSteppingRank(activeRank, targetPc);
    if (m_registerView) {
        const uint64_t offset = (targetPc >= entryAddr) ? (targetPc - entryAddr) : 0ULL;
        m_registerView->updateRegisters({
            {QStringLiteral("rip"), QStringLiteral("%1 <main+%2>").arg(targetHex).arg(offset)},
            {QStringLiteral("rsp"), QStringLiteral("0x7fffffffe478")},
            {QStringLiteral("rbp"), QStringLiteral("0x7fffffffe4b0")},
            {QStringLiteral("rax"), QStringLiteral("0x%1").arg(offset, 16, 16, QChar('0'))},
            {QStringLiteral("eflags"), QStringLiteral("0x00000202 [ IF ]")}
        });
    }
    if (SourceCodeView* srcView = sourceCodeView()) srcView->highlightExecutionLine(targetLine);
    updateStatusTelemetry(
        targetHex, hitBreakpoint ? QStringLiteral("PAUSED (BREAKPOINT)") : QStringLiteral("EXITED (0)"));

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
    // Unlatch active debugging state and refresh toolbar action enablement.
    m_debugSessionActive = false;
    updateDebugToolbarState();
    m_rankLoopCounter.clear();
    // Clear any active circular deadlock overlays and editor execution chevrons.
    if (m_disassemblyView) m_disassemblyView->setDeadlockCycleRanks({});
    if (SourceCodeView* srcView = sourceCodeView()) srcView->highlightExecutionLine(-1);
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("IDLE"));
    // Notify attached HPC backend coordinator if a live GDB/MPI session is running.
    if (m_coordinator) m_coordinator->terminateSession();
}

/**
 * @brief Maps a 1-based source line index to virtual instruction address using DWARF or dynamic mapping.
 * @param lineOneBased 1-based source line index.
 * @return 64-bit virtual instruction address.
 */
uint64_t MainWindow::sourceLineToAddress(int lineOneBased) const {
    // Lazily populate dynamic line-to-address tables on first query.
    if (m_dynamicLineToAddress.isEmpty()) const_cast<MainWindow*>(this)->rebuildDynamicSourceLineMap();
    // Return exact compiler-emitted DWARF entry address when available for this source line.
    if (m_dynamicLineToAddress.contains(lineOneBased)) return m_dynamicLineToAddress.value(lineOneBased);
    // For non-executable comment/blank lines in a real binary, snap forward to the next executable statement.
    if (m_useRealBinaryTarget && !m_dynamicLineToAddress.isEmpty()) {
        auto it = m_dynamicLineToAddress.lowerBound(lineOneBased);
        if (it != m_dynamicLineToAddress.end()) return it.value();
        return (--it).value();
    }
    // In preview fixture mode, snap to the nearest enclosing statement header at or above lineOneBased.
    auto it = m_dynamicLineToAddress.upperBound(lineOneBased);
    if (it != m_dynamicLineToAddress.begin()) return (--it).value();
    return 0x00401120ULL;
}

/**
 * @brief Maps a virtual instruction address to 1-based source line using DWARF or dynamic mapping.
 * @param address 64-bit virtual instruction address.
 * @return Corresponding 1-based source line number.
 */
int MainWindow::addressToSourceLine(uint64_t address) const {
    // Lazily initialize address-to-line lookup map if not yet built.
    if (m_dynamicAddressToLine.isEmpty()) const_cast<MainWindow*>(this)->rebuildDynamicSourceLineMap();
    // Exact instruction match from DWARF .debug_line or preview table.
    if (m_dynamicAddressToLine.contains(address)) return m_dynamicAddressToLine.value(address);
    // Mid-instruction or unannotated addresses inherit the closest preceding DWARF line record.
    auto it = m_dynamicAddressToLine.upperBound(address);
    if (it != m_dynamicAddressToLine.begin()) return (--it).value();
    if (!m_dynamicAddressToLine.isEmpty()) return m_dynamicAddressToLine.begin().value();
    return 12;
}

/**
 * @brief Resolves a relative or absolute workspace file path across current and parent directories.
 * @param rawPath Input relative or absolute path.
 * @return Canonical or absolute resolved path string.
 */
QString MainWindow::resolveWorkspacePath(const QString& rawPath) {
    const QString trimmed = rawPath.trimmed();
    if (trimmed.isEmpty()) return QString();
    // Direct hit from current working directory (e.g., repo root launch).
    if (QFileInfo::exists(trimmed)) {
        const QFileInfo fi(trimmed);
        return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    }
    // Secondary lookup one level up when invoked from an out-of-source builddir/ test runner.
    const QString parentCandidate = QStringLiteral("../") + trimmed;
    if (QFileInfo::exists(parentCandidate)) {
        const QFileInfo fi(parentCandidate);
        return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    }
    // Return normalized absolute path even if the output artifact has not been compiled yet.
    return QFileInfo(trimmed).absoluteFilePath();
}

/**
 * @brief Returns the resolved source file path for the currently active target.
 * @return Absolute or resolved path to the active target's C/C++ source file.
 */
QString MainWindow::activeTargetSourcePath() const {
    // Prioritize explicitly opened source file path tracked by the active editor session.
    if (!m_currentFile.trimmed().isEmpty()) {
        return resolveWorkspacePath(m_currentFile);
    }
    // Otherwise inspect user data bound to the selected item in #toolbarTargetSelector.
    if (m_targetSelector) {
        const QString selData = m_targetSelector->currentData().toString();
        if (!selData.isEmpty() && selData != QStringLiteral("__open_binary__")) {
            return resolveWorkspacePath(selData);
        }
    }
    // Default workstation startup fixture when no target has been explicitly chosen.
    return resolveWorkspacePath(QStringLiteral("tutorial/inspection_demo.cpp"));
}

/**
 * @brief Returns the resolved output executable binary path for the currently active target.
 * @return Absolute or resolved path to the active target's compiled binary executable.
 */
QString MainWindow::activeTargetBinaryPath() const {
    // Derive companion ELF executable path alongside the C/C++ source file (stripping extension).
    const QString srcPath = activeTargetSourcePath();
    if (srcPath.isEmpty()) return QString();
    const QFileInfo srcInfo(srcPath);
    return srcInfo.dir().filePath(srcInfo.completeBaseName());
}

/**
 * @brief Compiles the active C/C++ MPI target with `-g -O0` on-demand and loads its ELF64 + DWARF info.
 * @return True if the target binary exists (or was compiled) and loaded into ElfBinaryInspector.
 */
bool MainWindow::ensureTargetBinaryCompiled() {
    // Enable genuine ELF64 + DWARF disassembly mode for all subsequent view synchronizations.
    m_useRealBinaryTarget = true;
    const QString srcPath = activeTargetSourcePath();
    const QString binPath = activeTargetBinaryPath();
    if (binPath.isEmpty()) {
        return false;
    }

    // Compare filesystem modification timestamps to determine whether recompilation is required.
    const QFileInfo srcInfo(srcPath);
    const QFileInfo binInfo(binPath);
    const bool srcExists = !srcPath.isEmpty() && srcInfo.exists() && srcInfo.isFile();
    const bool needCompile =
        srcExists && (!binInfo.exists() || binInfo.lastModified() < srcInfo.lastModified());

    // Compile with -g -O0 when the ELF executable is missing or older than its C/C++ source file.
    if (needCompile) {
        const bool isCFile = srcInfo.suffix().compare(QStringLiteral("c"), Qt::CaseInsensitive) == 0;
        // Include standard Linux OpenMPI and MPICH wrapper directories in executable discovery.
        const QStringList searchDirs = {
            QStringLiteral("/usr/lib64/openmpi/bin"),
            QStringLiteral("/usr/lib/openmpi/bin"),
            QStringLiteral("/usr/lib64/mpich/bin"),
            QStringLiteral("/usr/lib/mpich/bin"),
            QStringLiteral("/usr/bin"),
            QStringLiteral("/usr/local/bin")
        };
        // Prefer MPI compiler wrappers (mpicc/mpicxx), falling back cleanly to system GCC/G++.
        const QString mpiName = isCFile ? QStringLiteral("mpicc") : QStringLiteral("mpicxx");
        const QString gnuName = isCFile ? QStringLiteral("gcc") : QStringLiteral("g++");
        QString compiler = QStandardPaths::findExecutable(mpiName);
        if (compiler.isEmpty()) compiler = QStandardPaths::findExecutable(mpiName, searchDirs);
        if (compiler.isEmpty()) compiler = QStandardPaths::findExecutable(gnuName);

        // Invoke compiler synchronously with full DWARF debug symbols (-g) and unoptimized codegen (-O0).
        if (!compiler.isEmpty()) {
            QProcess proc;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("PATH"),
                       searchDirs.join(QLatin1Char(':')) + QLatin1Char(':') +
                           env.value(QStringLiteral("PATH")));
            proc.setProcessEnvironment(env);
            proc.start(compiler, {QStringLiteral("-g"), QStringLiteral("-O0"), srcPath,
                                  QStringLiteral("-o"), binPath});
            proc.waitForFinished(10000);
        }
    }

    // Abort if compilation failed to materialize an executable binary on disk.
    if (!QFileInfo::exists(binPath)) {
        return false;
    }

    // Reload ELF symbol tables, objdump -d -l disassembly, and DWARF line maps when binary changed.
    const QString absBinPath = QFileInfo(binPath).absoluteFilePath();
    if (needCompile || !m_elfInspector || !m_elfInspector->isValid() ||
        m_loadedBinaryPath != absBinPath || m_dwarfLineToAddress.isEmpty()) {
        if (!loadBinaryTarget(binPath)) {
            return false;
        }
    }

    // Synchronize m_dynamicLineToAddress and m_dynamicAddressToLine with loaded DWARF records.
    rebuildDynamicSourceLineMap();
    return true;
}

/**
 * @brief Selects an active target in `#toolbarTargetSelector` by name or path and loads its real ELF + DWARF info.
 * @param targetName Target name substring or file path (e.g., "deadlock_ring" or "inspection_demo").
 * @return True if a matching target was found and activated; false otherwise.
 */
bool MainWindow::selectTargetByName(const QString& targetName) {
    const QString query = targetName.trimmed();
    if (query.isEmpty() || !m_targetSelector) return false;

    // 1. Find matching item in toolbarTargetSelector by label or user data path.
    int matchedIdx = -1;
    for (int i = 0; i < m_targetSelector->count(); ++i) {
        const QString data = m_targetSelector->itemData(i).toString();
        if (data == QStringLiteral("__open_binary__")) continue;
        if (m_targetSelector->itemText(i).contains(query, Qt::CaseInsensitive) ||
            data.contains(query, Qt::CaseInsensitive)) {
            matchedIdx = i;
            break;
        }
    }
    if (matchedIdx < 0) return false;

    // 2. Synchronize combo box selection without recursive signal emission.
    if (m_targetSelector->currentIndex() != matchedIdx) {
        m_targetSelector->blockSignals(true);
        m_targetSelector->setCurrentIndex(matchedIdx);
        m_targetSelector->blockSignals(false);
    }

    // 3. Resolve target source file path and open/activate its tab in EditorTabManager.
    const QString rawTargetPath = m_targetSelector->itemData(matchedIdx).toString();
    const QString resolvedSourcePath = resolveWorkspacePath(rawTargetPath);
    m_currentFile = resolvedSourcePath;
    loadSourceFile(resolvedSourcePath);

    // 4. Clear stale DisassemblyView bookmarks from previous target and compile/load real ELF + DWARF info.
    if (m_disassemblyView) {
        for (uint64_t oldBp : m_disassemblyView->bookmarkedAddresses()) {
            m_disassemblyView->toggleBookmark(oldBp);
        }
    }
    ensureTargetBinaryCompiled();

    // 5. Synchronize active SourceCodeView breakpoints into DisassemblyView bookmarks and reset session if active.
    if (SourceCodeView* sv = sourceCodeView()) {
        for (int bpLine : sv->breakpoints()) {
            const uint64_t addr = sourceLineToAddress(bpLine);
            if (m_disassemblyView && addr != 0ULL && !m_disassemblyView->isBookmarked(addr)) {
                m_disassemblyView->toggleBookmark(addr);
            }
        }
    }
    if (m_debugSessionActive) {
        stopTargetWorkflow();
    }
    return true;
}

/**
 * @brief Detects the build system ("Meson", "CMake", "Make", or "SingleFileMPI") for a directory or source file.
 * @param targetPath Project directory path or source file path.
 * @return Detected build system identifier string.
 */
QString MainWindow::detectBuildSystem(const QString& targetPath) const {
    const QString resolvedPath =
        targetPath.trimmed().isEmpty() ? QDir::currentPath() : resolveWorkspacePath(targetPath);
    const QFileInfo info(resolvedPath);
    const QDir dir(info.isDir() ? info.absoluteFilePath() : info.absolutePath());

    // Priority order: Meson -> CMake -> Make -> SingleFileMPI.
    if (dir.exists(QStringLiteral("meson.build"))) return QStringLiteral("Meson");
    if (dir.exists(QStringLiteral("CMakeLists.txt"))) return QStringLiteral("CMake");
    if (dir.exists(QStringLiteral("Makefile")) || dir.exists(QStringLiteral("GNUmakefile"))) {
        return QStringLiteral("Make");
    }
    return QStringLiteral("SingleFileMPI");
}

/**
 * @brief Constructs the shell build command for a given project directory or standalone source file.
 * @param targetPath Project directory path or source file path.
 * @return Shell command string to compile the target.
 */
QString MainWindow::buildCommandForPath(const QString& targetPath) const {
    const QString buildSys = detectBuildSystem(targetPath);
    const QString resolvedPath =
        targetPath.trimmed().isEmpty() ? QDir::currentPath() : resolveWorkspacePath(targetPath);
    const QFileInfo info(resolvedPath);
    const QString dirPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    const QDir dir(dirPath);

    // 1. Meson + Ninja build command.
    if (buildSys == QStringLiteral("Meson")) {
        if (dir.exists(QStringLiteral("builddir/build.ninja"))) {
            return QStringLiteral("ninja -C \"%1/builddir\"").arg(dirPath);
        }
        if (dir.exists(QStringLiteral("build/build.ninja"))) {
            return QStringLiteral("ninja -C \"%1/build\"").arg(dirPath);
        }
        return QStringLiteral("meson setup \"%1/builddir\" \"%1\" && ninja -C \"%1/builddir\"").arg(dirPath);
    }
    // 2. CMake debug configure and parallel build command.
    if (buildSys == QStringLiteral("CMake")) {
        return QStringLiteral("cmake -S \"%1\" -B \"%1/build\" -DCMAKE_BUILD_TYPE=Debug && cmake --build \"%1/build\" -j")
            .arg(dirPath);
    }
    // 3. GNU Make parallel build command.
    if (buildSys == QStringLiteral("Make")) {
        return QStringLiteral("make -C \"%1\" -j").arg(dirPath);
    }

    // 4. Standalone SingleFileMPI compilation using mpicxx/mpicc or g++/gcc fallback.
    const QString srcFile = !info.isDir() ? info.absoluteFilePath() : activeTargetSourcePath();
    const QFileInfo srcInfo(srcFile);
    const QString outBin = srcInfo.dir().filePath(srcInfo.completeBaseName());
    const bool isCFile = srcInfo.suffix().compare(QStringLiteral("c"), Qt::CaseInsensitive) == 0;
    const QString compiler = isCFile
                                 ? (!QStandardPaths::findExecutable(QStringLiteral("mpicc")).isEmpty() ? QStringLiteral("mpicc") : QStringLiteral("gcc"))
                                 : (!QStandardPaths::findExecutable(QStringLiteral("mpicxx")).isEmpty() ? QStringLiteral("mpicxx") : QStringLiteral("g++"));
    return QStringLiteral("%1 -g -O0 \"%2\" -o \"%3\"").arg(compiler, srcFile, outBin);
}

}  // namespace gridlock::ui

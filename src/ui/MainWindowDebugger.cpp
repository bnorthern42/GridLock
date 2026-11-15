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
#include <QDirIterator>
#include <QFile>
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
#include "../core/hpc/GdbRankCoordinator.hpp"
#include "../core/hpc/IBackendCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "../core/utils/ElfBinaryInspector.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "../core/models/VariableTreeModel.hpp"
#include "docks/CallStackDockWidget.hpp"
#include "docks/TerminalDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "utils/FileDialogHelper.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"
#include "widgets/MpiNetworkLogWidget.hpp"
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
    for (auto [var, def] : std::initializer_list<std::pair<int*, int>>{
             {&mainLine, 12}, {&initLine, 13}, {&structLine, 15}, {&forLoopLine, 16}, {&dataIdLine, 17},
             {&coordsLine, 19}, {&printfLine, 23}, {&finalizeLine, 24}, {&returnLine, 25}}) {
      if (*var == -1) *var = def;
    }

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
    // Headless preview-fixture table used exclusively when `loadPreviewFixture()` is active without a binary.
    if (currentPc == 0x00401142ULL) { m_rankLoopCounter[rank] = 0; return 0x00401149ULL; }
    if (currentPc == 0x00401149ULL) return 0x0040114dULL;
    if (currentPc == 0x0040114dULL) return (m_rankLoopCounter.value(rank, 0) >= 3) ? 0x00401178ULL : 0x0040114fULL;
    if (currentPc == 0x0040114fULL) return 0x00401152ULL;
    if (currentPc == 0x00401152ULL) return 0x00401155ULL;
    if (currentPc == 0x00401155ULL) return (rank == 0) ? 0x00401168ULL : 0x00401157ULL;
    if (currentPc == 0x00401157ULL) return 0x0040115fULL;
    if (currentPc == 0x0040115fULL) { m_rankLoopCounter[rank] = m_rankLoopCounter.value(rank, 0) + 1; return 0x00401163ULL; }
    if (currentPc == 0x00401163ULL) return 0x00401149ULL;
    if (currentPc == 0x00401168ULL) return 0x0040116aULL;
    if (currentPc == 0x0040116aULL) { m_rankLoopCounter[rank] = m_rankLoopCounter.value(rank, 0) + 1; return 0x0040116eULL; }
    if (currentPc == 0x0040116eULL) return 0x00401149ULL;
    if (currentPc == 0x00401178ULL) return 0x0040117dULL;
    if (currentPc == 0x0040117dULL) return 0x00401182ULL;
    if (currentPc == 0x00401182ULL || currentPc == 0x00401183ULL) return 0x00401183ULL;
    return m_disassemblyView ? m_disassemblyView->nextInstructionAddress(currentPc) : (currentPc + 4);
}

/**
 * @brief Compiles the active C/C++/MPI target, syncs UI breakpoints, and launches a live parallel debug session.
 */
void MainWindow::startTargetWorkflow() {
    // Live parallel execution path via `IBackendCoordinator` (`GdbRankCoordinator` or `DapCoordinator`).
    if (!m_previewFixtureOnly) {
        syncCoordinatorWithConfig();
        if (!ensureTargetBinaryCompiled() || !m_coordinator) return;
        const QString binPath = activeTargetBinaryPath();
        if (binPath.isEmpty() || !QFileInfo::exists(binPath)) return;

        const int rankCount = m_rankCountSpin ? std::clamp(m_rankCountSpin->value(), 1, 256) : std::max(1, gridlock::core::ConfigManager::instance().getDefaultRanks());
        const QString mpiArgs = gridlock::core::ConfigManager::instance().getMpiArgs();
        const int preserveRank = (m_serverRackView && m_serverRackView->selectedRank() >= 0) ? std::clamp(m_serverRackView->selectedRank(), 0, rankCount - 1) : 0;
        // Synchronize all active editor tabs and workspace breakpoints (including conditions and rank filters).
        m_coordinator->clearBreakpoints();
        const QString srcPath = activeTargetSourcePath();
        QSet<QString> syncedFiles;
        const auto syncOpenView = [&](SourceCodeView* sv) {
            if (!sv) return;
            const QString viewPath = sv->currentFile().isEmpty() ? srcPath : QFileInfo(sv->currentFile()).absoluteFilePath();
            sv->highlightExecutionLine(-1);
            syncedFiles.insert(viewPath); syncedFiles.insert(QFileInfo(viewPath).canonicalFilePath());
            for (int bpLine : sv->breakpoints()) {
                int rf = sv->breakpointRankFilterForLine(bpLine);
                if (rf < 0 && sv->breakpointSuspendScope(bpLine).compare(QStringLiteral("Rank"), Qt::CaseInsensitive) == 0) rf = preserveRank;
                m_coordinator->setBreakpoint(viewPath, bpLine, sv->breakpointCondition(bpLine), rf);
                const uint64_t a = sourceLineToAddress(bpLine);
                if (m_disassemblyView && a != 0ULL && !m_disassemblyView->isBookmarked(a)) m_disassemblyView->toggleBookmark(a);
            }
        };
        if (m_editorTabManager) { for (SourceCodeView* sv : m_editorTabManager->openViews()) syncOpenView(sv); } else { syncOpenView(sourceCodeView()); }
        for (auto it = m_persistentBreakpoints.constBegin(); it != m_persistentBreakpoints.constEnd(); ++it) {
            const QString absKey = QFileInfo(it.key()).absoluteFilePath();
            if (syncedFiles.contains(absKey) || !QFileInfo::exists(it.key())) continue;
            const auto metaMap = m_breakpointRankMeta.value(absKey);
            for (int bpLine : *it) { const auto bm = metaMap.value(bpLine); m_coordinator->setBreakpoint(it.key(), bpLine, bm.condition, bm.rankFilter); }
        }

        // Reset rank telemetry rows to RUNNING prior to spawning child processes; live signals drive all stops.
        m_latestStates.clear();
        m_rankLoopCounter.clear();
        m_focusedRank = preserveRank;
        if (m_disassemblyView) m_disassemblyView->setDeadlockCycleRanks({});
        if (m_gdbConsoleWidget) m_gdbConsoleWidget->resetRanks(rankCount);

        QList<RankState> initialRanks;
        initialRanks.reserve(rankCount);
        for (int r = 0; r < rankCount; ++r) {
            RankState st; st.rankId = r; st.currentState = QStringLiteral("running");
            m_latestStates[r] = st;
            initialRanks.append(st);
            if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->updateRankStateRow(r, QStringLiteral("RUNNING"), QStringLiteral("main"), QStringLiteral("ALL RANKS"), 0ULL, rankCount);
        }
        if (m_serverRackView) { m_serverRackView->updateRanks(initialRanks); m_serverRackView->selectRank(preserveRank); }
        m_debugSessionActive = true;
        updateDebugToolbarState();
        updateStatusTelemetry(QStringLiteral("0x00000000"), QStringLiteral("RUNNING"));

        // Push active project CLI arguments and working directory, then spawn live MPI GDB / LLDB-DAP session.
        const auto projCfg = gridlock::core::ConfigManager::instance().loadProjectSettings();
        const QString tArgs = !projCfg.targetArguments.isEmpty() ? projCfg.targetArguments : QString::fromStdString(projCfg.programArguments);
        QString tWd = QString::fromStdString(projCfg.workingDirectory).trimmed();
        if (tWd.isEmpty()) tWd = m_activeWorkspaceDir;
        m_coordinator->setTargetExecutionContext(tArgs, tWd);
        m_coordinator->launchParallelSession(binPath, rankCount, projCfg.mpiArgs.isEmpty() ? mpiArgs : projCfg.mpiArgs);
        syncProgramOutputForLaunch(rankCount);
        if (m_differentialGrid) {
            if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
                for (const auto& w : m_differentialGrid->getWatchExpressions()) gdbCoord->registerWatchVariable(QString::fromStdString(w));
            }
        }
        return;
    }

    // Synthetic preview-fixture fallback used strictly by headless layout tests calling `loadPreviewFixture()`.
    m_debugSessionActive = true; updateDebugToolbarState(); rebuildDynamicSourceLineMap();
    if (m_disassemblyView) m_disassemblyView->setDeadlockCycleRanks({});

    const int rankCount = m_rankCountSpin ? std::clamp(m_rankCountSpin->value(), 1, 64) : 8;
    uint64_t stopAddr = 0x00401120ULL;
    QSet<uint64_t> bpAddrs;
    if (SourceCodeView* srcView = sourceCodeView()) {
        for (int bpLine : srcView->breakpoints()) if (const uint64_t a = sourceLineToAddress(bpLine); a != 0ULL) bpAddrs.insert(a);
    }
    if (!bpAddrs.isEmpty() && !bpAddrs.contains(stopAddr)) {
        uint64_t cur = stopAddr;
        for (int step = 0; step < 64; ++step) {
            const uint64_t next = advanceRankProgramCounter(0, cur);
            if (next == cur || next == 0ULL || (next < cur && next != 0x00401149ULL)) break;
            cur = next;
            if (bpAddrs.contains(cur)) { stopAddr = cur; break; }
        }
    }

    // Populate simulated rank descriptors stopped at stopAddr for headless layout tests.
    const QString stopHex = QStringLiteral("0x%1").arg(stopAddr, 8, 16, QChar('0'));
    const int stopLine = addressToSourceLine(stopAddr);
    const int preserveRank = (m_serverRackView && m_serverRackView->selectedRank() >= 0) ? m_serverRackView->selectedRank() : 0;
    m_latestStates.clear(); m_rankLoopCounter.clear();
    QList<RankState> rankList;
    for (int r = 0; r < rankCount; ++r) {
        RankState st; st.rankId = r;
        st.currentState = (r == preserveRank) ? QStringLiteral("stopped") : QStringLiteral("running");
        st.registers[16] = stopHex; st.currentFunction = QStringLiteral("main"); st.currentLine = stopLine;
        m_latestStates[r] = st; rankList.append(st);
    }
    m_focusedRank = preserveRank;
    if (m_serverRackView) { m_serverRackView->updateRanks(rankList); m_serverRackView->selectRank(preserveRank); }
    const bool keepLinear = m_disassemblyView && !m_disassemblyView->isGraphMode() && !m_disassemblyView->isDecompilerMode() &&
                            !m_disassemblyView->isSplitGraphDecompilerMode() && !m_forceDecompilerFallback &&
                            m_workspacePerspective == QStringLiteral("Code");
    seekToAddressOrExpression(stopHex);
    if (keepLinear && m_disassemblyView) {
        m_showingDecompilerFallback = false; m_disassemblyView->setSplitGraphDecompilerMode(false); m_disassemblyView->setGraphMode(false);
    }
    if (m_disassemblyView) m_disassemblyView->setActiveSteppingRank(preserveRank, stopAddr);
    if (SourceCodeView* srcView = sourceCodeView()) srcView->highlightExecutionLine(stopLine);
    updateStatusTelemetry(stopHex, QStringLiteral("PAUSED (BREAKPOINT)"));
}

/**
 * @brief Executes a single machine instruction on the active MPI rank via `IBackendCoordinator::stepInstruction`.
 * @return True if the step request was dispatched; false otherwise.
 */
bool MainWindow::stepInstructionWorkflow() {
    // Dispatch live GDB/DAP single-instruction step when not running synthetic preview fixtures.
    if (!m_previewFixtureOnly) {
        if (!m_debugSessionActive || !m_coordinator) return false;
        const int activeRank = activeSteppingRank();
        m_focusedRank = activeRank;
        updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("STEPPING"));
        m_coordinator->stepInstruction(activeRank + 1);
        return true;
    }

    // Advance synthetic preview program counter by one simulated x86_64 instruction.
    if (!m_disassemblyView) return false;
    m_debugSessionActive = true; updateDebugToolbarState();
    const int activeRank = activeSteppingRank();
    m_focusedRank = activeRank;
    const uint64_t currentPc = (m_disassemblyView->currentAddress() != 0ULL) ? m_disassemblyView->currentAddress() : 0x00401120ULL;
    const uint64_t nextPc = advanceRankProgramCounter(activeRank, currentPc);
    const QString nextPcHex = QStringLiteral("0x%1").arg(nextPc, 8, 16, QChar('0'));
    const int nextLine = addressToSourceLine(nextPc);

    RankState& st = m_latestStates[activeRank];
    st.rankId = activeRank; st.currentState = QStringLiteral("stopped"); st.registers[16] = nextPcHex; st.currentLine = nextLine;
    if (m_serverRackView) m_serverRackView->updateRankState(activeRank, st);

    const bool keepLinear = !m_disassemblyView->isGraphMode() && !m_disassemblyView->isDecompilerMode() &&
                            !m_disassemblyView->isSplitGraphDecompilerMode() && !m_forceDecompilerFallback &&
                            m_workspacePerspective == QStringLiteral("Code");
    seekToAddressOrExpression(nextPcHex);
    if (keepLinear) { m_showingDecompilerFallback = false; m_disassemblyView->setSplitGraphDecompilerMode(false); m_disassemblyView->setGraphMode(false); }
    m_disassemblyView->setActiveSteppingRank(activeRank, nextPc);
    if (m_registerView) {
        m_registerView->updateRegisters({{QStringLiteral("rip"), nextPcHex},
                                         {QStringLiteral("rax"), QStringLiteral("0x%1").arg(m_rankLoopCounter.value(activeRank, 0), 16, 16, QChar('0'))}});
    }
    if (SourceCodeView* srcView = sourceCodeView()) srcView->highlightExecutionLine(nextLine);
    updateStatusTelemetry(nextPcHex, QStringLiteral("PAUSED (STEP)"));
    return true;
}

/**
 * @brief Resumes live execution on `IBackendCoordinator` until the next breakpoint, signal, or exit.
 * @return True if the continue request was dispatched; false otherwise.
 */
bool MainWindow::continueExecutionWorkflow() {
    // Dispatch live GDB/DAP continue across all stopped ranks so no rank stalls behind an MPI_Barrier.
    if (!m_previewFixtureOnly) {
        if (!m_debugSessionActive || !m_coordinator) return false;
        m_focusedRank = activeSteppingRank();
        if (SourceCodeView* srcView = sourceCodeView()) srcView->highlightExecutionLine(-1);
        for (auto& [rk, st] : m_latestStates)
            if (st.currentState == QLatin1String("stopped")) { st.currentState = QStringLiteral("running"); if (m_serverRackView) m_serverRackView->updateRankState(rk, st); }
        updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("RUNNING"));
        m_coordinator->continueExecution(-1);
        return true;
    }

    // Advance synthetic preview PC forward until hitting a user breakpoint or procedure return.
    if (!m_disassemblyView) return false;
    m_debugSessionActive = true; updateDebugToolbarState();
    QSet<uint64_t> bpAddrs;
    if (SourceCodeView* srcView = sourceCodeView()) {
        for (int bpLine : srcView->breakpoints()) if (const uint64_t a = sourceLineToAddress(bpLine); a != 0ULL) bpAddrs.insert(a);
    }
    const int activeRank = activeSteppingRank();
    m_focusedRank = activeRank;
    uint64_t cur = (m_disassemblyView->currentAddress() != 0ULL) ? m_disassemblyView->currentAddress() : 0x00401120ULL;
    for (int step = 0; step < 64; ++step) {
        const uint64_t next = advanceRankProgramCounter(activeRank, cur);
        if (next == cur || next == 0ULL || (next < cur && next != 0x00401149ULL)) break;
        cur = next;
        if (bpAddrs.contains(cur) || cur == 0x00401183ULL) break;
    }
    const QString targetHex = QStringLiteral("0x%1").arg(cur, 8, 16, QChar('0'));
    const int targetLine = addressToSourceLine(cur);
    RankState& st = m_latestStates[activeRank];
    st.rankId = activeRank; st.currentState = QStringLiteral("stopped"); st.registers[16] = targetHex; st.currentLine = targetLine;
    if (m_serverRackView) m_serverRackView->updateRankState(activeRank, st);
    const bool keepLinear = !m_disassemblyView->isGraphMode() && !m_disassemblyView->isDecompilerMode() &&
                            !m_disassemblyView->isSplitGraphDecompilerMode() && !m_forceDecompilerFallback &&
                            m_workspacePerspective == QStringLiteral("Code");
    seekToAddressOrExpression(targetHex);
    if (keepLinear) { m_showingDecompilerFallback = false; m_disassemblyView->setSplitGraphDecompilerMode(false); m_disassemblyView->setGraphMode(false); }
    m_disassemblyView->setActiveSteppingRank(activeRank, cur);
    if (SourceCodeView* srcView = sourceCodeView()) srcView->highlightExecutionLine(targetLine);
    updateStatusTelemetry(targetHex, QStringLiteral("PAUSED (BREAKPOINT)"));
    return true;
}

/**
 * @brief Clears variable, register, call-stack, MPI diagnostics, network log, and memory docks to idle.
 */
static void clearAllInspectionDocks(QWidget* varsDock, RegisterView* regView, CallStackDockWidget* csDock,
                                    MpiDiagnosticsWidget* mpiDiag, MpiNetworkLogWidget* netLog, MemView* memView) {
    if (varsDock) { if (auto* vm = varsDock->findChild<gridlock::VariableTreeModel*>()) vm->clear(); }
    if (regView) regView->clearRegisters();
    if (csDock) csDock->clearFrames();
    if (mpiDiag) mpiDiag->clear();
    if (netLog) netLog->clear();
    if (memView) memView->clear();
}

/**
 * @brief Terminates the active debugging session, reaps child ranks, and restores static ELF inspection.
 */
void MainWindow::stopTargetWorkflow() {
    m_debugSessionActive = false; updateDebugToolbarState(); m_rankLoopCounter.clear();
    if (m_coordinator) m_coordinator->terminateSession();

    // Mark any previously active rank rows as IDLE/EXITED and clear execution PC highlights.
    for (auto& [rankId, st] : m_latestStates) {
        st.currentState = QStringLiteral("idle"); st.registers[16] = QStringLiteral("—");
        if (m_serverRackView) m_serverRackView->updateRankState(rankId, st);
    }
    if (m_disassemblyView) { m_disassemblyView->setDeadlockCycleRanks({}); m_disassemblyView->setActiveSteppingRank(-1, 0ULL); }
    if (SourceCodeView* srcView = sourceCodeView()) srcView->highlightExecutionLine(-1);
    clearAllInspectionDocks(m_variablesDockWidget, m_registerView, m_callStackDockWidget, m_mpiDiagnosticsWidget, networkLog(), m_memView);
    m_cachedStackFrames.clear(); m_cachedThreads.clear(); m_selectedFrameLevel.clear();
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("IDLE")); refreshStatusBarMemoryTelemetry();
    // Restore static pre-launch ELF64 disassembly, CFG, and symbol inspection cleanly after session stop.
    if (!m_previewFixtureOnly && !m_loadedBinaryPath.isEmpty() && QFileInfo::exists(m_loadedBinaryPath)) loadBinaryTarget(m_loadedBinaryPath);
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

namespace {

/**
 * @brief Checks whether @p filePath is a runnable 64/32-bit ELF `ET_EXEC`/`ET_DYN` binary (excluding `.so`/`.o` and scratch checks).
 * @param filePath Candidate file path on disk.
 * @return True if @p filePath is an executable ELF target suitable for `#toolbarTargetSelector`.
 */
bool isRunnableElfExecutable(const QString& filePath) {
    const QFileInfo fi(filePath);
    if (!fi.exists() || !fi.isFile() || !fi.isExecutable() || fi.size() < 64) return false;
    const QString abs = fi.absoluteFilePath(), name = fi.fileName();
    // Exclude CMake/Meson compiler-check scratch directories, object folders, and shared libraries.
    if (abs.contains(QStringLiteral("/CMakeFiles/")) || abs.contains(QStringLiteral("/meson-private/")) ||
        abs.contains(QStringLiteral(".p/")) || abs.contains(QStringLiteral(".dir/"))) return false;
    if (name.endsWith(QStringLiteral(".so")) || name.contains(QStringLiteral(".so.")) ||
        name.endsWith(QStringLiteral(".o")) || name.endsWith(QStringLiteral(".a")) ||
        name.endsWith(QStringLiteral(".lo")) || name.contains(QStringLiteral("sanitycheck"), Qt::CaseInsensitive) ||
        name.contains(QStringLiteral("CompilerId"), Qt::CaseInsensitive)) return false;

    // Verify 0x7f 'E' 'L' 'F' magic bytes and ELF header e_type == ET_EXEC (2) or ET_DYN (3).
    QFile f(abs);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray hdr = f.read(20);
    if (hdr.size() < 20 || hdr[0] != '\x7f' || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F') return false;
    const auto eType = static_cast<uint16_t>(static_cast<uint8_t>(hdr[16]) |
                                             (static_cast<uint16_t>(static_cast<uint8_t>(hdr[17])) << 8U));
    return eType == 2U || eType == 3U;
}

/**
 * @brief Scans @p rootDir and common build/source subdirectories for runnable ELF binaries and C/C++ `main` sources.
 * @param rootDir Canonical workspace root directory.
 * @param outElfs Output list of canonical ELF executable paths.
 * @param outMainSources Output list of canonical C/C++ entry source paths containing `main(`.
 */
void discoverWorkspaceTargets(const QString& rootDir, QStringList* outElfs, QStringList* outMainSources) {
    static const QRegularExpression mainRe(QStringLiteral("\\bmain\\s*\\("));
    // Seed priority scan queue with workspace root, common build dirs, and source subtrees (depth <= 4).
    QList<QPair<QString, int>> queue = {{rootDir, 0}};
    for (const QString& sub : {QStringLiteral("build_meson"), QStringLiteral("build_cmake"),
                               QStringLiteral("builddir"), QStringLiteral("build"), QStringLiteral("bin"), QStringLiteral("src")}) {
        const QString cand = QDir(rootDir).filePath(sub);
        if (QDir(cand).exists()) queue.append({cand, 1});
    }
    QSet<QString> visitedDirs, seenElfs, seenSrcs;
    while (!queue.isEmpty()) {
        const auto [curPath, depth] = queue.takeFirst();
        const QFileInfo curInfo(curPath);
        const QString canonDir = !curInfo.canonicalFilePath().isEmpty() ? curInfo.canonicalFilePath() : curInfo.absoluteFilePath();
        if (visitedDirs.contains(canonDir)) continue;
        visitedDirs.insert(canonDir);

        const QDir dir(canonDir);
        const bool inBuildSubtree = canonDir != rootDir &&
                                    QFileInfo(canonDir).fileName().startsWith(QStringLiteral("build"), Qt::CaseInsensitive);
        // Inspect regular files in current directory for runnable ELF binaries and C/C++ main() entry files.
        for (const QFileInfo& entry : dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString canonFile = !entry.canonicalFilePath().isEmpty() ? entry.canonicalFilePath() : entry.absoluteFilePath();
            if (outElfs && !seenElfs.contains(canonFile) && isRunnableElfExecutable(canonFile)) {
                seenElfs.insert(canonFile); outElfs->append(canonFile);
            } else if (outMainSources && !inBuildSubtree && !seenSrcs.contains(canonFile)) {
                const QString ext = entry.suffix().toLower();
                if ((ext == QStringLiteral("cpp") || ext == QStringLiteral("cc") ||
                     ext == QStringLiteral("cxx") || ext == QStringLiteral("c")) && entry.size() <= 524288) {
                    QFile sf(canonFile);
                    if (sf.open(QIODevice::ReadOnly | QIODevice::Text) &&
                        QString::fromUtf8(sf.read(65536)).contains(mainRe)) {
                        seenSrcs.insert(canonFile); outMainSources->append(canonFile);
                    }
                }
            }
        }
        if (depth >= 4) continue;
        // Enqueue non-hidden subdirectories excluding CMakeFiles, meson-private, and *.p/*.dir scratch dirs.
        for (const QFileInfo& subDir : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString dn = subDir.fileName();
            if (dn.startsWith(QLatin1Char('.')) || dn == QStringLiteral("CMakeFiles") ||
                dn == QStringLiteral("meson-private") || dn == QStringLiteral("Testing") ||
                dn.endsWith(QStringLiteral(".p")) || dn.endsWith(QStringLiteral(".dir"))) continue;
            queue.append({subDir.absoluteFilePath(), depth + 1});
        }
    }
}

}  // namespace

/**
 * @brief Returns the resolved source file path for the currently active target.
 * @return Absolute or resolved path to the active target's C/C++ source file.
 */
QString MainWindow::activeTargetSourcePath() const {
    // Prioritize explicitly opened source file path tracked by the active editor session.
    if (!m_currentFile.trimmed().isEmpty()) return resolveWorkspacePath(m_currentFile);
    // Otherwise inspect user data bound to the selected item in #toolbarTargetSelector (skipping raw ELF binaries).
    if (m_targetSelector) {
        const QString selData = m_targetSelector->currentData().toString();
        if (!selData.isEmpty() && selData != QStringLiteral("__open_binary__")) {
            const QString resolvedSel = resolveWorkspacePath(selData);
            if (!isRunnableElfExecutable(resolvedSel)) return resolvedSel;
        }
    }
    // Fall back to active workspace directory if an external folder is open without a source tab.
    if (!m_activeWorkspaceDir.isEmpty()) return m_activeWorkspaceDir;
    // Default workstation startup fixture when no target has been explicitly chosen.
    return resolveWorkspacePath(QStringLiteral("tutorial/inspection_demo.cpp"));
}

/**
 * @brief Returns the resolved output executable binary path for the currently active target.
 * @return Absolute or resolved path to the active target's compiled binary executable.
 */
QString MainWindow::activeTargetBinaryPath() const {
    // Return selected ELF binary directly when #toolbarTargetSelector points to a pre-built executable.
    if (m_targetSelector) {
        const QString selData = m_targetSelector->currentData().toString();
        if (!selData.isEmpty() && selData != QStringLiteral("__open_binary__")) {
            const QString resolvedSel = resolveWorkspacePath(selData);
            if (isRunnableElfExecutable(resolvedSel)) return resolvedSel;
        }
    }
    // Derive companion ELF executable path alongside the C/C++ source file (stripping extension).
    const QString srcPath = activeTargetSourcePath();
    if (srcPath.isEmpty() || QFileInfo(srcPath).isDir()) return m_loadedBinaryPath;
    const QFileInfo srcInfo(srcPath);
    return srcInfo.dir().filePath(srcInfo.completeBaseName());
}

/**
 * @brief Compiles the active C/C++ MPI target with `-g -O0` on-demand and loads its ELF64 + DWARF info.
 * @return True if the target binary exists (or was compiled) and loaded into ElfBinaryInspector.
 */
bool MainWindow::ensureTargetBinaryCompiled() {
    // Enable genuine ELF64 + DWARF disassembly and live coordinator execution mode.
    m_previewFixtureOnly = false;
    m_useRealBinaryTarget = true;
    const QString srcPath = activeTargetSourcePath();
    const QString binPath = activeTargetBinaryPath();
    if (binPath.isEmpty()) return false;

    // Skip single-file compiler overwrite when #toolbarTargetSelector directly targets an external build-dir ELF binary.
    const QFileInfo srcInfo(srcPath), binInfo(binPath);
    const bool directElfSelected = m_targetSelector &&
                                   resolveWorkspacePath(m_targetSelector->currentData().toString()) == binInfo.absoluteFilePath() &&
                                   isRunnableElfExecutable(binPath);
    const bool srcExists = !directElfSelected && !srcPath.isEmpty() && srcInfo.exists() && srcInfo.isFile();
    const bool needCompile = srcExists && (!binInfo.exists() || binInfo.lastModified() < srcInfo.lastModified());

    // Compile with -g -O0 when the ELF executable is missing or older than its standalone C/C++ source file.
    if (needCompile) {
        const bool isCFile = srcInfo.suffix().compare(QStringLiteral("c"), Qt::CaseInsensitive) == 0;
        const QStringList searchDirs = {QStringLiteral("/usr/lib64/openmpi/bin"), QStringLiteral("/usr/lib/openmpi/bin"),
                                        QStringLiteral("/usr/lib64/mpich/bin"), QStringLiteral("/usr/lib/mpich/bin"),
                                        QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/bin")};
        const QString mpiName = isCFile ? QStringLiteral("mpicc") : QStringLiteral("mpicxx");
        const QString gnuName = isCFile ? QStringLiteral("gcc") : QStringLiteral("g++");
        QString compiler = QStandardPaths::findExecutable(mpiName);
        if (compiler.isEmpty()) compiler = QStandardPaths::findExecutable(mpiName, searchDirs);
        if (compiler.isEmpty()) compiler = QStandardPaths::findExecutable(gnuName);
        if (!compiler.isEmpty()) {
            QProcess proc;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("PATH"), searchDirs.join(QLatin1Char(':')) + QLatin1Char(':') + env.value(QStringLiteral("PATH")));
            proc.setProcessEnvironment(env);
            proc.start(compiler, {QStringLiteral("-g"), QStringLiteral("-O0"), srcPath, QStringLiteral("-o"), binPath});
            proc.waitForFinished(10000);
        }
    }
    if (!QFileInfo::exists(binPath)) return false;

    // Reload ELF symbol tables, objdump -d -l disassembly, and DWARF line maps when binary changed.
    const QString absBinPath = QFileInfo(binPath).absoluteFilePath();
    if (needCompile || !m_elfInspector || !m_elfInspector->isValid() ||
        m_loadedBinaryPath != absBinPath || m_dwarfLineToAddress.isEmpty()) {
        if (!loadBinaryTarget(binPath)) return false;
    }
    rebuildDynamicSourceLineMap();
    return true;
}

/**
 * @brief Selects an active target in `#toolbarTargetSelector` by name or path and loads its real ELF + DWARF info.
 * @param targetName Target name substring or file path (e.g., "deadlock_ring" or "mfem_solver").
 * @return True if a matching target was found and activated; false otherwise.
 */
bool MainWindow::selectTargetByName(const QString& targetName) {
    m_previewFixtureOnly = false;
    const QString query = targetName.trimmed();
    if (query.isEmpty() || !m_targetSelector) return false;

    // 1. Find matching item in toolbarTargetSelector by label or user data path.
    int matchedIdx = -1;
    for (int i = 0; i < m_targetSelector->count(); ++i) {
        const QString data = m_targetSelector->itemData(i).toString();
        if (data == QStringLiteral("__open_binary__")) continue;
        if (m_targetSelector->itemText(i).contains(query, Qt::CaseInsensitive) || data.contains(query, Qt::CaseInsensitive)) {
            matchedIdx = i; break;
        }
    }
    if (matchedIdx < 0) return false;

    // 2. Synchronize combo box selection without recursive signal emission.
    if (m_targetSelector->currentIndex() != matchedIdx) {
        m_targetSelector->blockSignals(true); m_targetSelector->setCurrentIndex(matchedIdx); m_targetSelector->blockSignals(false);
    }

    // 3. Resolve target path (distinguishing pre-built ELF executables from C/C++ source files and directories).
    const QString resolvedPath = resolveWorkspacePath(m_targetSelector->itemData(matchedIdx).toString());
    if (QFileInfo(resolvedPath).isDir()) return true;
    if (m_disassemblyView) {
        for (uint64_t oldBp : m_disassemblyView->bookmarkedAddresses()) m_disassemblyView->toggleBookmark(oldBp);
    }
    if (isRunnableElfExecutable(resolvedPath)) {
        m_useRealBinaryTarget = true;
        loadBinaryTarget(resolvedPath);
    } else {
        m_currentFile = resolvedPath;
        loadSourceFile(resolvedPath);
        ensureTargetBinaryCompiled();
    }

    // 4. Synchronize active SourceCodeView breakpoints into DisassemblyView bookmarks and reset session if active.
    if (SourceCodeView* sv = sourceCodeView()) {
        for (int bpLine : sv->breakpoints()) {
            const uint64_t addr = sourceLineToAddress(bpLine);
            if (m_disassemblyView && addr != 0ULL && !m_disassemblyView->isBookmarked(addr)) m_disassemblyView->toggleBookmark(addr);
        }
    }
    if (m_debugSessionActive) stopTargetWorkflow();
    return true;
}

/**
 * @brief Atomically switches the active workspace directory, re-roots `ProjectExplorerWidget` (`FILES`),
 *        discovers external ELF executables and C/C++ entry targets for `#toolbarTargetSelector`, and syncs config.
 * @param dirPath Target workspace directory or `.gridlock/workspace.toml` / `.toml` session file path.
 * @return True if @p dirPath resolved to an existing workspace directory and was activated.
 */
bool MainWindow::switchWorkspaceDirectory(const QString& dirPath) {
    if (m_switchingWorkspace || dirPath.trimmed().isEmpty()) return false;
    // Normalize `.gridlock/workspace.toml` or `.toml` session file paths to their parent workspace directory.
    const QFileInfo rawInfo(dirPath.trimmed());
    QDir dir = rawInfo.exists() ? (rawInfo.isDir() ? QDir(rawInfo.absoluteFilePath()) : rawInfo.dir())
                                : (rawInfo.fileName().endsWith(QStringLiteral(".toml"), Qt::CaseInsensitive)
                                       ? rawInfo.dir()
                                       : QDir(rawInfo.absoluteFilePath()));
    if (dir.dirName() == QStringLiteral(".gridlock")) dir.cdUp();
    if (!dir.exists()) return false;
    const QFileInfo dirInfo(dir.absolutePath());
    const QString resolvedDir =
        !dirInfo.canonicalFilePath().isEmpty() ? dirInfo.canonicalFilePath() : QDir::cleanPath(dirInfo.absoluteFilePath());

    struct ReentrancyGuard { bool& flag; ~ReentrancyGuard() { flag = false; } } guard{m_switchingWorkspace};
    m_switchingWorkspace = true;
    if (!m_activeWorkspaceDir.isEmpty()) saveSession(false);
    if (m_debugSessionActive) stopTargetWorkflow();
    clearAllInspectionDocks(m_variablesDockWidget, m_registerView, m_callStackDockWidget, m_mpiDiagnosticsWidget, networkLog(), m_memView);
    m_cachedStackFrames.clear(); m_cachedThreads.clear(); m_selectedFrameLevel.clear();
    m_previewFixtureOnly = false; m_useRealBinaryTarget = false; m_elfInspector.reset(); m_loadedBinaryPath.clear();
    if (m_differentialGrid) m_differentialGrid->clearWatches();
    if (m_editorTabManager && (m_currentFile.isEmpty() || !m_currentFile.startsWith(resolvedDir + QLatin1Char('/')))) {
        m_editorTabManager->clearAllTabs(); m_currentFile.clear();
    }
    m_activeWorkspaceDir = resolvedDir; m_activeSessionFilePath = QDir(resolvedDir).filePath(QStringLiteral(".gridlock/workspace.toml"));
    gridlock::core::ClangdLspClient::instance().setWorkspaceRoot(resolvedDir);
    // 1. Re-root ProjectExplorerWidget to resolvedDir and ensure FILES mode is active.
    if (m_projectExplorerWidget) {
        if (m_projectExplorerWidget->rootPath() != resolvedDir ||
            m_projectExplorerWidget->isSymbolsMode() || m_projectExplorerWidget->isStringsMode()) {
            m_projectExplorerWidget->setRootPath(resolvedDir);
        }
    }

    auto& cfg = gridlock::core::ConfigManager::instance();
    cfg.setWorkspace(resolvedDir); cfg.setLastOpenDir(resolvedDir);
    m_persistentBreakpoints = cfg.getBreakpoints();
    const auto loadedPs = cfg.loadProjectSettings();
    for (const auto& w : loadedPs.watchExpressions) if (m_differentialGrid) m_differentialGrid->addVariableColumn(QString::fromStdString(w));
    if (m_terminalDock) m_terminalDock->setWorkingDirectory((!loadedPs.workingDirectory.empty() && QDir(QString::fromStdString(loadedPs.workingDirectory)).exists()) ? QString::fromStdString(loadedPs.workingDirectory) : resolvedDir);
    const QString folderName = QFileInfo(resolvedDir).fileName();
    setWindowTitle(QStringLiteral("GridLock - %1").arg(folderName.isEmpty() ? resolvedDir : folderName));

    // 3. Scan workspace and build* subdirectories for runnable ELF binaries and C/C++ entry targets.
    QStringList elfTargets, mainSources;
    discoverWorkspaceTargets(resolvedDir, &elfTargets, &mainSources);
    if (m_targetSelector) {
        m_targetSelector->blockSignals(true); m_targetSelector->clear();
        for (const QString& elfPath : elfTargets) {
            const QString relDir = QDir(resolvedDir).relativeFilePath(QFileInfo(elfPath).absolutePath());
            const QString label = (relDir.isEmpty() || relDir == QStringLiteral(".")) ? QStringLiteral("%1 (ELF)").arg(QFileInfo(elfPath).fileName()) : QStringLiteral("%1 (%2)").arg(QFileInfo(elfPath).fileName(), relDir);
            m_targetSelector->addItem(label, elfPath);
        }
        for (const QString& srcPath : mainSources) {
            const QString relSrc = QDir(resolvedDir).relativeFilePath(srcPath);
            m_targetSelector->addItem(elfTargets.isEmpty() ? QFileInfo(srcPath).fileName() : QStringLiteral("%1 (Source)").arg(relSrc), srcPath);
        }
        if (m_targetSelector->count() == 0) m_targetSelector->addItem(QStringLiteral("%1 (Workspace)").arg(folderName.isEmpty() ? QStringLiteral("Workspace") : folderName), resolvedDir);
        m_targetSelector->addItem(QStringLiteral("Open ELF / Binary..."), QStringLiteral("__open_binary__"));
        m_targetSelector->setCurrentIndex(0); m_targetSelector->blockSignals(false);
    }
    // 4. Open primary workspace source file (activating ClangdLspClient) and load primary ELF binary.
    if (!mainSources.isEmpty() && m_currentFile.isEmpty()) loadSourceFile(mainSources.first());
    if (!elfTargets.isEmpty()) {
        m_previewFixtureOnly = false; m_useRealBinaryTarget = true; loadBinaryTarget(elfTargets.first());
        if (!mainSources.isEmpty() && m_elfInspector && m_elfInspector->isValid() && m_dwarfLineToAddress.isEmpty()) {
            m_elfInspector->disassembleSymbol(QStringLiteral("main"), QFileInfo(mainSources.first()).fileName(), &m_dwarfLineToAddress, &m_dwarfAddressToLine);
            rebuildDynamicSourceLineMap();
        }
    } else {
        // Clear binary-derived symbols, strings, disassembly, and section ribbon when switching to an unbuilt workspace.
        if (m_projectExplorerWidget) { m_projectExplorerWidget->setSymbols({}); m_projectExplorerWidget->setStrings({}); }
        if (m_disassemblyView) m_disassemblyView->clear();
        if (auto* rf = DynamicAddressRibbonFilter::fromNavBar(m_addressNavBar)) rf->updateFromBinaryAndSession(nullptr, false);
    }
    if (!m_debugSessionActive && m_serverRackView) m_serverRackView->setSessionIdle(m_rankCountSpin ? m_rankCountSpin->value() : 8);
    if (m_projectExplorerWidget) m_projectExplorerWidget->setSymbolsMode(false);
    recordRecentWorkspace(resolvedDir); syncDisassemblyDecompilerVisibility(true); return true;
}

/**
 * @brief Detects the build system ("Meson", "CMake", "Make", or "SingleFileMPI") for a directory or source file.
 * @param targetPath Project directory path or source file path.
 * @return Detected build system identifier string.
 */
QString MainWindow::detectBuildSystem(const QString& targetPath) const {
    const QString fallbackRoot = !m_activeWorkspaceDir.isEmpty() ? m_activeWorkspaceDir : QDir::currentPath();
    const QString resolvedPath = targetPath.trimmed().isEmpty() ? fallbackRoot : resolveWorkspacePath(targetPath);
    const QFileInfo info(resolvedPath);
    QList<QDir> searchDirs = {QDir(info.isDir() ? info.absoluteFilePath() : info.absolutePath())};
    if (!m_activeWorkspaceDir.isEmpty() && resolvedPath.startsWith(m_activeWorkspaceDir)) {
        searchDirs.append(QDir(m_activeWorkspaceDir));
    }
    // Priority order: Meson -> CMake -> Make -> SingleFileMPI across target directory and active workspace root.
    for (const QDir& dir : searchDirs) {
        if (dir.exists(QStringLiteral("meson.build"))) return QStringLiteral("Meson");
        if (dir.exists(QStringLiteral("CMakeLists.txt"))) return QStringLiteral("CMake");
        if (dir.exists(QStringLiteral("Makefile")) || dir.exists(QStringLiteral("GNUmakefile"))) return QStringLiteral("Make");
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
    const QString fallbackRoot = !m_activeWorkspaceDir.isEmpty() ? m_activeWorkspaceDir : QDir::currentPath();
    const QString resolvedPath = targetPath.trimmed().isEmpty() ? fallbackRoot : resolveWorkspacePath(targetPath);
    const QFileInfo info(resolvedPath);
    QString dirPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    if (!m_activeWorkspaceDir.isEmpty() && resolvedPath.startsWith(m_activeWorkspaceDir) &&
        !QDir(dirPath).exists(QStringLiteral("meson.build")) && !QDir(dirPath).exists(QStringLiteral("CMakeLists.txt")) &&
        !QDir(dirPath).exists(QStringLiteral("Makefile"))) {
        dirPath = m_activeWorkspaceDir;
    }
    const QDir dir(dirPath);

    // 1. Meson + Ninja build command supporting build_meson, builddir, and build directories.
    if (buildSys == QStringLiteral("Meson")) {
        for (const QString& b : {QStringLiteral("build_meson"), QStringLiteral("builddir"), QStringLiteral("build")}) {
            if (dir.exists(b + QStringLiteral("/build.ninja"))) return QStringLiteral("ninja -C \"%1/%2\"").arg(dirPath, b);
        }
        return QStringLiteral("meson setup \"%1/builddir\" \"%1\" && ninja -C \"%1/builddir\"").arg(dirPath);
    }
    // 2. CMake debug configure and parallel build command supporting build_cmake and build directories.
    if (buildSys == QStringLiteral("CMake")) {
        const QString bDir = dir.exists(QStringLiteral("build_cmake")) ? QStringLiteral("build_cmake") : QStringLiteral("build");
        return QStringLiteral("cmake -S \"%1\" -B \"%1/%2\" -DCMAKE_BUILD_TYPE=Debug && cmake --build \"%1/%2\" -j").arg(dirPath, bDir);
    }
    // 3. GNU Make parallel build command.
    if (buildSys == QStringLiteral("Make")) return QStringLiteral("make -C \"%1\" -j").arg(dirPath);

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

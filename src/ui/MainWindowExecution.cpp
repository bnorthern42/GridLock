/**
 * @file MainWindowExecution.cpp
 * @brief Execution scope control, stepping workflows, rank focus sync, and rank state dispatch.
 *
 * Implements backend engine synchronization, All Ranks vs Focused Rank execution scope,
 * source/instruction/reverse/non-linear stepping workflows, live rank state updates,
 * the master setFocusedRank() synchronization hub, per-rank breakpoint metadata, and teardown.
 */

#include "MainWindow.hpp"

#include "../core/hpc/dap/DapCoordinator.hpp"
#include "../core/hpc/analysis/DeadlockAnalyzer.hpp"
#include "../core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "EditorTabManager.hpp"
#include "GdbConsoleWidget.hpp"
#include "dialogs/plotter/ArrayPlotDialog.hpp"
#include "dialogs/debugger/CheckpointDialog.hpp"
#include "dialogs/graph/PointerGraphDialog.hpp"
#include "docks/CallStackDockWidget.hpp"
#include "docks/VariablesDockWidget.hpp"
#include "views/DisassemblyView.hpp"
#include "views/MemView.hpp"
#include "views/RegisterView.hpp"
#include "views/ServerRackView.hpp"
#include "views/SourceCodeView.hpp"
#include "widgets/DifferentialGrid.hpp"
#include "widgets/MpiDiagnosticsWidget.hpp"

#include <algorithm>

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>

namespace gridlock::ui {

/** @brief Synchronizes `m_coordinator` with `ConfigManager::instance().debuggerEngine()` when idle. */
void MainWindow::syncCoordinatorWithConfig() {
    if (m_debugSessionActive) return;
    auto& cfg = gridlock::core::ConfigManager::instance();
    const QString desired = (cfg.debuggerEngine().trimmed().toLower() == QStringLiteral("lldb-dap"))
                                ? QStringLiteral("lldb-dap")
                                : QStringLiteral("gdb");
    auto pushExecutionContext = [&]() {
        if (!m_coordinator) return;
        const auto ps = cfg.loadProjectSettings();
        const QString args = !ps.targetArguments.isEmpty() ? ps.targetArguments : QString::fromStdString(ps.programArguments);
        const QString wd = QString::fromStdString(ps.workingDirectory).trimmed();
        m_coordinator->setTargetExecutionContext(args, wd.isEmpty() ? m_activeWorkspaceDir : wd);
    };
    if (m_coordinator && m_coordinator->activeEngineName() == desired) { pushExecutionContext(); return; }
    if (m_coordinator) m_coordinator->disconnect();
    delete m_deadlockAnalyzer;
    m_deadlockAnalyzer = nullptr;
    if (m_ownedCoordinator) {
        m_ownedCoordinator->terminateSession();
        delete m_ownedCoordinator;
        m_ownedCoordinator = nullptr;
    }
    m_coordinator = nullptr;
    m_ownedCoordinator = (desired == QStringLiteral("lldb-dap"))
                             ? static_cast<IBackendCoordinator*>(new DapCoordinator(this))
                             : static_cast<IBackendCoordinator*>(new gridlock::GdbRankCoordinator(this));
    setCoordinator(m_ownedCoordinator);
    pushExecutionContext();
}

bool MainWindow::isAllRanksExecutionScope() const {
    const int ranks = (m_coordinator && m_coordinator->getProcessCount() > 0)
                          ? m_coordinator->getProcessCount()
                          : (m_rankCountSpin ? m_rankCountSpin->value() : 1);
    return ranks > 1 && (!m_executionScopeCombo || m_executionScopeCombo->currentData().toInt() < 0);
}

int MainWindow::executionTargetRank() const { return isAllRanksExecutionScope() ? -1 : activeSteppingRank(); }

void MainWindow::setExecutionScopeAllRanks(bool allRanks) {
    if (m_executionScopeCombo) m_executionScopeCombo->setCurrentIndex(allRanks ? 0 : 1);
    syncExecutionScopeUi();
}

void MainWindow::syncExecutionScopeUi() {
    if (m_syncingScopeUi) return;
    m_syncingScopeUi = true;
    const int ranks = (m_coordinator && m_coordinator->getProcessCount() > 0)
                          ? m_coordinator->getProcessCount()
                          : (m_rankCountSpin ? m_rankCountSpin->value() : 1);
    const int focused = activeSteppingRank();
    if (m_executionScopeCombo && m_executionScopeCombo->count() >= 2) {
        QSignalBlocker sb(m_executionScopeCombo);
        m_executionScopeCombo->setItemText(1, QStringLiteral("Rank %1").arg(focused));
        m_executionScopeCombo->setItemData(1, focused);
    }
    const bool active = m_debugSessionActive || (m_coordinator && m_coordinator->getProcessCount() > 0);
    const bool showMultiRankControls = active && ranks > 1;
    if (m_executionScopeAction) m_executionScopeAction->setVisible(showMultiRankControls);
    if (auto* paTbAct = findChild<QAction*>(QStringLiteral("actionToolbarPauseAllRanks"))) paTbAct->setVisible(showMultiRankControls);
    const bool allRanks = isAllRanksExecutionScope();
    const QString scope = allRanks ? QStringLiteral("All Ranks") : QStringLiteral("Rank %1").arg(focused);
    const QString idleSuffix = active ? QString() : QStringLiteral(" (disabled while idle — click Run Target first)");
    if (m_executionScopeCombo) m_executionScopeCombo->setToolTip(QStringLiteral("Execution Scope (%1) — governs Continue, Pause, Step Over, Step Into, and Step Out").arg(scope));
    if (m_coordinator) m_coordinator->setProperty("gl_scoped_continue_rank", allRanks ? -1 : focused);
    if (m_continueAction) m_continueAction->setToolTip(QStringLiteral("Continue Execution on %1 (F5)%2").arg(scope, idleSuffix));
    if (m_stepOverAction) m_stepOverAction->setToolTip(QStringLiteral("Step Over on %1 (F10)%2").arg(scope, idleSuffix));
    if (m_stepIntoAction) m_stepIntoAction->setToolTip(QStringLiteral("Step Into on %1 (F11)%2").arg(scope, idleSuffix));
    if (m_stepOutAction) m_stepOutAction->setToolTip(QStringLiteral("Step Out on %1 (Shift+F11)%2").arg(scope, idleSuffix));
    if (m_pauseAction) m_pauseAction->setToolTip(QStringLiteral("Pause %1%2").arg(scope, idleSuffix));
    if (m_pauseAllAction) { m_pauseAllAction->setEnabled(active); m_pauseAllAction->setToolTip(QStringLiteral("Pause All Ranks (F6)%1").arg(idleSuffix)); }
    m_syncingScopeUi = false;
}

static uint64_t parseHexToken(const QString& raw, bool* okOut) {
    const QString tok = raw.section(QLatin1Char(' '), 0, 0);
    return tok.mid(tok.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 2 : 0).toULongLong(okOut, 16);
}

/** @brief Dispatches a scoped source-level step-over (`-exec-next` / DAP `next`). */
bool MainWindow::stepOverWorkflow() {
    if (m_previewFixtureOnly) return stepInstructionWorkflow();
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    m_focusedRank = activeSteppingRank();
    const int target = executionTargetRank();
    updateStatusTelemetry(m_currentSeekOrPc, target < 0 ? QStringLiteral("STEPPING ALL") : QStringLiteral("STEPPING"));
    m_coordinator->stepOver(target < 0 ? -1 : (target + 1));
    return true;
}

/** @brief Dispatches a scoped source-level step-into (`-exec-step` / DAP `stepIn`). */
bool MainWindow::stepIntoWorkflow() {
    if (m_previewFixtureOnly) return stepInstructionWorkflow();
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    m_focusedRank = activeSteppingRank();
    const int target = executionTargetRank();
    updateStatusTelemetry(m_currentSeekOrPc, target < 0 ? QStringLiteral("STEPPING ALL") : QStringLiteral("STEPPING"));
    m_coordinator->stepInto(target < 0 ? -1 : (target + 1));
    return true;
}

/** @brief Dispatches a scoped source-level step-out (`-exec-finish` / DAP `stepOut`). */
bool MainWindow::stepOutWorkflow() {
    if (m_previewFixtureOnly) return stepInstructionWorkflow();
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    m_focusedRank = activeSteppingRank();
    const int target = executionTargetRank();
    updateStatusTelemetry(m_currentSeekOrPc, target < 0 ? QStringLiteral("STEPPING OUT ALL") : QStringLiteral("STEPPING OUT"));
    m_coordinator->stepOut(target < 0 ? -1 : (target + 1));
    return true;
}

/** @brief Interrupts execution on the active scope (`All Ranks` or focused `Rank k`). */
bool MainWindow::pauseRankWorkflow() {
    if (!m_coordinator) return false;
    m_focusedRank = activeSteppingRank();
    const int target = executionTargetRank();
    updateStatusTelemetry(m_currentSeekOrPc, target < 0 ? QStringLiteral("PAUSED ALL") : QStringLiteral("PAUSED"));
    m_coordinator->pauseRank(target);
    return true;
}

/** @brief Interrupts execution across all active ranks (`F6` -> `m_coordinator->haltAll()`). */
bool MainWindow::pauseAllRanksWorkflow() {
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("PAUSED ALL"));
    m_coordinator->haltAll();
    return true;
}

/** @brief Steps backward over the previous source statement (`-exec-next --reverse` / DAP `stepBack`). */
bool MainWindow::reverseStepOverWorkflow() {
    if (!m_debugSessionActive || !m_coordinator) return false;
    m_focusedRank = activeSteppingRank();
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("STEPPING BACK"));
    m_coordinator->reverseStepOver(m_focusedRank);
    return true;
}

/** @brief Runs backward in recorded execution history (`-exec-continue --reverse` / DAP `reverseContinue`). */
bool MainWindow::reverseContinueWorkflow() {
    if (!m_debugSessionActive || !m_coordinator) return false;
    m_focusedRank = activeSteppingRank();
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("REVERSE RUNNING"));
    m_coordinator->reverseContinue(m_focusedRank);
    return true;
}

/** @brief Steps over a single machine instruction (`nexti` / `-exec-next-instruction`) on the focused MPI rank. */
bool MainWindow::nextInstructionWorkflow() {
    if (m_previewFixtureOnly) return stepInstructionWorkflow();
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    m_focusedRank = activeSteppingRank();
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("STEPPING (NEXTI)"));
    m_coordinator->nextInstruction(m_focusedRank);
    return true;
}

/** @brief Runs execution until `filePath:lineOneBased` (or active editor/disassembly cursor) is reached (`Ctrl+F10`). */
bool MainWindow::runToCursorWorkflow(const QString& filePath, int lineOneBased) {
    if (filePath.trimmed().isEmpty() && lineOneBased <= 0 && m_disassemblyView) {
        if (QWidget* fw = QApplication::focusWidget(); fw && (fw == m_disassemblyView || m_disassemblyView->isAncestorOf(fw))) {
            if (const uint64_t addr = m_disassemblyView->selectedInstructionAddress(); addr != 0ULL) return runToAddressWorkflow(addr);
        }
    }
    SourceCodeView* sv = sourceCodeView();
    const QString targetFile = !filePath.trimmed().isEmpty()
                                   ? filePath.trimmed()
                                   : (sv && !sv->currentFile().isEmpty() ? sv->currentFile() : activeTargetSourcePath());
    const int targetLine = (lineOneBased > 0) ? lineOneBased : (sv ? sv->getCurrentLineNumber() : -1);
    if (targetFile.isEmpty() || targetLine <= 0) return false;
    if (m_previewFixtureOnly) { if (sv) sv->highlightExecutionLine(targetLine); return true; }
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    m_focusedRank = activeSteppingRank();
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("RUNNING TO CURSOR"));
    m_coordinator->runToCursor(targetFile, targetLine, m_focusedRank);
    return true;
}

/** @brief Runs execution until instruction address `*0x<hex>` is reached (`-exec-until *0x...`). */
bool MainWindow::runToAddressWorkflow(uint64_t address) {
    if (address == 0ULL) return false;
    if (m_previewFixtureOnly) { if (m_disassemblyView) m_disassemblyView->seekToAddress(address); return true; }
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    m_focusedRank = activeSteppingRank();
    updateStatusTelemetry(QStringLiteral("0x%1").arg(address, 0, 16), QStringLiteral("RUNNING TO ADDR"));
    m_coordinator->runToAddress(address, m_focusedRank);
    return true;
}

/** @brief Relocates execution PC to `filePath:lineOneBased` (`-exec-jump`) with lexical scope safety confirmation. */
bool MainWindow::jumpToLocationWorkflow(const QString& filePath, int lineOneBased, bool skipPrompt) {
    SourceCodeView* sv = sourceCodeView();
    const QString targetFile = !filePath.trimmed().isEmpty()
                                   ? filePath.trimmed()
                                   : (sv && !sv->currentFile().isEmpty() ? sv->currentFile() : activeTargetSourcePath());
    if (targetFile.isEmpty() || lineOneBased <= 0) return false;
    const int activeRank = activeSteppingRank();
    const auto stIt = m_latestStates.find(activeRank);
    const QString curFunc = (stIt != m_latestStates.end()) ? stIt->second.currentFunction : QString();
    int curLine = sv ? sv->executionLine() : -1;
    if (curLine <= 0 && stIt != m_latestStates.end()) curLine = stIt->second.currentLine;
    QString srcText = sv ? sv->toPlainText() : QString();
    if (srcText.isEmpty() && QFileInfo::exists(targetFile)) {
        QFile f(targetFile); if (f.open(QIODevice::ReadOnly | QIODevice::Text)) srcText = QString::fromUtf8(f.readAll());
    }
    if (!skipPrompt) {
        if (const QString h = checkJumpLexicalScopeHazard(srcText, curLine, lineOneBased, curFunc);
            !h.isEmpty() && QMessageBox::warning(this, tr("Set Next Statement / Jump Hazard"),
                                                 h + tr("\n\nProceed with jump anyway?"),
                                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;
    }
    if (m_previewFixtureOnly) { if (sv) sv->highlightExecutionLine(lineOneBased); return true; }
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    m_focusedRank = activeRank;
    m_coordinator->jumpToLocation(targetFile, lineOneBased, activeRank);
    return true;
}

/** @brief Relocates execution PC to machine instruction @p address (`-exec-jump *0x...`) with safety confirmation. */
bool MainWindow::jumpToAddressWorkflow(uint64_t address, bool skipPrompt) {
    if (address == 0ULL) return false;
    if (!skipPrompt) {
        if (const int ml = addressToSourceLine(address); ml > 0) {
            if (SourceCodeView* sv = sourceCodeView()) {
                if (const QString h = checkJumpLexicalScopeHazard(sv->toPlainText(), sv->executionLine(), ml);
                    !h.isEmpty() && QMessageBox::warning(this, tr("Jump to Instruction Hazard"),
                                                         h + tr("\n\nJump to 0x%1 anyway?").arg(address, 0, 16),
                                                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return false;
            }
        }
    }
    if (m_previewFixtureOnly) { if (m_disassemblyView) m_disassemblyView->seekToAddress(address); return true; }
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    m_focusedRank = activeSteppingRank();
    m_coordinator->jumpToAddress(address, m_focusedRank);
    return true;
}

/** @brief Forces an immediate return from the active/selected frame (`-exec-return [expr]`) on the focused MPI rank. */
bool MainWindow::forceEarlyReturnWorkflow(const QString& explicitExpr, bool promptIfEmpty) {
    const int rank = activeSteppingRank();
    const int frameLvl = m_callStackDockWidget ? m_callStackDockWidget->activeFrameLevel() : m_selectedFrameLevel.value(rank, 0);
    const QString funcName = (m_callStackDockWidget && !m_callStackDockWidget->activeFrameFunction().isEmpty())
                                 ? m_callStackDockWidget->activeFrameFunction() : QStringLiteral("current frame");
    QString expr = explicitExpr.trimmed();
    if (promptIfEmpty) {
        bool ok = false;
        expr = QInputDialog::getText(this, tr("Force Early Return (Rank %1)").arg(rank),
                                     tr("Return expression for Frame #%1 (%2) on Rank %3 (leave empty for void):").arg(frameLvl).arg(funcName).arg(rank),
                                     QLineEdit::Normal, expr, &ok).trimmed();
        if (!ok) return false;
        if (!property("gl_bypass_confirm").toBool() &&
            QMessageBox::warning(this, tr("Confirm Force Early Return"),
                                 tr("Force immediate return from Frame #%1 (%2) on Rank %3?\n\nRemaining instructions and local destructors in the unwound frame(s) will be skipped.")
                                     .arg(frameLvl).arg(funcName).arg(rank),
                                 QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return false;
    }
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    m_focusedRank = rank;
    if (m_callStackDockWidget) m_coordinator->selectStackFrame(rank, m_callStackDockWidget->activeThreadId(), frameLvl);
    m_coordinator->forceEarlyReturn(expr, rank);
    return true;
}

/** @brief Installs function-symbol regex breakpoints via GDB `rbreak <pattern>`. */
bool MainWindow::insertRegexBreakpointWorkflow(const QString& explicitPattern, bool promptIfEmpty) {
    QString pat = explicitPattern.trimmed();
    if (promptIfEmpty && pat.isEmpty()) {
        bool ok = false;
        pat = QInputDialog::getText(this, tr("Break on Function Regex (rbreak)"),
                                    tr("Function regex pattern (e.g. MPI_.*|Solver::step.*):"),
                                    QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok) return false;
    }
    if (pat.isEmpty() || !m_coordinator) return false;
    m_coordinator->insertRegexBreakpoint(pat);
    return true;
}

/** @brief Updates workspace views upon receiving live state changes for an MPI rank. */
void MainWindow::onRankStateChanged(int rankId, const RankState& state) {
    const bool hadPrev = m_latestStates.contains(rankId);
    const RankState prev = hadPrev ? m_latestStates[rankId] : RankState{};
    const bool isNewStop = (state.currentState == QLatin1String("stopped")) &&
                           (!hadPrev || prev.currentState != QLatin1String("stopped") ||
                            prev.currentFile != state.currentFile || prev.currentLine != state.currentLine);

    m_latestStates[rankId] = state;
    if (state.currentState == QLatin1String("running")) {
        m_selectedFrameLevel[rankId] = 0;
        if (rankId == m_focusedRank && m_editorTabManager) m_editorTabManager->clearInlineVariableValues();
    }
    if (m_serverRackView) m_serverRackView->updateRankState(rankId, state);
    const QString pcAddr = state.registers.value(16, m_currentSeekOrPc);
    bool pcOk = false;
    const uint64_t pcVal = parseHexToken(pcAddr, &pcOk);
    if (m_gdbConsoleWidget && m_coordinator && m_coordinator->getProcessCount() > 0) {
        m_gdbConsoleWidget->ensureRankCount(m_coordinator->getProcessCount());
    }
    if (m_mpiDiagnosticsWidget) {
        const QString diagStatus = (state.currentState == QLatin1String("stopped")) ? QStringLiteral("PAUSED") : state.currentState.toUpper();
        const qint64 livePid = m_coordinator ? m_coordinator->getPidForRank(rankId) : 0;
        QString activePrim = state.currentFunction.isEmpty() ? QStringLiteral("main") : state.currentFunction;
        for (const auto& fr : m_cachedStackFrames.value(rankId)) {
            if (fr.function.startsWith(QStringLiteral("MPI_")) || fr.function.startsWith(QStringLiteral("PMPI_")) ||
                fr.function.contains(QStringLiteral("barrierWait"))) { activePrim = fr.function; break; }
        }
        const QString locStr = (!state.currentFile.isEmpty() && state.currentLine > 0)
            ? QStringLiteral("%1:%2 (0x%3)").arg(QFileInfo(state.currentFile).fileName()).arg(state.currentLine).arg(pcOk ? pcVal : 0ULL, 8, 16, QChar('0'))
            : ((diagStatus == QLatin1String("RUNNING") && (!pcOk || pcVal == 0ULL))
                   ? QStringLiteral("running") : QStringLiteral("0x%1").arg(pcOk ? pcVal : 0ULL, 8, 16, QChar('0')));
        m_mpiDiagnosticsWidget->updateLiveRankRow(rankId, livePid, diagStatus.isEmpty() ? QStringLiteral("IDLE") : diagStatus,
            activePrim, QStringLiteral("LIVE"), locStr, m_coordinator ? std::max(1, m_coordinator->getProcessCount()) : 1);
    }

    if (rankId == m_focusedRank) {
        if (m_disassemblyView && !state.disassemblyText.isEmpty() &&
            (!m_useRealBinaryTarget || !pcOk || pcVal == 0ULL || m_disassemblyView->lineForAddress(pcVal) < 0)) {
            m_disassemblyView->updateDisassembly(state.disassemblyText);
        }
        if (m_disassemblyView && pcOk && pcVal != 0ULL) { m_disassemblyView->setCurrentAddress(pcVal); m_disassemblyView->setActiveSteppingRank(rankId, pcVal); }
        updateStatusTelemetry(pcAddr, state.currentState.isEmpty() ? m_currentTargetState : state.currentState.toUpper());
        refreshStatusBarMemoryTelemetry();
        if (state.currentState == QLatin1String("stopped") && state.currentLine > 0) {
            if (isNewStop && !m_suppressExecutionTabSwitch) {
                SourceCodeView* view = (!state.currentFile.isEmpty() && QFileInfo::exists(state.currentFile) && m_editorTabManager)
                                           ? m_editorTabManager->openFile(state.currentFile) : getSourceCodeView();
                if (view) { syncTabBreakpointMetadata(view); view->highlightCurrentLine(state.currentLine); view->highlightExecutionLine(state.currentLine); }
            } else if (m_editorTabManager && !state.currentFile.isEmpty()) {
                if (SourceCodeView* sv = m_editorTabManager->getSourceCodeViewForFile(state.currentFile)) sv->setExecutionLineOnly(state.currentLine);
            }
        }
        if (m_registerView && !state.registers.isEmpty()) m_registerView->updateRegisters(state);
    }
    if (m_differentialGrid) m_differentialGrid->setVariableData(rankId, state.variableWatches);

    if (m_debugSessionActive && m_coordinator && m_coordinator->getProcessCount() > 0) {
        bool allExited = true;
        for (int r = 0; r < m_coordinator->getProcessCount(); ++r) {
            if (!m_latestStates.contains(r) || m_latestStates[r].currentState != QLatin1String("exited")) { allExited = false; break; }
        }
        if (allExited) {
            m_debugSessionActive = false;
            m_cachedStackFrames.clear(); m_cachedThreads.clear(); m_selectedFrameLevel.clear();
            if (m_callStackDockWidget) m_callStackDockWidget->clearFrames();
            updateDebugToolbarState();
            if (SourceCodeView* sv = getSourceCodeView()) sv->highlightExecutionLine(-1);
            updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("EXITED (0)"));
        }
    }
}

/** @brief Delegates legacy rank-selection slots directly to the master `setFocusedRank()` hub. */
void MainWindow::onRankSelected(int rankId) { setFocusedRank(rankId); }

/** @brief Master rank-focus hub synchronizing all docks, dialogs, toolbar state, and views without signal loops. */
void MainWindow::setFocusedRank(int rank) {
    if (m_syncingFocusedRank) return;
    m_syncingFocusedRank = true;
    const int target = qMax(0, rank);
    const bool changed = (m_focusedRank != target);
    m_focusedRank = target;
    const int effRanks = (m_coordinator && m_coordinator->getProcessCount() > 0)
                             ? m_coordinator->getProcessCount()
                             : qMax(m_rankCountSpin ? m_rankCountSpin->value() : 1, target + 1);
    if (m_serverRackView && m_serverRackView->selectedRank() != target && target < m_serverRackView->count()) {
        QSignalBlocker sb(m_serverRackView); m_serverRackView->selectRank(target);
    }
    if (m_variablesDockWidget) m_variablesDockWidget->setFocusedRank(target, effRanks);
    if (m_registerView) m_registerView->setFocusedRank(target);
    if (m_memView) m_memView->setFocusedRank(target, effRanks);
    if (m_arrayPlotDialog) m_arrayPlotDialog->setFocusedRank(target);
    if (m_pointerGraphDialog) m_pointerGraphDialog->setFocusedRank(target);
    if (m_checkpointDialog) m_checkpointDialog->setFocusedRank(target);
    if (auto* recBtn = findChild<QPushButton*>(QStringLiteral("toolbarRecordHistoryBtn"))) {
        QSignalBlocker sb(recBtn); recBtn->setChecked(m_coordinator && m_coordinator->isReverseRecordingEnabled(target));
    }
    if (changed && m_editorTabManager) m_editorTabManager->clearInlineVariableValues();
    updateDebugToolbarState();
    syncExecutionScopeUi();
    if (m_editorTabManager) m_editorTabManager->setActiveFocusRank(target);
    if (m_callStackDockWidget) {
        m_callStackDockWidget->setThreads(m_cachedThreads.value(target));
        m_callStackDockWidget->setStackFrames(m_cachedStackFrames.value(target), m_selectedFrameLevel.value(target, 0));
    }
    if (m_latestStates.count(target)) {
        const auto& state = m_latestStates[target];
        const QString pcAddr = state.registers.value(16, m_currentSeekOrPc);
        bool pcOk = false;
        const uint64_t pcVal = parseHexToken(pcAddr, &pcOk);
        updateStatusTelemetry(pcAddr, state.currentState.isEmpty() ? m_currentTargetState : state.currentState.toUpper());
        refreshStatusBarMemoryTelemetry();
        if (m_disassemblyView && !state.disassemblyText.isEmpty() && (!m_useRealBinaryTarget || !pcOk || pcVal == 0ULL || m_disassemblyView->lineForAddress(pcVal) < 0))
            m_disassemblyView->updateDisassembly(state.disassemblyText);
        if (m_disassemblyView && pcOk && pcVal != 0ULL) { m_disassemblyView->setCurrentAddress(pcVal); m_disassemblyView->setActiveSteppingRank(target, pcVal); }
        if (state.currentState == QLatin1String("stopped")) {
            if (state.disassemblyText.isEmpty())
                if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) gdbCoord->requestDisassemblyFallback(target);
            if (m_coordinator) m_coordinator->requestStackAndThreads(target);
            if (state.currentLine > 0) {
                SourceCodeView* view = (!state.currentFile.isEmpty() && QFileInfo::exists(state.currentFile) && m_editorTabManager)
                                           ? m_editorTabManager->openFile(state.currentFile) : getSourceCodeView();
                if (view) { syncTabBreakpointMetadata(view); view->highlightCurrentLine(state.currentLine); view->highlightExecutionLine(state.currentLine); }
            }
        }
        if (m_differentialGrid) m_differentialGrid->setVariableData(target, state.variableWatches);
        if (m_registerView && !state.registers.isEmpty()) m_registerView->updateRegisters(state);
    } else {
        updateStatusTelemetry(); refreshStatusBarMemoryTelemetry();
    }
    m_syncingFocusedRank = false;
    if (changed) emit focusedRankChanged(m_focusedRank);
}

/** @brief Records per-rank breakpoint metadata and stages rank-filter hints for coordinator insertion. */
void MainWindow::onRankBreakpointChanged(const QString& file, int line, bool isSet, int rankFilter, const QString& condition) {
    if (file.isEmpty() || line <= 0) return;
    const QFileInfo fi(file);
    const QString absPath = fi.absoluteFilePath();
    const QString canonPath = fi.canonicalFilePath().isEmpty() ? absPath : fi.canonicalFilePath();
    if (isSet) {
        BreakpointRankMeta meta{rankFilter < 0 ? -1 : rankFilter, condition.trimmed()};
        m_breakpointRankMeta[absPath][line] = meta;
        if (canonPath != absPath) m_breakpointRankMeta[canonPath][line] = meta;
    } else {
        m_breakpointRankMeta[absPath].remove(line);
        if (canonPath != absPath) m_breakpointRankMeta[canonPath].remove(line);
    }
    if (m_coordinator) {
        m_coordinator->setProperty("gl_pending_bp_rank_key", QString("%1:%2").arg(absPath).arg(line));
        m_coordinator->setProperty("gl_pending_bp_rank_val", rankFilter < 0 ? -1 : rankFilter);
    }
}

/** @brief Restores cached per-rank breakpoint colors/conditions onto an open SourceCodeView tab. */
void MainWindow::syncTabBreakpointMetadata(SourceCodeView* view) const {
    if (!view || view->currentFile().isEmpty()) return;
    const QFileInfo fi(view->currentFile());
    const QString absPath = fi.absoluteFilePath();
    const QString canonPath = fi.canonicalFilePath().isEmpty() ? absPath : fi.canonicalFilePath();
    QMap<int, SourceCodeView::BreakpointVisualInfo> meta;
    const QMap<int, BreakpointRankMeta>& stored = m_breakpointRankMeta.contains(canonPath)
                                                      ? m_breakpointRankMeta[canonPath]
                                                      : m_breakpointRankMeta.value(absPath);
    if (stored.isEmpty()) return;
    for (int line : view->breakpoints()) {
        const auto s = stored.value(line, BreakpointRankMeta{-1, view->breakpointCondition(line)});
        meta[line] = SourceCodeView::BreakpointVisualInfo{s.rankFilter, s.condition, 0, QString()};
    }
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) {
        meta[it.key()] = SourceCodeView::BreakpointVisualInfo{it.value().rankFilter, it.value().condition, 0, QString()};
    }
    view->syncBreakpointMetadata(meta);
}

/** @brief Safely shuts down live coordinators and disconnects signals before Qt destroys child docks. */
MainWindow::~MainWindow() {
    m_isClosing = true;
    if (qApp) qApp->removeEventFilter(this);
    delete m_deadlockAnalyzer;
    m_deadlockAnalyzer = nullptr;
    if (m_coordinator) QObject::disconnect(m_coordinator, nullptr, this, nullptr);
    if (m_ownedCoordinator) {
        m_ownedCoordinator->blockSignals(true);
        m_ownedCoordinator->terminateSession();
        delete m_ownedCoordinator;
        m_ownedCoordinator = nullptr;
    }
    m_coordinator = nullptr; m_callStackDockWidget = nullptr; m_mpiDiagnosticsWidget = nullptr;
    m_gdbConsoleWidget = nullptr; m_variablesDockWidget = nullptr; m_serverRackView = nullptr;
    m_projectExplorerWidget = nullptr; m_editorTabManager = nullptr; m_pointerGraphDialog = nullptr;
}

}  // namespace gridlock::ui

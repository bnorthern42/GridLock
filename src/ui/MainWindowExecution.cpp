/**
 * @file MainWindowExecution.cpp
 * @brief Implementation of target resolution, tutorial discovery, and execution workflows.
 *
 * Provides resolution helpers for bundled tutorial examples across local development
 * paths and AppImage read-only deployment directories (/usr/share/gridlock/tutorial/
 * or $APPDIR/usr/share/gridlock/tutorial/). Supports on-demand copy/execution for
 * read-only standalone deployments.
 */

#include "MainWindow.hpp"

#include "../core/hpc/DapCoordinator.hpp"
#include "../core/hpc/DeadlockAnalyzer.hpp"
#include "../core/hpc/GdbRankCoordinator.hpp"
#include "../core/hpc/NativeMemoryReader.hpp"
#include "../core/hpc/ProcessScanner.hpp"
#include "../core/managers/ConfigManager.hpp"
#include "dialogs/ArrayPlotDialog.hpp"
#include "dialogs/AttachProcessDialog.hpp"
#include "dialogs/PointerGraphDialog.hpp"
#include "docks/CallStackDockWidget.hpp"
#include "views/MemView.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QString>
#include <QTableWidget>
#include <QTimer>

#include <cstring>

namespace gridlock::ui {

/** @brief Resolves a tutorial target name or relative path across local and AppImage share directories. */
QString resolveTutorialTarget(const QString& target) {
    QString rel = target.trimmed();
    if (rel.isEmpty()) return QString();
    if (!rel.endsWith(QStringLiteral(".cpp")) && !rel.contains(QLatin1Char('/'))) rel = QStringLiteral("tutorial/") + rel + QStringLiteral(".cpp");
    else if (!rel.startsWith(QStringLiteral("tutorial/")) && !rel.startsWith(QLatin1Char('/'))) rel = QStringLiteral("tutorial/") + rel;
    auto canonOrAbs = [](const QString& p) { const QFileInfo fi(p); return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath(); };
    if (QFileInfo::exists(rel)) return canonOrAbs(rel);
    if (const QString parentCand = QStringLiteral("../") + rel; QFileInfo::exists(parentCand)) return canonOrAbs(parentCand);
    const QString envAppDir = qEnvironmentVariable("APPDIR");
    if (!envAppDir.isEmpty()) {
        if (const QString appDirShare = envAppDir + QStringLiteral("/usr/share/gridlock/") + rel; QFileInfo::exists(appDirShare)) return canonOrAbs(appDirShare);
        if (const QString appDirDirect = envAppDir + QStringLiteral("/") + rel; QFileInfo::exists(appDirDirect)) return canonOrAbs(appDirDirect);
    }
    const QString appDirPath = QCoreApplication::applicationDirPath(), shareCand = appDirPath + QStringLiteral("/../share/gridlock/") + rel;
    if (QFileInfo::exists(shareCand)) return canonOrAbs(shareCand);
    if (const QString localCand = appDirPath + QStringLiteral("/") + rel; QFileInfo::exists(localCand)) return canonOrAbs(localCand);
    return !envAppDir.isEmpty() ? (envAppDir + QStringLiteral("/usr/share/gridlock/") + rel) : shareCand;
}

/** @brief Ensures a writable execution path for tutorial targets in read-only AppImage environments. */
QString ensureWritableTutorialBinaryPath(const QString& sourcePath) {
    if (sourcePath.isEmpty()) return QString();
    const QFileInfo srcInfo(sourcePath); const QString baseName = srcInfo.completeBaseName(), companionBin = srcInfo.dir().filePath(baseName);
    if (!srcInfo.dir().exists() || !QFileInfo(srcInfo.dir().absolutePath()).isWritable()) {
        const QString cacheDir = QStringLiteral("/tmp/gridlock_tutorials"); QDir().mkpath(cacheDir);
        const QString targetBin = cacheDir + QStringLiteral("/") + baseName;
        if (QFileInfo::exists(companionBin) && (!QFileInfo::exists(targetBin) || QFileInfo(targetBin).lastModified() < QFileInfo(companionBin).lastModified())) {
            QFile::remove(targetBin); QFile::copy(companionBin, targetBin);
            QFile::setPermissions(targetBin, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup);
        }
        return targetBin;
    }
    return companionBin;
}

/** @brief Synchronizes `m_coordinator` with `ConfigManager::instance().debuggerEngine()` when idle. */
void MainWindow::syncCoordinatorWithConfig() {
    if (m_debugSessionActive) return;
    auto& cfg = gridlock::core::ConfigManager::instance();
    const QString desired = (cfg.debuggerEngine().trimmed().toLower() == QStringLiteral("lldb-dap")) ? QStringLiteral("lldb-dap") : QStringLiteral("gdb");
    auto pushExecutionContext = [&]() {
        if (!m_coordinator) return;
        const auto ps = cfg.loadProjectSettings();
        const QString args = !ps.targetArguments.isEmpty() ? ps.targetArguments : QString::fromStdString(ps.programArguments);
        QString wd = QString::fromStdString(ps.workingDirectory).trimmed();
        m_coordinator->setTargetExecutionContext(args, wd.isEmpty() ? m_activeWorkspaceDir : wd);
    };
    if (m_coordinator && m_coordinator->activeEngineName() == desired) { pushExecutionContext(); return; }
    if (m_coordinator) m_coordinator->disconnect();
    delete m_deadlockAnalyzer; m_deadlockAnalyzer = nullptr;
    if (m_ownedCoordinator) { m_ownedCoordinator->terminateSession(); delete m_ownedCoordinator; m_ownedCoordinator = nullptr; }
    m_coordinator = nullptr;
    m_ownedCoordinator = (desired == QStringLiteral("lldb-dap")) ? static_cast<IBackendCoordinator*>(new DapCoordinator(this)) : static_cast<IBackendCoordinator*>(new gridlock::GdbRankCoordinator(this));
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
/** @brief Dispatches a scoped source-level step-over (`-exec-next` / DAP `next`). */
bool MainWindow::stepOverWorkflow() {
    if (m_previewFixtureOnly) return stepInstructionWorkflow();
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    m_focusedRank = activeSteppingRank(); const int target = executionTargetRank();
    updateStatusTelemetry(m_currentSeekOrPc, target < 0 ? QStringLiteral("STEPPING ALL") : QStringLiteral("STEPPING"));
    m_coordinator->stepOver(target < 0 ? -1 : (target + 1)); return true;
}
/** @brief Dispatches a scoped source-level step-into (`-exec-step` / DAP `stepIn`). */
bool MainWindow::stepIntoWorkflow() {
    if (m_previewFixtureOnly) return stepInstructionWorkflow();
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    m_focusedRank = activeSteppingRank(); const int target = executionTargetRank();
    updateStatusTelemetry(m_currentSeekOrPc, target < 0 ? QStringLiteral("STEPPING ALL") : QStringLiteral("STEPPING"));
    m_coordinator->stepInto(target < 0 ? -1 : (target + 1)); return true;
}
/** @brief Dispatches a scoped source-level step-out (`-exec-finish` / DAP `stepOut`). */
bool MainWindow::stepOutWorkflow() {
    if (m_previewFixtureOnly) return stepInstructionWorkflow();
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    m_focusedRank = activeSteppingRank(); const int target = executionTargetRank();
    updateStatusTelemetry(m_currentSeekOrPc, target < 0 ? QStringLiteral("STEPPING OUT ALL") : QStringLiteral("STEPPING OUT"));
    m_coordinator->stepOut(target < 0 ? -1 : (target + 1)); return true;
}
/** @brief Interrupts execution on the active scope (`All Ranks` or focused `Rank k`). */
bool MainWindow::pauseRankWorkflow() {
    if (!m_coordinator) return false;
    m_focusedRank = activeSteppingRank(); const int target = executionTargetRank();
    updateStatusTelemetry(m_currentSeekOrPc, target < 0 ? QStringLiteral("PAUSED ALL") : QStringLiteral("PAUSED"));
    m_coordinator->pauseRank(target); return true;
}
/** @brief Interrupts execution across all active ranks (`F6` -> `m_coordinator->haltAll()`). */
bool MainWindow::pauseAllRanksWorkflow() {
    if (!m_coordinator || (!m_debugSessionActive && m_coordinator->getProcessCount() <= 0)) return false;
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("PAUSED ALL"));
    m_coordinator->haltAll(); return true;
}
/** @brief Steps backward over the previous source statement (`-exec-next --reverse` / DAP `stepBack`). */
bool MainWindow::reverseStepOverWorkflow() {
    if (!m_debugSessionActive || !m_coordinator) return false;
    m_focusedRank = activeSteppingRank(); updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("STEPPING BACK"));
    m_coordinator->reverseStepOver(m_focusedRank); return true;
}
/** @brief Runs backward in recorded execution history (`-exec-continue --reverse` / DAP `reverseContinue`). */
bool MainWindow::reverseContinueWorkflow() {
    if (!m_debugSessionActive || !m_coordinator) return false;
    m_focusedRank = activeSteppingRank(); updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("REVERSE RUNNING"));
    m_coordinator->reverseContinue(m_focusedRank); return true;
}

/**
 * @brief Updates workspace views upon receiving live state changes for an MPI rank.
 * @param rankId Zero-based MPI rank index.
 * @param state Snapshot containing current execution state, active PC, source line, and registers.
 */
void MainWindow::onRankStateChanged(int rankId, const RankState& state) {
    // Capture previous stop snapshot before overwriting so only genuine stop transitions switch editor tabs.
    const bool hadPrev = m_latestStates.contains(rankId);
    const RankState prevState = hadPrev ? m_latestStates[rankId] : RankState{};
    const bool isNewStopTransition =
        (state.currentState == QLatin1String("stopped")) &&
        (!hadPrev || prevState.currentState != QLatin1String("stopped") ||
         prevState.currentFile != state.currentFile ||
         prevState.currentLine != state.currentLine);

    // Cache the newest rank state and reflect it on the Server Rack overview widget.
    m_latestStates[rankId] = state;
    if (state.currentState == QLatin1String("running")) { m_selectedFrameLevel[rankId] = 0; if (rankId == m_focusedRank && m_editorTabManager) m_editorTabManager->clearInlineVariableValues(); }
    if (m_serverRackView) m_serverRackView->updateRankState(rankId, state);

    // Parse the 64-bit instruction pointer from $rip (register slot 16) for CFG/disassembly sync.
    const QString pcAddr = state.registers.value(16, m_currentSeekOrPc);
    const QString pcToken = pcAddr.section(QLatin1Char(' '), 0, 0);
    bool pcOk = false;
    const uint64_t pcVal = pcToken
                               .mid(pcToken.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 2 : 0)
                               .toULongLong(&pcOk, 16);

    // Synchronize rank selector count in GdbConsoleWidget when live ranks exist.
    if (m_gdbConsoleWidget && m_coordinator && m_coordinator->getProcessCount() > 0) {
        m_gdbConsoleWidget->ensureRankCount(m_coordinator->getProcessCount());
    }

    // Push per-rank live OS PID, active MPI_* primitive, and source/PC location into MpiDiagnosticsWidget.
    if (m_mpiDiagnosticsWidget) {
        const QString diagStatus = (state.currentState == QLatin1String("stopped"))
                                       ? QStringLiteral("PAUSED")
                                       : state.currentState.toUpper();
        const qint64 livePid = m_coordinator ? m_coordinator->getPidForRank(rankId) : 0;
        QString activePrim = state.currentFunction.isEmpty() ? QStringLiteral("main") : state.currentFunction;
        for (const auto& fr : m_cachedStackFrames.value(rankId)) {
            if (fr.function.startsWith(QStringLiteral("MPI_")) ||
                fr.function.startsWith(QStringLiteral("PMPI_")) ||
                fr.function.contains(QStringLiteral("barrierWait"))) {
                activePrim = fr.function;
                break;
            }
        }
        QString locStr;
        if (!state.currentFile.isEmpty() && state.currentLine > 0) {
            locStr = QStringLiteral("%1:%2 (0x%3)")
                         .arg(QFileInfo(state.currentFile).fileName())
                         .arg(state.currentLine)
                         .arg(pcOk ? pcVal : 0ULL, 8, 16, QChar('0'));
        } else if (diagStatus == QLatin1String("RUNNING") && (!pcOk || pcVal == 0ULL)) {
            locStr = QStringLiteral("running");
        } else {
            locStr = QStringLiteral("0x%1").arg(pcOk ? pcVal : 0ULL, 8, 16, QChar('0'));
        }
        m_mpiDiagnosticsWidget->updateLiveRankRow(
            rankId, livePid, diagStatus.isEmpty() ? QStringLiteral("IDLE") : diagStatus,
            activePrim, QStringLiteral("LIVE"), locStr,
            m_coordinator ? std::max(1, m_coordinator->getProcessCount()) : 1);
    }

    // Synchronize detail inspection views when the updated rank is currently focused.
    if (rankId == m_focusedRank) {
        if (m_disassemblyView && !state.disassemblyText.isEmpty() &&
            (!m_useRealBinaryTarget || !pcOk || pcVal == 0ULL ||
             m_disassemblyView->lineForAddress(pcVal) < 0)) {
            m_disassemblyView->updateDisassembly(state.disassemblyText);
        }
        // Highlight the active hardware instruction address in both Linear ASM and CFG views.
        if (m_disassemblyView && pcOk && pcVal != 0ULL) {
            m_disassemblyView->setCurrentAddress(pcVal);
            m_disassemblyView->setActiveSteppingRank(rankId, pcVal);
        }
        updateStatusTelemetry(
            pcAddr, state.currentState.isEmpty() ? m_currentTargetState : state.currentState.toUpper());
        refreshStatusBarMemoryTelemetry();
        // Switch active tab and center PC line ONLY on genuine execution stop transitions.
        if (state.currentState == QLatin1String("stopped") && state.currentLine > 0) {
            if (isNewStopTransition && !m_suppressExecutionTabSwitch) {
                SourceCodeView* view =
                    (!state.currentFile.isEmpty() && QFileInfo::exists(state.currentFile) && m_editorTabManager)
                        ? m_editorTabManager->openFile(state.currentFile)
                        : getSourceCodeView();
                if (view) {
                    syncTabBreakpointMetadata(view);
                    view->highlightCurrentLine(state.currentLine);
                    view->highlightExecutionLine(state.currentLine);
                }
            } else if (m_editorTabManager && !state.currentFile.isEmpty()) {
                if (SourceCodeView* stoppedView = m_editorTabManager->getSourceCodeViewForFile(state.currentFile)) {
                    stoppedView->setExecutionLineOnly(state.currentLine);
                }
            }
        }
        if (m_registerView && !state.registers.isEmpty()) m_registerView->updateRegisters(state);
    }

    // Update cross-rank variable matrix values for differential comparison.
    if (m_differentialGrid) m_differentialGrid->setVariableData(rankId, state.variableWatches);

    // Automatically unlatch Run Target when every rank in an active session has exited cleanly.
    if (m_debugSessionActive && m_coordinator && m_coordinator->getProcessCount() > 0) {
        bool allExited = true;
        for (int r = 0; r < m_coordinator->getProcessCount(); ++r) {
            if (!m_latestStates.contains(r) ||
                m_latestStates[r].currentState != QLatin1String("exited")) {
                allExited = false;
                break;
            }
        }
        if (allExited) {
            m_debugSessionActive = false;
            m_cachedStackFrames.clear();
            m_cachedThreads.clear();
            m_selectedFrameLevel.clear();
            if (m_callStackDockWidget) m_callStackDockWidget->clearFrames();
            updateDebugToolbarState();
            if (SourceCodeView* sv = getSourceCodeView()) sv->highlightExecutionLine(-1);
            updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("EXITED (0)"));
        }
    }
}

/**
 * @brief Focuses inspection views (Disassembly, Registers, Call Stack, Variables, Source) on a selected MPI rank.
 * @param rankId The target 0-based rank index selected by the user.
 */
void MainWindow::onRankSelected(int rankId) {
    // Track newly focused rank and synchronize VariablesDockWidget, CallStackDockWidget, and EditorTabManager.
    m_focusedRank = rankId;
    if (m_serverRackView && m_serverRackView->selectedRank() != rankId) { m_serverRackView->blockSignals(true); m_serverRackView->selectRank(rankId); m_serverRackView->blockSignals(false); }
    updateDebugToolbarState(); syncExecutionScopeUi();
    if (m_editorTabManager) m_editorTabManager->setActiveFocusRank(rankId);
    if (m_variablesDockWidget) {
        if (auto* rankCombo = m_variablesDockWidget->findChild<QComboBox*>()) {
            const int idx = rankCombo->findData(rankId);
            if (idx >= 0 && rankCombo->currentIndex() != idx) rankCombo->setCurrentIndex(idx);
        }
    }
    if (m_callStackDockWidget) {
        // Hydrate thread selector and stack frame rows from the newly focused rank's cache.
        m_callStackDockWidget->setThreads(m_cachedThreads.value(rankId));
        m_callStackDockWidget->setStackFrames(m_cachedStackFrames.value(rankId),
                                              m_selectedFrameLevel.value(rankId, 0));
    }

    if (m_latestStates.count(rankId)) {
        // Extract cached RankState and active $rip / $pc address for the selected rank.
        const auto& state = m_latestStates[rankId];
        const QString pcAddr = state.registers.value(16, m_currentSeekOrPc);
        const QString pcToken = pcAddr.section(QLatin1Char(' '), 0, 0);
        bool pcOk = false;
        const uint64_t pcVal = pcToken
                                   .mid(pcToken.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 2 : 0)
                                   .toULongLong(&pcOk, 16);

        // Synchronize status bar PC, rank state badge, and live /proc RSS/stack telemetry.
        updateStatusTelemetry(
            pcAddr, state.currentState.isEmpty() ? m_currentTargetState : state.currentState.toUpper());
        refreshStatusBarMemoryTelemetry();
        // Update DisassemblyView text when running without a static ELF dump or outside mapped lines.
        if (m_disassemblyView && !state.disassemblyText.isEmpty() &&
            (!m_useRealBinaryTarget || !pcOk || pcVal == 0ULL ||
             m_disassemblyView->lineForAddress(pcVal) < 0)) {
            m_disassemblyView->updateDisassembly(state.disassemblyText);
        }
        // Highlight the selected rank's current instruction address in Linear ASM and CFG views.
        if (m_disassemblyView && pcOk && pcVal != 0ULL) {
            m_disassemblyView->setCurrentAddress(pcVal);
            m_disassemblyView->setActiveSteppingRank(rankId, pcVal);
        }
        // When the focused rank is halted, refresh its disassembly fallback, 64-frame backtrace, threads, and editor PC line.
        if (state.currentState == QLatin1String("stopped")) {
            if (state.disassemblyText.isEmpty()) {
                if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) gdbCoord->requestDisassemblyFallback(rankId);
            }
            if (m_coordinator) m_coordinator->requestStackAndThreads(rankId);
            if (state.currentLine > 0) {
                SourceCodeView* view =
                    (!state.currentFile.isEmpty() && QFileInfo::exists(state.currentFile) && m_editorTabManager)
                        ? m_editorTabManager->openFile(state.currentFile)
                        : getSourceCodeView();
                if (view) {
                    syncTabBreakpointMetadata(view);
                    view->highlightCurrentLine(state.currentLine);
                    view->highlightExecutionLine(state.currentLine);
                }
            }
        }
        // Push cached watch expressions and CPU register bank values into their respective docks.
        if (m_differentialGrid) m_differentialGrid->setVariableData(rankId, state.variableWatches);
        if (m_registerView && !state.registers.isEmpty()) m_registerView->updateRegisters(state);
    } else {
        // Refresh baseline status bar telemetry without querying GDB while rank state is not stopped.
        updateStatusTelemetry();
        refreshStatusBarMemoryTelemetry();
    }
}

/**
 * @brief Connects `CallStackDockWidget` and `m_coordinator` stack/thread signals for caller frame switching.
 *
 * Synchronizes 64-frame call stacks, OS/OpenMP thread selectors, caller-frame DWARF `file:line`
 * jumps, `DisassemblyView` `$pc` updates, and live `MPI_*` primitive telemetry in `MpiDiagnosticsWidget`.
 */
void MainWindow::bindCallStackCoordinatorRoutes() {
    // Sync paused locals via QSortFilterProxyModel (survives setCoordinator model swaps) with QPointer shutdown guard.
    if (m_variablesDockWidget && !m_variablesDockWidget->property("gl_inline_var_bound").toBool()) {
        m_variablesDockWidget->setProperty("gl_inline_var_bound", true);
        auto* tree = m_variablesDockWidget->findChild<QTreeView*>();
        if (auto* proxy = tree ? qobject_cast<QAbstractProxyModel*>(tree->model()) : nullptr) {
            QPointer<EditorTabManager> etmPtr(m_editorTabManager); auto* evalTimer = new QTimer(m_variablesDockWidget); evalTimer->setSingleShot(true); evalTimer->setInterval(120);
            auto syncVars = [this, proxy, etmPtr, evalTimer]() {
                QAbstractItemModel* src = proxy ? proxy->sourceModel() : nullptr; if (!src || !etmPtr) return;
                QMap<QString, QString> varMap;
                std::function<void(const QModelIndex&, const QString&)> collect = [&](const QModelIndex& p, const QString& pfx) { for (int r = 0; r < src->rowCount(p); ++r) { const QModelIndex idx = src->index(r, 0, p); const QString n = src->data(idx).toString().trimmed(), v = src->data(src->index(r, 1, p)).toString().trimmed(), full = pfx.isEmpty() ? n : (pfx + QLatin1Char('.') + n); if (!full.isEmpty() && !v.isEmpty()) varMap.insert(full, v); if (src->rowCount(idx) > 0) collect(idx, full); } };
                collect(QModelIndex(), QString()); etmPtr->setInlineVariableValues(varMap); evalTimer->start();
            };
            connect(evalTimer, &QTimer::timeout, this, [this, proxy, etmPtr]() {
                QAbstractItemModel* src = proxy ? proxy->sourceModel() : nullptr; auto* sv = etmPtr ? etmPtr->currentSourceCodeView() : nullptr; if (!src || !sv || !m_coordinator || sv->executionLine() <= 0 || src->rowCount() == 0) return;
                QSet<QString> roots, q; for (int r = 0; r < src->rowCount(); ++r) roots.insert(src->data(src->index(r, 0)).toString().trimmed());
                static const QRegularExpression kChainRe(QStringLiteral("\\b([A-Za-z_][A-Za-z0-9_]*(?:(?:\\.|->)[A-Za-z_][A-Za-z0-9_]*)+)\\b")); int sent = 0;
                for (int ln = std::max(1, sv->executionLine() - 15); ln <= sv->executionLine() + 15 && sent < 12; ++ln) for (auto it = kChainRe.globalMatch(sv->document()->findBlockByNumber(ln - 1).text()); it.hasNext() && sent < 12;) { const QString ex = it.next().captured(1), root = ex.section(QRegularExpression(QStringLiteral("\\.|->")), 0, 0); if (!q.contains(ex) && roots.contains(root)) { q.insert(ex); ++sent; m_coordinator->evaluateExpression(m_focusedRank, ex); } }
            });
            connect(proxy, &QAbstractItemModel::modelReset, this, syncVars); connect(proxy, &QAbstractItemModel::rowsInserted, this, [syncVars](const QModelIndex&, int, int) { syncVars(); });
        }
    }
    if (m_coordinator && !m_coordinator->property("gl_inline_eval_bound").toBool()) {
        m_coordinator->setProperty("gl_inline_eval_bound", true); QPointer<EditorTabManager> etmPtr(m_editorTabManager);
        connect(m_coordinator, &IBackendCoordinator::expressionEvaluated, this, [this, etmPtr](int rank, const QString& expr, const QString& val) { if (rank == m_focusedRank && etmPtr && (expr.contains(QLatin1Char('.')) || expr.contains(QStringLiteral("->"))) && !val.startsWith(QStringLiteral("Error:"))) if (auto* sv = etmPtr->currentSourceCodeView()) sv->mergeInlineVariableValue(expr, val); });
    }
    if (m_pointerGraphDialog) { m_pointerGraphDialog->setCoordinator(m_coordinator); m_pointerGraphDialog->setFocusedRank(m_focusedRank); }
    // Wire UI -> Coordinator signals once on `m_callStackDockWidget` (routing dynamically via `m_coordinator`).
    if (m_callStackDockWidget && !m_callStackDockWidget->property("gl_callstack_bound").toBool()) {
        m_callStackDockWidget->setProperty("gl_callstack_bound", true);

        // Switching OS/OpenMP threads resets frame level to #0 and queries that thread's stack and locals when stopped.
        connect(m_callStackDockWidget, &CallStackDockWidget::threadSelected, this, [this](int threadId) {
            m_selectedFrameLevel[m_focusedRank] = 0;
            const auto stIt = m_latestStates.find(m_focusedRank);
            if (m_coordinator && stIt != m_latestStates.end() && stIt->second.currentState == QLatin1String("stopped")) m_coordinator->selectStackFrame(m_focusedRank, threadId, 0);
        });
        // Activating a caller frame jumps SourceCodeView + DisassemblyView and re-queries frame locals/registers when stopped.
        connect(m_callStackDockWidget, &CallStackDockWidget::frameActivated, this, [this](int threadId, int frameLevel, const QString& file, int line, quint64 address) {
            m_selectedFrameLevel[m_focusedRank] = frameLevel;
            if (!file.isEmpty() && QFileInfo::exists(file) && line > 0) {
                SourceCodeView* view = m_editorTabManager ? m_editorTabManager->openFile(file) : getSourceCodeView();
                if (view) { view->highlightCurrentLine(line); view->highlightExecutionLine(line); }
            }
            if (m_disassemblyView && address != 0ULL) { m_disassemblyView->setCurrentAddress(address); m_disassemblyView->setActiveSteppingRank(m_focusedRank, address); }
            const auto stIt = m_latestStates.find(m_focusedRank);
            if (m_coordinator && stIt != m_latestStates.end() && stIt->second.currentState == QLatin1String("stopped")) m_coordinator->selectStackFrame(m_focusedRank, threadId, frameLevel);
        });
    }

    if (!m_coordinator) return;

    // Wire Coordinator -> UI signals for 64-frame backtraces, MPI primitive telemetry, and thread lists.
    connect(m_coordinator, &IBackendCoordinator::stackTraceUpdated, this,
            [this](int rank, const QVector<StackFrameEntry>& frames) {
                // Empty frame broadcast with zero active processes signals full session teardown.
                if (frames.isEmpty() && m_coordinator && m_coordinator->getProcessCount() == 0) {
                    m_cachedStackFrames.clear(); m_cachedThreads.clear(); m_selectedFrameLevel.clear();
                    if (m_callStackDockWidget) m_callStackDockWidget->clearFrames();
                    return;
                }
                // Cache unwound frames per rank and update CallStackDock + MemView profiler if focused.
                m_cachedStackFrames[rank] = frames;
                if (rank == m_focusedRank && m_callStackDockWidget) m_callStackDockWidget->setStackFrames(frames, m_selectedFrameLevel.value(rank, 0));
                if (rank == m_focusedRank && m_memView && m_coordinator && m_memView->isMemoryMapMode() && (m_memView->isVisible() || m_memView->isPoppedOut())) {
                    m_memView->notifyRankStoppedOrSelected(rank, m_coordinator->getPidForRank(rank), std::max(1, m_coordinator->getProcessCount()));
                }

                // Surface active MPI_* primitive and DWARF file:line in MpiDiagnosticsWidget.
                if (!frames.isEmpty() && m_mpiDiagnosticsWidget) {
                    const StackFrameEntry* mpiFrame = &frames.first();
                    for (int i = 0; i < std::min<int>(frames.size(), 8); ++i) {
                        if (frames[i].function.startsWith(QStringLiteral("MPI_")) ||
                            frames[i].function.startsWith(QStringLiteral("PMPI_")) ||
                            frames[i].function.contains(QStringLiteral("barrierWait"))) {
                            mpiFrame = &frames[i]; break;
                        }
                    }
                    const qint64 livePid = m_coordinator ? m_coordinator->getPidForRank(rank) : 0;
                    const QString locStr = (!mpiFrame->file.isEmpty() && mpiFrame->line > 0)
                        ? QStringLiteral("%1:%2 (0x%3)").arg(QFileInfo(mpiFrame->file).fileName()).arg(mpiFrame->line).arg(mpiFrame->address, 8, 16, QChar('0'))
                        : QStringLiteral("0x%1").arg(mpiFrame->address, 8, 16, QChar('0'));
                    m_mpiDiagnosticsWidget->updateLiveRankRow(
                        rank, livePid, QStringLiteral("PAUSED"),
                        mpiFrame->function.isEmpty() ? QStringLiteral("main") : mpiFrame->function,
                        QStringLiteral("LIVE"), locStr, m_coordinator ? std::max(1, m_coordinator->getProcessCount()) : 1);
                }
            });

    // Update per-rank OS/OpenMP thread cache and repopulate `#threadSelectorCombo` for the focused rank.
    connect(m_coordinator, &IBackendCoordinator::threadsUpdated, this,
            [this](int rank, const QVector<ThreadEntry>& threads) {
                if (threads.isEmpty() && m_coordinator && m_coordinator->getProcessCount() == 0) return;
                m_cachedThreads[rank] = threads;
                if (rank == m_focusedRank && m_callStackDockWidget) {
                    m_callStackDockWidget->setThreads(threads);
                }
            });
}

/**
 * @brief Binds `GdbConsoleWidget` REPL dispatch, hardware data watchpoint triggers, and `WATCH` dock controls.
 *
 * Routes user-submitted debugger commands through `IBackendCoordinator::sendDebuggerCommand`
 * (backed by `MiSanitizer`), installs the Write/Read/R/W hardware watchpoint controls in
 * `WatchExpressionsDock`, and surfaces `watchpointHit` notifications across the workstation.
 */
void MainWindow::bindConsoleAndWatchpointRoutes() {
    // Route interactive GdbConsoleWidget prompt commands through MiSanitizer-backed sendDebuggerCommand.
    if (m_gdbConsoleWidget && !m_gdbConsoleWidget->property("gl_repl_bound").toBool()) {
        m_gdbConsoleWidget->setProperty("gl_repl_bound", true);
        connect(m_gdbConsoleWidget, &GdbConsoleWidget::commandEntered, this,
                [this](int rank, const QString& command) {
                    if (m_coordinator) m_coordinator->sendDebuggerCommand(rank, command);
                });
    }

    // Augment WatchExpressionsDock toolbar once with Hardware Watchpoint mode selector + button.
    if (auto* watchDock = findChild<QDockWidget*>(QStringLiteral("WatchExpressionsDock"))) {
        if (!watchDock->property("gl_wp_controls_bound").toBool()) {
            watchDock->setProperty("gl_wp_controls_bound", true);
            if (auto* watchInput = watchDock->findChild<QLineEdit*>()) {
                if (auto* barLayout = qobject_cast<QHBoxLayout*>(watchInput->parentWidget() ? watchInput->parentWidget()->layout() : nullptr)) {
                    // Populate Write / Read / ReadWrite CPU debug-register access modes.
                    auto* modeCombo = new QComboBox(watchInput->parentWidget());
                    modeCombo->setObjectName(QStringLiteral("watchpointAccessCombo"));
                    modeCombo->setFixedHeight(22);
                    modeCombo->addItem(QStringLiteral("Write"), static_cast<int>(WatchpointAccessMode::Write));
                    modeCombo->addItem(QStringLiteral("Read"), static_cast<int>(WatchpointAccessMode::Read));
                    modeCombo->addItem(QStringLiteral("R/W"), static_cast<int>(WatchpointAccessMode::ReadWrite));

                    // Create `+ Watchpoint` button to arm hardware watchpoints and add a diff-grid column.
                    auto* wpBtn = new QPushButton(QStringLiteral("+ Watchpoint"), watchInput->parentWidget());
                    wpBtn->setObjectName(QStringLiteral("addHardwareWatchpointBtn")); wpBtn->setFixedHeight(22);
                    connect(wpBtn, &QPushButton::clicked, this, [this, watchInput, modeCombo]() {
                        const QString expr = watchInput->text().trimmed();
                        if (expr.isEmpty()) return;
                        const auto mode = static_cast<WatchpointAccessMode>(modeCombo->currentData().toInt());
                        if (m_coordinator) m_coordinator->setDataWatchpoint(expr, mode, -1);
                        if (m_differentialGrid) m_differentialGrid->addVariableColumn(expr);
                        watchInput->clear();
                    });
                    barLayout->addWidget(modeCombo); barLayout->addWidget(wpBtn);
                    updateDebugToolbarState();
                }
            }
        }
    }
    if (!m_coordinator) return;

    // Stream REPL console evaluation replies and MiSanitizer diagnostics into GdbConsoleWidget.
    if (m_gdbConsoleWidget) {
        connect(m_coordinator, &IBackendCoordinator::debuggerConsoleOutput,
                m_gdbConsoleWidget, &GdbConsoleWidget::appendDebuggerConsoleOutput);
    }

    // Surface hardware data watchpoint triggers across console, network log, and focused rank views.
    // Automatically switches workstation focus to the rank whose memory trap fired.
    connect(m_coordinator, &IBackendCoordinator::watchpointHit, this,
            [this](int rank, const QString& expr, const QString& oldVal, const QString& newVal) {
                // Format human-readable old -> new transition string for console and telemetry banners.
                const QString summary =
                    QStringLiteral("Hardware watchpoint '%1' hit: %2 -> %3").arg(expr, oldVal, newVal);
                if (m_gdbConsoleWidget) {
                    m_gdbConsoleWidget->appendDebuggerConsoleOutput(rank, summary, false);
                }
                if (networkLog()) {
                    networkLog()->appendText(QStringLiteral("WATCHPOINT"), summary + QLatin1Char('\n'));
                }
                // Focus the halted rank and latch status bar state to highlight the watchpoint hit.
                onRankSelected(rank);
                updateStatusTelemetry(m_currentSeekOrPc,
                                      QStringLiteral("WATCHPOINT (%1: %2 -> %3)").arg(expr, oldVal, newVal));
            });
}

/**
 * @brief Binds `MemView` live rank address evaluation and `process_vm_readv` + debugger fallback callbacks.
 *
 * Enables `MemView` to resolve per-rank register/symbol expressions (such as `$rsp`), read raw
 * target rank memory via `NativeMemoryReader` (`process_vm_readv`), and fall back seamlessly to
 * `IBackendCoordinator::readMemory` (`-data-read-memory-bytes` / DAP `readMemory`) when
 * `kernel.yama.ptrace_scope` restricts sibling `process_vm_readv` calls.
 */
void MainWindow::bindLiveMemViewHandlers() {
    if (!m_memView) return;

    // Resolve hex addresses, cached x86_64 registers (`$rsp`, `$rbp`, `$rip`), or live expressions per rank.
    // Returns 0 and sets `*okOut = false` whenever the expression cannot be resolved to a valid VA.
    auto resolver = [this](int rank, const QString& rawExpr, bool* okOut) -> uint64_t {
        if (okOut) *okOut = false;
        const QString expr = rawExpr.trimmed();
        if (expr.isEmpty()) return 0ULL;

        // 1. Parse direct hexadecimal literal (`0x...` or bare hex digits) without round-tripping to GDB.
        if (!expr.startsWith(QLatin1Char('$')) && !expr.startsWith(QLatin1Char('&'))) {
            const QString clean = expr.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? expr.mid(2) : expr;
            bool hexOk = false;
            const uint64_t val = clean.toULongLong(&hexOk, 16);
            if (hexOk && val != 0ULL) {
                if (okOut) *okOut = true;
                return val;
            }
        }

        // 2. Resolve `$rsp` / `$rbp` / `$rip` / general-purpose registers from cached per-rank register map.
        // Slot indices correspond to canonical GDB x86_64 register numbers (0=rax .. 7=rsp, 16=rip).
        if (expr.startsWith(QLatin1Char('$')) && m_latestStates.count(rank)) {
            const QString reg = expr.mid(1).toLower();
            int slot = -1;
            if (reg == QLatin1String("rsp") || reg == QLatin1String("sp")) slot = 7;
            else if (reg == QLatin1String("rbp") || reg == QLatin1String("fp")) slot = 6;
            else if (reg == QLatin1String("rip") || reg == QLatin1String("pc")) slot = 16;
            else if (reg == QLatin1String("rax")) slot = 0;
            else if (reg == QLatin1String("rbx")) slot = 1;
            else if (reg == QLatin1String("rcx")) slot = 2;
            else if (reg == QLatin1String("rdx")) slot = 3;
            else if (reg == QLatin1String("rsi")) slot = 4;
            else if (reg == QLatin1String("rdi")) slot = 5;
            if (slot >= 0) {
                const QString tok = m_latestStates[rank].registers.value(slot).section(QLatin1Char(' '), 0, 0);
                const QString hexTok = tok.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? tok.mid(2) : tok;
                bool regOk = false;
                const uint64_t regVal = hexTok.toULongLong(&regOk, 16);
                if (regOk && regVal != 0ULL) {
                    if (okOut) *okOut = true;
                    return regVal;
                }
            }
        }

        // 3. Evaluate arbitrary register/pointer expression via active debugger coordinator when stopped.
        // Uses a local single-shot 1200 ms timeout guard so an unresponsive target cannot hang the GUI.
        const auto stIt = m_latestStates.find(rank);
        if (m_debugSessionActive && m_coordinator && stIt != m_latestStates.end() && stIt->second.currentState == QLatin1String("stopped")) {
            uint64_t evalAddr = 0ULL;
            bool evalOk = false;
            QEventLoop loop; QTimer timer; timer.setSingleShot(true);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            const QMetaObject::Connection conn = QObject::connect(m_coordinator, &IBackendCoordinator::expressionEvaluated, &loop, [&](int rId, const QString&, const QString& result) {
                if (rId != rank) return;
                static const QRegularExpression hexRe(QStringLiteral("0x([0-9a-fA-F]+)"));
                if (const auto m = hexRe.match(result); m.hasMatch()) evalAddr = m.captured(1).toULongLong(&evalOk, 16);
                loop.quit();
            });
            timer.start(1200); m_coordinator->evaluateExpression(rank, expr); loop.exec(); QObject::disconnect(conn);
            if (evalOk && evalAddr != 0ULL) { if (okOut) *okOut = true; return evalAddr; }
        }
        return 0ULL;
    };

    // Read raw bytes from `rank` via `NativeMemoryReader` (`process_vm_readv`), falling back to GDB/DAP.
    // Fast-path avoids MI serialization overhead when ptrace_scope allows direct sibling VM reads.
    auto fetcher = [this](int rank, uint64_t addr, int length) -> QByteArray {
        if (!m_debugSessionActive || !m_coordinator || addr == 0ULL || length <= 0) return {};
        const qint64 pid = m_coordinator->getPidForRank(rank);
        if (pid > 0) {
            try {
                // Round requested byte span up to 8-byte double words for aligned process_vm_readv transfer.
                const std::vector<double> words = NativeMemoryReader::readDoubles(static_cast<pid_t>(pid), static_cast<uintptr_t>(addr), static_cast<size_t>((length + 7) / 8));
                if (!words.empty()) { QByteArray out(length, Qt::Uninitialized); std::memcpy(out.data(), words.data(), static_cast<size_t>(length)); return out; }
            } catch (...) {
                // Fall through to debugger-tracer memory read when `yama/ptrace_scope` blocks sibling readv.
            }
        }

        // Tracer fallback: request raw memory bytes directly from GDB/DAP only when the rank is stopped.
        const auto stIt = m_latestStates.find(rank);
        if (stIt == m_latestStates.end() || stIt->second.currentState != QLatin1String("stopped")) return {};
        QByteArray fallbackBytes; QEventLoop loop; QTimer timer; timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        m_suppressMemViewBroadcast = true;
        const QMetaObject::Connection conn = QObject::connect(m_coordinator, &IBackendCoordinator::memoryRead, &loop, [&](int rId, const QString&, const QByteArray& data) {
            if (rId != rank) return;
            fallbackBytes = data; loop.quit();
        });
        timer.start(1500); m_coordinator->readMemory(rank, QStringLiteral("0x%1").arg(addr, 0, 16), length); loop.exec();
        QObject::disconnect(conn); m_suppressMemViewBroadcast = false;
        if (fallbackBytes.size() > length) fallbackBytes.truncate(length);
        return fallbackBytes;
    };

    // Expose live inferior OS PID lookup per MPI rank so MemView can display and verify distinct PIDs.
    auto pidGetter = [this](int rank) -> qint64 { return (m_debugSessionActive && m_coordinator) ? m_coordinator->getPidForRank(rank) : 0LL; };
    m_memView->setLiveMemoryCallbacks(resolver, fetcher, pidGetter);

    // Ensure File -> Attach to Process... exposes #actionAttachProcess and default Ctrl+Alt+P shortcut.
    if (menuBar()) {
        for (QAction* topAct : menuBar()->actions()) {
            if (QMenu* menu = topAct->menu()) {
                for (QAction* act : menu->actions()) {
                    if (act->text().contains(QStringLiteral("Attach to Process"), Qt::CaseInsensitive)) {
                        act->setObjectName(QStringLiteral("actionAttachProcess"));
                        if (act->shortcut().isEmpty()) act->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_P));
                        act->disconnect(this);
                        connect(act, &QAction::triggered, this, [this]() { attachToRunningProcesses(); });
                    }
                }
            }
        }
    }
}

bool MainWindow::attachToProcessWorkflow(qint64 explicitPid) {
    return attachToRunningProcesses(explicitPid > 0 ? QList<qint64>{explicitPid} : QList<qint64>{});
}

bool MainWindow::attachToRunningProcesses(const QList<qint64>& explicitPids) {
    QList<ProcessEntry> targets;
    if (explicitPids.isEmpty()) {
        AttachProcessDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted) return false;
        if (dlg.isRemoteTargetTabActive()) {
            return attachToRemoteTargets(dlg.selectedRemoteTargets());
        }
        targets = dlg.selectedProcesses();
    } else {
        for (int i = 0; i < explicitPids.size(); ++i) {
            if (explicitPids[i] <= 0) continue;
            ProcessEntry e;
            e.pid = static_cast<pid_t>(explicitPids[i]);
            e.mpiRank = i;
            e.mpiWorldSize = explicitPids.size();
            e.executablePath = QFileInfo(QStringLiteral("/proc/%1/exe").arg(e.pid)).symLinkTarget();
            targets.append(e);
        }
    }
    if (targets.isEmpty()) return false;

    if (m_debugSessionActive) stopTargetWorkflow();
    syncCoordinatorWithConfig();
    if (!m_coordinator) return false;

    bool attached = false;
    if (auto* gdbCoord = qobject_cast<GdbRankCoordinator*>(m_coordinator)) {
        attached = gdbCoord->attachToProcesses(targets);
    } else if (auto* dapCoord = qobject_cast<DapCoordinator*>(m_coordinator)) {
        attached = dapCoord->attachToProcesses(targets);
    } else {
        attached = m_coordinator->attachToProcess(static_cast<qint64>(targets.first().pid));
    }
    if (!attached) return false;

    const int rankCount = targets.size();
    m_debugSessionActive = true;
    m_focusedRank = 0;
    m_latestStates.clear();
    QList<RankState> rankStates;
    if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->beginLiveSession(rankCount);
    for (int r = 0; r < rankCount; ++r) {
        RankState st; st.rankId = r; st.currentState = QStringLiteral("stopped");
        m_latestStates[r] = st; rankStates.append(st);
        if (m_mpiDiagnosticsWidget) {
            m_mpiDiagnosticsWidget->updateLiveRankRow(
                r, static_cast<qint64>(targets[r].pid), QStringLiteral("PAUSED"),
                QStringLiteral("attached"), QStringLiteral("LOCAL PID"),
                QStringLiteral("0x00000000"), rankCount);
        }
    }
    if (m_serverRackView) m_serverRackView->updateRanks(rankStates);
    if (m_memView) m_memView->setSourceModeLabel(QStringLiteral("LIVE RANK 0 (PID %1)").arg(targets.first().pid));
    if (m_gdbConsoleWidget) m_gdbConsoleWidget->ensureRankCount(rankCount);
    m_coordinator->requestStackAndThreads(0);
    updateDebugToolbarState();
    updateStatusTelemetry(QStringLiteral("0x00000000"),
                          QStringLiteral("ATTACHED (%1 RANK%2)").arg(rankCount).arg(rankCount > 1 ? QStringLiteral("S") : QString()));
    refreshStatusBarMemoryTelemetry();
    return true;
}

bool MainWindow::attachToRemoteTargets(const QList<RemoteTargetSpec>& explicitTargets) {
    if (explicitTargets.isEmpty()) return false;
    if (m_debugSessionActive) stopTargetWorkflow();
    gridlock::core::ConfigManager::instance().setDebuggerEngine(QStringLiteral("gdb"));
    syncCoordinatorWithConfig();
    auto* gdbCoord = qobject_cast<GdbRankCoordinator*>(m_coordinator);
    if (!gdbCoord || !gdbCoord->attachToRemoteTargets(explicitTargets)) return false;

    const int rankCount = explicitTargets.size();
    m_debugSessionActive = true; m_focusedRank = 0; m_latestStates.clear();
    QList<RankState> rankStates;
    if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->beginLiveSession(rankCount);
    for (int r = 0; r < rankCount; ++r) {
        RankState st; st.rankId = r; st.currentState = QStringLiteral("stopped");
        m_latestStates[r] = st; rankStates.append(st);
        if (m_mpiDiagnosticsWidget) m_mpiDiagnosticsWidget->updateLiveRankRow(r, 0, QStringLiteral("PAUSED"), QStringLiteral("remote"), explicitTargets[r].endpoint, QStringLiteral("0x00000000"), rankCount);
    }
    if (m_serverRackView) m_serverRackView->updateRanks(rankStates);
    if (m_memView) m_memView->setSourceModeLabel(QStringLiteral("REMOTE RANK 0 (%1)").arg(explicitTargets.first().endpoint));
    if (m_gdbConsoleWidget) m_gdbConsoleWidget->ensureRankCount(rankCount);
    m_coordinator->requestStackAndThreads(0);
    updateDebugToolbarState();
    updateStatusTelemetry(QStringLiteral("0x00000000"), QStringLiteral("REMOTE (%1)").arg(explicitTargets.first().endpoint));
    refreshStatusBarMemoryTelemetry();
    return true;
}

/**
 * @brief Records per-rank breakpoint metadata and stages rank-filter hints for coordinator insertion.
 */
void MainWindow::onRankBreakpointChanged(const QString& file, int line, bool isSet, int rankFilter, const QString& condition) {
    if (file.isEmpty() || line <= 0) return;
    const QFileInfo fi(file); const QString absPath = fi.absoluteFilePath(), canonPath = fi.canonicalFilePath().isEmpty() ? absPath : fi.canonicalFilePath();
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

/**
 * @brief Restores cached per-rank breakpoint colors/conditions onto an open SourceCodeView tab.
 */
void MainWindow::syncTabBreakpointMetadata(SourceCodeView* view) const {
    if (!view || view->currentFile().isEmpty()) return;
    const QFileInfo fi(view->currentFile()); const QString absPath = fi.absoluteFilePath(), canonPath = fi.canonicalFilePath().isEmpty() ? absPath : fi.canonicalFilePath();
    QMap<int, SourceCodeView::BreakpointVisualInfo> meta;
    const QMap<int, BreakpointRankMeta>& stored = m_breakpointRankMeta.contains(canonPath) ? m_breakpointRankMeta[canonPath] : m_breakpointRankMeta.value(absPath);
    if (stored.isEmpty()) return;
    // Restore rank filters and condition expressions onto all open breakpoint lines.
    for (int line : view->breakpoints()) { const auto s = stored.value(line, BreakpointRankMeta{-1, view->breakpointCondition(line)}); meta[line] = SourceCodeView::BreakpointVisualInfo{s.rankFilter, s.condition, 0, QString()}; }
    for (auto it = stored.constBegin(); it != stored.constEnd(); ++it) meta[it.key()] = SourceCodeView::BreakpointVisualInfo{it.value().rankFilter, it.value().condition, 0, QString()};
    view->syncBreakpointMetadata(meta);
}

namespace {
QStringList collectExpressionCandidates(MainWindow* mw) {
    QStringList out; QSet<QString> seen;
    auto push = [&](const QString& raw) { const QString s = raw.trimmed(); if (!s.isEmpty() && !seen.contains(s)) { seen.insert(s); out.append(s); } };
    if (!mw) return out;
    if (auto* model = mw->variablesDockWidget() ? mw->variablesDockWidget()->findChild<VariableTreeModel*>() : nullptr) {
        auto walk = [&](auto& self, const QModelIndex& parent, int depth) -> void {
            if (depth > 3) return;
            for (int r = 0, rows = model->rowCount(parent); r < rows; ++r) {
                const QModelIndex idx = model->index(r, 0, parent);
                const QString expr = model->expressionPath(idx), name = model->data(idx).toString(), type = model->data(model->index(r, 2, parent)).toString();
                push(expr); push(name);
                if (!expr.isEmpty() && !expr.endsWith(QLatin1Char(']')) && (type.contains(QStringLiteral("vector")) || type.contains(QStringLiteral("array")) || type.contains(QLatin1Char('[')) || type.contains(QLatin1Char('*')))) push(expr + QStringLiteral("[0]"));
                self(self, idx, depth + 1);
            }
        };
        walk(walk, QModelIndex(), 0);
    }
    const QList<SourceCodeView*> views = mw->editorTabManager() ? mw->editorTabManager()->openViews() : (mw->sourceCodeView() ? QList<SourceCodeView*>{mw->sourceCodeView()} : QList<SourceCodeView*>{});
    static const QRegularExpression kTypeRe(QStringLiteral(R"(\b(?:struct|class)\s+([A-Za-z_][A-Za-z0-9_]*)\b)"));
    static const QRegularExpression kVecDeclRe(QStringLiteral(R"(\b(?:(?:vector|array)\s*<[^>;]+>\s+([A-Za-z_][A-Za-z0-9_]*)|[A-Za-z_][A-Za-z0-9_]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[[0-9]+\]))"));
    static const QRegularExpression kIdxRe(QStringLiteral(R"(\b([A-Za-z_][A-Za-z0-9_]*(?:\[[0-9]+\])+(?:\.[A-Za-z_][A-Za-z0-9_]*)*))"));
    static const QRegularExpression kDeclRe(QStringLiteral(R"(^\s*(?:const\s+)?(?:std::)?[A-Za-z_][A-Za-z0-9_:<>*&\s]*?\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\]|\(|=|\{|;))"), QRegularExpression::MultilineOption);
    for (SourceCodeView* sv : views) {
        if (!sv) continue;
        const QString src = sv->toPlainText();
        for (auto it = kTypeRe.globalMatch(src); it.hasNext();) push(it.next().captured(1));
        for (auto it = kVecDeclRe.globalMatch(src); it.hasNext();) { const auto m = it.next(); const QString v = !m.captured(1).isEmpty() ? m.captured(1) : m.captured(2); push(v); push(v + QStringLiteral("[0]")); }
        for (auto it = kIdxRe.globalMatch(src); it.hasNext();) push(it.next().captured(1));
        for (auto it = kDeclRe.globalMatch(src); it.hasNext();) { const QString id = it.next().captured(1); if (id != QLatin1String("return") && id != QLatin1String("if") && id != QLatin1String("for") && id != QLatin1String("while") && id != QLatin1String("main")) push(id); }
    }
    if (auto* symTbl = mw->projectExplorerWidget() ? mw->projectExplorerWidget()->findChild<QTableWidget*>(QStringLiteral("explorerSymbolsTable")) : nullptr) {
        for (int r = 0, cap = qMin(250, symTbl->rowCount()); r < cap; ++r) if (auto* it = symTbl->item(r, 2)) push(it->text());
    }
    return out;
}
void refreshDialogCompleters(MainWindow* mw) {
    if (!mw || !mw->variablesDockWidget()) return;
    if (auto* model = mw->variablesDockWidget()->findChild<VariableTreeModel*>(); model && !model->property("gl_completer_wired").toBool()) {
        model->setProperty("gl_completer_wired", true);
        QPointer<MainWindow> safeMw(mw);
        QObject::connect(model, &QAbstractItemModel::modelReset, mw, [safeMw]() { if (safeMw && safeMw->variablesDockWidget()) refreshDialogCompleters(safeMw); }, Qt::QueuedConnection);
        QObject::connect(model, &QAbstractItemModel::rowsInserted, mw, [safeMw]() { if (safeMw && safeMw->variablesDockWidget()) refreshDialogCompleters(safeMw); }, Qt::QueuedConnection);
    }
    const auto pgs = mw->findChildren<PointerGraphDialog*>(); const auto aps = mw->findChildren<ArrayPlotDialog*>();
    if (pgs.isEmpty() && aps.isEmpty()) return;
    const QStringList cands = collectExpressionCandidates(mw);
    for (auto* pg : pgs) pg->setCompletionCandidates(cands);
    for (auto* ap : aps) ap->setCompletionCandidates(cands);
}
void graphStructTypeFromSource(MainWindow* mw, const QString& typeName) {
    if (!mw || typeName.trimmed().isEmpty()) return;
    const QString cleanType = typeName.trimmed(); QStringList matchedExprs;
    if (auto* model = mw->variablesDockWidget() ? mw->variablesDockWidget()->findChild<VariableTreeModel*>() : nullptr) {
        for (int r = 0; r < model->rowCount(); ++r) {
            const QModelIndex idx = model->index(r, 0);
            const QString expr = model->expressionPath(idx), tp = model->data(model->index(r, 2)).toString();
            if (tp.contains(cleanType)) matchedExprs.append((tp.contains(QStringLiteral("vector")) || tp.contains(QStringLiteral("array")) || tp.contains(QLatin1Char('['))) ? (expr + QStringLiteral("[0]")) : expr);
        }
    }
    if (matchedExprs.isEmpty() && mw->sourceCodeView()) {
        const QString src = mw->sourceCodeView()->toPlainText();
        const QRegularExpression arrInst(QStringLiteral(R"(\b(?:(?:vector|array)\s*<\s*(?:const\s+)?%1\b[^>]*>\s+([A-Za-z_][A-Za-z0-9_]*)|%1\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[[0-9]+\]))").arg(QRegularExpression::escape(cleanType)));
        for (auto it = arrInst.globalMatch(src); it.hasNext();) { const auto m = it.next(); const QString v = !m.captured(1).isEmpty() ? m.captured(1) : m.captured(2); matchedExprs.append(v + QStringLiteral("[0]")); }
        const QRegularExpression dirInst(QStringLiteral(R"(\b%1\s*[\*&]?\s+([A-Za-z_][A-Za-z0-9_]*)\b(?!\s*\[))").arg(QRegularExpression::escape(cleanType)));
        for (auto it = dirInst.globalMatch(src); it.hasNext();) { const QString v = it.next().captured(1); if (v != cleanType && !matchedExprs.contains(v)) matchedExprs.append(v); }
    }
    mw->openPointerGraphVisualizer();
    if (auto* pg = mw->findChild<PointerGraphDialog*>(QStringLiteral("PointerGraphDialog"))) {
        for (const QString& ex : matchedExprs) pg->addAndEvaluateRootExpression(ex, cleanType);
        pg->setRootInputText(matchedExprs.isEmpty() ? cleanType : matchedExprs.first());
    }
    refreshDialogCompleters(mw);
}
void bindSourceViewGraphSignals(MainWindow* mw, SourceCodeView* sv) {
    if (!mw || !sv || sv->property("gl_graph_ctx_wired").toBool()) return;
    sv->setProperty("gl_graph_ctx_wired", true);
    QObject::connect(sv, &SourceCodeView::graphExpressionRequested, mw, [mw](const QString& expr) { mw->openPointerGraphVisualizer(expr); });
    QObject::connect(sv, &SourceCodeView::graphStructTypeRequested, mw, [mw](const QString& tp) { graphStructTypeFromSource(mw, tp); });
    QObject::connect(sv, &SourceCodeView::sendToCalculatorRequested, mw, &MainWindow::openProgrammerCalculatorWithText, Qt::UniqueConnection);
}
}  // namespace

/**
 * @brief Opens the modeless Pointer & Struct Graph Visualizer and optionally graphs @p exprPath.
 * @param exprPath Optional C++ expression path to graph immediately.
 * @param varValue Optional runtime value or pointer address hint.
 * @param varType Optional static C/C++ type hint.
 */
void MainWindow::openPointerGraphVisualizer(const QString& exprPath, const QString& varValue, const QString& varType) {
    if (!m_pointerGraphDialog) m_pointerGraphDialog = new PointerGraphDialog(m_coordinator, this);
    m_pointerGraphDialog->setCoordinator(m_coordinator); m_pointerGraphDialog->setFocusedRank(m_focusedRank);
    refreshDialogCompleters(this);
    if (!exprPath.trimmed().isEmpty()) { m_pointerGraphDialog->addAndEvaluateRootExpression(exprPath.trimmed(), varType, varValue); }
    m_pointerGraphDialog->show(); m_pointerGraphDialog->raise(); m_pointerGraphDialog->activateWindow();
}
/**
 * @brief Safely shuts down live coordinators and disconnects signals before Qt destroys child docks.
 */
MainWindow::~MainWindow() {
    m_isClosing = true;
    // Detach global shortcut/hover event filter before child widget destruction begins.
    if (qApp) qApp->removeEventFilter(this);
    delete m_deadlockAnalyzer; m_deadlockAnalyzer = nullptr; if (m_coordinator) QObject::disconnect(m_coordinator, nullptr, this, nullptr);
    // Silence and destroy owned coordinator cleanly without post-delete signal-blocker UB.
    if (m_ownedCoordinator) { m_ownedCoordinator->blockSignals(true); m_ownedCoordinator->terminateSession(); delete m_ownedCoordinator; m_ownedCoordinator = nullptr; }
    // Nullify cached dock and view pointers prior to QMainWindow child tree cleanup.
    m_coordinator = nullptr; m_callStackDockWidget = nullptr; m_mpiDiagnosticsWidget = nullptr; m_gdbConsoleWidget = nullptr; m_variablesDockWidget = nullptr; m_serverRackView = nullptr; m_projectExplorerWidget = nullptr; m_editorTabManager = nullptr; m_pointerGraphDialog = nullptr;
}
}  // namespace gridlock::ui

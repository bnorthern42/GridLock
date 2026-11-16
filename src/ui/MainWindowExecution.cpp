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
#include "../core/managers/ConfigManager.hpp"
#include "dialogs/PointerGraphDialog.hpp"
#include "dialogs/ProjectSettingsDialog.hpp"
#include "docks/CallStackDockWidget.hpp"
#include "views/MemView.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QString>
#include <QTimer>

#include <cstring>

namespace gridlock::ui {

/**
 * @brief Resolves a tutorial target name or relative path across local and AppImage share directories.
 * @param target Target name or relative path (e.g. "deadlock_ring", "tutorial/deadlock_ring.cpp").
 * @return Canonical or absolute path to the resolved tutorial source or binary file.
 *
 * Checks in order:
 * 1. Current working directory (./tutorial/<name>.cpp)
 * 2. Parent directory for builddir test execution (../tutorial/<name>.cpp)
 * 3. AppDir share directory ($APPDIR/usr/share/gridlock/tutorial/<name>.cpp)
 * 4. Application directory share path (QCoreApplication::applicationDirPath() + "/../share/gridlock/tutorial/")
 * 5. Direct AppDir directory ($APPDIR/tutorial/<name>.cpp)
 * 6. Local application binary directory (QCoreApplication::applicationDirPath() + "/tutorial/")
 */
QString resolveTutorialTarget(const QString& target) {
    QString rel = target.trimmed();
    if (rel.isEmpty()) return QString();

    // Normalize bare target names into relative tutorial source file paths.
    if (!rel.endsWith(QStringLiteral(".cpp")) && !rel.contains(QLatin1Char('/'))) {
        rel = QStringLiteral("tutorial/") + rel + QStringLiteral(".cpp");
    } else if (!rel.startsWith(QStringLiteral("tutorial/")) && !rel.startsWith(QLatin1Char('/'))) {
        rel = QStringLiteral("tutorial/") + rel;
    }

    // 1. Direct hit from current working directory (standard repository root execution).
    if (QFileInfo::exists(rel)) {
        const QFileInfo fi(rel);
        return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    }

    // 2. Secondary lookup one level up (out-of-source builddir/ test harness runner).
    const QString parentCand = QStringLiteral("../") + rel;
    if (QFileInfo::exists(parentCand)) {
        const QFileInfo fi(parentCand);
        return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    }

    // 3. AppImage deployment environment variable ($APPDIR/usr/share/gridlock/tutorial/...).
    const QString envAppDir = qEnvironmentVariable("APPDIR");
    if (!envAppDir.isEmpty()) {
        const QString appDirShare = envAppDir + QStringLiteral("/usr/share/gridlock/") + rel;
        if (QFileInfo::exists(appDirShare)) {
            const QFileInfo fi(appDirShare);
            return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
        }
        const QString appDirDirect = envAppDir + QStringLiteral("/") + rel;
        if (QFileInfo::exists(appDirDirect)) {
            const QFileInfo fi(appDirDirect);
            return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
        }
    }

    // 4. Relative to application binary dir (e.g. AppDir/usr/bin/../share/gridlock/tutorial/...).
    const QString appDirPath = QCoreApplication::applicationDirPath();
    const QString shareCand = appDirPath + QStringLiteral("/../share/gridlock/") + rel;
    if (QFileInfo::exists(shareCand)) {
        const QFileInfo fi(shareCand);
        return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    }

    // 5. Direct companion directory in binary folder (e.g. AppDir/usr/bin/tutorial/...).
    const QString localCand = appDirPath + QStringLiteral("/") + rel;
    if (QFileInfo::exists(localCand)) {
        const QFileInfo fi(localCand);
        return !fi.canonicalFilePath().isEmpty() ? fi.canonicalFilePath() : fi.absoluteFilePath();
    }

    // 6. Default to expected share or appdir path when simulating outside repository.
    if (!envAppDir.isEmpty()) {
        return envAppDir + QStringLiteral("/usr/share/gridlock/") + rel;
    }
    return shareCand;
}

/**
 * @brief Ensures a writable execution path for tutorial targets in read-only AppImage environments.
 * @param sourcePath Canonical path to tutorial source file.
 * @return Writable binary path in /tmp/gridlock_tutorials/ or direct binary path.
 */
QString ensureWritableTutorialBinaryPath(const QString& sourcePath) {
    if (sourcePath.isEmpty()) return QString();
    const QFileInfo srcInfo(sourcePath);
    const QString baseName = srcInfo.completeBaseName();
    const QString companionBin = srcInfo.dir().filePath(baseName);

    // If existing companion binary exists and directory is read-only, use it or copy to /tmp.
    if (!srcInfo.dir().exists() || !QFileInfo(srcInfo.dir().absolutePath()).isWritable()) {
        const QString cacheDir = QStringLiteral("/tmp/gridlock_tutorials");
        QDir().mkpath(cacheDir);
        const QString targetBin = cacheDir + QStringLiteral("/") + baseName;
        if (QFileInfo::exists(companionBin) && (!QFileInfo::exists(targetBin) || QFileInfo(targetBin).lastModified() < QFileInfo(companionBin).lastModified())) {
            QFile::remove(targetBin);
            QFile::copy(companionBin, targetBin);
            QFile::setPermissions(targetBin, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup);
        }
        return targetBin;
    }
    return companionBin;
}

/**
 * @brief Synchronizes `m_coordinator` with `ConfigManager::instance().debuggerEngine()` when idle.
 *
 * Instantiates `GdbRankCoordinator` by default (for `"gdb"`, `"cuda-gdb"`, and `"rocgdb"`) so
 * multi-rank MPI debugging works out of the box, or switches to `DapCoordinator` when `"lldb-dap"`
 * is selected in Preferences while no active debugging session is running.
 */
void MainWindow::syncCoordinatorWithConfig() {
    // Never hot-swap the active coordinator backend mid-session while inferiors are attached.
    if (m_debugSessionActive) return;

    auto& cfg = gridlock::core::ConfigManager::instance();
    const QString rawEngine = cfg.debuggerEngine().trimmed().toLower();
    const QString desired = (rawEngine == QStringLiteral("lldb-dap"))
                                ? QStringLiteral("lldb-dap")
                                : QStringLiteral("gdb");

    auto pushExecutionContext = [&]() {
        if (!m_coordinator) return;
        const auto ps = cfg.loadProjectSettings();
        const QString args = !ps.targetArguments.isEmpty() ? ps.targetArguments : QString::fromStdString(ps.programArguments);
        QString wd = QString::fromStdString(ps.workingDirectory).trimmed();
        if (wd.isEmpty()) wd = m_activeWorkspaceDir;
        m_coordinator->setTargetExecutionContext(args, wd);
    };

    // Preserve the existing coordinator instance when its engine identifier already matches.
    if (m_coordinator && m_coordinator->activeEngineName() == desired) {
        pushExecutionContext();
        return;
    }

    if (m_coordinator) m_coordinator->disconnect();
    if (m_deadlockAnalyzer) {
        delete m_deadlockAnalyzer;
        m_deadlockAnalyzer = nullptr;
    }
    if (m_ownedCoordinator) {
        m_ownedCoordinator->terminateSession();
        delete m_ownedCoordinator;
        m_ownedCoordinator = nullptr;
    }
    m_coordinator = nullptr;

    // Instantiate the requested live backend coordinator and wire all workstation signal routes.
    if (desired == QStringLiteral("lldb-dap")) {
        m_ownedCoordinator = new DapCoordinator(this);
    } else {
        m_ownedCoordinator = new gridlock::GdbRankCoordinator(this);
    }
    setCoordinator(m_ownedCoordinator);
    pushExecutionContext();
}

/**
 * @brief Dispatches a live source-level step-over (`-exec-next` / DAP `next`) on the active rank.
 * @return True if an active session coordinator accepted the step-over request.
 */
bool MainWindow::stepOverWorkflow() {
    if (!m_debugSessionActive || !m_coordinator) return false;
    m_focusedRank = activeSteppingRank(); updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("STEPPING"));
    m_coordinator->stepOver(m_focusedRank + 1); return true;
}

/**
 * @brief Dispatches a live source-level step-into (`-exec-step` / DAP `stepIn`) on the active rank.
 * @return True if an active session coordinator accepted the step-into request.
 */
bool MainWindow::stepIntoWorkflow() {
    if (!m_debugSessionActive || !m_coordinator) return false;
    m_focusedRank = activeSteppingRank(); updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("STEPPING"));
    m_coordinator->stepInto(m_focusedRank + 1); return true;
}

/**
 * @brief Interrupts execution on the currently focused MPI rank (`SIGINT` + `-exec-interrupt` / DAP `pause`).
 * @return True if a coordinator instance received the pause request.
 */
bool MainWindow::pauseRankWorkflow() {
    if (!m_coordinator) return false;
    m_focusedRank = activeSteppingRank(); updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("PAUSED"));
    m_coordinator->pauseRank(m_focusedRank); return true;
}

/**
 * @brief Steps backward over the previous source statement (`-exec-next --reverse` / DAP `stepBack`).
 * @return True if an active session coordinator accepted the reverse step-over request.
 */
bool MainWindow::reverseStepOverWorkflow() {
    if (!m_debugSessionActive || !m_coordinator) return false;
    m_focusedRank = activeSteppingRank(); updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("STEPPING BACK"));
    m_coordinator->reverseStepOver(m_focusedRank); return true;
}

/**
 * @brief Runs backward in recorded execution history (`-exec-continue --reverse` / DAP `reverseContinue`).
 * @return True if an active session coordinator accepted the reverse continue request.
 */
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
        connect(m_variablesDockWidget, &VariablesDockWidget::visualizeInPointerGraphRequested, this, [this](const QString& expr, const QString&, const QString& val, const QString& typ) { openPointerGraphVisualizer(expr, val, typ); });
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
                    wpBtn->setObjectName(QStringLiteral("addHardwareWatchpointBtn"));
                    wpBtn->setFixedHeight(22);
                    wpBtn->setToolTip(QStringLiteral("Arm CPU Hardware Data Watchpoint (Write / Read / R/W)"));
                    connect(wpBtn, &QPushButton::clicked, this, [this, watchInput, modeCombo]() {
                        // Ignore empty watch expressions so blank inputs never emit spurious MI/DAP commands.
                        const QString expr = watchInput->text().trimmed();
                        if (expr.isEmpty()) return;
                        const auto mode = static_cast<WatchpointAccessMode>(modeCombo->currentData().toInt());
                        if (m_coordinator) m_coordinator->setDataWatchpoint(expr, mode, -1);
                        if (m_differentialGrid) m_differentialGrid->addVariableColumn(expr);
                        watchInput->clear();
                    });
                    barLayout->addWidget(modeCombo);
                    barLayout->addWidget(wpBtn);
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
            QEventLoop loop;
            QTimer timer;
            timer.setSingleShot(true);
            QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
            const QMetaObject::Connection conn = QObject::connect(
                m_coordinator, &IBackendCoordinator::expressionEvaluated, &loop,
                [&](int rId, const QString&, const QString& result) {
                    if (rId != rank) return;
                    static const QRegularExpression hexRe(QStringLiteral("0x([0-9a-fA-F]+)"));
                    const auto m = hexRe.match(result);
                    if (m.hasMatch()) evalAddr = m.captured(1).toULongLong(&evalOk, 16);
                    loop.quit();
                });
            timer.start(1200);
            m_coordinator->evaluateExpression(rank, expr);
            loop.exec();
            QObject::disconnect(conn);
            if (evalOk && evalAddr != 0ULL) {
                if (okOut) *okOut = true;
                return evalAddr;
            }
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
                const size_t doubleCount = static_cast<size_t>((length + 7) / 8);
                const std::vector<double> words = NativeMemoryReader::readDoubles(
                    static_cast<pid_t>(pid), static_cast<uintptr_t>(addr), doubleCount);
                if (!words.empty()) {
                    QByteArray out(length, Qt::Uninitialized);
                    std::memcpy(out.data(), words.data(), static_cast<size_t>(length));
                    return out;
                }
            } catch (...) {
                // Fall through to debugger-tracer memory read when `yama/ptrace_scope` blocks sibling readv.
            }
        }

        // Tracer fallback: request raw memory bytes directly from GDB/DAP only when the rank is stopped.
        const auto stIt = m_latestStates.find(rank);
        if (stIt == m_latestStates.end() || stIt->second.currentState != QLatin1String("stopped")) return {};
        QByteArray fallbackBytes;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        m_suppressMemViewBroadcast = true;
        const QMetaObject::Connection conn = QObject::connect(
            m_coordinator, &IBackendCoordinator::memoryRead, &loop,
            [&](int rId, const QString&, const QByteArray& data) {
                if (rId != rank) return;
                fallbackBytes = data;
                loop.quit();
            });
        timer.start(1500);
        m_coordinator->readMemory(rank, QStringLiteral("0x%1").arg(addr, 0, 16), length);
        loop.exec();
        QObject::disconnect(conn);
        m_suppressMemViewBroadcast = false;
        if (fallbackBytes.size() > length) fallbackBytes.truncate(length);
        return fallbackBytes;
    };

    // Expose live inferior OS PID lookup per MPI rank so MemView can display and verify distinct PIDs.
    auto pidGetter = [this](int rank) -> qint64 {
        return (m_debugSessionActive && m_coordinator) ? m_coordinator->getPidForRank(rank) : 0LL;
    };

    m_memView->setLiveMemoryCallbacks(resolver, fetcher, pidGetter);
}

/**
 * @brief Attaches the active debugger coordinator to a live local OS process (`AttachProcessDialog`).
 * @param explicitPid Explicit target OS PID, or `<= 0` to prompt the user via `AttachProcessDialog`.
 * @return True if `m_coordinator->attachToProcess(pid)` succeeded and the UI latched into session mode.
 */
bool MainWindow::attachToProcessWorkflow(qint64 explicitPid) {
    // Prompt user via live `/proc` process picker when no explicit PID was passed programmatically.
    qint64 targetPid = explicitPid;
    if (targetPid <= 0) {
        AttachProcessDialog dlg(this);
        if (dlg.exec() != QDialog::Accepted) return false;
        targetPid = dlg.selectedPid();
    }
    if (targetPid <= 0) return false;

    // Tear down any pre-existing debug session cleanly before attaching to the selected PID.
    if (m_debugSessionActive) stopTargetWorkflow();
    syncCoordinatorWithConfig();
    if (!m_coordinator || !m_coordinator->attachToProcess(targetPid)) return false;

    // Initialize Rank 0 workstation state, topology card, MPI diagnostics row, and console selector.
    m_debugSessionActive = true;
    m_focusedRank = 0;
    m_latestStates.clear();
    RankState attachedState;
    attachedState.rankId = 0;
    attachedState.currentState = QStringLiteral("stopped");
    m_latestStates[0] = attachedState;
    if (m_serverRackView) m_serverRackView->updateRanks({attachedState});
    if (m_mpiDiagnosticsWidget) {
        m_mpiDiagnosticsWidget->beginLiveSession(1);
        m_mpiDiagnosticsWidget->updateLiveRankRow(
            0, targetPid, QStringLiteral("PAUSED"), QStringLiteral("attached"),
            QStringLiteral("LOCAL PID"), QStringLiteral("0x00000000"), 1);
    }
    if (m_memView) m_memView->setSourceModeLabel(QStringLiteral("LIVE RANK 0 (PID %1)").arg(targetPid));
    if (m_gdbConsoleWidget) m_gdbConsoleWidget->ensureRankCount(1);
    // Latch execution toolbar actions and refresh `/proc/<targetPid>` stack/heap telemetry.
    updateDebugToolbarState();
    updateStatusTelemetry(QStringLiteral("0x00000000"),
                          QStringLiteral("ATTACHED (PID %1)").arg(targetPid));
    refreshStatusBarMemoryTelemetry();
    return true;
}

/**
 * @brief Records per-rank breakpoint metadata and stages rank-filter hints for coordinator insertion.
 */
void MainWindow::onRankBreakpointChanged(const QString& file, int line, bool isSet, int rankFilter,
                                         const QString& condition) {
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
/**
 * @brief Opens the modeless DDD Pointer & Struct Graph Visualizer and optionally graphs @p exprPath.
 * @param exprPath Optional C++ expression path to graph immediately.
 * @param varValue Optional runtime value or pointer address hint.
 * @param varType Optional static C/C++ type hint.
 */
void MainWindow::openPointerGraphVisualizer(const QString& exprPath, const QString& varValue, const QString& varType) {
    if (!m_pointerGraphDialog) m_pointerGraphDialog = new PointerGraphDialog(m_coordinator, this);
    m_pointerGraphDialog->setCoordinator(m_coordinator); m_pointerGraphDialog->setFocusedRank(m_focusedRank);
    if (!exprPath.trimmed().isEmpty()) { m_pointerGraphDialog->clearGraph(); m_pointerGraphDialog->graphExpression(exprPath.trimmed(), varType, varValue); }
    m_pointerGraphDialog->show(); m_pointerGraphDialog->raise(); m_pointerGraphDialog->activateWindow();
}
/**
 * @brief Safely shuts down live coordinators and disconnects signals before Qt destroys child docks.
 */
MainWindow::~MainWindow() {
    // Detach global shortcut/hover event filter before child widget destruction begins.
    if (qApp) qApp->removeEventFilter(this);
    delete m_deadlockAnalyzer; m_deadlockAnalyzer = nullptr; if (m_coordinator) QObject::disconnect(m_coordinator, nullptr, this, nullptr);
    // Silence and destroy owned coordinator cleanly without post-delete signal-blocker UB.
    if (m_ownedCoordinator) { m_ownedCoordinator->blockSignals(true); m_ownedCoordinator->terminateSession(); delete m_ownedCoordinator; m_ownedCoordinator = nullptr; }
    // Nullify cached dock and view pointers prior to QMainWindow child tree cleanup.
    m_coordinator = nullptr; m_callStackDockWidget = nullptr; m_mpiDiagnosticsWidget = nullptr; m_gdbConsoleWidget = nullptr; m_variablesDockWidget = nullptr; m_serverRackView = nullptr;
}
}  // namespace gridlock::ui

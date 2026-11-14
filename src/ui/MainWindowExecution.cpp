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
#include "../core/managers/ConfigManager.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QString>

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

    const QString rawEngine =
        gridlock::core::ConfigManager::instance().debuggerEngine().trimmed().toLower();
    const QString desired = (rawEngine == QStringLiteral("lldb-dap"))
                                ? QStringLiteral("lldb-dap")
                                : QStringLiteral("gdb");

    // Preserve the existing coordinator instance when its engine identifier already matches.
    if (m_coordinator && m_coordinator->activeEngineName() == desired) return;

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
}

/**
 * @brief Dispatches a live source-level step-over (`-exec-next` / DAP `next`) on the active rank.
 * @return True if an active session coordinator accepted the step-over request.
 */
bool MainWindow::stepOverWorkflow() {
    if (!m_debugSessionActive || !m_coordinator) return false;
    const int activeRank = activeSteppingRank();
    m_focusedRank = activeRank;
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("STEPPING"));
    m_coordinator->stepOver(activeRank + 1);
    return true;
}

/**
 * @brief Dispatches a live source-level step-into (`-exec-step` / DAP `stepIn`) on the active rank.
 * @return True if an active session coordinator accepted the step-into request.
 */
bool MainWindow::stepIntoWorkflow() {
    if (!m_debugSessionActive || !m_coordinator) return false;
    const int activeRank = activeSteppingRank();
    m_focusedRank = activeRank;
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("STEPPING"));
    m_coordinator->stepInto(activeRank + 1);
    return true;
}

/**
 * @brief Interrupts execution on the currently focused MPI rank (`SIGINT` + `-exec-interrupt` / DAP `pause`).
 * @return True if a coordinator instance received the pause request.
 */
bool MainWindow::pauseRankWorkflow() {
    if (!m_coordinator) return false;
    const int activeRank = activeSteppingRank();
    m_focusedRank = activeRank;
    updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("PAUSED"));
    m_coordinator->pauseRank(activeRank);
    return true;
}

/**
 * @brief Updates workspace views upon receiving live state changes for an MPI rank.
 * @param rankId Zero-based MPI rank index.
 * @param state Snapshot containing current execution state, active PC, source line, and registers.
 */
void MainWindow::onRankStateChanged(int rankId, const RankState& state) {
    // Cache the newest rank state and reflect it on the Server Rack overview widget.
    m_latestStates[rankId] = state;
    if (m_serverRackView) m_serverRackView->updateRankState(rankId, state);

    // Parse the 64-bit instruction pointer from $rip (register slot 16) for CFG/disassembly sync.
    const QString pcAddr = state.registers.value(16, m_currentSeekOrPc);
    const QString pcToken = pcAddr.section(QLatin1Char(' '), 0, 0);
    bool pcOk = false;
    const uint64_t pcVal = pcToken
                               .mid(pcToken.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 2 : 0)
                               .toULongLong(&pcOk, 16);

    // Push per-rank live status into the MPI Diagnostics table dock.
    if (m_mpiDiagnosticsWidget) {
        const QString diagStatus = (state.currentState == QLatin1String("stopped"))
                                       ? QStringLiteral("PAUSED")
                                       : state.currentState.toUpper();
        m_mpiDiagnosticsWidget->updateRankStateRow(
            rankId, diagStatus.isEmpty() ? QStringLiteral("IDLE") : diagStatus,
            state.currentFunction.isEmpty() ? QStringLiteral("main") : state.currentFunction,
            QStringLiteral("LIVE"), pcOk ? pcVal : 0ULL,
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
        // Open or focus the resolved DWARF source file and move the yellow PC arrow to currentLine.
        if (state.currentState == "stopped" && state.currentLine > 0) {
            SourceCodeView* view =
                (!state.currentFile.isEmpty() && QFileInfo::exists(state.currentFile) && m_editorTabManager)
                    ? m_editorTabManager->openFile(state.currentFile)
                    : getSourceCodeView();
            if (view) {
                view->highlightCurrentLine(state.currentLine);
                view->highlightExecutionLine(state.currentLine);
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
            updateDebugToolbarState();
            if (SourceCodeView* sv = getSourceCodeView()) sv->highlightExecutionLine(-1);
            updateStatusTelemetry(m_currentSeekOrPc, QStringLiteral("EXITED (0)"));
        }
    }
}

/**
 * @brief Focuses inspection views (Disassembly, Registers, Memory, Source) on a selected MPI rank.
 * @param rankId The target 0-based rank index selected by the user.
 */
void MainWindow::onRankSelected(int rankId) {
    // Track newly focused rank and immediately rebind all inspection panes to its cached snapshot.
    m_focusedRank = rankId;
    if (m_latestStates.count(rankId)) {
        const auto& state = m_latestStates[rankId];
        const QString pcAddr = state.registers.value(16, m_currentSeekOrPc);
        const QString pcToken = pcAddr.section(QLatin1Char(' '), 0, 0);
        bool pcOk = false;
        const uint64_t pcVal = pcToken
                                   .mid(pcToken.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ? 2 : 0)
                                   .toULongLong(&pcOk, 16);

        updateStatusTelemetry(
            pcAddr, state.currentState.isEmpty() ? m_currentTargetState : state.currentState.toUpper());
        refreshStatusBarMemoryTelemetry();
        if (m_disassemblyView && !state.disassemblyText.isEmpty() &&
            (!m_useRealBinaryTarget || !pcOk || pcVal == 0ULL ||
             m_disassemblyView->lineForAddress(pcVal) < 0)) {
            m_disassemblyView->updateDisassembly(state.disassemblyText);
        }
        if (m_disassemblyView && pcOk && pcVal != 0ULL) {
            m_disassemblyView->setCurrentAddress(pcVal);
            m_disassemblyView->setActiveSteppingRank(rankId, pcVal);
        }
        // Trigger on-demand GDB disassembly fallback when no cached text is present for this rank.
        if (state.disassemblyText.isEmpty()) {
            if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
                gdbCoord->requestDisassemblyFallback(rankId);
            }
        }
        if (state.currentState == "stopped" && state.currentLine > 0) {
            SourceCodeView* view =
                (!state.currentFile.isEmpty() && QFileInfo::exists(state.currentFile) && m_editorTabManager)
                    ? m_editorTabManager->openFile(state.currentFile)
                    : getSourceCodeView();
            if (view) {
                view->highlightCurrentLine(state.currentLine);
                view->highlightExecutionLine(state.currentLine);
            }
        }
        if (m_differentialGrid) m_differentialGrid->setVariableData(rankId, state.variableWatches);
        if (m_registerView && !state.registers.isEmpty()) m_registerView->updateRegisters(state);
    } else {
        // Refresh baseline telemetry and request initial disassembly if rank state has not arrived yet.
        updateStatusTelemetry();
        refreshStatusBarMemoryTelemetry();
        if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
            gdbCoord->requestDisassemblyFallback(rankId);
        }
    }
}

}  // namespace gridlock::ui

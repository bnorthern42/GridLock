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
    if (state.currentState == QLatin1String("running")) m_selectedFrameLevel[rankId] = 0;
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
    // Track newly focused rank and synchronize VariablesDockWidget + CallStackDockWidget scope immediately.
    m_focusedRank = rankId;
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
        // Trigger on-demand GDB disassembly fallback when no cached text is present for this rank.
        if (state.disassemblyText.isEmpty()) {
            if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
                gdbCoord->requestDisassemblyFallback(rankId);
            }
        }
        // When the focused rank is halted, refresh its 64-frame backtrace, threads, and editor PC line.
        if (state.currentState == "stopped") {
            if (m_coordinator) m_coordinator->requestStackAndThreads(rankId);
            if (state.currentLine > 0) {
                SourceCodeView* view =
                    (!state.currentFile.isEmpty() && QFileInfo::exists(state.currentFile) && m_editorTabManager)
                        ? m_editorTabManager->openFile(state.currentFile)
                        : getSourceCodeView();
                if (view) {
                    view->highlightCurrentLine(state.currentLine);
                    view->highlightExecutionLine(state.currentLine);
                }
            }
        }
        // Push cached watch expressions and CPU register bank values into their respective docks.
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

/**
 * @brief Connects `CallStackDockWidget` and `m_coordinator` stack/thread signals for caller frame switching.
 */
void MainWindow::bindCallStackCoordinatorRoutes() {
    // Wire UI -> Coordinator signals once on `m_callStackDockWidget` (routing dynamically via `m_coordinator`).
    if (m_callStackDockWidget && !m_callStackDockWidget->property("gl_callstack_bound").toBool()) {
        m_callStackDockWidget->setProperty("gl_callstack_bound", true);

        // Switching OS/OpenMP threads resets frame level to #0 and queries that thread's stack and locals.
        connect(m_callStackDockWidget, &CallStackDockWidget::threadSelected, this,
                [this](int threadId) {
                    m_selectedFrameLevel[m_focusedRank] = 0;
                    if (m_coordinator) m_coordinator->selectStackFrame(m_focusedRank, threadId, 0);
                });

        // Activating a caller frame jumps SourceCodeView + DisassemblyView and re-queries frame locals/registers.
        connect(m_callStackDockWidget, &CallStackDockWidget::frameActivated, this,
                [this](int threadId, int frameLevel, const QString& file, int line, quint64 address) {
                    m_selectedFrameLevel[m_focusedRank] = frameLevel;
                    // Open or focus the frame's DWARF source file and move the active line highlight.
                    if (!file.isEmpty() && QFileInfo::exists(file) && line > 0) {
                        SourceCodeView* view = m_editorTabManager ? m_editorTabManager->openFile(file)
                                                                  : getSourceCodeView();
                        if (view) {
                            view->highlightCurrentLine(line);
                            view->highlightExecutionLine(line);
                        }
                    }
                    // Synchronize DisassemblyView cursor to the caller frame's return/call site address.
                    if (m_disassemblyView && address != 0ULL) {
                        m_disassemblyView->setCurrentAddress(address);
                        m_disassemblyView->setActiveSteppingRank(m_focusedRank, address);
                    }
                    // Instruct the active backend coordinator to select `threadId` + `frameLevel`.
                    if (m_coordinator) m_coordinator->selectStackFrame(m_focusedRank, threadId, frameLevel);
                });
    }

    if (!m_coordinator) return;

    // Wire Coordinator -> UI signals for 64-frame backtraces and OS/OpenMP thread lists.
    connect(m_coordinator, &IBackendCoordinator::stackTraceUpdated, this,
            [this](int rank, const QVector<StackFrameEntry>& frames) {
                // Empty frame broadcast with zero active processes signals full session teardown.
                if (frames.isEmpty() && m_coordinator && m_coordinator->getProcessCount() == 0) {
                    m_cachedStackFrames.clear();
                    m_cachedThreads.clear();
                    m_selectedFrameLevel.clear();
                    if (m_callStackDockWidget) m_callStackDockWidget->clearFrames();
                    return;
                }
                // Cache unwound frames per rank and update the dock immediately if `rank` is focused.
                m_cachedStackFrames[rank] = frames;
                if (rank == m_focusedRank && m_callStackDockWidget) {
                    m_callStackDockWidget->setStackFrames(frames, m_selectedFrameLevel.value(rank, 0));
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

        // 3. Evaluate arbitrary register/pointer expression via active debugger coordinator with bounded wait.
        if (m_debugSessionActive && m_coordinator) {
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
    auto fetcher = [this](int rank, uint64_t addr, int length) -> QByteArray {
        if (!m_debugSessionActive || !m_coordinator || addr == 0ULL || length <= 0) return {};
        const qint64 pid = m_coordinator->getPidForRank(rank);
        if (pid > 0) {
            try {
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

        // Tracer fallback: request raw memory bytes directly from GDB (`-data-read-memory-bytes`) or DAP.
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

    m_debugSessionActive = true;
    m_focusedRank = 0;
    m_latestStates.clear();
    RankState attachedState;
    attachedState.rankId = 0;
    attachedState.currentState = QStringLiteral("stopped");
    m_latestStates[0] = attachedState;
    if (m_serverRackView) m_serverRackView->updateRanks({attachedState});
    updateDebugToolbarState();
    updateStatusTelemetry(QStringLiteral("0x00000000"),
                          QStringLiteral("ATTACHED (PID %1)").arg(targetPid));
    refreshStatusBarMemoryTelemetry();
    return true;
}

}  // namespace gridlock::ui

/**
 * @file DebugCommands.cpp
 * @brief Implementation of concrete debugger command classes.
 *
 * Bridges UI command invocations to MainWindow, IBackendCoordinator,
 * GdbRankCoordinator, and DapCoordinator while honoring user preferences
 * such as automatic SIGFPE signal trapping across all MPI ranks.
 */

#include "DebugCommands.hpp"

#include "../../ui/MainWindow.hpp"
#include "../../ui/views/SourceCodeView.hpp"
#include "../hpc/DapCoordinator.hpp"
#include "../hpc/GdbRankCoordinator.hpp"
#include "../hpc/IBackendCoordinator.hpp"
#include "../managers/ConfigManager.hpp"

namespace gridlock::core::commands {

/**
 * @brief Constructs a LaunchCommand bound to @p mainWindow, @p binaryPath, and @p ranks.
 */
LaunchCommand::LaunchCommand(gridlock::ui::MainWindow* mainWindow, const QString& binaryPath, int ranks)
    : m_mainWindow(mainWindow), m_binaryPath(binaryPath), m_ranks(ranks) {}

/**
 * @brief Starts the parallel session and optionally configures `SIGFPE` trapping across all GDB ranks.
 */
void LaunchCommand::execute() {
    if (!m_mainWindow) {
        return;
    }
    // Spawn the parallel debugging session across the requested number of MPI ranks.
    m_mainWindow->startDebuggingSession(m_binaryPath, m_ranks);

    // If floating-point exception trapping is enabled in settings, broadcast catchpoint to all GDB ranks.
    const auto ds = gridlock::core::ConfigManager::instance().getDebuggerSettings();
    if (ds.trapFpe) {
        if (auto* coord = m_mainWindow->coordinator()) {
            if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(coord)) {
                // Rank ID -1 broadcasts the GDB/MI console command to every active rank process.
                gdbCoord->sendCommand(-1, "-interpreter-exec console \"catch signal SIGFPE\"");
            }
        }
    }
}

/**
 * @brief Constructs a ContinueCommand targeting @p threadId on @p coordinator.
 */
ContinueCommand::ContinueCommand(IBackendCoordinator* coordinator, int threadId)
    : m_coordinator(coordinator), m_threadId(threadId) {}

/**
 * @brief Resumes execution on `m_threadId` via `IBackendCoordinator::continueExecution()`.
 */
void ContinueCommand::execute() {
    // Forward execution resumption to the active backend coordinator for the target thread/rank.
    if (m_coordinator) {
        m_coordinator->continueExecution(m_threadId);
    }
}

/**
 * @brief Constructs a StepCommand for either step-into (`stepInto == true`) or step-over.
 */
StepCommand::StepCommand(IBackendCoordinator* coordinator, int threadId, bool stepInto)
    : m_coordinator(coordinator), m_threadId(threadId), m_stepInto(stepInto) {}

/**
 * @brief Executes `stepInto()` or `stepOver()` on `m_coordinator`.
 */
void StepCommand::execute() {
    if (!m_coordinator) {
        return;
    }
    // Route either step-into (enter callees) or step-over (next source line in current frame).
    if (m_stepInto) {
        m_coordinator->stepInto(m_threadId);
    } else {
        m_coordinator->stepOver(m_threadId);
    }
}

/**
 * @brief Constructs a PauseCommand targeting @p threadId on @p coordinator.
 */
PauseCommand::PauseCommand(IBackendCoordinator* coordinator, int threadId)
    : m_coordinator(coordinator), m_threadId(threadId) {}

/**
 * @brief Interrupts execution on `m_threadId` via `IBackendCoordinator::pauseExecution()`.
 */
void PauseCommand::execute() {
    // Request asynchronous execution interruption on the target thread/rank.
    if (m_coordinator) {
        m_coordinator->pauseExecution(m_threadId);
    }
}

/**
 * @brief Constructs a TerminateCommand bound to @p mainWindow.
 */
TerminateCommand::TerminateCommand(gridlock::ui::MainWindow* mainWindow)
    : m_mainWindow(mainWindow) {}

/**
 * @brief Closes `m_mainWindow` and tears down all active debugging sessions.
 */
void TerminateCommand::execute() {
    // Closing MainWindow triggers closeEvent(), which cleanly terminates all backend processes.
    if (m_mainWindow) {
        m_mainWindow->close();
    }
}

/**
 * @brief Constructs a ToggleBreakpointCommand for @p file at @p line on @p coordinator.
 */
ToggleBreakpointCommand::ToggleBreakpointCommand(DapCoordinator* coordinator, const QString& file, int line)
    : m_coordinator(coordinator), m_file(file), m_line(line) {}

/**
 * @brief Toggles the breakpoint at `(m_file, m_line)` via `DapCoordinator::toggleBreakpoint()`.
 */
void ToggleBreakpointCommand::execute() {
    // Toggle the breakpoint at (file, line) and synchronize the updated breakpoint table over DAP.
    if (m_coordinator) {
        m_coordinator->toggleBreakpoint(m_file, m_line);
    }
}

}  // namespace gridlock::core::commands

#include "DebugCommands.hpp"
#include "../hpc/GdbRankCoordinator.hpp"
#include "../hpc/IBackendCoordinator.hpp"
#include "../../ui/MainWindow.hpp"
#include "../../ui/views/SourceCodeView.hpp"
#include "../hpc/DapCoordinator.hpp"
#include "../managers/ConfigManager.hpp"

namespace gridlock::core::commands {

LaunchCommand::LaunchCommand(gridlock::ui::MainWindow* mainWindow, const QString& binaryPath, int ranks)
    : m_mainWindow(mainWindow), m_binaryPath(binaryPath), m_ranks(ranks) {}

void LaunchCommand::execute() {
    if (m_mainWindow) {
        m_mainWindow->startDebuggingSession(m_binaryPath, m_ranks);

        auto ds = gridlock::core::ConfigManager::instance().getDebuggerSettings();
        if (ds.trapFpe) {
            if (auto* coord = m_mainWindow->coordinator()) {
                if (auto* gdbCoord = dynamic_cast<gridlock::GdbRankCoordinator*>(coord)) {
                    // Send to all ranks
                    gdbCoord->sendCommand(-1, "-interpreter-exec console \"catch signal SIGFPE\"");
                }
            }
        }
    }
}

ContinueCommand::ContinueCommand(IBackendCoordinator* coordinator, int threadId)
    : m_coordinator(coordinator), m_threadId(threadId) {}

void ContinueCommand::execute() {
    if (m_coordinator) {
        m_coordinator->continueExecution(m_threadId);
    }
}

StepCommand::StepCommand(IBackendCoordinator* coordinator, int threadId, bool stepInto)
    : m_coordinator(coordinator), m_threadId(threadId), m_stepInto(stepInto) {}

void StepCommand::execute() {
    if (m_coordinator) {
        if (m_stepInto) {
            m_coordinator->stepInto(m_threadId);
        } else {
            m_coordinator->stepOver(m_threadId);
        }
    }
}

PauseCommand::PauseCommand(IBackendCoordinator* coordinator, int threadId)
    : m_coordinator(coordinator), m_threadId(threadId) {}

void PauseCommand::execute() {
    if (m_coordinator) {
        m_coordinator->pauseExecution(m_threadId);
    }
}

TerminateCommand::TerminateCommand(gridlock::ui::MainWindow* mainWindow)
    : m_mainWindow(mainWindow) {}

void TerminateCommand::execute() {
    if (m_mainWindow) {
        m_mainWindow->close();
    }
}

ToggleBreakpointCommand::ToggleBreakpointCommand(DapCoordinator* coordinator, const QString& file, int line)
    : m_coordinator(coordinator), m_file(file), m_line(line) {}

void ToggleBreakpointCommand::execute() {
    if (m_coordinator) {
        m_coordinator->toggleBreakpoint(m_file, m_line);
    }
}

} // namespace gridlock::core::commands

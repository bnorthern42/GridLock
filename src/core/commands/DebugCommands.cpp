#include "DebugCommands.hpp"
#include "../../GdbRankCoordinator.hpp"
#include "../../ui/MainWindow.hpp"

namespace gridlock::core::commands {

LaunchCommand::LaunchCommand(gridlock::ui::MainWindow* mainWindow, const QString& binaryPath, int ranks)
    : m_mainWindow(mainWindow), m_binaryPath(binaryPath), m_ranks(ranks) {}

void LaunchCommand::execute() {
    if (m_mainWindow) {
        m_mainWindow->startDebuggingSession(m_binaryPath, m_ranks);
    }
}

ContinueCommand::ContinueCommand(gridlock::GdbRankCoordinator* coordinator)
    : m_coordinator(coordinator) {}

void ContinueCommand::execute() {
    if (m_coordinator) {
        m_coordinator->continueAll();
    }
}

StepCommand::StepCommand(gridlock::GdbRankCoordinator* coordinator)
    : m_coordinator(coordinator) {}

void StepCommand::execute() {
    if (m_coordinator) {
        m_coordinator->stepAll();
    }
}

PauseCommand::PauseCommand(gridlock::GdbRankCoordinator* coordinator, int rankId)
    : m_coordinator(coordinator), m_rankId(rankId) {}

void PauseCommand::execute() {
    if (m_coordinator) {
        m_coordinator->pauseFocusedRank(m_rankId);
    }
}

TerminateCommand::TerminateCommand(gridlock::ui::MainWindow* mainWindow)
    : m_mainWindow(mainWindow) {}

void TerminateCommand::execute() {
    if (m_mainWindow) {
        m_mainWindow->close();
    }
}

} // namespace gridlock::core::commands

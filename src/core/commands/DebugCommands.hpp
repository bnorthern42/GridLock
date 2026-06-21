#pragma once
#include "IDebugCommand.hpp"
#include <QString>

namespace gridlock {
class GdbRankCoordinator;
}

namespace gridlock::ui {
class MainWindow;
}

namespace gridlock::core::commands {

class LaunchCommand : public IDebugCommand {
public:
    LaunchCommand(gridlock::ui::MainWindow* mainWindow, const QString& binaryPath, int ranks);
    void execute() override;
private:
    gridlock::ui::MainWindow* m_mainWindow;
    QString m_binaryPath;
    int m_ranks;
};

class ContinueCommand : public IDebugCommand {
public:
    ContinueCommand(gridlock::GdbRankCoordinator* coordinator);
    void execute() override;
private:
    gridlock::GdbRankCoordinator* m_coordinator;
};

class StepCommand : public IDebugCommand {
public:
    StepCommand(gridlock::GdbRankCoordinator* coordinator);
    void execute() override;
private:
    gridlock::GdbRankCoordinator* m_coordinator;
};

class PauseCommand : public IDebugCommand {
public:
    PauseCommand(gridlock::GdbRankCoordinator* coordinator, int rankId);
    void execute() override;
private:
    gridlock::GdbRankCoordinator* m_coordinator;
    int m_rankId;
};

class TerminateCommand : public IDebugCommand {
public:
    TerminateCommand(gridlock::ui::MainWindow* mainWindow);
    void execute() override;
private:
    gridlock::ui::MainWindow* m_mainWindow;
};

} // namespace gridlock::core::commands

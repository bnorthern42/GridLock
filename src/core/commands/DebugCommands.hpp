#pragma once
#include "IDebugCommand.hpp"
#include <QString>

namespace gridlock {
class GdbRankCoordinator;
}

namespace gridlock::ui {
class MainWindow;
class SourceCodeView;
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

class ToggleBreakpointCommand : public IDebugCommand {
public:
    ToggleBreakpointCommand(gridlock::ui::SourceCodeView* view, int line);
    void execute() override;
private:
    gridlock::ui::SourceCodeView* m_view;
    int m_line;
};

} // namespace gridlock::core::commands

#pragma once
#include "IDebugCommand.hpp"
#include <QString>

namespace gridlock {
class GdbRankCoordinator;
}

class IBackendCoordinator;

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
    ContinueCommand(IBackendCoordinator* coordinator, int threadId);
    void execute() override;
private:
    IBackendCoordinator* m_coordinator;
    int m_threadId;
};

class StepCommand : public IDebugCommand {
public:
    StepCommand(IBackendCoordinator* coordinator, int threadId, bool stepInto = false);
    void execute() override;
private:
    IBackendCoordinator* m_coordinator;
    int m_threadId;
    bool m_stepInto;
};

class PauseCommand : public IDebugCommand {
public:
    PauseCommand(IBackendCoordinator* coordinator, int threadId);
    void execute() override;
private:
    IBackendCoordinator* m_coordinator;
    int m_threadId;
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

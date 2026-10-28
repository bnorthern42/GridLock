/**
 * @file DebugCommands.hpp
 * @brief Concrete IDebugCommand implementations for parallel MPI session control.
 *
 * Defines LaunchCommand, ContinueCommand, StepCommand, PauseCommand,
 * TerminateCommand, and ToggleBreakpointCommand used by MainWindow::executeCommand()
 * to route UI actions to IBackendCoordinator, GdbRankCoordinator, or DapCoordinator.
 */

#pragma once

#include <QString>

#include "IDebugCommand.hpp"

namespace gridlock {
class GdbRankCoordinator;
} // namespace gridlock

class IBackendCoordinator;
class DapCoordinator;

namespace gridlock::ui {
class MainWindow;
class SourceCodeView;
} // namespace gridlock::ui

namespace gridlock::core::commands {

/**
 * @brief Launches a parallel MPI debugging session across the configured rank count.
 */
class LaunchCommand : public IDebugCommand {
public:
    /**
     * @brief Constructs a launch command targeting the specified MPI executable.
     * @param mainWindow Owning MainWindow instance managing session UI state.
     * @param binaryPath Filesystem path to the compiled MPI target binary.
     * @param ranks Number of parallel MPI ranks to spawn.
     */
    LaunchCommand(gridlock::ui::MainWindow* mainWindow, const QString& binaryPath, int ranks);

    /** @brief Starts the parallel session and configures optional SIGFPE trapping. */
    void execute() override;

private:
    gridlock::ui::MainWindow* m_mainWindow = nullptr;
    QString m_binaryPath;
    int m_ranks = 0;
};

/**
 * @brief Resumes execution of a paused MPI rank or all ranks (-1).
 */
class ContinueCommand : public IDebugCommand {
public:
    /**
     * @brief Constructs a continue command for the specified thread or rank ID.
     * @param coordinator Active backend coordinator.
     * @param threadId Target rank/thread identifier (-1 for all ranks).
     */
    ContinueCommand(IBackendCoordinator* coordinator, int threadId);

    /** @brief Dispatches continueExecution() to the backend coordinator. */
    void execute() override;

private:
    IBackendCoordinator* m_coordinator = nullptr;
    int m_threadId = 0;
};

/**
 * @brief Steps execution by one source line (step over or step into).
 */
class StepCommand : public IDebugCommand {
public:
    /**
     * @brief Constructs a step command for the specified thread or rank ID.
     * @param coordinator Active backend coordinator.
     * @param threadId Target rank/thread identifier.
     * @param stepInto True to step into function calls; false to step over.
     */
    StepCommand(IBackendCoordinator* coordinator, int threadId, bool stepInto = false);

    /** @brief Dispatches stepInto() or stepOver() to the backend coordinator. */
    void execute() override;

private:
    IBackendCoordinator* m_coordinator = nullptr;
    int m_threadId = 0;
    bool m_stepInto = false;
};

/**
 * @brief Interrupts and pauses execution of a running MPI rank or all ranks.
 */
class PauseCommand : public IDebugCommand {
public:
    /**
     * @brief Constructs a pause command for the specified thread or rank ID.
     * @param coordinator Active backend coordinator.
     * @param threadId Target rank/thread identifier.
     */
    PauseCommand(IBackendCoordinator* coordinator, int threadId);

    /** @brief Dispatches pauseExecution() to the backend coordinator. */
    void execute() override;

private:
    IBackendCoordinator* m_coordinator = nullptr;
    int m_threadId = 0;
};

/**
 * @brief Terminates the active debugging session and closes the workstation window.
 */
class TerminateCommand : public IDebugCommand {
public:
    /**
     * @brief Constructs a terminate command bound to the main workstation window.
     * @param mainWindow Target MainWindow instance to close.
     */
    explicit TerminateCommand(gridlock::ui::MainWindow* mainWindow);

    /** @brief Closes the main window and triggers session teardown. */
    void execute() override;

private:
    gridlock::ui::MainWindow* m_mainWindow = nullptr;
};

/**
 * @brief Toggles a source-line breakpoint through the Debug Adapter Protocol coordinator.
 */
class ToggleBreakpointCommand : public IDebugCommand {
public:
    /**
     * @brief Constructs a breakpoint toggle command for a source file and 1-based line.
     * @param coordinator Active DapCoordinator instance.
     * @param file Source file path.
     * @param line 1-based source line number.
     */
    ToggleBreakpointCommand(DapCoordinator* coordinator, const QString& file, int line);

    /** @brief Invokes toggleBreakpoint() on the DAP coordinator. */
    void execute() override;

private:
    DapCoordinator* m_coordinator = nullptr;
    QString m_file;
    int m_line = 0;
};

} // namespace gridlock::core::commands

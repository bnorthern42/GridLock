# Core Debug Commands (`src/core/commands/`)

Command-pattern abstractions and concrete debug action implementations decoupling `MainWindow` triggers from `IBackendCoordinator` execution engines.

## Contents

- `IDebugCommand.hpp`:
  - Declares the abstract `IDebugCommand` interface (`26` lines).
  - Exposes `execute()`, `canExecute()`, and `description()` for uniform dispatch and UI action gating.
- `DebugCommands.hpp`:
  - Declares concrete execution control commands (`154` lines):
  - `LaunchCommand`: Launches single-rank or multi-rank targets via GDB/MI3 or DAP with executable path, rank count, and initial breakpoints.
  - `ContinueCommand`: Resumes execution across all ranks or only the focused rank based on execution scope.
  - `StepCommand`: Issues step-over, step-into, or step-out operations for the active target scope.
  - `PauseCommand`: Interrupts running inferior processes cleanly and refreshes paused frame context.
  - `TerminateCommand`: Stops active sessions and tears down backend rank processes and temporary named pipes.
- `DebugCommands.cpp`:
  - Implements constructor wiring, `canExecute()` precondition checks, and coordinator dispatch calls.
  - Routes execution errors into structured status feedback for the workstation console and toolbar state machine.

## Expected Behavior & Invariants

- **Precondition Verification**:
  - Every command evaluates coordinator state through `canExecute()` before emitting execution traffic.
- **MI Synchronization Safety**:
  - `ContinueCommand` and `StepCommand` must never fire execution or stepping requests unless target ranks hold a valid paused thread and frame context.
- **Execution Scope Parity**:
  - All-Ranks vs. Focused-Rank mode propagates cleanly from `MainWindowExecution` / `#MainToolbar` into `LaunchCommand`, `ContinueCommand`, and `StepCommand`.
- **Headless Backend Independence**:
  - Commands depend only on `IBackendCoordinator`, `GdbRankCoordinator`, and `DapCoordinator` (zero `QtWidgets` coupling), allowing direct unit verification in `tests/dap/test_dap_commands.cpp`.
- **No Raw Owning Pointers**:
  - Collaborator coordinator pointers are strictly non-owning; command lifetimes are managed by callers or `std::unique_ptr`.

## Maintainer & Agent Notes

- Implement new replayable, batchable, or undoable debugger mutations (e.g., breakpoint toggles, binary patching, or register writes) as `IDebugCommand` subclasses in this directory rather than embedding state inside `MainWindow*.cpp`.
- Keep command classes strictly as lightweight action orchestrators; protocol serialization belongs inside `src/core/hpc/gdb/` or `src/core/hpc/dap/`.
- Use forward declarations in `DebugCommands.hpp` to prevent circular header dependencies with `src/ui/` views.
- Update `meson.build` (`src_files`, `ui_test_src`, and `dap_commands_test_src`) whenever adding new `.cpp` files here.
- Maintain strict adherence to the `< 950` line file ceiling, `#pragma once`, `override` annotations, and Google C++ conventions documented in `agent.md`.

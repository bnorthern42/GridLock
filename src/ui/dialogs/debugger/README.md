# Debugger Control Dialogs (`src/ui/dialogs/debugger/`)

Focused modal, modeless, and source-anchored dialogs for process attaching, copy-on-write checkpoints, conditional breakpoints, signal/catchpoint matrices, and binary cross-references.

## Contents

- `AttachProcessDialog.hpp` (`132` lines), `AttachProcessDialog.cpp`:
  - Tab 1 scans local `/proc` processes grouped under `MPI Job (N ranks)` headers and checks `kernel.yama.ptrace_scope`.
  - Tab 2 connects to remote `gdbserver`/`qemu-user` stubs (`host:port`, ranges, or lists) with optional local ELF DWARF symbols.
- `CheckpointDialog.hpp` (`146` lines), `CheckpointDialog.cpp`:
  - Manages GDB `checkpoint`/`restart` workflows using `SafetyLevel` and `CheckpointSafetyReport`.
  - Scans `/proc/[pid]/fd` and `/proc/[pid]/maps` to warn before forking active sockets, pipes, RDMA verbs, or GPU mappings.
- `ConditionalBreakpointDialog.hpp` (`243` lines), `ConditionalBreakpointDialog.cpp` (`796` lines):
  - Frameless gutter-anchored popover configuring breakpoint enablement, All-Ranks vs. Focused-Rank suspend scope, MPI condition presets, and `clangd` completion.
- `SignalCatchpointDialog.hpp` (`144` lines), `SignalCatchpointDialog.cpp`:
  - Tab 1 configures POSIX/fabric `handle <SIG>` dispositions (`Stop`/`Print`/`Pass`).
  - Tab 2 installs and removes C++ (`throw`/`catch`), Linux syscall, and `dlopen`/`dlclose` catchpoints.
- `XrefInspectorDialog.hpp` (`130` lines), `XrefInspectorDialog.cpp`:
  - Displays incoming and outgoing code/data cross-references with filtering, structured tables, and live assembly context previews.

## Expected Behavior & Invariants

- **Checkpoint Pre-Flight Hazard Warning**:
  - `CheckpointDialog` evaluates rank FDs and memory mappings before issuing GDB `checkpoint` on unsafe shared or fabric processes.
- **Anchored Popover Screen Clamping**:
  - `ConditionalBreakpointDialog` clamps screen coordinates inside available monitor geometry and commits edits cleanly on Enter or focus dismissal.
- **Line-Count Watch**:
  - `ConditionalBreakpointDialog.cpp` sits at `796` lines (`< 950` limit); keep `clangd` virtual-buffer completion logic inside `src/core/utils/ClangdLspClient.{hpp,cpp}`.
- **Verification**:
  - Run `meson test -C builddir ui_tests dap_commands_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Namespace & Ownership Hygiene:
  - Note that `src/ui/dialogs/project/ProjectSettingsDialog.hpp` defines a separate lightweight `AttachProcessDialog` in `gridlock::ui::dialogs`; qualify includes and namespaces explicitly.
  - Guard asynchronous coordinator slots with `QPointer` so closing modeless dialogs (`SignalCatchpointDialog`, `XrefInspectorDialog`) never crashes on stale signals.
- Build & Verification Dependencies:
  - Keep `ConditionalBreakpointDialog` dependencies minimal because `dap_commands_tests` in `meson.build` links it alongside `ClangdLspClient`.
  - Test popover clamping, attach filtering, and checkpoint hazard diagnostics cleanly under `QT_QPA_PLATFORM=offscreen`.
- File Ceiling & Docs:
  - Watch `ConditionalBreakpointDialog.cpp` (`796` lines) against `< 950`, and update this `README.md` whenever adding or splitting debugger dialogs.

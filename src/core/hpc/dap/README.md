# DAP Backend Coordinator (`src/core/hpc/dap/`)

Debug Adapter Protocol (DAP) coordinator implementing `IBackendCoordinator` for JSON-framed debug adapter sessions across single-rank, multi-rank, and remote targets.

## Contents

- `DapCoordinator.hpp` (`408` lines):
  - Defines `SessionState` lifecycle enum (`Disconnected`, `Launching`, `Queued`, `Running`, `Paused`, `Terminating`).
  - Declares `DapCoordinator` implementing `IBackendCoordinator`, managing DAP subprocess streams, monotonically increasing `seq` request IDs, breakpoint caches, and `variablesReference` handles.
- `DapCoordinator.cpp` (`491` lines):
  - Implements `QProcess` adapter lifecycle, `Content-Length` framed DAP JSON packet accumulation, header parsing, request serialization, and session teardown (`stopSession` / `terminateSession`).
  - Dispatches incoming `response` (`initialize`, `stackTrace`, `threads`, `scopes`, `variables`, `readMemory`, `evaluate`, `dataBreakpointInfo`) and `event` (`initialized`, `stopped`, `output`, `process`, `terminated`) messages in `handleMessage`.
- `DapCoordinatorRequests.cpp` (`460` lines):
  - Implements outbound target `launch`/`attach` setup, environment validation, execution/stepping (`continue`, `next`, `stepIn`, `pause`), source breakpoints, and hardware data watchpoints (`setDataWatchpoint` / `syncDataBreakpoints`).
  - Implements outbound `stackTrace`, `threads`, `scopes`, `variables`, `evaluate`, `NativeMemoryReader` (`process_vm_readv`) fast-path `readMemory` / heatmap queries, `MiSanitizer`-guarded REPL commands, and live variable/register writes.

## Expected Behavior & Invariants

- **Exact `Content-Length` Framing**:
  - Incoming byte buffers parse strictly by byte length, surviving partial socket/pipe chunks, split HTTP-style headers, and coalesced multi-packet reads (`tests/dap/test_dap_parser.cpp`).
- **SessionState Lifecycle Discipline**:
  - Execution (`continue`, `next`, `stepIn`, `stepOut`, `pause`), evaluation, and inspection requests honor `SessionState` transitions and do not fire before `initialized`/`configurationDone` completes (`tests/dap/test_dap_lifecycle.cpp`).
- **Native Memory Bridge Fallback**:
  - When `process_vm_readv` lacks permissions or remote targets lack local PIDs, memory inspection falls back cleanly to standard DAP `readMemory` requests (`tests/dap/test_dap_native_bridge.cpp`).
- **IBackendCoordinator Signal Parity**:
  - Emitted stopped frames, variables, breakpoints, memory dumps, and console output conform to identical contracts as `GdbRankCoordinator`.
  - Protocol errors emit structured console diagnostics instead of silently dropping pending request tokens.
- **Standalone Offscreen Test Verification**:
  - Run all 9 DAP suites via `meson test -C builddir dap_parser_tests dap_commands_tests dap_breakpoints_tests dap_stacktrace_tests dap_variables_tests dap_evaluate_tests dap_lifecycle_tests dap_memory_tests dap_native_bridge_tests`.

## Maintainer & Agent Notes

- **Translation Unit Line Budget (`< 500`)**:
  - Keep `DapCoordinator.cpp` (`491` lines) and `DapCoordinatorRequests.cpp` (`460` lines) strictly `< 500` lines; extract additional protocol helpers before expanding either file.
- Synchronize both `DapCoordinator.cpp` and `DapCoordinatorRequests.cpp` across all 13 Meson targets (`src_files`, `test_src`, `ui_test_src`, `advanced_features_test_src`, and all 9 `dap_*_test_src` arrays) in `meson.build`.
- Keep `DapCoordinator` completely free of `QtWidgets` includes so headless DAP tests remain lightweight and fast.
- Guard all pending request sequence lookups against empty/cancelled responses during adapter shutdown or sudden EOF.
- Preserve strict `const` correctness, `override` markers on `IBackendCoordinator` overrides, and Google C++ formatting, and update this `README.md` whenever splitting DAP files.

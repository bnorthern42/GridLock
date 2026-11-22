# DAP Backend Coordinator (`src/core/hpc/dap/`)

Debug Adapter Protocol (DAP) coordinator implementing `IBackendCoordinator` for JSON-framed debug adapter sessions across single-rank, multi-rank, and remote targets.

## Contents

- `DapCoordinator.hpp` (`407` lines):
  - Defines `SessionState` lifecycle enum (`Disconnected`, `Initializing`, `Configured`, `Running`, `Stopped`, `Terminated`).
  - Declares `DapCoordinator` implementing `IBackendCoordinator`.
  - Manages DAP subprocess/transport streams, monotonically increasing `seq` request IDs, breakpoint caches, and `variablesReference` handles.
- `DapCoordinator.cpp` (`932` lines):
  - Implements `Content-Length` framed DAP JSON packet accumulation, header parsing, and request serialization.
  - Handles `initialize`, `launch`/`attach`, and `configurationDone` handshakes.
  - Implements execution/stepping (`continue`, `next`, `stepIn`, `stepOut`, `pause`), `threads`, `stackTrace`, `scopes`, `variables`, `evaluate`, `setBreakpoints`, `readMemory`/`writeMemory`, and `disassemble`.
  - Extracts debuggee OS PIDs from DAP `process` events to bridge large memory reads directly to `NativeMemoryReader` (`process_vm_readv`).

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

- **Critical Line Ceiling Warning (`932 / 950`)**:
  - `DapCoordinator.cpp` sits at `932` lines (strict ceiling is `< 950` lines).
  - Do NOT add non-trivial features directly to `DapCoordinator.cpp`; extract DAP JSON builders, parsers, or inspection handlers into a separate translation unit (e.g., `DapProtocolParser.cpp` or `DapInspection.cpp`) first.
- Check all 9 `dap_*_test_src` targets plus `src_files`, `ui_test_src`, and `advanced_features_test_src` in `meson.build` whenever splitting `DapCoordinator.cpp` so standalone tests keep linking cleanly.
- Keep `DapCoordinator` completely free of `QtWidgets` includes so headless DAP tests remain lightweight and fast.
- Guard all pending request sequence lookups against empty/cancelled responses during adapter shutdown or sudden EOF.
- Preserve strict `const` correctness, `override` markers on `IBackendCoordinator` overrides, and Google C++ formatting.
- Always update this `README.md` in the same commit when splitting `DapCoordinator.cpp` or adding new DAP helpers.

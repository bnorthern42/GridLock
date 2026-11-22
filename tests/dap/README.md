# DAP Protocol & Coordinator Tests (`tests/dap/`)

Standalone QTest suites exercising every stage of `DapCoordinator` and Debug Adapter Protocol wire communication across 9 Meson targets.

## Contents

- `test_dap_parser.hpp`, `test_dap_parser.cpp` (`dap_parser_tests`):
  - Verifies `Content-Length` wire framing, split TCP/pipe chunks, and coalesced multi-packet boundaries.
- `test_dap_lifecycle.hpp`, `test_dap_lifecycle.cpp` (`dap_lifecycle_tests`):
  - Verifies `SessionState` transitions across `initialize`, `launch`/`attach`, `configurationDone`, disconnect, and teardown.
- `test_dap_commands.hpp`, `test_dap_commands.cpp` (`dap_commands_tests`):
  - Exercises `continue`, `next`, `stepIn`, `stepOut`, `pause`, `terminate`, and `DebugCommands` parity.
- `test_dap_breakpoints.hpp`, `test_dap_breakpoints.cpp` (`dap_breakpoints_tests`):
  - Validates source, conditional, and hit-count `setBreakpoints` serialization and verified breakpoint ID mapping.
- `test_dap_stacktrace.hpp`, `test_dap_stacktrace.cpp` (`dap_stacktrace_tests`):
  - Verifies `threads` and `stackTrace` frame decoding and caller frame switching.
- `test_dap_variables.hpp`, `test_dap_variables.cpp` (`dap_variables_tests`):
  - Tests `scopes`, `variables`, `variablesReference` child expansion, and `VariableTreeModel` integration.
- `test_dap_evaluate.hpp`, `test_dap_evaluate.cpp` (`dap_evaluate_tests`):
  - Tests watch/hover `evaluate` request routing, expression results, and structured error replies.
- `test_dap_memory.hpp`, `test_dap_memory.cpp` (`dap_memory_tests`):
  - Verifies `readMemory` / `writeMemory` encoding, alignment, and failure diagnostics.
- `test_dap_native_bridge.hpp`, `test_dap_native_bridge.cpp` (`dap_native_bridge_tests`):
  - Verifies debuggee PID capture from DAP `process` events and fast native reads via `NativeMemoryReader` (`process_vm_readv`).

## Expected Behavior & Invariants

- **Hermetic Offline Execution**:
  - All 9 targets execute deterministically under `QT_QPA_PLATFORM=offscreen` via mock byte streams without external LLDB/GDB servers.
- **Minimal Dependency Surface**:
  - Each executable links only `DapCoordinator`, `NativeMemoryReader`, `ConfigManager`, and minimal collaborators required by that vertical slice.
- **Verification**:
  - Run `meson test -C builddir dap_parser_tests dap_commands_tests dap_breakpoints_tests dap_stacktrace_tests dap_variables_tests dap_evaluate_tests dap_lifecycle_tests dap_memory_tests dap_native_bridge_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Always run all 9 DAP suites after editing `DapCoordinator` or `IBackendCoordinator`.
- If `src/core/hpc/dap/DapCoordinator.cpp` is split across multiple `.cpp` files, update every `dap_*_test_src` array in `meson.build`.
- Keep `#include "../../src/..."` paths accurate and keep each test file focused and well under `< 950` lines.
- Update this `README.md` whenever adding new DAP protocol test targets.

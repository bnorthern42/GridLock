# GDB/MI3 Rank Backend (`src/core/hpc/gdb/`)

Multi-rank GDB Machine Interface v3 (`gdb --interpreter=mi3`) process launcher, asynchronous MI stream parser, execution/breakpoint command builder, frame/varobj inspector, and payload sanitizer.

## Contents

- `GdbRankCoordinator.hpp` (`712` lines):
  - Declares `GdbRankCoordinator` implementing `IBackendCoordinator`.
  - Owns per-rank `QProcess` instances, POSIX named pipe descriptors, breakpoint/watchpoint/catchpoint tables, signal dispositions, and checkpoint records.
- `GdbRankCoordinator.cpp`:
  - Launches multi-rank sessions via `mpirun`/`mpiexec` and `mkfifo` named pipes, or standalone single-rank `gdb`.
  - Handles local `/proc` attach, remote `gdbserver`/`qemu-user` target connection, signal injection, and process teardown.
- `GdbMiParser.cpp`:
  - Implements incremental GDB/MI3 stream parsing in `processGdbOutput`.
  - Routes `*stopped`/`*running`, `^done`/`^error` tokens, hover/expression evaluations, locals, registers, memory reads, watchpoint triggers, and inferior stdout/stderr.
- `GdbRankCommands.cpp`:
  - Emits MI commands for execution stepping (`-exec-*`), reverse debugging, breakpoints, watchpoints, catchpoints, signal `handle` rules, checkpoints, and variable/register writes.
- `GdbRankInspection.cpp`:
  - Implements 64-frame backtrace unwinding (`requestStackAndThreads`), OS/OpenMP thread parsing, `selectStackFrame`, and lazy `varobj` child expansion (`requestVariableChildren`).
- `MiSanitizer.hpp` (`57` lines), `MiSanitizer.cpp`:
  - Enforces payload size truncation, brace/bracket nesting depth limits, and ASCII control-character escaping on untrusted MI streams.

## Expected Behavior & Invariants

- **Strict `*stopped` Synchronization**:
  - Disassembly, frame, register, and variable queries remain gated until `GdbMiParser.cpp` processes `*stopped` with a valid thread and frame context.
- **FIFO & Process Teardown Safety**:
  - Named FIFOs under `/tmp` must unlink cleanly on exit or error; session termination must cleanly kill child `gdb` PIDs without leaving orphaned processes.
- **Adversarial MI Hardening**:
  - Oversized payloads, deeply nested tuples/lists, and injected control characters pass through `MiSanitizer` guards (`tests/hpc/test_sanitizer.cpp`, `tests/hpc/test_coordinator.cpp`).
- **Bounds-Checked Rank Dispatch**:
  - Every per-rank operation validates `rankId >= 0 && rankId < m_ranks.size()` before indexing rank state.
- **Verification**:
  - Run `meson test -C builddir coordinator_tests sanitizer_tests mi_parser_tests --print-errorlogs` after modifying any file here.

## Maintainer & Agent Notes

- Preserve the split across the four coordinator `.cpp` files (lifecycle, MI parsing, frame/varobj inspection, command emission) so each file stays well under the `< 950` line limit.
- Any new GDB/MI3 async record handler belongs in `GdbMiParser.cpp` with matching unit coverage in `tests/hpc/test_coordinator.cpp` or `tests/hpc/test_mi_parser.cpp`.
- Never fire execution (`-exec-*`) or inspection (`-data-disassemble`, `-stack-list-frames`) commands blindly; synchronize state transitions on `*stopped` / `*running` tokens.
- Check `meson.build` (`coordinator_test_src`, `dap_commands_test_src`, `dap_variables_test_src`, `advanced_features_test_src`) whenever adding or splitting files here, as multiple test targets compile GDB files directly.
- Update this `README.md` whenever adding new MI helpers or splitting coordinator files.

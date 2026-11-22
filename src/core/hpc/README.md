# HPC Backends & Coordination (`src/core/hpc/`)

Top-level backend abstractions (`IBackendCoordinator`, `HpcBackend`) and domain-grouped engines for GDB/MI3, DAP, LSP, cross-rank memory inspection, deadlock analysis, and process telemetry.

## Contents

- `IBackendCoordinator.hpp` (`523` lines):
  - Abstract `QObject` debugger interface implemented identically by `GdbRankCoordinator` and `DapCoordinator`.
  - Defines shared POD structures: `StackFrameEntry`, `ThreadEntry`, `SignalDisposition`, `CatchpointSpec`, and `ProcessCheckpoint`.
  - Declares unified signals/slots for execution control, breakpoints, watchpoints, catchpoints, call stacks, registers, memory reads, and reverse debugging.
- `HpcBackend.hpp` (`72` lines):
  - Defines `HpcResult` and the abstract `HpcBackend` interface for asynchronous cluster queries (`fetchPackages()`, `submitSlurmJob()`, `cancelSlurmJob()`).
- Subdirectories:
  - `gdb/`: Multi-rank `gdb --interpreter=mi3` orchestration, FIFO transport, MI parser, inspection, command builders, and payload sanitizer.
  - `dap/`: Debug Adapter Protocol coordinator (`DapCoordinator`) for local, multi-rank, and remote DAP sessions.
  - `lsp/`: JSON-RPC 2.0 `clangd` coordinator (`LspCoordinator`) for AST hover telemetry.
  - `memory/`: Linux `process_vm_readv` zero-copy reader, cross-rank differ, bounds validator, and CSV/binary exporter.
  - `analysis/`: Automated MPI collective/P2P deadlock analyzer (`DeadlockAnalyzer`) and deterministic `MockHpcBackend`.
  - `system/`: Linux `/proc` process/MPI-job scanner (`ProcessScanner`) and constant-rate MTU telemetry shaper (`TelemetryObfuscator`).

## Expected Behavior & Invariants

- **Protocol-Agnostic UI Contract**:
  - `MainWindow`, dock widgets, and `DebugCommands` drive debuggers exclusively through `IBackendCoordinator` or shared POD types so GDB/MI3 and DAP remain interchangeable.
- **Stop-Driven Query Discipline**:
  - Disassembly, stack frame, thread, variable, and register queries dispatch only after a `*stopped` (or DAP `stopped`) event establishes a valid thread and frame context.
- **Non-Blocking Event Loop**:
  - Coordinator subclasses perform asynchronous `QProcess`, FIFO, and timer I/O without blocking the main Qt GUI thread.
- **Native Domain Terminology**:
  - Class names, comments, and signals avoid external third-party debugger names per `agent.md`.
- **Single-Responsibility Subdirectories**:
  - Protocol adapters (`gdb/`, `dap/`, `lsp/`), Linux memory tools (`memory/`), analysis (`analysis/`), and OS probes (`system/`) remain cleanly partitioned rather than mixed flat in `src/core/hpc/`.

## Maintainer & Agent Notes

- Put protocol-neutral POD structs shared across GDB and DAP in `IBackendCoordinator.hpp`; keep protocol parsers strictly inside `gdb/` or `dap/`.
- When adding, moving, or splitting files in any `src/core/hpc/<subdir>/`, update that subdirectory's `README.md` alongside `meson.build` (`src_files`, `moc_headers`, and affected test targets).
- `IBackendCoordinator.hpp` is `523` lines; if additional diagnostic tables push it past ~750 lines, extract a dedicated `CoordinatorTypes.hpp`.
- Always check every signal signature override (`override`) in both `GdbRankCoordinator` and `DapCoordinator` when updating `IBackendCoordinator.hpp`.
- Verify backend changes with `meson test -C builddir coordinator_tests lsp_tests hpc_memory_tests sanitizer_tests obfuscator_tests memory_export_tests`.

# HPC, GDB/MI & System Tests (`tests/hpc/`)

Targeted unit and integration suites for multi-rank GDB/MI3 coordination, MI parsing/sanitization, `clangd` LSP hover, Linux native memory reads, CSV/binary exports, and MTU traffic shaping.

## Contents

- `test_coordinator.hpp`, `test_coordinator.cpp` (`coordinator_tests` / `gridlock_tests`):
  - Tests `GdbRankCoordinator` multi-rank startup, `*stopped`/`*running` state transitions, breakpoint routing, register extraction, variable inspection, deterministic ROCm `rocgdb` MI3 parsing (`amdgcn:gfx*` pending-to-resolved kernel breakpoints, `AMDGPU Wave` threads, SIMD lanes, and VGPR/PC registers), and live HIP GPU kernel debugging when `/dev/kfd`, `hipcc`, and `rocgdb` are available (gracefully calling `QSKIP` on CPU-only hosts).
- `test_mi_parser.cpp` (`mi_parser_tests`):
  - Tests `GdbConsoleWidget` GDB/MI output normalization, record stream classification, and `InlineFindBar` integration (embeds its `QObject` fixture via `#include "test_mi_parser.moc"`).
- `test_sanitizer.hpp`, `test_sanitizer.cpp` (`sanitizer_tests`):
  - Exercises `MiSanitizer` length truncation, MI brace/bracket recursion depth caps, and ASCII control-character escaping.
- `test_lsp.hpp`, `test_lsp.cpp` (`lsp_tests`):
  - Tests `LspCoordinator` `Content-Length` stdio framing, fragmented JSON-RPC packets, initialization replies, and hover markdown extraction.
- `test_hpc_memory.hpp`, `test_hpc_memory.cpp` (`hpc_memory_tests`):
  - Forks a local child process and verifies `NativeMemoryReader` (`process_vm_readv`) double-array reads directly from child virtual memory.
- `test_memory_export.hpp`, `test_memory_export.cpp` (`memory_export_tests`):
  - Verifies `MemoryExporter` byte-exact raw dumps, 2D CSV matrix formatting across `Int32`/`Float`/`Double`, and buffer underflow guards.
- `test_obfuscator.hpp`, `test_obfuscator.cpp` (`obfuscator_tests`):
  - Verifies `TelemetryObfuscator` constant `1400`-byte MTU padding and `50 ms` timer flushing.

## Expected Behavior & Invariants

- **Ptrace Environment Awareness (`hpc_memory_tests`)**:
  - `test_hpc_memory.cpp` uses Linux `process_vm_readv` between parent and forked child processes under Yama `ptrace_scope`.
- **Single-File `.moc` Rule (`mi_parser_tests`)**:
  - Because `test_mi_parser.cpp` defines its `QObject` test class directly in `.cpp`, `meson.build` must keep `sources : 'tests/hpc/test_mi_parser.cpp'` inside `qt6.compile_moc(...)`.
- **Adversarial Input Hardening**:
  - New GDB/MI parsing or escaping fixes must add regression coverage in `test_sanitizer.cpp` or `test_coordinator.cpp`.
- **Verification**:
  - Run `meson test -C builddir coordinator_tests mi_parser_tests sanitizer_tests lsp_tests hpc_memory_tests memory_export_tests obfuscator_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Coordinator Target Linkage:
  - `coordinator_tests` in `meson.build` links `GdbRankCoordinator.cpp`, `GdbMiParser.cpp`, `GdbRankInspection.cpp`, and `GdbRankCommands.cpp` directly.
  - Update its source list whenever splitting or renaming files in `src/core/hpc/gdb/`.
- Offline Execution Guarantees:
  - Keep `lsp_tests`, `sanitizer_tests`, and `obfuscator_tests` executable without external cluster daemons or live `clangd` binaries.
- Paths & File Ceiling:
  - Keep `#include "../../src/..."` relative includes accurate across every file in `tests/hpc/`.
  - Maintain strict `< 950` lines per file and update this `README.md` when adding new backend test targets.

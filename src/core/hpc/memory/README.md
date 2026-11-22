# Cross-Rank Memory Engine (`src/core/hpc/memory/`)

High-speed Linux cross-process memory reader, byte-level rank differ, overflow-safe slice bounds validator, and structured CSV/binary buffer exporter.

## Contents

- `NativeMemoryReader.hpp` (`34` lines), `NativeMemoryReader.cpp`:
  - Zero-copy Linux `process_vm_readv` reader extracting contiguous virtual address ranges from local MPI rank PIDs.
  - Bypasses GDB/MI hex string serialization bottlenecks on large IEEE-754 arrays (`tests/hpc/test_hpc_memory.cpp`).
- `MemoryDiffer.hpp` (`44` lines), `MemoryDiffer.cpp`:
  - Reads identical virtual address spans from baseline and target rank PIDs.
  - Produces `CompareResult` divergence masks and change metrics for `MemView` and `DifferentialGrid`.
- `MemoryBoundsValidator.hpp` (`62` lines), `MemoryBoundsValidator.cpp`:
  - Implements overflow-safe `uintptr_t` and `size_t` arithmetic checks (`MemoryBoundsValidator`).
  - Guards tensor stride indexing, element byte sizes, and null memory dereferences (`tests/core/test_stride_security.cpp`).
- `MemoryExporter.hpp` (`46` lines), `MemoryExporter.cpp`:
  - Exports decoded buffers (`DataType`: `Int32`, `Float`, `Double`, etc.) to byte-exact raw binary files or reshaped 2D CSV matrices (`tests/hpc/test_memory_export.cpp`).

## Expected Behavior & Invariants

- **Overflow Protection Before Allocation**:
  - Every array plot, tensor slice, memory export, and rank diff must pass `MemoryBoundsValidator` before allocating buffers or issuing `process_vm_readv`.
- **Ptrace Permission Graceful Fallback**:
  - When `process_vm_readv` fails with `EPERM` (e.g., restricted `kernel.yama.ptrace_scope`) or `ESRCH`, readers return explicit failure booleans cleanly so callers fall back to GDB/MI or DAP memory commands.
- **Exact Serialization & Underflow Rejection**:
  - `MemoryExporter` preserves exact byte payloads for raw dumps and rejects under-sized input buffers before reshaping CSV matrix rows and columns.
- **Same-Process Cross-Rank Safety**:
  - `MemoryDiffer` aligns base addresses and bounds checks before comparing remote rank byte buffers, returning clear diagnostics when lengths or PIDs mismatch.
- **Verification**:
  - Run `meson test -C builddir hpc_memory_tests stride_security_tests memory_export_tests dap_native_bridge_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Keep security-critical address and stride arithmetic strictly isolated inside `MemoryBoundsValidator`:
  - Check multiplication overflow (`count * elementSize`) before casting offsets or resizing host buffers.
  - Add failing overflow regression cases in `tests/core/test_stride_security.cpp` for every new indexing helper.
- Standalone test linkage rules:
  - `NativeMemoryReader`, `MemoryBoundsValidator`, and `MemoryExporter` compile directly into `hpc_memory_tests`, `stride_security_tests`, `memory_export_tests`, and `dap_*_tests` in `meson.build`.
  - Keep their dependencies restricted to standard C++, Linux headers, and `QtCore` (zero `QtWidgets` includes).
- Account for Yama `ptrace_scope` restrictions on hardened Linux kernels cleanly when invoking `process_vm_readv` in callers and tests.
- Maintain strict `< 950` lines per file, document overflow preconditions in Doxygen headers, and update this `README.md` when adding new memory tools.

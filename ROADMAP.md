# GridLock Engineering Roadmap

This document tracks planned engineering milestones from **`v0.8.7` through `v1.0.0`**.

For capabilities already shipped in **`v0.8.66`** (including live MPMD GDB/MI3 and LLDB-DAP multi-rank debugging, local `/proc` MPI job attach, remote `qemu-user -g` and `gdbserver` target support, `DeadlockAnalyzer`, `MemView` `process_vm_readv` diffing, `MemoryProfilerWidget` `LD_PRELOAD` heap attribution, `PointerGraphDialog`, 2D CFG + Decompiler, `ClangdLspClient`, non-suspending logpoints, reverse execution, and 12 dark/light themes), see [CHANGELOG.md](CHANGELOG.md) and the user guides in [`docs/`](docs/Quickstart-and-Workflows.md).

> **Scope Note**: GridLock focuses on local workstation MPI debugging, remote `gdbserver` / `qemu-user` target attachment, and offline `#SBATCH` batch script generation; live remote SLURM queue submission over SSH is out of scope.

---

## `v0.8.7`: Execution Control, Catchpoints, and Command Palette

* **Non-Linear Execution Control (`Run to Cursor`, `Set Next Statement / Jump`, `Force Early Return`)**:
  * `Ctrl+F10` Run to Cursor (`-exec-until`).
  * Draggable gutter PC arrow and `Set PC Here` (`$pc` / `-exec-jump` with lexical scope guard).
  * `Force Return Immediately...` (`-exec-return [expr]`).
* **Exception, Syscall, and Shared-Library Catchpoints & Signal Matrix**:
  * C++ `throw`, `rethrow`, and `catch` catchpoints with exception-type regex filtering.
  * Linux syscall catchpoints (`mmap`, `openat`, `clone3`, `ioctl`) and `dlopen`/`dlclose` regex catchpoints.
  * Per-signal (`SIGFPE`, `SIGPIPE`, `SIGUSR1`, `SIGBUS`) Stop / Print / Pass disposition matrix.
* **Copy-on-Write Fork Checkpoints (`checkpoint` / `restart`)**:
  * Process state snapshots via GDB `checkpoint` and `restart` with `/proc/[pid]/fd` and `/proc/[pid]/maps` IPC/RDMA safety checks.
* **Fuzzy Command Palette (`Ctrl+P`)**:
  * Keyboard launcher across `ShortcutManager` actions (`>`), workspace files (`Ctrl+P`), `clangd`/ELF symbols (`@`), themes, and live MPI ranks/groups (`#`).

---

## `v0.8.8`: Numerical, Vector, and SIMD Inspection Suite

* **Cross-Rank IEEE-754 Anomaly and Poison-Pattern Scanner (`NaN` / `Inf` / Subnormals)**:
  * Concurrent `process_vm_readv` sweep across stopped ranks with per-rank distribution stats (`Min | Mean | Max | L2 Norm`), exact index reporting for quiet/signaling `NaN`, `+-Inf`, IEEE-754 subnormals, and allocator poison patterns (`0xdeadbeef`, `0xcdcdcdcd`), plus a 1-click `$mxcsr` hardware FP trap toggle (`FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW`).
* **Watch Expression Format Specifiers and Array-of-Structs (AoS) Columnar Views**:
  * Inline comma suffixes (`ptr, [N]`, `,x`, `,b`) and AoS tabular projections (`particles, [1000], cols(pos.x, pos.y, energy)`) strided via `process_vm_readv` and DWARF / `ptype /o` member offsets.
* **Typed SIMD Vector Lane and Flag Bitfield Inspector**:
  * Expandable `XMM`/`YMM`/`ZMM` typed vector lanes (`f64x4`/`f64x8`, `f32x8`/`f32x16`, `i64x8`, `i32x16`) with per-lane step diff highlighting, `k0..k7` AVX-512 mask checkboxes, and decoded `RFLAGS` / `MXCSR` exception badges (`IE`, `DE`, `ZE`, `OE`, `UE`, `PE`, `FTZ`, `DAZ`).
* **Multidimensional Array, Slice, and Heatmap Visualizer**:
  * Dockable 1D/2D/3D array and tensor inspector (`[Nx, Ny, Nz]` shape/stride, C row-major vs. Fortran column-major) with a virtualized 2D numerical table, multi-rank 1D curve overlays for halo/ghost-cell verification, and a 2D heatmap (`Viridis`, `Plasma`, `Ocean`).

---

## `v0.8.9`: Post-Mortem Crash Analysis and Low-Level Memory Layout

* **Post-Mortem Core Dump and Multi-Rank Crash Loader (`coredumpctl` Integration)**:
  * `File -> Open Core Dump(s)...` (multi-rank `core.*` globs) and `File -> Recent System Crashes (systemd-coredump)` (`coredumpctl list --json=short` + `coredumpctl dump`), ELF `PT_NOTE` (`NT_GNU_BUILD_ID` / `NT_FILE`) executable matching, and read-only `POST-MORTEM` inspection across all docks.
* **DWARF Struct Layout, Alignment Padding, and Cache-Line Inspector**:
  * `pahole`-style struct byte-map showing field offsets, bitfields, vptr slots, compiler padding holes, wasted byte percentage, field packing preview, 64-byte cache-line boundaries for OpenMP false-sharing analysis, and live `(StructType*)0x...` typed overlays in `MemView`.
* **Annotated Stack Frame Memory and Pointer-Telescoping Inspector**:
  * Aligned 8-byte stack slot table (`[RSP .. RBP .. Caller CFA]`) highlighting saved `RIP`, `RBP` chains, stack canaries, DWARF stack locals, and 4-hop pointer telescoping (`stack -> heap -> .rodata` symbol/string resolution).

---

## `v0.9.0`: Large-Scale HPC Runtime Introspection

* **Merged Parallel Stack Tree (STAT-Style Prefix Aggregation) and Dynamic Rank Sub-Groups**:
  * Prefix-trie call graph in `CallStackDockWidget` merging backtraces across all $N$ ranks with proportional `[M/N ranks]` membership bars to isolate stragglers, paired with `RankBitset` execution sub-groups (`All Ranks`, `Root (0)`, `Workers (1..N-1)`, `Stopped Ranks`, or custom branch subsets).
* **OpenMP Logical Region, Task, and Barrier Inspector (`OMPD` / `libompd.so`)**:
  * Logical OpenMP team/thread hierarchy labeling, runtime frame collapsing, originating `omp task` backtraces, and a 1-click `"Align All Team Threads Here"` action.
* **MPI Message Queue and Unmatched `MPI_Request` Inspector (`MPIR` / `mqs_` DLL Interface)**:
  * Host adapter for `mpi_interface.h` (`libompi_dbg_msgq.so` / `libtvmpich.so`) backed by `process_vm_readv` callbacks, exposing **Posted Receives**, **Pending Sends**, and **Unexpected Messages** inside `MpiDiagnosticsWidget` alongside a 2D Rank-by-Rank communication matrix.

---

## `v0.9.x` to `v1.0.0`: UI/UX Cohesion and `v1.0.0` Stabilization

* **Progressive Disclosure and Toolbar Consolidation**: Grouping specialized HPC and reverse-engineering controls contextually so inactive tools do not crowd the main viewport.
* **Task-Focused Workspace Presets**: 1-click layout presets (*Source & MPI Debugging*, *Binary CFG & Decompiler*, *Memory & Numerical Profiling*, *Post-Mortem Crash Triage*) verified across 1080p, 1440p, 4K, and tiling Wayland compositors.
* **Cross-View Selection and Keyboard Navigation**: Unified badge styling, keyboard focus traversal, and synchronized address/symbol/rank selection across all docks and pop-out windows.
* **Production Hardening and `v1.0.0` Release Readiness**: Resolving OpenMPI, MPICH, PMIx, and SLURM edge cases and locking configuration file schemas for `v1.0.0`.

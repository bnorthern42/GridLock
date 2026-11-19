# GridLock Engineering Roadmap

This roadmap tracks completed milestones (`Phases 1–12` / `v0.8.66`) and upcoming releases for GridLock, a C++23 and Qt6 MPI debugger and ELF binary inspector on Linux.

---

## Completed Foundations (Phases 1–6.5 / `v0.7.0`)

### Phase 1–3: Multi-Rank MPI Debugging & Binary Inspection
- [x] **ELF64 & DWARF Debugging (`ElfBinaryInspector`, `GdbRankCoordinator`, `DapCoordinator`)**: Disassembly via `objdump -d -M intel --insn-width=10`, symbol extraction via `nm`, and `.debug_line` source-to-address mapping via `objdump --dwarf=decodedline` / `readelf -wL`.
- [x] **Breakpoint-First Execution & Deadlock Detection (`DeadlockAnalyzer`)**: On-demand target compilation (`mpicxx -g -O0`), automatic halt at user-set breakpoints or `main()` entry, and wait-for cycle detection across blocked `MPI_Send`, `MPI_Recv`, and `MPI_Barrier` calls.
- [x] **Process Memory Reading & Cross-Rank Diffing (`NativeMemoryReader`, `MemoryDiffer`, `MemView`)**: `process_vm_readv` memory reads with stride bounds validation (`MemoryBoundsValidator`), cross-rank byte diffing, and live hex/instruction binary patching (`savePatchedBinary`).
- [x] **Cross-Rank Variables & Registers (`DifferentialGrid`, `VariablesDockWidget`, `RegisterView`)**: Side-by-side inspection of C/C++ expressions, stack frames, and x86_64 registers across active MPI ranks.

### Phase 4–6.5: UI Panels, `clangd` Completion & Linux Packaging (`v0.7.0`)
- [x] **Editor Gutter & Breakpoint Popover (`SourceCodeView`, `ConditionalBreakpointDialog`)**: Separate gutter columns for breakpoints, current instruction pointer, and line numbers, plus a right-click popover for rank-filtered conditional breakpoints (`All MPI Ranks` or `Focused Rank Only`).
- [x] **`clangd` JSON-RPC 2.0 Completion (`ClangdLspClient`)**: Asynchronous stdio client providing C/C++ struct and member completion inside `ConditionalBreakpointDialog` and `SourceCodeView` (`Ctrl+Space` or `.`, `->`, `::`).
- [x] **Dock Layout & Wayland Support**: Left vertical icon rail (`Debug`, `MPI`, `Terminal`, `Docs`) for the bottom dock and native drag/redock behavior for floating panels (`ServerRackView`, `RegisterView`, `VariablesDockWidget`).
- [x] **Themes & SLURM Script Importer (`ThemeManager`, `PreferencesDialog`)**: Live theme switching and `#SBATCH` batch script import/export for 16 common SLURM directives.
- [x] **System Package Search & Offline Docsets (`HpcConsoleWidget`, `DocsetManager`)**: Package search across `rpm`, `dnf5`/`dnf4`, `pacman`, `apt`, `zypper`, and `spack` with `Table`, `List`, and `Raw` views, SQLite3 Zeal `.docset` and `man` lookup (`F1`), and embedded `QTermWidget` PTY terminals.
- [x] **GitHub Actions `appimagetool` Release Pipeline**: Self-contained `AppDir` bundling with `$ORIGIN/../lib` RPATH configuration, headless Meson test runs, and `AppImage` packaging.

---

## Phase 7: Static Analysis & Security Hardening

- [x] **External Static Analysis Passes (`cppcheck`, `clang-tidy`)**:
  - Ran `cppcheck` and `clang-tidy` (`bugprone-*`, `modernize-*`, `performance-*`, `cppcoreguidelines-*`) across `src/` and `tests/` to clean up dead code, uninitialized members, variable shadowing, and `QObject` lifetime issues.
- [x] **SonarQube / SonarCloud & Compiler Quality Gates**:
  - Updated `sonar-project.properties` (`sonar.projectVersion=0.8.6`) for complexity and coverage tracking alongside zero-warning compiler flags (`-Wall -Wextra -Wpedantic`).
- [x] **Memory & Process Security Audit (`MemoryBoundsValidator`, `ElfBinaryInspector`, `MiSanitizer`)**:
  - Validated `uint64_t` overflow guards in `MemoryBoundsValidator`, corrupt/truncated ELF header rejection in `ElfBinaryInspector`, shell metacharacter filtering in `MiSanitizer`, and direct argument-list process spawning across all `QProcess` and `KPtyProcess` call sites under ASan, UBSan, and `MALLOC_PERTURB_`.

---

## Phase 8: Theme Engine & UI Fixes

- [x] **Light Theme Contrast Fixes (`daylight_light`, `solarized_light`, `catppuccin_latte`)**:
  - Replaced hardcoded dark backgrounds in `MemView` byte cells, `ProjectExplorerWidget` (`FILES | SYMBOLS | STRINGS`) buttons, the binary section navigation bar (`.plt`, `.text`, `.rodata`, `.data`, `stack`), and `CODE | REVERSE` toggle buttons with theme-aware colors.
- [x] **Disassembly Header Sizing & Theme Screenshots**:
  - Fixed horizontal label clipping on the `SPLIT (CFG+DECOMP)`, `DECOMPILER`, and `CFG (SPACE)` buttons in `DisassemblyView` and generated preview screenshots for `Solarized Warm` (`theme_solarized_light.png`) and `Catppuccin Latte` (`theme_catppuccin_latte.png`) in `docs/THEMES.md`.
- [x] **Palette Token Struct & 12 Built-In Themes (`ThemePalette`, `ThemeManager`)**:
  - Added semantic `ThemePalette` tokens (`canvasBg`, `panelBg`, `headerBg`, `border`, `accent`, `textPrimary`, `textMuted`, `selectionBg`, `disasmOpcode`, `disasmRegister`, `disasmAddress`, `cfgEdgeTrue`, `cfgEdgeFalse`, `cfgEdgeUncond`) and expanded the catalog to 12 themes with `Nord` (`nord_dark`), `One Dark Pro` (`one_dark_pro`), `One Light` (`one_light`), and `Dracula` (`dracula_dark`).
- [x] **Movable Toolbars, Live CFG Map Default & Status Bar Memory Telemetry (`MainWindow`, `DisassemblyView`, `ElfBinaryInspector`)**:
  - Enabled movable toolbars, made the multi-block Control Flow Graph (`MAP (CFG)`) the persistent default disassembly view across live stepping, and added real-time per-rank `STACK` and `HEAP` telemetry alongside host `RAM` and `CPU %` in the bottom status bar ribbon.

---

## Phase 9: Guided Tutorials (`docs/TUTORIAL.md`)

- [x] **Step-by-Step Walkthrough (`docs/TUTORIAL.md`)**:
  - Expanded the hands-on developer guide covering:
    1. Building and selecting all 7 bundled MPI showcase examples (`deadlock_ring.cpp`, `inspection_demo.cpp`, `collective_mismatch.cpp`, `ghost_cell_drift.cpp`, `heap_imbalance.cpp`, `tag_mismatch_stall.cpp`, `simd_struct_watch.cpp`) from the `TARGET:` toolbar selector or CLI.
    2. Diagnosing circular `MPI_Send` deadlocks, `MPI_Bcast` vs. `MPI_Barrier` collective divergence, 1D stencil `NaN` propagation, per-rank `STACK` / `HEAP` telemetry imbalance, and point-to-point tag stalls.
    3. Comparing cross-rank watch expressions in `DifferentialGrid`, highlighting memory differences across ranks with `DIFF RANKS`, and saving memory slices with `EXPORT` in `MemView`.
    4. Looking up C/C++, MPI, and x86_64/ARM64 assembly documentation in offline Zeal docsets and `man` pages (`F1`), importing `#SBATCH` scripts, and searching dependencies in the `PACKAGE MANAGER` dock.

---

## Phase 10 (`v0.8.0`): Source Editor & `clangd` LSP Features

> **Scope Note**: GridLock focuses on local and workstation MPI debugging and offline `#SBATCH` script generation; live remote SLURM queue submission is out of scope.

- [x] **Project File Tree Operations (`ProjectExplorerWidget`)**:
  - Added right-click context menu actions and programmatic helpers on the `FILES` tree for `New Source File (.cpp)`, `New Header File (.hpp)`, `New Folder`, `Rename (F2)`, `Delete`, and 1-click source registration in `meson.build` or `CMakeLists.txt`.
- [x] **Dirty Buffer Tracking & Split Editors (`EditorTabManager`, `SourceCodeView`)**:
  - Added unsaved buffer indicators (`●`) on editor tabs, close prompts, `Ctrl+Shift+S` (*Save All*), horizontal and vertical split editor panes (`Split Right`, `Split Down`), Header/Source switching (`F10`), and a scope breadcrumb bar (`File > Class/Struct > Function`).
- [x] **`clangd` Diagnostics, Hover & Navigation (`ClangdLspClient`, `SourceCodeView`)**:
  - Added inline compiler error and warning underlines (`textDocument/publishDiagnostics`), symbol/Doxygen hover tooltips (`textDocument/hover`), `F12` Go-to-Definition (`textDocument/definition`), `Shift+F12` Find References (`textDocument/references`), and `Shift+F6` AST symbol rename (`textDocument/rename`).
- [x] **Git Diff Gutter & Debugger Engine Selector (`SourceCodeView`, `PreferencesDialog`, `ConfigManager`)**:
  - Added `git diff` gutter indicators (added, modified, and deleted line markers) in `SourceCodeView` and a debugger backend selector (`gdb`, `lldb-dap`, `cuda-gdb`, `rocgdb`) in `Preferences -> Debugger`.

---

## Phase 11 (`v0.8.5`): Live Multi-Rank Debugging, Observability & Release Stabilization

- [x] **Live Multi-Rank GDB/MI3 & DAP Backends (`GdbRankCoordinator`, `DapCoordinator`)**:
  - Replaced static simulation stepping with real MPMD GDB/MI3 process orchestration, per-rank OS PID tracking, `/proc` target attach (`AttachProcessDialog`), hardware data watchpoints, and 60 FPS coalesced `PROGRAM OUTPUT` / `GDB CONSOLE` streaming.
- [x] **Stack Unwinding, Composite Inspection & Live Filtering (`CallStackDockWidget`, `VariablesDockWidget`, `RegisterView`, `ProjectExplorerWidget`)**:
  - Added 64-frame call stack unwinding and OpenMP/OS thread selection, recursive `{...}` struct/class/vector expansion, and live search filtering plus interactive column sorting across `VARIABLES`, `REGISTERS`, `SYMBOLS`, and `STRINGS`.
- [x] **MPI + OpenMP Execution Reliability & Workspace Sync (`MainWindowWorkspace`, `SessionManager`, `TerminalDockWidget`)**:
  - Eliminated running-state MI stalls across multi-rank OpenMP solvers, synchronized `ProjectExplorerWidget` + `TARGET:` + `TerminalDockWidget` working directory on workspace switch, persisted recent sessions in `debugger.conf`, and packaged dual Lite and Tutorials AppImage editions.

---

## Phase 12 (`v0.8.6`): Memory Attribution, Pointer Graph Visualization & Time-Travel Debugging

- [x] **Visual Library & Heap Memory Profiler (`MemoryProfilerWidget`, `MemView`)**:
  - Combined `/proc/<pid>/smaps` VMA region attribution with a lightweight `LD_PRELOAD` caller-attribution tracker and `addr2line` symbol resolution in an interactive 2D treemap, proportional stacked bar, and sortable table with 5 color schemes (`Distinct`, `Viridis`, `Plasma`, `Ocean`, `HighContrast`) and standalone pop-out window support.
- [x] **Interactive Pointer & Struct Graph Visualizer (`PointerGraphDialog`)**:
  - Added a DDD-inspired 2D pointer and struct graph canvas with draggable struct cards, automatic 1-hop composite and pointer expansion, directed cubic Bezier edges, null-pointer (`0x0`) guards, and cycle/shared-alias detection.
- [x] **Inline Variable Ghost Values, Logpoints & Reverse Debugging (`SourceCodeView`, `GdbRankCoordinator`, `DapCoordinator`)**:
  - Added inline variable ghost values (resolving local scalars and dotted/arrow member chains), non-suspending Logpoints (`{expr}`) and breakpoint hit ignore counts, and GDB Process Record / DAP reverse execution (`⏺ Record`, `↶ Step Back`, `⏮ Rev Continue`).

---

## Upcoming Milestones (`v0.8.7` – `v1.0.0`)

### `v0.8.7` — Execution Power Tools, Catchpoints & Fast Ergonomics
- **Non-Linear Execution Control (`Run to Cursor`, `Set Next Statement / Jump`, `Force Early Return`)**: `Ctrl+F10` Run to Cursor (`-exec-until`), draggable gutter PC arrow / `Set PC Here` (`$pc` / `-exec-jump` with lexical scope guard), and `Force Return Immediately...` (`-exec-return [expr]`).
- **First-Class Catchpoints & Signal Disposition Manager**: C++ `throw`/`rethrow`/`catch` catchpoints (with type regex filtering), Linux syscall catchpoints (`mmap`, `openat`, `clone3`, `ioctl`), `dlopen`/`dlclose` regex catchpoints, and a per-signal (`SIGFPE`, `SIGPIPE`, `SIGUSR1`, `SIGBUS`) Stop/Print/Pass matrix.
- **Instant Copy-on-Write Fork Checkpoints (`checkpoint` / `restart`)**: Native hardware-speed process state snapshots via GDB `checkpoint`/`restart` with `/proc/[pid]/fd` and `/proc/[pid]/maps` IPC/RDMA safety checks.
- **Unified Fuzzy Command Palette (`Ctrl+Shift+P` / `Ctrl+P`)**: Modal-free fuzzy launcher across `ShortcutManager` actions (`>`), workspace files (`Ctrl+P`), `clangd`/ELF symbols (`@`), themes, and live MPI ranks/groups (`#`).

### `v0.8.8` — Numerical, Vector & SIMD Inspection Suite
- **Cross-Rank IEEE-754 Anomaly & Poison-Pattern Scanner (`NaN` / `Inf` / Subnormals)**: Concurrent `process_vm_readv` sweep across stopped ranks with per-rank distribution sparklines (`Min | Mean | Max | L2 Norm`), exact index flagging for Quiet/Signaling `NaN`, `±Inf`, IEEE-754 denormals/subnormals, and allocator poison patterns (`0xdeadbeef`, `0xcdcdcdcd`), plus a 1-click `$mxcsr` hardware FP exception trap toggle (`FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW`).
- **Watch Expression Format Specifiers & AoS Columnar Table Views**: Inline comma suffixes (`ptr, [N]`, `,x`, `,b`) and Array-of-Structs tabular projections (`particles, [1000], cols(pos.x, pos.y, energy)`) strided locally via `process_vm_readv` and DWARF / `ptype /o` member offsets.
- **Typed SIMD Vector Lane & Control/Flag Bitfield Inspector**: Expandable `XMM`/`YMM`/`ZMM` typed vector lanes (`f64x4`/`f64x8`, `f32x8`/`f32x16`, `i64x8`, `i32x16`) with per-lane step diff highlighting, `k0..k7` AVX-512 mask checkboxes, and decoded `RFLAGS` / `MXCSR` sticky exception badges (`IE`, `DE`, `ZE`, `OE`, `UE`, `PE`, `FTZ`, `DAZ`).
- **Multidimensional Array, Slice & Heatmap Visualizer**: Dockable 1D/2D/3D array and tensor inspector (`[Nx, Ny, Nz]` shape/stride, row-major vs. Fortran column-major) with a virtualized 2D numerical table, multi-rank 1D curve overlays for halo/ghost-cell verification, and a 2D `QImage` heatmap (`Viridis`/`Plasma`/`Ocean`).

### `v0.8.9` — Post-Mortem Crash Analysis & Low-Level Memory Layout
- **Post-Mortem Core Dump & Multi-Rank Crash Loader (`coredumpctl` Integration)**: `File -> Open Core Dump(s)...` (multi-rank `core.*` globs) and `File -> Recent System Crashes (systemd-coredump)` (`coredumpctl list --json=short` + `coredumpctl dump`), ELF `PT_NOTE` (`NT_GNU_BUILD_ID` / `NT_FILE`) executable matching, and `POST-MORTEM` inspection across all docks.
- **DWARF Struct Layout, Alignment Padding, Cache-Line & Hex-Dump Overlay Inspector**: `pahole`-style struct byte-map showing field offsets, bitfields, vptr slots, compiler padding holes, wasted byte %, optimal field packing preview, 64-byte hardware cache-line boundaries for OpenMP false-sharing analysis, and live `(StructType*)0x...` typed hex-grid overlays in `MemView`.
- **Annotated Stack Frame Memory & Pointer-Telescoping Inspector**: Aligned 8-byte stack slot table (`[RSP .. RBP .. Caller CFA]`) bracketing stack frames, highlighting saved `RIP`, `RBP` chains, stack canaries, DWARF stack locals, and 4-hop pointer telescoping (`stack -> heap -> .rodata` symbol/string resolution).

### `v0.9.0` — Flagship HPC Scale & Runtime Introspection (`TotalView` / `Forge` Parity)
- **Merged Parallel Stack Tree ("STAT-Style" Prefix Aggregation) & Dynamic Rank Control Sub-Groups**: Prefix-trie call graph in `CallStackDockWidget` merging backtraces across all $N$ ranks with proportional `[M/N ranks]` membership bars to isolate stragglers, paired with `RankBitset` execution sub-groups (`All Ranks`, `Root (0)`, `Workers (1..N-1)`, `Stopped Ranks`, or custom branch subsets).
- **OpenMP Logical Region, Task & Barrier Inspector (`OMPD` / `libompd.so` + Thread Alignment)**: Logical OpenMP team/thread hierarchy labeling, runtime frame collapsing, originating `omp task` backtraces, and a 1-click `"Align All Team Threads Here"` button.
- **MPI Message Queue & Unmatched `MPI_Request` Inspector (`MPIR` / `mqs_` DLL Interface)**: Host adapter for `mpi_interface.h` (`libompi_dbg_msgq.so` / `libtvmpich.so`) backed by `process_vm_readv` callbacks, exposing **Posted Receives**, **Pending Sends**, and **Unexpected Messages** inside `MpiDiagnosticsWidget` alongside a 2D Rank × Rank communication matrix.

### `v0.9.x` → `v1.0.0` — UI/UX Cohesion, Visual Ergonomics & Workstation Unification (Feature Freeze & Polish)
- **Progressive Disclosure & Visual De-Cluttering**: Consolidating overlapping toolbars, context menus, and dock headers so advanced HPC and reverse-engineering tools appear contextually when needed without overwhelming the primary viewport.
- **Task-Focused Workspace Perspective Presets**: 1-click and auto-switching layout perspectives (*Source & MPI Debugging*, *Binary CFG & Decompiler*, *Memory & Numerical Profiling*, *Post-Mortem Crash Triage*) with clean splitter proportions and zero clipped headers across 1080p, 1440p, and 4K/tiling Wayland setups.
- **Unified Cross-View Selection, Navigation & Theme Harmony**: Consistent typography, spacing, badge semantics, keyboard navigation (`Ctrl+Shift+P`), and synchronized address/symbol/rank selection across all docks and pop-out windows.
- **Real-World Dogfooding & `v1.0.0` Release Readiness**: Addressing community and production HPC user feedback, hardening OpenMPI/MPICH/SLURM edge cases, and stabilizing ABI/config compatibility for `v1.0.0`.


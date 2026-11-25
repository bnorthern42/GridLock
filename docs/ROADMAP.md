# GridLock Engineering Roadmap

This document tracks completed **`v0.9.0` (Current)** milestones and planned post-`v0.9.0` engineering targets through **`v1.0.0`**.

For capabilities shipped through **`v0.9.0`** (including the frozen C99 Plugin ABI `v1.0`, multi-forge `plugins.toml` / `plugins-lock.toml` installer, live MPMD GDB/MI3 and LLDB-DAP multi-rank debugging, local `/proc` MPI job attach, remote `qemu-user -g` and `gdbserver` target support, `DeadlockAnalyzer`, `MemView` `process_vm_readv` diffing, `MemoryProfilerWidget` `LD_PRELOAD` heap attribution, topological `PointerGraphDialog`, multi-rank `ArrayPlotDialog`, `ProgrammerCalculatorDialog`, multi-scope Search Everywhere, customizable toolbar chrome, 2D CFG + Decompiler, `ClangdLspClient`, non-suspending logpoints, CoW fork checkpoints, reverse execution, and 12 dark/light themes), see [CHANGELOG.md](https://github.com/bnorthern42/GridLock/blob/master/CHANGELOG.md) and the user guides in `docs/`.

> **Scope Note**: GridLock focuses on local workstation MPI debugging, remote `gdbserver` / `qemu-user` target attachment, and offline `#SBATCH` batch script generation; live remote SLURM queue submission over SSH is out of scope.

---

## `v0.8.7`: Execution Power Tools, Catchpoints, and Fast Ergonomics (Completed)

- [x] **Fuzzy Command Palette (`Ctrl+P` / `Ctrl+Shift+P`)**: Keyboard launcher across `ShortcutManager` actions (`>`), workspace files (`Ctrl+P`), `clangd`/ELF symbols (`@`), themes, and live MPI ranks/groups (`#`).
- [x] **Undocked Programmer's Calculator (`Ctrl+Alt+C`)**: Floating base converter and keypad (`HEX`, `DEC` signed/unsigned, `OCT`, `BIN`, IEEE-754 float/double, `8/16/32/64-bit` width & `BSWAP`, interactive 64-bit toggle grid, full `0-9`/`A-F`/bitwise/arithmetic/float keypad, Preferences defaults, and right-click "Open in Programmer's Calculator" handoff across code, variables, registers, memory, and disassembly views).
- [x] **Live Auto-Refreshing 1D/2D Array and Field Plotter (`Ctrl+Alt+G`)**: Floating 1D line/bar and 2D colormapped heatmap plotter (`Viridis`, `Plasma`, `Diverging Cool-Warm`, `Grayscale`) with IEEE-754 `NaN`/`Inf` anomaly flagging, `Min`/`Max`/`Mean`/`L2` telemetry, previous-stop ghost curve delta overlay, CSV/PNG export, and `gnuplot` pipe.
- [x] **Persistent Auto-Updating Pointer and Struct Graph Nodes**: Pin root expressions across stops in `PointerGraphDialog` with automatic re-evaluation and `"old -> new"` field-level mutation highlighting.
- [x] **Complete Instruction and Non-Linear Execution Control (`nexti`, `Run to Cursor`, `Set Next Statement / Jump`, `Force Early Return`)**:
  - [x] Instruction step-over `nexti` (`-exec-next-instruction`) alongside `stepi`.
  - [x] `Ctrl+F10` Run to Cursor (`-exec-until`).
  - [x] Draggable gutter PC arrow and `Set Next Statement / Jump` (`-exec-jump` / PC relocation with lexical scope safety checks).
  - [x] `Force Early Return...` (`-exec-return [expr]`).
- [x] **Non-Destructive Breakpoint Lifecycle and Temporary/Regex Breakpoints**:
  - [x] Toggle breakpoint enabled/disabled state (`-break-enable` / `-break-disable`) with hollow gutter markers instead of deleting breakpoints.
  - [x] Temporary one-shot breakpoints (`tbreak`), regex symbol breakpoints (`rbreak`), and custom GDB command lists (`commands ... end`).
- [x] **Signal Disposition Matrix and Catchpoints**:
  - [x] Per-signal (`SIGFPE`, `SIGPIPE`, `SIGUSR1`, `SIGBUS`) Stop / Print / Pass disposition matrix (`handle <SIG>`).
  - [x] C++ `throw`, `rethrow`, and `catch` catchpoints with exception-type regex filtering.
  - [x] Linux syscall catchpoints (`mmap`, `openat`, `clone3`, `ioctl`) and `dlopen`/`dlclose` regex catchpoints.
- [x] **GDB Console Readline Ergonomics**:
  - [x] Persistent command history (`Up`/`Down` saved across sessions), `Ctrl+R` incremental reverse search, and live `complete` Tab completion popup in `GdbConsoleWidget`.
- [x] **Copy-on-Write Fork Checkpoints (`checkpoint` / `restart`)**:
  - [x] Process state snapshots via GDB `checkpoint` and `restart` with `/proc/[pid]/fd` and `/proc/[pid]/maps` IPC/RDMA safety checks.

---

## `v0.8.8`: UX Polish, Progressive Disclosure & Workflow Ergonomics (Completed)

- [x] **Topological Pointer & Struct Graph Enhancements**:
  - [x] Layered Sugiyama DAG layout preserving manual node drag positions across steps, incremental multi-root pinning, and per-root unpinning.
  - [x] Mouse-wheel zoom (`0.25x` to `2.5x`), interactive corner minimap, per-node field suppression (`+N hidden`), horizontal/vertical card orientation toggle, scalar format overrides (`/x`, `/d`, `/b`), `Shift+Click` per-node width expansion, un-elided full-value hover tooltips, double-click pointer dereferencing, dynamic alias splitting/merging on pointer reassignment, and `2x` PNG / vector SVG export.
- [x] **Multi-Rank & Scientific Array Plotter Extensions**:
  - [x] Multi-rank 1D overlay mode for halo/ghost-cell comparison, `start:end:stride` slicing, base-10 logarithmic (`Log10`) Y and colorbar scaling, interactive drag-box zoom and reset, and scalar time-series history mode over stops.
  - [x] `Complex64` / `Complex128` decoding (`Magnitude`, `Phase`, `Real`, `Imaginary`), 2D matrix `splot` / `pm3d` Gnuplot export with executable/stderr validation, and zero-copy virtualized 2D/3D tensor tables (`[Nx, Ny, Nz]` C row-major vs. Fortran column-major).
- [x] **Search Everywhere, Customizable Toolbar & Progressive Disclosure**:
  - [x] Multi-scope Search Everywhere (`Double Shift`, `Ctrl+P`, `Ctrl+Shift+P`) with `All | Files | Symbols | Text | Actions | Ranks` tabs, 2-line workspace-relative rows, 35-line numbered context preview, and `Small` / `Medium` / `Large` / `X-Large` popup size presets.
  - [x] Compact `< 1,040 px` labeled toolbar (`Config`, `Cont`, split dropdown compartments, `F10`/`F11`/`Shift+F11` source stepping) plus full user layout customization in `Preferences -> Appearance -> Toolbar` (`24px` spacers, flex springs, vertical separators, item reordering, and reset).
  - [x] `Source` / `Debug` / `Binary` / `MPI` perspectives without force-redocking floating panels, contextual `AddressNavToolbar` and `DisassemblyView` gating, automatic collapsing of multi-rank chrome when `ranks == 1`, and clean empty-state watch placeholders.
- [x] **Unified Multi-Rank Scope, Master Rank Focus & Launch Ergonomics**:
  - [x] Explicit `All Ranks` vs. `Rank k` execution scope toggle, `Pause All Ranks (F6)` (`haltAll`), `ServerRackView` `DEADLOCK` / `EXITED` badges and state filter, and master `setFocusedRank()` synchronization across all docks and dialogs (lifting the 8-rank plotter cap).
  - [x] `All Ranks` expression evaluation broadcast, inline `Launch Options` (`Config`) popover (`--args` scoped per target/workspace, working directory, `Stop at main()`), CLI positional target / `-- <args...>` parsing, and asynchronous binary launch.
- [x] **Cross-View Inspection, Live Editing & Safety Guardrails**:
  - [x] Single-popup `DisassemblyView` / CFG / Decompiler context menus, functional `Watch` registration/removal, exact `MemView` cursor offset handoff to Programmer's Calculator, `SourceCodeView` pointer/struct graph context actions, and live expression autocomplete.
  - [x] Full-value tooltips and `Inspect Full Value...` popover, live in-place variable and register value editing, fail-loud compilation and `+x` permission checks blocking stale-binary launches, validated hex/NOP instruction patching with `Revert All Patches`, and confirmation/undo guardrails for checkpoints and graph/plot clearing.
- [x] **Editor, Keybindings, Project Tree, Terminal, Testing & Modular Architecture**:
  - [x] Real `QSplitter` split editor, inline `Ctrl+F` editor find bar, exact 4-column soft `Tab`/`Backtab` indentation, `CodeMinimap` toggle + rebindable shortcut, virtualized large-CSV previewer, opaque `KeySequenceRecorderWidget` in `Keybindings`, and hierarchical `QTreeWidget` sidebar in `PreferencesDialog`.
  - [x] Unified `~/.config/gridlock/` configuration and `Recent Sessions` persistence, proportional UI typography and multi-language vector file/folder icons in `ProjectExplorerWidget`, clean terminal `Clear` without Zsh `Ctrl+L` expansion, compact terminal context menu + `Ctrl+Shift+C`/`V`, clean exit-while-running process/PTY reaping, zero-dependency Node.js CLI/tutorial E2E suite (`scripts/e2e_cli_suite.mjs`), per-subdirectory `README.md` guides, Graphify AST knowledge graph integration, and sub-500-line modular splits across `src/` and `tests/`.

---

## `v0.9.0`: Native C99 Plugin ABI (`v1.0`), Multi-Forge Ecosystem & UI Extensibility (Current / Completed)

- [x] **Frozen C99 Native Plugin ABI (`v1.0`) & Header-Only C++23 Wrappers**:
  - [x] Binary-stable `include/gridlock/plugin_api.h` (`GRIDLOCK_PLUGIN_ABI_MAJOR = 1u`, `GRIDLOCK_PLUGIN_ABI_MINOR = 0u`, `9` standard-layout POD structs with `16 B` version headers and `void* reserved[8]` expansion tail, `28` `GridLockHostApi` callbacks, and zero Qt dependencies) plus optional `noexcept` / `std::expected` RAII wrappers in `include/gridlock/plugin_cpp_helpers.hpp`.
  - [x] Zero-inferior-call DWARF/MI type and contiguous-buffer introspection (`query_type_info`, `resolve_contiguous_range`) supporting C arrays `T[N]`, `std::array`, `std::vector`, `std::span`, and `std::valarray` across `libstdc++` and `libc++`, alongside silent plugin MI channels (`m_pendingPluginEvals` and `m_pendingPluginMemReads`) that never pollute **Evaluator** history.
- [x] **Decentralized Multi-Forge Installer, Declarative Manifest Sync & SHA-256 Workspace Trust**:
  - [x] Shell-free `QProcess` installer (`PluginInstaller` / `PluginManifest`) supporting GitHub, GitLab (nested subgroups), Codeberg, Gitea, and custom `https://` / `git@` Git URLs, plus `plugins.toml` and user-global `~/.config/gridlock/plugins-lock.toml` commit/checksum pinning.
  - [x] Pre-`dlopen` workspace `SHA-256` trust gate (`[plugins.trusted_workspace_binaries]`), dual **Global** / **Workspace** toggles, `Version (<commit_short>)` provenance column, per-row **Update**, and toolbar **Update All**.
- [x] **Live Hot-Reload, Conformance Verifier & Scaffolding Generator**:
  - [x] Live in-place **Hot-Reload Now** (`PluginManager::hotReloadPlugins`) with active-session `on_session_start` replay and Linux `STB_GNU_UNIQUE` (`'u'`) fallback warnings.
  - [x] Sandboxed conformance verifier (`PluginVerifier::verifyBinary`, exposed via `gridlock --verify-plugin <path>`) checking symbol hygiene, ABI headers, regex/menu registrations, dry-run lifecycle hooks, and zero leaked `alloc_bytes` heap blocks.
  - [x] Interactive and `--yes` CLI Lua plugin scaffolding generator (`scripts/init_plugin.lua`) emitting C++23 or C99 starters, `plugin.map` linker version scripts, and Meson/CMake/Make build recipes.
- [x] **Workstation UI, Editor, Theme & Report Extension Surfaces**:
  - [x] Nested **Tools -> Plugins** submenus (`<= 4` levels), 5 `GridLockContextSurface` right-click surfaces (`Variables`, `Source Editor`, `Call Stack`, `Breakpoint Gutter`, `Rank List`), clickable status-bar badges, inline editor annotations, modal dialogs/prompt caching/toasts, `ThemeManager` token sync, and `PluginReportDockWidget` (`MARKDOWN`, `KEY_VALUE_TABLE`, `JSON_TREE`).

---

> [!NOTE]
> **Pre-`v1.0.0` Planning Scope (`v0.10.0+`)**: Post-`v0.9.0` milestones below represent target engineering capabilities prior to `v1.0.0` stabilization rather than rigid release guarantees. Domain-specific inspectors in `v0.10.0` (including IEEE-754 anomaly scanning, `pahole`-style DWARF struct/cache-line layout inspection, annotated stack telescoping, and AoS table projections) can ship directly as out-of-tree native plugins built on GridLock ABI `v1.0`.

## `v0.10.0`: Deep Register/Memory Inspection, Session Snapshots, CLI Launch Flags, and Core Dump Analysis

- [ ] **Dynamic All-Register Discovery, SIMD Vector Lanes, and Live Register/Memory Editing**:
  - [ ] Replace fixed x86 GPR-only register filtering with dynamic `-data-list-register-names` discovery across architectures (x86-64, AArch64, RISC-V).
  - [ ] Expandable FPU/SSE/AVX/NEON vector lanes (`XMM`/`YMM`/`ZMM`/NEON in `f32`, `f64`, `i32`, `i64`, `hex`) with per-lane step diff highlighting, `k0..k7` AVX-512 mask checkboxes, and decoded `RFLAGS` / `MXCSR` status bitfield badges (`IE`, `DE`, `ZE`, `OE`, `UE`, `PE`, `FTZ`, `DAZ`).
  - [ ] Inline register editing (`-gdb-set`), live inferior memory writes (`-data-write-memory-bytes`), typed `x/FMT` memory examination (`b`/`h`/`w`/`g` unit sizes and `x`/`d`/`u`/`f`/`s` formats), and `"Set Watchpoint at Address"` in `MemView`.
- [ ] **Cross-Rank IEEE-754 Anomaly and Poison-Pattern Scanner (`NaN` / `Inf` / Subnormals)** *(core or ABI `v1.0` plugin)*:
  - [ ] Concurrent `process_vm_readv` sweep across stopped ranks with per-rank distribution stats (`Min | Mean | Max | L2 Norm`), exact index reporting for quiet/signaling `NaN`, `+-Inf`, IEEE-754 subnormals, and allocator poison patterns (`0xdeadbeef`, `0xcdcdcdcd`), plus a 1-click `$mxcsr` hardware FP trap toggle (`FE_INVALID | FE_DIVBYZERO | FE_OVERFLOW`).
- [ ] **Watch Expression Format Specifiers and Array-of-Structs (AoS) Columnar Views** *(core or ABI `v1.0` plugin)*:
  - [ ] Inline comma suffixes (`ptr, [N]`, `,x`, `,b`) and AoS tabular projections (`particles, [1000], cols(pos.x, pos.y, energy)`) strided via `process_vm_readv` / `resolve_contiguous_range` and DWARF / `ptype /o` member offsets.
- [ ] **Rich Named Session Snapshots and CLI Automation Flags**:
  - [ ] Persist full breakpoint metadata (conditions, ignore counts, rank filters, logpoints), pinned graph roots, and target environment variables across `.gridlock/` sessions.
  - [ ] Add `--ranks`/`-np`, `--attach <pid>`, `--remote <host:port>`, `--core <file>`, `--session <name>`, and `--args` flags to `CliParser`.
- [ ] **Core Dump Post-Mortem Inspection (`core-file` and `coredumpctl` Integration)**:
  - [ ] Load ELF executables paired with core dump files (`core-file`, multi-rank `core.*` globs) and recent system crashes (`coredumpctl list --json=short` + `coredumpctl dump`) with ELF `PT_NOTE` (`NT_GNU_BUILD_ID` / `NT_FILE`) matching for offline multi-thread/rank inspection.
- [ ] **DWARF Struct Layout, Alignment Padding, and Cache-Line Inspector** *(core or ABI `v1.0` plugin)*:
  - [ ] `pahole`-style struct byte-map showing field offsets, bitfields, vptr slots, compiler padding holes, wasted byte percentage, field packing preview, 64-byte cache-line boundaries for OpenMP false-sharing analysis, and live `(StructType*)0x...` typed overlays in `MemView`.
- [ ] **Annotated Stack Frame Memory and Pointer-Telescoping Inspector** *(core or ABI `v1.0` plugin)*:
  - [ ] Aligned 8-byte stack slot table (`[RSP .. RBP .. Caller CFA]`) highlighting saved `RIP`, `RBP` chains, stack canaries, DWARF stack locals, and 4-hop pointer telescoping (`stack -> heap -> .rodata` symbol/string resolution).

---

## `v0.11.0`: Large-Scale HPC Runtime Introspection

- [ ] **Merged Parallel Stack Tree (STAT-Style Prefix Aggregation) and Dynamic Rank Sub-Groups**:
  - [ ] Prefix-trie call graph in `CallStackDockWidget` merging backtraces across all $N$ ranks with proportional `[M/N ranks]` membership bars to isolate stragglers, paired with `RankBitset` execution sub-groups (`All Ranks`, `Root (0)`, `Workers (1..N-1)`, `Stopped Ranks`, or custom branch subsets).
- [ ] **OpenMP Logical Region, Task, and Barrier Inspector (`OMPD` / `libompd.so`)**:
  - [ ] Logical OpenMP team/thread hierarchy labeling, runtime frame collapsing, originating `omp task` backtraces, and a 1-click `"Align All Team Threads Here"` action.
- [ ] **MPI Message Queue and Unmatched `MPI_Request` Inspector (`MPIR` / `mqs_` DLL Interface)**:
  - [ ] Host adapter for `mpi_interface.h` (`libompi_dbg_msgq.so` / `libtvmpich.so`) backed by `process_vm_readv` callbacks, exposing **Posted Receives**, **Pending Sends**, and **Unexpected Messages** inside `MpiDiagnosticsWidget` alongside a 2D Rank-by-Rank communication matrix.

---

## `v0.11.x` to `v1.0.0`: UI/UX Cohesion and `v1.0.0` Stabilization

- [ ] **Task-Focused Workspace Presets**: 1-click layout presets (*Source & MPI Debugging*, *Binary CFG & Decompiler*, *Memory & Numerical Profiling*, *Post-Mortem Crash Triage*) verified across 1080p, 1440p, 4K, and tiling Wayland compositors.
- [ ] **Cross-View Selection and Keyboard Navigation**: Unified badge styling, keyboard focus traversal, and synchronized address/symbol/rank selection across all docks and pop-out windows.
- [ ] **Production Hardening and `v1.0.0` Release Readiness**: Resolving OpenMPI, MPICH, PMIx, and SLURM edge cases, locking configuration file schemas, and maintaining backward-compatible ABI `v1.x` guarantees for `v1.0.0`.

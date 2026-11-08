# GridLock Engineering Roadmap

This roadmap tracks completed milestones (`Phases 1–10`) and remaining tasks for GridLock, a C++23 and Qt6 MPI debugger and ELF binary inspector on Linux.

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
  - Added `sonar-project.properties` for complexity and coverage tracking alongside zero-warning compiler flags (`-Wall -Wextra -Wpedantic`).
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

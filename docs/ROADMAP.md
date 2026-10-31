# GridLock Engineering Roadmap

GridLock is an actively maintained C++23 / Qt6 parallel MPI graphical debugger and binary analysis workstation. This roadmap tracks completed foundational releases (`Phases 1–6.5` through `v0.7.0`) alongside upcoming engineering milestones focused on static analysis, theme engine stabilization, guided tutorials, and `v0.8.0` IDE editor ergonomics.

---

## Completed Foundations (Phases 1–6.5 & `v0.7.0` Release ✅)

### Phase 1–3: Multi-Rank MPI Debugging & Binary Inspection Core ✅
- [x] **Real ELF64/DWARF Debugging (`ElfBinaryInspector`, `GdbRankCoordinator`, `DapCoordinator`)**: Authentic disassembly via `objdump -d -M intel --insn-width=10`, symbol extraction via `nm`, and exact `.debug_line` source-to-address mapping via `objdump --dwarf=decodedline` / `readelf -wL`.
- [x] **Breakpoint-First Execution & Circular Deadlock Detection (`DeadlockAnalyzer`)**: On-demand target compilation (`mpicxx -g -O0`), automatic halt at user-set breakpoints or `main()` entry, and directed wait-for cycle analysis across blocked `MPI_Send` / `MPI_Recv` / `MPI_Barrier` ranks.
- [x] **Zero-Copy Process Memory & Cross-Rank Diffing (`NativeMemoryReader`, `MemoryDiffer`, `MemView`)**: Kernel-level `process_vm_readv` memory extraction with stride bounds validation (`MemoryBoundsValidator`), cross-rank byte diffing, and live hex/instruction binary patching (`savePatchedBinary`).
- [x] **Cross-Rank Variable & Register Telemetry (`DifferentialGrid`, `VariablesDockWidget`, `RegisterView`)**: Simultaneous inspection of C/C++ expressions, local stack frames, and x86_64 registers across all active MPI ranks.

### Phase 4–6.5: Workstation UI, `clangd` LSP & Linux Packaging (`v0.7.0`) ✅
- [x] **Two-Lane Vector Gutter & Cursor-Anchored Breakpoint Popover (`SourceCodeView`, `ConditionalBreakpointDialog`)**: Dedicated breakpoint/PC gutter lanes and frameless cursor-anchored conditional breakpoint configuration (`All MPI Ranks` vs. `Focused Rank Only`).
- [x] **Live `clangd` JSON-RPC 2.0 Completion Engine (`ClangdLspClient`)**: Asynchronous stdio JSON-RPC 2.0 client providing virtual-buffer C/C++ struct/member autocomplete inside both `ConditionalBreakpointDialog` and `SourceCodeView` (`Ctrl+Space` or `.`, `->`, `::`).
- [x] **Workstation Layout & Wayland-Compatible Floating Docks**: Zero-gutter dark/light chrome, `36px` Left Vertical Tool-Window Icon Rail (`Debug`, `MPI`, `Terminal`, `Docs`), and native window-manager drag/redock support for floating panels (`ServerRackView`, `RegisterView`, `VariablesDockWidget`).
- [x] **8 Curated Dark/Light Themes & 16-Directive SLURM Script Generator (`ThemeManager`, `PreferencesDialog`)**: Instant switching across 5 Dark (`gridlock_slate`, `dark_teal`, `tokyo_night`, `catppuccin_mocha`, `gruvbox_dark`) and 3 Light (`daylight_light`, `solarized_light`, `catppuccin_latte`) themes, plus 16-directive `#SBATCH` batch script parsing and export.
- [x] **Live Multi-Distro Package Manager & Offline Zeal Docsets (`HpcConsoleWidget`, `DocsetManager`)**: Asynchronous two-stage host package search across `rpm`, `dnf5`/`dnf4`, `pacman`, `apt`, `zypper`, and `spack` with `Table`/`List`/`Raw` views, alongside SQLite3 Zeal `.docset` and system `man` lookup (`F1`) and isolated `QTermWidget` PTY terminals.
- [x] **Automated GitHub Actions `appimagetool` Release Pipeline**: Self-contained `AppDir` bundling with `$ORIGIN/../lib` RPATH stamping, headless 26-suite Meson verification, and automated `GridLock-0.7.0-x86_64.AppImage` artifact publishing via `appimagetool`.

---

## Phase 7: Continuous Bug Auditing, Static Analysis & Security Hardening

- [ ] **External Static Analysis & Linting Sweeps (`cppcheck`, `clang-tidy`, CLion Inspections)**:
  - Run systematic static analysis passes across `src/` and `tests/` using `cppcheck`, `clang-tidy` (`bugprone-*`, `modernize-*`, `performance-*`, `cppcoreguidelines-*`), and JetBrains CLion inspection profiles to eliminate dead code, uninitialized members, variable shadowing, and subtle Qt `QObject` lifetime hazards.
- [ ] **SonarQube / SonarCloud & Compiler Quality Gates**:
  - Establish reproducible SonarQube / SonarCloud analysis configuration (`sonar-project.properties`) tracking cognitive complexity, code smells, and test coverage alongside strict zero-warning compiler gates (`-Wall -Wextra -Wpedantic`).
- [ ] **Memory & Process Security Audit (ASan, UBSan, `MALLOC_PERTURB_`)**:
  - Audit `process_vm_readv` stride bounds (`MemoryBoundsValidator`), ELF64/DWARF binary parser inputs (`ElfBinaryInspector`), MI/DAP wire parsers (`MiSanitizer`), and `QProcess` / `KPtyProcess` argument handling under AddressSanitizer (ASan), UndefinedBehaviorSanitizer (UBSan), and `MALLOC_PERTURB_`.

---

## Phase 8: Qt6 Theme Engine Stabilization & UI Polish

- [ ] **Light Theme Contrast & Custom Widget Polish (`daylight_light`, `solarized_light`, `catppuccin_latte`)**:
  - Eliminate remaining hardcoded dark backgrounds and low-contrast hex text in Light themes across `MemView` hexdump byte cells, `ProjectExplorerWidget` (`FILES | SYMBOLS | STRINGS`) segmented buttons, the binary section navigation ribbon (`.plt`, `.text`, `.rodata`, `.data`), and `CODE | REVERSE` perspective toggle buttons.
- [ ] **Disassembly Header Layout & Theme Gallery Completeness**:
  - Fix clipped mode button labels (`SPLIT (CFG+DECOMP)`) in `DisassemblyView` header toolbars and generate/embed missing high-DPI preview screenshots for `Solarized Warm` (`theme_solarized_light.png`) and `Catppuccin Latte` (`theme_catppuccin_latte.png`) in `docs/THEMES.md`.
- [ ] **Tokenized Qt Palette Architecture & Expanded Theme Catalog**:
  - Migrate remaining inline widget QSS overrides to a unified semantic token architecture (modeled after Qt Creator and Rizin Cutter) and expand the built-in theme catalog with `Nord`, `One Dark Pro`, `One Light`, and `Dracula`.

---

## Phase 9: Documentation & Guided Hands-On Tutorials (`docs/TUTORIAL.md`)

- [ ] **End-to-End Hands-On Walkthrough (`docs/TUTORIAL.md`)**:
  - Author a comprehensive, step-by-step `docs/TUTORIAL.md` with annotated screenshots guiding users through:
    1. Diagnosing the 8-rank circular `MPI_Send` hang in `tutorial/deadlock_ring.cpp` using `ServerRackView`, `DeadlockAnalyzer`, and the 2D Control Flow Graph.
    2. Setting rank-filtered conditional breakpoints with live `clangd` struct member autocomplete (`data[i].coords.x`) on `tutorial/inspection_demo.cpp`.
    3. Comparing cross-rank watch expressions in `DifferentialGrid` and inspecting byte-level rank memory divergence in `MemView`.
    4. Browsing offline MPI/C/C++ Zeal `.docset` documentation (`F1`) and generating `#SBATCH` batch scripts in `Preferences -> HPC / Cluster`.

---

## Phase 10 (`v0.8.0`): Workstation Editor Ergonomics & Deep `clangd` LSP (No Live SLURM Runner)

> **Scope Note**: GridLock focuses on local and workstation MPI debugging and offline `#SBATCH` script generation; live remote SLURM job queue management is intentionally out of scope.

- [ ] **Interactive Project File Tree CRUD (`ProjectExplorerWidget`)**:
  - Right-click context menu on the `FILES` tree supporting `New Source File (.cpp)`, `New Header File (.hpp)`, `New Folder`, `Rename (F2)`, `Delete`, and optional 1-click target registration in `meson.build` or `CMakeLists.txt`.
- [ ] **Dirty Buffer Lifecycle & Split Source Editors (`EditorTabManager`, `SourceCodeView`)**:
  - Unsaved modification indicators (`●`) on editor tabs, close confirmation dialogs (`Save / Discard / Cancel`), `Ctrl+Shift+S` (*Save All*), side-by-side horizontal/vertical editor splits (`Split Right`, `Split Down`), Header/Source toggle (`F10`), and a sticky scope breadcrumb bar (`File > Namespace > Class > Function`).
- [ ] **Deep `clangd` LSP Integration (`ClangdLspClient`)**:
  - Live inline compiler error/warning squiggles (`textDocument/publishDiagnostics`), rich symbol & Doxygen hover cards (`textDocument/hover`), `F12` Go-to-Definition (`textDocument/definition`), Find References (`Shift+F12`), and `Shift+F6` workspace AST symbol rename.
- [ ] **Git Diff Gutter & Local Debugger Engine Selector**:
  - Live gutter change indicators (`git diff` added green, modified blue, and deleted red line markers in `SourceCodeView`) and selectable local debugger backend engines (`gdb`, `lldb-dap`, `cuda-gdb`, `rocgdb`) in `Preferences -> Debugger`.

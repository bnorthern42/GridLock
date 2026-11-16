# Changelog

All notable changes to the GridLock project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.8.6] - 2026-11-15

### Added
- **Visual Library & Heap Memory Profiler (`MemoryProfilerWidget`, `MemView`):** Added a `LIBRARY MEMORY MAP` view inside `MemView` combining `/proc/<pid>/smaps` virtual memory region attribution with a lightweight `LD_PRELOAD` caller-attribution tracker (`malloc`/`calloc`/`realloc`/`free` unwinding past `libstdc++` `operator new` with `addr2line` symbol resolution), an interactive 2D treemap and proportional stacked bar, a sortable and filterable breakdown table, a `⤢ POP OUT` / `⤡ DOCK` standalone window toggle, and five configurable color schemes (`Distinct`, `Viridis`, `Plasma`, `Ocean`, `HighContrast`) in `Preferences -> Appearance` and the profiler toolbar.
- **Inline Variable Ghost Values (`SourceCodeView`, `EditorTabManager`, `ConfigManager`):** Added muted italic inline variable values at the end of visible source lines when paused at a breakpoint or stack frame, resolving both local scalar variables and dotted/arrow member chains (such as `Iconfig.gmres.relTolerance`) while suppressing opaque `{...}` placeholders, with a toggle in `Preferences -> Editing`.
- **Non-Suspending Logpoints & Breakpoint Hit Ignore Counts (`ConditionalBreakpointDialog`, `GdbRankCoordinator`, `DapCoordinator`):** Added breakpoint log messages (`{expr}` expression evaluation streamed directly to `PROGRAM OUTPUT` without pausing ranks or stalling MPI barriers, rendered with an amber/cyan diamond gutter marker) and hit ignore counts (`-break-insert -i N` / `-break-after` and DAP `hitCondition`).
- **Interactive Pointer & Struct Graph Visualizer (`PointerGraphDialog`, `VariablesDockWidget`):** Added a DDD-inspired 2D pointer and struct graph canvas (`Variables` context menu and `Tools -> Pointer & Struct Graph Visualizer...`) with draggable struct cards, automatic 1-hop composite and pointer expansion, directed cubic Bezier edges, layered DAG auto-layout, null-pointer (`0x0`) guards, and cycle/shared-alias detection.
- **Reverse Debugging / Time-Travel Execution (`GdbRankCoordinator`, `DapCoordinator`, `MainWindow`):** Added GDB Process Record (`record full` with scheduler-locking support) and DAP reverse execution (`stepBack` / `reverseContinue`), exposing `⏺ Record`, `↶ Step Back`, and `⏮ Rev Continue` controls in the main execution toolbar.

### Changed
- **Incremental Output Streaming & Lazy Memory Map Refreshes (`MainWindowDocks`, `GdbConsoleWidget`, `MainWindowExecution`, `MemView`):** Kept `PROGRAM OUTPUT` and `GDB CONSOLE` flushes strictly incremental by filtering incoming rank output on arrival, debounced dotted inline variable evaluations, and deferred `/proc/<pid>/smaps` refreshes until `LIBRARY MEMORY MAP` is active and visible on the focused rank.

### Fixed
- **Clean Shutdown Lifecycle (`ClangdLspClient`, `MainWindow`, `MemView`, `TerminalDockWidget`):** Fixed an exit segmentation fault on window close by shutting down `ClangdLspClient` on `QCoreApplication::aboutToQuit`, guarding signal teardown during destruction, and re-docking popped-out `MemView` windows before teardown.
- **Wayland Splitter Resize Warnings:** Eliminated Wayland non-popup mouse-grab warnings when dragging dock and panel splitters by enabling `setOpaqueResize(true)` across all `QSplitter` instances.
- **Variable Deduplication & Access-Specifier Unwrapping (`VariableTreeModel`, `GdbRankCoordinator`, `GdbRankInspection`):** Fixed duplicate variable rows (`{...}` vs `{...} @ 0x...` and `@entry` parameter shadows), unwrapped C++ `public`/`protected`/`private` access-specifier pseudo-nodes at all tree depths, and restricted `VariablesDockWidget` live updates to the focused rank.
- **Toolbar Layout & Tiling Window Controls (`MemView`, `PointerGraphDialog`):** Fixed button and source badge overlap in `MemView`'s toolbar when switching modes, and added an explicit `✕ CLOSE` toolbar button to `PointerGraphDialog` for frameless and tiling Wayland compositors.

## [0.8.5] - 2026-11-15

### Added
- **Live Multi-Rank Orchestration (`GdbRankCoordinator`, `DapCoordinator`):** Added live GDB/MI3 MPMD multi-rank process launching and stepping (`GdbRankCoordinator`) alongside Debug Adapter Protocol coordination (`DapCoordinator`), replacing the static `objdump` stepping simulation.
- **Per-Rank PID Capture, `/proc` Attach & Cross-Rank Memory Diffing (`AttachProcessDialog`, `MemView`, `MemoryDiffer`):** Captured live inferior OS PIDs (`=thread-group-started` / `systemProcessId`), added a live `/proc` process scanner in `AttachProcessDialog`, and implemented cross-rank memory reads (`process_vm_readv` with GDB/DAP memory-read fallback) with byte diff highlighting in `MemView`.
- **Call Stack Unwinding & Thread Switching (`CallStackDockWidget`):** Added the `CALL STACK` dock supporting 64-frame backtrace unwinding, OS/OpenMP thread selection, and caller-frame local variable, register, and source synchronization.
- **Expandable Composite Variables & Live Table Filtering/Sorting (`VariablesDockWidget`, `VariableTreeModel`, `RegisterView`, `ProjectExplorerWidget`):** Added recursive GDB/MI3 and DAP `{...}` struct, class, and vector child expansion, plus live search inputs and click-to-sort interactive column headers across `VARIABLES`, `REGISTERS`, `SYMBOLS`, and `STRINGS`.
- **Rank-Coded Breakpoint Gutter Badges (`SourceCodeView`):** Added crimson All-Ranks badges with a white ring alongside deterministic per-rank accordion colors, rank index labels, and gutter context-menu actions.
- **Live Deadlock Analysis, REPL Console & Data Watchpoints (`DeadlockAnalyzer`, `MpiNetworkLogWidget`, `GdbConsoleWidget`, `MiSanitizer`):** Wired real rank call stacks into `DeadlockAnalyzer` for circular wait and collective divergence detection (`MPI_Bcast` vs `MPI_Barrier`), streamed live events to `MpiNetworkLogWidget`, enabled `MiSanitizer`-validated interactive commands in `GdbConsoleWidget`, and added hardware data watchpoints (`Write`, `Read`, `ReadWrite`).
- **Dedicated Program Output Dock (`MainWindowExecution`, `MainWindowDocks`):** Added a `PROGRAM OUTPUT` bottom dock tab displaying a read-only launch command and working directory header bar above 60 FPS coalesced inferior stdout/stderr output with rank filtering.
- **Cross-File `clangd` Navigation & Build Database Discovery (`ClangdLspClient`):** Enabled workspace-scoped JSON-RPC 2.0 `F12` Go-to-Definition, `findReferences`, AST-safe `WorkspaceEdit` symbol rename, live `publishDiagnostics` editor squiggles, and automatic `compile_commands.json` discovery across `builddir`, `build`, `build_meson`, and `build_cmake`.
- **Session Persistence, Dirty Close Prompts & Syntax Modes (`SessionManager`, `EditorTabManager`, `SourceCodeView`):** Added live background Git diff gutter refreshes, `Save` / `Discard` / `Cancel` prompts on dirty tab and window close, `File -> Save Session` with workspace auto-save to `.gridlock/workspace.toml`, persistent `File -> Recent Sessions` in `~/.config/gridlock/debugger.conf`, syntax highlighters for `TOML`, `YAML`, `Markdown`, and `CSV`/`TSV`, and stateful Rainbow Brackets in `Preferences -> Editing`.
- **Upgraded Project Settings, `MemView` Source Mode & Dual AppImages (`ProjectSettingsDialog`, `FileDialogHelper`, `scripts/build_appimage.sh`):** Added workspace ELF auto-discovery, file/folder browsers, `Program Arguments`, and `Working Directory` terminal synchronization in `ProjectSettingsDialog`; added explicit `MemView` source mode indicators (`IDLE (NO BINARY)`, `STATIC ELF (READ-ONLY)`, `LIVE RANK N (PID M)`); and packaged both Lite (`GridLock-<version>-x86_64.AppImage`) and Tutorials (`GridLock-<version>-Tutorials-x86_64.AppImage`) AppImage editions.

### Changed
- **Clean Idle & Unbuilt Workspace State:** Removed startup constructor mock fixtures across `VariablesDockWidget`, `RegisterView`, `MemView`, `MpiDiagnosticsWidget`, `MpiNetworkLogWidget`, `ProjectExplorerWidget`, `DisassemblyView`, `ServerRackView`, the section navigation ribbon, and status bar telemetry so idle and unbuilt workspaces start clean.
- **Non-Blocking Asynchronous Pipelines:** Moved `clangd` document completion, Git diff gutter refreshes, and 8-rank `PROGRAM OUTPUT` / `GDB CONSOLE` batch insertion off synchronous GUI blocking paths.

### Fixed
- **Multi-Rank MPI + OpenMP Stalls (`GdbRankCoordinator`):** Guarded running-state MI inspection queries, resumed all stopped ranks on `Continue`, allowed dynamic breakpoint updates while ranks execute, and tuned `OMP_NUM_THREADS` / `OMP_WAIT_POLICY=PASSIVE` for responsive hybrid debugging.
- **Secondary-Tab Breakpoint Focus Jump:** Prevented toggling a breakpoint in another source file tab from snapping focus back to the stopped execution file.
- **Atomic Workspace Switching (`MainWindowWorkspace`):** Synchronized `ProjectExplorerWidget` (`FILES`), `TARGET:`, and `TerminalDockWidget` working directory together when opening external workspaces or `.gridlock/workspace.toml` sessions.
- **Control Flow Graph & Decompiler Polish (`DisassemblyView`):** Clamped wide demangled C++ template signatures in CFG basic blocks, left-anchored wide blocks in the viewport, cleared the stuck hand-pan cursor, and honored the `"Show Pseudo-C Decompiler when Source Editor is visible"` preference consistently whenever any source tab is open.
- **File Dialogs & Diagnostics Table Layout (`FileDialogHelper`, `MpiDiagnosticsWidget`, `MpiNetworkLogWidget`):** Defaulted `QFileDialog` to the active workspace, pruned non-existent ghost bookmarks from `sidebarUrls()`, and enabled interactive column resizing without PID truncation in `MPI DIAGNOSTICS` and `NETWORK LOG`.

## [0.8.0] - 2026-11-08

### Added
- **File Tree CRUD Operations & Build Registration (`ProjectExplorerWidget`):** Added context-menu and programmatic operations for creating source/header files, creating folders, renaming (`F2`), and deleting entries, with external filesystem synchronization and 1-click source registration in `meson.build` and `CMakeLists.txt`.
- **Split Source Editors & Dirty Buffer Tracking (`EditorTabManager`, `SourceCodeView`):** Added horizontal (`Split Right`) and vertical (`Split Down`) split editor views, unsaved buffer markers (`*` / `●`) with save/discard prompts, `Ctrl+Shift+S` (*Save All*), Header/Source switching (`F10`), and a scope breadcrumb bar.
- **`clangd` Diagnostics, Hover & AST Navigation (`ClangdLspClient`):** Added inline compiler error and warning underlines (`textDocument/publishDiagnostics`), hover type and Doxygen tooltips (`textDocument/hover`), `F12` go-to-definition (`textDocument/definition`), `Shift+F12` find references (`textDocument/references`), `Shift+F6` AST symbol rename (`textDocument/rename`), and `Ctrl+Space` auto-completion.
- **Git Diff Gutter Indicators (`SourceCodeView`):** Added gutter line markers for added, modified, and deleted lines parsed from unified `git diff` hunks.
- **Configurable Debugger Backend Engine (`PreferencesDialog`, `ConfigManager`):** Added debugger backend selection (`gdb`, `lldb-dap`, `cuda-gdb`, `rocgdb`), custom binary path configuration, and `mpirun` launch argument settings in `Preferences -> Debugger`.
- **12-Theme Palette Catalog (`ThemePalette`, `ThemeManager`):** Expanded the theme engine to 12 built-in themes (8 Dark, 4 Light), adding `Nord`, `One Dark Pro`, `Dracula`, and `One Light`.
- **Live Status Bar Resource Telemetry (`ElfBinaryInspector`, `MainWindow`):** Added real-time per-rank `STACK` and `HEAP` memory usage alongside system `RAM` (in GB/total or `%`) and `CPU %` in the bottom status bar ribbon, with a configurable display format in `Preferences`.
- **5 New MPI Debugging Showcase Examples & Expanded Tutorial (`tutorial/`, `docs/TUTORIAL.md`):** Added `collective_mismatch.cpp`, `ghost_cell_drift.cpp`, `heap_imbalance.cpp`, `tag_mismatch_stall.cpp`, and `simd_struct_watch.cpp` (bringing the suite to 7 runnable examples registered in the `TARGET:` selector) and expanded `docs/TUTORIAL.md` with step-by-step walkthroughs.
- **Static Analysis & Compiler Quality Gates (`sonar-project.properties`, `meson.build`):** Added `sonar-project.properties` for complexity and coverage tracking, ran `cppcheck` and `clang-tidy` sweeps across `src/` and `tests/`, and enforced `-Wall -Wextra -Wpedantic` with zero compiler warnings.

### Changed
- **Default Control Flow Graph & Conditional Decompiler Visibility (`DisassemblyView`):** Defaulted `DisassemblyView` to the multi-block Control Flow Graph (`MAP (CFG)`) view and hid the redundant pseudo-C decompiler pane when the C++ source editor is visible (configurable in `Preferences -> Debugger`).
- **Right-Sidebar Dock Consolidation & Movable Toolbars (`MainWindow`):** Tabified `Watch` into the top-right `VARIABLES | REGISTERS | WATCH` dock tab group above `SERVER RACK` and enabled movable main toolbars (`setMovable(true)`).
- **Documentation Overhaul (`README.md`, `ROADMAP.md`, `docs/*.md`):** Rewrote project documentation, architecture guides, and user manuals in concise developer prose.

### Fixed
- **Live GDB Disassembly CFG Splitting (`DisassemblyView`):** Fixed the `BB:1 E:0` Control Flow Graph collapse when stepping through live GDB disassembly containing `<symbol+offset>xjmp` annotations.
- **Light-Theme Contrast (`ThemeManager`, `MemView`, `ProjectExplorerWidget`):** Fixed light-theme contrast across the `Preferences` category sidebar, binary section ribbon, `FILES | SYMBOLS | STRINGS` toggles, `CODE | REVERSE | MPI` switches, and non-zero `MemView` hex bytes.
- **Main Toolbar Control Sizing (`MainWindowWorkspace`):** Fixed clipped digits in the `RANKS (-np)` spinbox, truncated text in the `TARGET:` dropdown, and oversized `Run Target` button height.
- **Subprocess Pipe Guards (`DapCoordinator`, `LspCoordinator`, `GdbRankCoordinator`, `ClangdLspClient`, `TerminalDockWidget`):** Guarded `QProcess::write` and `closeWriteChannel` calls against closed or unstarted pipes.

## [0.7.0] - 2026-10-30

### Added
- **On-Demand ELF64 Compilation & DWARF `.debug_line` Disassembly Mapping (`ElfBinaryInspector::disassembleSymbolWithDwarf`):** Compiles C/C++ MPI targets on demand with `-g -O0` (`mpicxx` / `mpicc` / `g++`) and parses `.debug_line` and `.text` disassembly via `objdump`, `nm`, and `readelf` for exact bidirectional source-line to instruction-address synchronization.
- **`ClangdLspClient` Virtual-Buffer JSON-RPC 2.0 Expression Autocomplete:** Real `clangd` Language Server Protocol client splicing breakpoint condition expressions into an in-memory C++ translation unit at target line `N` to provide context-aware struct, vector, and local variable completions inside `ConditionalBreakpointDialog` and `SourceCodeView`.
- **Cursor-Anchored `Qt::Popup` Conditional Breakpoint Popover (`ConditionalBreakpointDialog`):** Frameless popover anchored directly beneath the gutter badge displaying target line context, an `Enabled` toggle, `Suspend: All | Rank` radio scope buttons, MPI condition preset pills, and live `clangd` completion.
- **Two-Lane Antialiased Vector Gutter (`SourceCodeView`):** Dedicated `20px` left badge lane rendering `QPainterPath` vector indicators (`Breakpoint` circle, `ConditionalBreakpoint` amber diamond, `ExecutionPc` emerald chevron, and combined ring+chevron) isolated from right-aligned line numbers and editor context menus.
- **36px Left Vertical Tool-Window Icon Rail (`#bottomToolWindowRail`):** Organizes the bottom dock into four linked contextual tool groups (`Debug`, `MPI`, `Terminal`, `Docs`) with a flattened single-row `MPI Diagnostics` header (`Deadlock Analyzer` and `Network Log`).
- **Multi-Distro HPC & System Package Manager (`HpcConsoleWidget`):** Automatic backend discovery (`spack`, `pacman`, `dnf4`/`dnf5`, `apt`, `zypper`, `rpm`, `dpkg-query`, `eb`) with live two-stage local/repository querying and switchable `Table`, `List`, and `Raw` views.
- **16-Directive SLURM `#SBATCH` Auto-Detector (`PreferencesDialogSlurm`):** Parses `.sbatch` / `.sh` job scripts to auto-populate 16 SLURM directives (`--job-name`, `--partition`, `--account`, `--qos`, `--nodes`, `--ntasks`, `--ntasks-per-node`, `--cpus-per-task`, `--gres`, `--mem`, `--time`, `--constraint`, `--reservation`, `--chdir`, `--output`, `--error`) and `module load` toolchains.
- **8 Curated Dark & Light Workstation Themes (`ThemeManager`):** Full palette and QSS propagation across 5 dark themes (`GridLock Slate`, `Obsidian Teal`, `Tokyo Night`, `Catppuccin Mocha`, `Gruvbox Industrial`) and 3 high-contrast light themes (`Daylight Paper`, `Solarized Warm`, `Catppuccin Latte`) with live UI and code font scaling.
- **Splitter-Aware Dock Pop-Out & Redocking:** Supports popping out `Variables`, `Registers`, `Server Rack`, and `Project Explorer` into floating windows with translucent drop-zone overlays and 1-click/double-click redocking across X11 and Wayland.

## [0.6.0] - 2026-06-30

### Added
- **Integrated Interactive Terminal:** Real embedded terminal leveraging `qtermwidget`, providing a full PTY-backed interactive shell inside the IDE as `TerminalDockWidget`.
- **Centralized `VersionManager`:** The `meson.build` file is now the single source of truth for the project version. A `VersionConfig.hpp.in` template is configured at build time, exposing `GRIDLOCK_VERSION_MAJOR`, `GRIDLOCK_VERSION_MINOR`, and `GRIDLOCK_VERSION_PATCH` constants to all C++ translation units via the `VersionManager` API.
- **DAP Exception Handling:** The DAP lifecycle now catches fatal target exceptions (e.g., `SIGFPE`, segfaults). When `DapCoordinator` receives a stopped event with `reason: "exception"`, it emits a `targetCrashed(QString reason)` signal. `MainWindow` connects this signal to display a clean, non-blocking warning dialog rather than leaving the UI in a hung state.
- **Project-Local Configuration Overrides:** `ConfigManager` now implements a two-tier resolution hierarchy. On startup and session load, it checks for `<project_root>/.gridlock/workspace.toml` first. If present, values in that file override global XDG settings (`~/.config/gridlock/config.toml`), enabling fully reproducible, project-portable debug configurations.
- **Automated Stylesheet Asset Bundling:** `Qt-Advanced-Stylesheets` theme assets are now compiled directly into a Qt Resource (`.qrc`) file via a Meson `custom_target`. The generated `acss_resources.qrc` is processed by `qt.compile_resources()`, embedding all style assets into the binary for flawless zero-dependency deployment.
- **Persistent Layout Restoration:** `MainWindow` now saves and restores `QMainWindow::saveGeometry()`, `QMainWindow::saveState()`, and individual dock widget positions between sessions using `QSettings`. The layout is persisted on close and fully restored on the next launch.

### Changed
- **Terminal Architecture Separation:** The old monolithic pseudo-terminal was split into two distinct docks: `MpiNetworkLogWidget` (read-only, captures DAP/GDB stdout streams for MPI Diagnostics) and `TerminalDockWidget` (a live, fully interactive shell via `qtermwidget`).
- **`BreakpointManager` Scoping:** The `BreakpointManager` is now scoped to the active Workspace Directory. All breakpoint paths are stored as absolute paths relative to the project root. This prevents global breakpoint accumulation across projects and eliminates the flood of `<PENDING>` GDB/MI traffic that occurred when stale paths from other workspaces were sent to the debugger on session load.
- **`ProjectWizardDialog` Persistent Memory:** The Project Wizard now automatically re-populates all fields (binary path, arguments, MPI rank count, working directory) with the values from the last session. Fields are persisted via `QSettings` and restored on the next time the wizard is opened.
- **CI Pipeline Overhaul:** The GitHub Actions `release.yml` pipeline was updated to build all Qt6-native subproject dependencies from source on older Ubuntu runners. This includes `lxqt-build-tools`, `qtermwidget`, and `Qt-Advanced-Stylesheets` (pinned to their `2.0.0` tags), ensuring a reproducible and hermetic build environment.

### Fixed
- **Wayland/Niri Dock Regressions:** Resolved compositor-specific regressions affecting dock widget dragging and floating window management under Wayland compositors, specifically the Niri tiling compositor.
- **`mpi_mm.c` Divide-by-Zero Crash:** Fixed a fatal divide-by-zero crash in the MPI matrix multiplication tutorial (`tutorial/mpi_mm.c`) that occurred when the program was executed in singleton mode (rank count of 1) without any worker ranks, causing an integer division by zero when computing the row distribution.

## [0.5.3] - 2026-06-28

### Changed
- **Tooltip Rendering**: Removed heavy LSP Markdown tooltips and the `HoverWidget` to improve UI performance and stability. Reverted to a lightweight, custom `QLabel` plain-jane tooltip for GDB variable evaluations.
- **Tutorial Subsystem**: Decoupled tutorial E2E tests from the main build pipeline, removing them from `meson.build` to migrate to a separate repository.

### Fixed
- **Wayland Tooltip Glitches**: Fixed tooltips rendering as blank white boxes on Wayland compositors by refactoring them to use `Qt::ToolTip | Qt::FramelessWindowHint` without translucent background attributes.
- **Hover Jitter**: Implemented debouncing on `SourceCodeView` to eliminate rapid-fire GDB requests and UI flickering when moving the mouse cursor over the same variable.
- **Variable Tree Performance**: Eliminated eager DAP variable fetches, significantly improving responsiveness and eliminating I/O bottlenecks when stepping through code.

## [0.5.2] - 2026-06-27

### Added
- **Tutorial Mode**: Initial scaffolding for `--tutorial-mode` (Note: This feature is currently a Work In Progress / WIP).

### Fixed
- **DAP Execution**: Fixed DAP `stopOnEntry` stalling the debugger on the assembly entry point instead of hitting user code.
- **LSP Handshake**: Fixed Clangd LSP handshake failing due to a missing `initialized` notification.
- **UI Lifecycle**: Fixed application windowing lifecycle duplicating windows at startup when launching dialogs.

## [0.5.1] - 2026-06-24
- **Global Configuration Engine:** Implemented a new TOML-based `ConfigManager` that persistently stores toolchain paths (`lldb-dap`, `gdb`, `clangd`) and default MPI launch arguments in `~/.config/gridlock/config.toml`.
- **Unified Preferences UI:** Added a comprehensive, Kate/KDevelop-style sidebar `PreferencesDialog` (Edit -> Preferences) to manage Appearance, Editing, Behavior, Debugger tools, HPC/Cluster settings, SLURM integration, and Docsets.
- **Project Creation Wizard:** Replaced the disjointed "New Project" menu with a linear, multi-page `QWizard`. Includes dynamic MPI launch string generation with quick-toggles for common OpenMPI flags (e.g., `--bind-to core`, `--map-by node`, `--oversubscribe`).
- **Recent Sessions (MRU):** Added a "Recent Sessions" submenu under the File menu, tracking the last 5 loaded workspaces dynamically.
- **Headless CI Support:** Added `xvfb` (X Virtual FrameBuffer) to the GitHub Actions pipeline to allow Qt GUI and DAP parser tests to run successfully on headless Ubuntu runners.

### Changed
- **LSP Toolchain:** `LspCoordinator` now dynamically resolves the `clangd` binary path from the user's global configuration rather than relying on the system `PATH`.
- **Architecture:** Moved away from hardcoded configuration values; MPI execution flags and debugger paths are now injected directly from the `ConfigManager` singleton into the backend coordinators.

### Fixed
- **Workspace State Refresh:** Fixed a critical state bug where loading a new session or workspace failed to visually update the `ProjectExplorerWidget`. The file tree now correctly re-roots upon the `sessionLoaded` signal.
- **Vulkan CI Crash:** Completely purged lingering `QVulkanInstance` boilerplate from `src/main.cpp` that was causing `SIGABRT` compilation failures in environments lacking Vulkan headers.
- **Test Suite Linker Errors:** Fixed a Meson dependency issue where `gridlock_lsp_tests` failed to link against the newly implemented `ConfigManager.cpp`.
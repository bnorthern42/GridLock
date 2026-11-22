# Developer Architecture and Testing

GridLock (`v0.8.8`) is written in C++23 (`-Wall -Wextra -Wpedantic`) on Qt6 and Meson, with modular translation units split under 500 lines (hard repository ceiling `< 950` lines) and per-subdirectory architectural `README.md` guides across `src/` and `tests/`.

---

## 1. Repository Layout

```text
GridLock/
├── meson.build                     # Build definition: gridlock, mpi_mm_bin, and 22 test suites
├── agent.md                        # Architectural invariants, Graphify workflow, and style rules
├── external/
│   ├── Qt-Advanced-Stylesheets/    # Submodule providing dynamic SVG/QSS theming (libqtadvancedcss)
│   └── tomlplusplus.wrap           # Meson wrap fallback for toml++ 3.4.0
├── resources/                      # Qt .qrc bundles (icons, Nerd Font glyphs, base stylesheets)
├── scripts/                        # Packaging, UI capture, manual e2e_cli_suite.mjs, coverage, Wiki sync
├── tutorial/                       # 8 primary C++ MPI showcase targets + 5 auxiliary C/C++ demos
├── tests/                          # Modular QtTest suites (ui/, dap/, hpc/, core/) with README guides
└── src/
    ├── main.cpp                    # CLI fast-path, Wayland/QPA setup, splash, and mode dispatch
    ├── RankState.hpp               # Per-rank execution, frame, watch, and register telemetry model
    ├── backend/                    # GridLockAutomationRunner (--test-mode) and TestGenerator
    ├── core/
    │   ├── commands/               # IDebugCommand implementations (Launch, Continue, Step, Pause, etc.)
    │   ├── hpc/                    # Organized into gdb/, dap/, lsp/, memory/, analysis/, system/
    │   ├── managers/               # ConfigManager*, Session, Shortcut, Theme, Docset*, Spack (< 500 lines)
    │   ├── models/                 # VariableNode and lazy-expanding VariableTreeModel
    │   └── utils/                  # CliParser, SearchEverywhereEngine, ElfBinaryInspector*, ClangdLspClient
    └── ui/
        ├── MainWindow*.cpp         # Cohesive < 500-line units (Toolbar, Perspectives, Execution, BinaryNav, etc.)
        ├── EditorTabManager.*      # Multi-tab and QSplitter horizontal/vertical split editor coordinator
        ├── GdbConsoleWidget.*      # 60 FPS coalesced GDB/MI console with readline history, Ctrl+R, Tab complete
        ├── dialogs/                # Organized into plotter/, graph/, calculator/, debugger/, project/, search/, preferences/
        ├── docks/                  # VariablesDockWidget, CallStackDockWidget, TerminalDockWidget
        ├── shaders/                # Precompiled SPIR-V GLSL 450 heatmap shaders
        ├── tutorial/               # TutorialDialog (--tutorial-mode)
        ├── views/                  # SourceCodeView*, Disassembly*, MemView*, MemoryProfiler*, ServerRack
        └── widgets/                # DifferentialGrid, MpiDiagnostics, MpiNetworkLog, HpcConsole*, ReferenceManual
```

---

## 2. Core Subsystem Architecture

### Polymorphic Debugger Coordinators (`src/core/hpc/gdb/`, `src/core/hpc/dap/`)

`IBackendCoordinator` defines a backend-agnostic Qt signal/slot contract implemented by two coordinators:

1. **`GdbRankCoordinator` (`GdbRankCoordinator.cpp`, `GdbRankCommands.cpp`, `GdbRankInspection.cpp`, `GdbMiParser.cpp`)**:
   * **Local MPMD Launch**: Allocates `/tmp/gridlock_mi_<pid>_<seq>/r<i>.{in,out}` named pipes (`mkfifo`, `0600`, `O_RDWR | O_NONBLOCK`) and launches one `mpirun` job with `:`-separated `-np 1` GDB/MI3 clauses.
   * **Local Attach (`attachToProcesses`)**: Spawns one `gdb --interpreter=mi3` instance per selected `ProcessEntry` sorted by `mpiRank`, sends `-target-attach <pid>`, syncs cached breakpoints, and detaches cleanly via `-target-detach` on session stop.
   * **Remote Stubs (`attachToRemoteTargets`)**: Spawns `gdb` or `gdb-multiarch`, loads local DWARF info via `-file-exec-and-symbols`, and connects via `-target-select remote <endpoint>` or `-target-select extended-remote <endpoint>`.
   * **Execution Control, Breakpoints, Signals, Catchpoints & Checkpoints**: Supports `nexti` (`-exec-next-instruction`), `Run to Cursor` (`-exec-until`), `Jump` (`-exec-jump`), `Force Early Return` (`-exec-return`), `haltAll` (`F6`), in-place `-gdb-set` variable/register writes, non-destructive `-break-enable`/`-break-disable`, temporary `-break-insert -t`, `rbreak`, `-break-commands`, `handle <SIG>` / `info handle` (token `210`), `complete` tab completion (token `211`), copy-on-write `checkpoint` / `info checkpoints` / `restart` / `delete checkpoint` (tokens `212` and `213`), client-side `{ident}` logpoints, hardware watchpoints (`-break-watch [-r|-a]`), and Process Record (`record full`).
2. **`DapCoordinator` (`DapCoordinator.cpp`, `DapCoordinatorRequests.cpp`)**:
   * Speaks JSON-RPC 2.0 (`Content-Length` framing) to `lldb-dap` (or `lldb-vscode`), mapping 0-based `rankId` to 1-based DAP `threadId`, supporting `launch`, `attach`, `setBreakpoints` (`condition`, `hitCondition`, `logMessage`), `dataBreakpointInfo` / `setDataBreakpoints`, `stackTrace`, `scopes`, `variables`, `evaluate`, `readMemory` (with `NativeMemoryReader` fast path for reads `> 1024` doubles), and `stepBack` / `reverseContinue`.

### Floating Diagnostic & Navigation Dialogs (`src/ui/dialogs/`)

* **`CommandPaletteDialog` + `SearchEverywhereEngine` (`search/`)**: JetBrains-style Search Everywhere (`Double Shift`, `Ctrl+P`, `Ctrl+Shift+P`) with 6 tabbed scopes, CamelCase/acronym fuzzy scoring, 2-line workspace-relative rows, 35-line numbered context preview (`250`-line / `32 KB` mega-file cap), and `Small`/`Medium`/`Large`/`X-Large` size presets.
* **`PointerGraphDialog` (`graph/`)**: Topological Sugiyama DAG canvas preserving manual drag positions, with multi-root pinning, `"old -> new"` mutation badges, `Shift+Click` per-node width expansion, full-value hover tooltips, double-click pointer expansion, corner minimap, `/x` `/d` `/b` format overrides, and `2x` PNG / SVG export.
* **`ArrayPlotDialog` (`plotter/`)**: Multi-rank 1D curve overlays, `start:end:stride` slicing, `Log10` scaling, `Complex64`/`Complex128` decoding, 2D colormapped heatmaps, validated `splot`/`pm3d` `gnuplot` export, and virtualized 2D/3D C/Fortran tensor tables.
* **`PreferencesDialog` (`preferences/`)**: Modular per-page preferences coordinated via a hierarchical `QTreeWidget` (`Appearance -> Toolbar` layout customizer, `Keybindings` `KeySequenceRecorderWidget`, `SlurmBatchParser`, etc.).
* **`ProgrammerCalculatorDialog` (`calculator/`), `SignalCatchpointDialog` & `CheckpointDialog` (`debugger/`)**: Synchronized multi-base/IEEE-754 calculator, POSIX/HPC signal matrix + catchpoints, and `/proc`-guarded copy-on-write fork checkpoints.

### Startup Performance, QSS Disk Cache & Lazy-Loading Architecture

* **Asynchronous Binary Launch & QSS Disk Cache (`ThemeManager`, `MainWindowExecution`)**: Compiled theme stylesheets from `Qt-Advanced-Stylesheets` are hashed and cached under `AppConfigLocation/acss`, and binary launch avoids synchronous disassembly/symbol stalls on the UI thread.
* **Deferred Bottom-Tab Initialization**: Hidden bottom-rail tabs (**`Package Manager`**, **`Terminal`**, and **`Reference Manual`**) initialize lazily on first activation or explicit lookup (`F1`, `Build Target`), while `SourceCodeView` routes multi-megabyte data files through a virtualized large-CSV viewer.
* **AST Knowledge Graph (`graphify-out/`)**: Developers and coding agents query `graphify` (`graphify query ...`) alongside local `README.md` guides to trace cross-module dependencies before modifying translation units.

### Split `MainWindow` Architecture (`src/ui/MainWindow*.cpp`)

To keep every translation unit under 500 lines while maintaining fast incremental builds, `MainWindow` is partitioned into focused modules:
* `MainWindow.cpp` & `MainWindowLifecycle.cpp`: Construction, menu bar, preferences application, clean session/PTY reaping on exit, and preview fixtures.
* `MainWindowToolbar.cpp` & `MainWindowPerspectives.cpp`: `< 1,040 px` customizable `#MainToolbar` (`rebuildToolbarFromLayout`), `Launch Options` (`Config`) popover, `SOURCE | DEBUG | BINARY | MPI` perspectives, rank-count progressive disclosure, and `captureDocumentationScreenshots`.
* `MainWindowWorkspace.cpp` & `MainWindowDocks.cpp`: Central editor/disassembly splitters, right-side docks, bottom tool rail, lazy tab activation, floating dock redocking, and theme propagation.
* `MainWindowDebugger.cpp` & `MainWindowExecution.cpp`: `Debug` actions, non-linear execution (`nexti`, `Ctrl+F10`, PC jump, early return, `rbreak`), master `setFocusedRank()` sync hub, coordinator signal wiring, `/proc` and remote attach, and `+x`/build preflight checks.
* `MainWindowNavigation.cpp`, `MainWindowBinaryNav.cpp` & `MainWindowCohesion.cpp`: Session I/O, workspace PNG snapshots (`--snapshot`), `AddressSpaceNavBar` history, DWARF line/address sync, `F1` documentation lookup, and UI cohesion.

---

## 3. Running the 22 Meson Test Suites and Manual E2E Runner

Delete stale `.gcda` coverage counters (if coverage was previously enabled) and run all 22 suites headlessly (unit test binaries automatically isolate `ConfigManager` into a temporary directory when `GRIDLOCK_TEST_CONFIG_DIR` is unset):

```bash
find builddir -name "*.gcda" -delete
meson test -C builddir --print-errorlogs
```

Every test target in `meson.build` runs with `QT_QPA_PLATFORM=offscreen`:

| # | Suite Name | Executable | Scope Tested |
| :--- | :--- | :--- | :--- |
| 1 | `coordinator_tests` | `gridlock_tests` | `GdbRankCoordinator`, `ProcessScanner` (`/proc` & remote endpoint ranges), `DeadlockAnalyzer` |
| 2 | `lsp_tests` | `gridlock_lsp_tests` | `ClangdLspClient` completion, diagnostics, hover, definition, references, rename |
| 3 | `ui_tests` | `gridlock_ui_tests` | 64 offscreen end-to-end UI tests across `MainWindow`, customizable toolbar, Search Everywhere, Calculator, Array Plotter, Pointer Graph, Preferences tree, Signals/Catchpoints, GDB Readline, Non-Linear Execution, Breakpoint Lifecycle, CoW Checkpoints, and QSS caching |
| 4 | `config_tests` | `gridlock_config_tests` | `ConfigManager` (per-target `programArgs`, toolbar layouts, popup presets) and `SessionManager` round-trips |
| 5 | `docset_tests` | `gridlock_docset_tests` | `DocsetManager` SQLite3 Zeal `.docset` indexing and `man` lookup |
| 6 | `dap_parser_tests` | `gridlock_dap_parser_tests` | DAP `Content-Length` framing and JSON event parsing |
| 7 | `dap_commands_tests` | `gridlock_dap_commands_tests` | DAP `next`, `stepIn`, `continue`, `pause`, and reverse execution requests |
| 8 | `dap_breakpoints_tests` | `gridlock_dap_breakpoints_tests` | DAP `setBreakpoints`, conditions, and `dataBreakpointInfo` watchpoints |
| 9 | `dap_stacktrace_tests` | `gridlock_dap_stacktrace_tests` | DAP `threads` and 64-frame `stackTrace` unwinding |
| 10 | `dap_variables_tests` | `gridlock_dap_variables_tests` | DAP `scopes`, `variables`, and `VariableTreeModel` lazy expansion |
| 11 | `dap_evaluate_tests` | `gridlock_dap_evaluate_tests` | DAP `evaluate` (`watch` and `repl` contexts) |
| 12 | `dap_lifecycle_tests` | `gridlock_dap_lifecycle_tests` | DAP `initialize`, `launch`, `attach`, SLURM `scancel`, and `disconnect` |
| 13 | `dap_memory_tests` | `gridlock_dap_memory_tests` | DAP `readMemory` base64 decoding |
| 14 | `hpc_memory_tests` | `gridlock_hpc_memory_tests` | `NativeMemoryReader` (`process_vm_readv`) and `MemoryDiffer` |
| 15 | `stride_security_tests` | `gridlock_stride_security_tests` | `MemoryBoundsValidator` overflow, null-page, alignment, and 64 MiB guards |
| 16 | `dap_native_bridge_tests` | `gridlock_dap_native_bridge_tests` | Hybrid DAP + `NativeMemoryReader` large-buffer fast path |
| 17 | `sanitizer_tests` | `gridlock_sanitizer_tests` | `MiSanitizer` metacharacter, control-byte, nesting-depth, and smuggling rejection |
| 18 | `obfuscator_tests` | `gridlock_obfuscator_tests` | `TelemetryObfuscator` 1400-byte MTU padding and 50 ms chaff timing |
| 19 | `advanced_features_tests` | `gridlock_advanced_features_tests` | `ElfBinaryInspector`, `MemoryProfilerWidget`, `PointerGraphDialog`, logpoints |
| 20 | `memory_export_tests` | `gridlock_memory_export_tests` | `MemoryExporter` typed CSV (`Int32`, `Float`, `Double`) and raw `.bin` output |
| 21 | `mi_parser_tests` | `gridlock_mi_parser_tests` | GDB/MI3 tuple/list parsing, `@entry` filtering, and C++ access-specifier unwrapping |
| 22 | `cliparser_tests` | `gridlock_cliparser_tests` | `CliParser` flag, positional target, and `-- <args...>` parsing |

### Individual UI Tests, Manual CLI/Tutorial E2E Suite, Coverage, Screenshots, and Wiki Sync

* **Run a Single `QtTest` Slot in `gridlock_ui_tests`**:
  ```bash
  ninja -C builddir gridlock_ui_tests
  QT_QPA_PLATFORM=offscreen ./builddir/gridlock_ui_tests testWorkstationUiCohesionAndIntegratedDocViewer
  ```
* **Manual Developer CLI & Tutorial End-to-End Suite (`scripts/e2e_cli_suite.mjs`)**:
  Zero-dependency Node.js runner exercising CLI flags (`--version`, `--help`, `--snapshot`, `--test-mode`) and real `mpicxx`/`mpicc` tutorial builds and launches (manual developer verification tool; excluded from automated Meson/CI test runs and packaged releases):
  ```bash
  ./scripts/e2e_cli_suite.mjs
  ```
* **Generate HTML Line/Branch Coverage (`scripts/run_coverage.sh`)**:
  ```bash
  bash scripts/run_coverage.sh
  ```
* **Regenerate Documentation Screenshots (`scripts/capture_ui.sh`)**:
  ```bash
  bash scripts/capture_ui.sh --docs
  ```
* **Dry-Run GitHub Wiki Synchronization (`scripts/sync_wiki.sh`)**:
  ```bash
  bash scripts/sync_wiki.sh /tmp/gridlock-wiki-preview
  ```

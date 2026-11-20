# Developer Architecture and Testing

GridLock is written in C++23 (`-Wall -Wextra -Wpedantic`) on Qt6 and Meson, with a hard architectural ceiling of `< 950` lines per source and header file.

---

## 1. Repository Layout

```text
GridLock/
├── meson.build                     # Build definition: gridlock, mpi_mm_bin, and 22 test suites
├── external/
│   ├── Qt-Advanced-Stylesheets/    # Submodule providing dynamic SVG/QSS theming (libqtadvancedcss)
│   └── tomlplusplus.wrap           # Meson wrap fallback for toml++ 3.4.0
├── resources/                      # Qt .qrc bundles (icons, Nerd Font glyphs, base stylesheets)
├── scripts/                        # Packaging, UI screenshot capture, coverage, and Wiki sync scripts
├── tutorial/                       # 8 primary C++ MPI showcase targets + 5 auxiliary C/C++ demos
├── tests/                          # QtTest unit, protocol, security, and offscreen UI suites
└── src/
    ├── main.cpp                    # CLI fast-path, Wayland/QPA setup, splash, and mode dispatch
    ├── RankState.hpp               # Per-rank execution, frame, watch, and register telemetry model
    ├── backend/                    # GridLockAutomationRunner (--test-mode) and TestGenerator
    ├── core/
    │   ├── commands/               # IDebugCommand implementations (Launch, Continue, Step, Pause, etc.)
    │   ├── hpc/                    # Coordinators, ProcessScanner, DeadlockAnalyzer, Memory & Sanitizer
    │   ├── managers/               # Config, Session, Shortcut, Theme (with QSS cache), Docset, Spack
    │   ├── models/                 # VariableNode and lazy-expanding VariableTreeModel
    │   └── utils/                  # CliParser, ElfBinaryInspector, and ClangdLspClient
    └── ui/
        ├── MainWindow*.cpp         # Split across 7 cohesive translation units (< 950 lines each)
        ├── EditorTabManager.*      # Multi-tab and horizontal/vertical split editor coordinator
        ├── GdbConsoleWidget.*      # 60 FPS coalesced GDB/MI console with readline history, Ctrl+R, Tab complete
        ├── dialogs/                # AttachProcess, ConditionalBreakpoint, CommandPalette, ProgrammerCalculator, ArrayPlot, PointerGraph, SignalCatchpoint, Checkpoint, Preferences, Xref
        ├── docks/                  # VariablesDockWidget, CallStackDockWidget, TerminalDockWidget
        ├── shaders/                # Precompiled SPIR-V GLSL 450 heatmap shaders (reserved for v0.8.8)
        ├── tutorial/               # TutorialDialog (--tutorial-mode)
        ├── views/                  # SourceCodeView, DisassemblyView, MemView, MemoryProfiler, ServerRack
        └── widgets/                # DifferentialGrid, MpiDiagnostics, MpiNetworkLog, HpcConsole, ReferenceManual
```

---

## 2. Core Subsystem Architecture

### Polymorphic Debugger Coordinators (`src/core/hpc/`)

`IBackendCoordinator` defines a backend-agnostic Qt signal/slot contract implemented by two coordinators:

1. **`GdbRankCoordinator` (`GdbRankCoordinator.hpp`, `GdbRankCoordinator.cpp`, `GdbRankInspection.cpp`)**:
   * **Local MPMD Launch**: Allocates `/tmp/gridlock_mi_<pid>_<seq>/r<i>.{in,out}` named pipes (`mkfifo`, `0600`, `O_RDWR | O_NONBLOCK`) and launches one `mpirun` job with `:`-separated `-np 1` GDB/MI3 clauses.
   * **Local Attach (`attachToProcesses`)**: Spawns one `gdb --interpreter=mi3` instance per selected `ProcessEntry` sorted by `mpiRank`, sends `-target-attach <pid>`, syncs cached breakpoints, and detaches cleanly via `-target-detach` on session stop.
   * **Remote Stubs (`attachToRemoteTargets`)**: Spawns `gdb` or `gdb-multiarch`, loads local DWARF info via `-file-exec-and-symbols`, and connects via `-target-select remote <endpoint>` or `-target-select extended-remote <endpoint>`.
   * **Execution Control, Breakpoints, Signals, Catchpoints & Checkpoints**: Supports `nexti` (`-exec-next-instruction`), `Run to Cursor` (`-exec-until`), `Jump` (`-exec-jump`), `Force Early Return` (`-exec-return`), non-destructive `-break-enable`/`-break-disable`, temporary `-break-insert -t`, `rbreak`, `-break-commands`, `handle <SIG>` / `info handle` (token `210`), `complete` tab completion (token `211`), copy-on-write `checkpoint` / `info checkpoints` / `restart` / `delete checkpoint` (tokens `212` and `213`), client-side `{ident}` logpoints, hardware watchpoints (`-break-watch [-r|-a]`), and Process Record (`record full`).
2. **`DapCoordinator` (`DapCoordinator.cpp`)**:
   * Speaks JSON-RPC 2.0 (`Content-Length` framing) to `lldb-dap` (or `lldb-vscode`), mapping 0-based `rankId` to 1-based DAP `threadId`, supporting `launch`, `attach`, `setBreakpoints` (`condition`, `hitCondition`, `logMessage`), `dataBreakpointInfo` / `setDataBreakpoints`, `stackTrace`, `scopes`, `variables`, `evaluate`, `readMemory` (with `NativeMemoryReader` fast path for reads `> 1024` doubles), and `stepBack` / `reverseContinue`.

### Floating Diagnostic Dialogs (`src/ui/dialogs/`)

* **`CommandPaletteDialog`**: Subsequence fuzzy scorer across `ShortcutManager` actions (`>`), cached workspace files, `clangd`/ELF symbols (`@`), themes, and live MPI ranks (`#`).
* **`ProgrammerCalculatorDialog`**: Modeless `Qt::Window` base converter (`HEX`/`DEC`/`OCT`/`BIN`, IEEE-754 float/double, `8/16/32/64-bit` width, `BSWAP`, 64-bit interactive bit-toggle grid, radix-aware keypad) wired to right-click selection handoff signals across all major inspection views.
* **`ArrayPlotDialog`**: Modeless 1D line/stem and 2D colormapped heatmap canvas (`Viridis`, `Plasma`, `Cool-Warm`, `Grayscale`) with IEEE-754 `NaN`/`Inf` telemetry, auto-refresh previous-stop ghost curve diffing, CSV/PNG export, and `gnuplot` output.
* **`SignalCatchpointDialog`**: Two-tab manager for POSIX/HPC signal dispositions (`handle <SIG>`) and C++ exception, Linux syscall, and `dlopen`/`dlclose` catchpoints.
* **`CheckpointDialog`**: Modeless copy-on-write snapshot manager pairing GDB `checkpoint`/`restart`/`delete checkpoint` with `inspectProcessCheckpointSafety(pid, procRoot)` scanning `<procRoot>/<pid>/fd` raw symlink targets (`::readlink`) and `<procRoot>/<pid>/maps` VMAs for `Safe`, `Warning`, and `BlockedOrUnsafe` handles.

### Startup Performance, QSS Disk Cache & Lazy-Loading Architecture

* **QSS Disk Cache (`ThemeManager`)**: Compiled theme stylesheets from `Qt-Advanced-Stylesheets` are hashed and cached on disk under `~/.cache/gridlock/qss/` so warm launches skip SVG/QSS recompilation and apply the cached stylesheet directly.
* **Deferred Bottom-Tab Initialization**: Hidden bottom-rail tabs (**`Package Manager`**, **`Terminal`**, and **`Reference Manual`**) avoid spawning `QTermWidget` PTY shells, querying `spack`/`dnf`/`pacman`, or scanning Zeal SQLite3 docsets during startup; each initializes lazily on first tab activation or explicit API call (`F1` docset lookup, `Build Target`).
* **Non-Blocking `clangd` Startup**: `ClangdLspClient` launches asynchronously after the main window paints so interactive window startup stays under `450 ms` (verified in `gridlock_ui_tests`).

### Process Discovery, Deadlock Analysis, Memory & Security

* **`ProcessScanner`**: Parses `/proc/[pid]/{comm,status,cmdline,exe,environ}` (recognizing `OMPI_*`, `PMI_*`, `PMIX_*`, `SLURM_*`, and `MV2_*` rank/world/job variables), checks `/proc/sys/kernel/yama/ptrace_scope`, and expands single, range (`..` / `-`), and comma-separated `RemoteTargetSpec` endpoints.
* **`DeadlockAnalyzer`**: Inspects frames `0..7` across paused ranks for 16 canonical MPI calls (`9` collectives + `7` P2P/wait calls and their `PMPI_*` wrappers), classifying multi-rank stalls into `Collective Divergence` or `Wait-For Cycle Stall` and resolving user C/C++ caller frames.
* **`NativeMemoryReader`, `MemoryBoundsValidator`, `MemoryDiffer`, `MemoryExporter`**: Wraps Linux `process_vm_readv` behind overflow-checked bounds (`>= 0x1000`, `<= 64 MiB`, natural stride alignment), computes byte-level cross-rank diff masks, and exports `Int32`/`Float`/`Double` CSV matrices or raw `.bin` files.
* **`MiSanitizer` & `TelemetryObfuscator`**: Rejects embedded NULs, nesting depth `> 32`, shell metacharacters (`;`, `|`, `&`, `` ` ``, `$`), multi-line smuggling, and GDB shell-escape commands (`!`, `shell`, `pipe`, `make`), while `TelemetryObfuscator` shapes telemetry into constant-rate `1400`-byte frames at `20 Hz` (`50 ms` ticks).
* **`ElfBinaryInspector` & `ClangdLspClient`**: Zero-dependency ELF64 section/symbol/string parser + `objdump -d -l -C -M intel` DWARF mapper + `/proc` memory/CPU parser, paired with an asynchronous JSON-RPC 2.0 `clangd` client (`completion`, `publishDiagnostics`, `hover`, `definition`, `references`, `rename`).

### Split `MainWindow` Architecture (`src/ui/MainWindow*.cpp`)

To keep every file under `< 950` lines while maintaining fast incremental builds, `MainWindow` is partitioned by responsibility:
* `MainWindow.cpp`: Construction, menu bar, preferences application, and window close lifecycle.
* `MainWindowWorkspace.cpp`: Toolbar, address navigation ribbon, central editor/disassembly splitters, right-side docks, and `View` menu. *(Note: `HpcConsoleWidget.cpp` is unity-included at the bottom of `MainWindowWorkspace.cpp`.)*
* `MainWindowDocks.cpp`: Bottom tool rail (`Debug`, `MPI`, `Terminal`, `Docs`), lazy bottom-tab activation, floating dock drag/redock handlers, and `ThemePalette` propagation.
* `MainWindowDebugger.cpp`: `Debug` menu action wiring, non-linear execution workflows (`nexti`, `Ctrl+F10`, PC jump, early return, `rbreak`), floating dialog launchers (`ArrayPlotDialog`, `SignalCatchpointDialog`, `CheckpointDialog`), target compilation (`ensureTargetBinaryCompiled`), workspace switching, and build system detection.
* `MainWindowExecution.cpp`: Live coordinator signal wiring, local `/proc` attach, remote `gdbserver`/QEMU attach, `MemView` / `MemoryProfilerWidget` / `PointerGraphDialog` integration, inline ghost values, and reverse debugging workflows.
* `MainWindowNavigation.cpp`: Bidirectional `sourceLineToAddress` / `addressToSourceLine` mapping, history back/forward stack, and `Ctrl+Shift+F` workspace search.
* `MainWindowCohesion.cpp`: Compact `<= 26px` bottom-tab layout normalization, `F1` documentation lookup, and deterministic screenshot generation (`captureDocumentationScreenshots`).

---

## 3. Running the 22 Meson Test Suites

Delete stale `.gcda` coverage counters (if coverage was previously enabled) and run all 22 suites headlessly:

```bash
find builddir -name "*.gcda" -delete
meson test -C builddir --print-errorlogs
```

Every test target in `meson.build` runs with `QT_QPA_PLATFORM=offscreen`:

| # | Suite Name | Executable | Scope Tested |
| :--- | :--- | :--- | :--- |
| 1 | `coordinator_tests` | `gridlock_tests` | `GdbRankCoordinator`, `ProcessScanner` (`/proc` & remote endpoint ranges), `DeadlockAnalyzer` |
| 2 | `lsp_tests` | `gridlock_lsp_tests` | `ClangdLspClient` completion, diagnostics, hover, definition, references, rename |
| 3 | `ui_tests` | `gridlock_ui_tests` | 57 offscreen end-to-end UI tests across `MainWindow`, docks, Command Palette, Calculator, Array Plotter, Pointer Graph, Signals/Catchpoints, GDB Readline, Non-Linear Execution, Breakpoint Lifecycle, CoW Checkpoints, and startup QSS caching |
| 4 | `config_tests` | `gridlock_config_tests` | `ConfigManager` and `SessionManager` TOML / `QSettings` round-trips |
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
| 22 | `cliparser_tests` | `gridlock_cliparser_tests` | `CliParser` flag parsing and `shouldUseOffscreenPlatform` detection |

### Running Individual UI Tests, Coverage, Screenshots, and Wiki Sync

* **Run a Single `QtTest` Slot in `gridlock_ui_tests`**:
  ```bash
  QT_QPA_PLATFORM=offscreen ./builddir/gridlock_ui_tests testWorkstationUiCohesionAndIntegratedDocViewer
  ```
* **Generate HTML Line/Branch Coverage (`scripts/run_coverage.sh`)**:
  ```bash
  bash scripts/run_coverage.sh
  # Writes builddir/meson-logs/coveragereport/index.html via gcovr
  ```
* **Regenerate All 21 Documentation Screenshots (`scripts/capture_ui.sh`)**:
  Normal `meson test` runs never modify `docs/screenshots/`. To regenerate all screenshots deterministically (using sanitized `/proc` fixtures for `attach_process.png` and `attach_remote.png`):
  ```bash
  bash scripts/capture_ui.sh
  ```
* **Dry-Run GitHub Wiki Synchronization (`scripts/sync_wiki.sh`)**:
  Mirrors `docs/*.md` and `docs/screenshots/*.png` into a flat GitHub Wiki checkout with rewritten links, `Home.md`, `_Sidebar.md`, and `_Footer.md`:
  ```bash
  bash scripts/sync_wiki.sh /tmp/gridlock-wiki-preview
  ```

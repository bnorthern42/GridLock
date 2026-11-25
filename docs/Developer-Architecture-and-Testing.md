# Developer Architecture and Testing

GridLock (`v0.8.81`) is written in C++23 (`-Wall -Wextra -Wpedantic`) on Qt6 and Meson, with modular translation units split under 500 lines (hard repository ceiling `< 950` lines) and per-subdirectory architectural `README.md` guides across `src/`, `tests/`, and `scripts/`.

---

## 1. Repository Layout & CLI Entry Points

```text
GridLock/
├── meson.build                     # Build definition: gridlock, mpi_mm_bin, and 24 test suites
├── agent.md                        # Architectural invariants, Graphify workflow, and style rules
├── include/gridlock/               # Frozen C99 plugin ABI (plugin_api.h) and C++23 helpers
├── examples/plugins/               # Standalone out-of-tree native plugin examples (domain_mesh_probe)
├── external/
│   ├── Qt-Advanced-Stylesheets/    # Submodule providing dynamic SVG/QSS theming (libqtadvancedcss)
│   └── tomlplusplus.wrap           # Meson wrap fallback for toml++ 3.4.0
├── resources/                      # Qt .qrc bundles (icons, Nerd Font glyphs, base stylesheets)
├── scripts/                        # init_plugin.lua, packaging, UI capture, coverage, Wiki sync (see scripts/README.md)
├── tutorial/                       # 8 primary C++ MPI showcase targets + 5 auxiliary C/C++ demos
├── tests/                          # Modular QtTest suites (ui/, dap/, hpc/, core/) with README guides
└── src/
    ├── main.cpp                    # CLI fast-path (--verify-plugin, --snapshot), Wayland/QPA setup, mode dispatch
    ├── RankState.hpp               # Per-rank execution, frame, watch, and register telemetry model
    ├── backend/                    # GridLockAutomationRunner (--test-mode) and TestGenerator
    ├── core/
    │   ├── commands/               # IDebugCommand implementations (Launch, Continue, Step, Pause, etc.)
    │   ├── hpc/                    # Organized into gdb/, dap/, lsp/, memory/, analysis/, system/
    │   ├── managers/               # ConfigManager*, Session, Shortcut, Theme, Docset*, Spack (< 500 lines)
    │   ├── models/                 # VariableNode and lazy-expanding VariableTreeModel
    │   ├── plugins/                # PluginManager, PluginHostBridge*, PluginInstaller, PluginManifest, PluginVerifier
    │   └── utils/                  # CliParser, SearchEverywhereEngine, ElfBinaryInspector*, ClangdLspClient
    └── ui/
        ├── MainWindow*.cpp         # Cohesive < 500-line units (Toolbar, Perspectives, Execution, Plugins, etc.)
        ├── EditorTabManager.*      # Multi-tab and QSplitter horizontal/vertical split editor coordinator
        ├── GdbConsoleWidget.*      # 60 FPS coalesced GDB/MI console with readline history, Ctrl+R, Tab complete
        ├── dialogs/                # Organized into plotter/, graph/, calculator/, debugger/, project/, search/, preferences/
        ├── docks/                  # VariablesDockWidget, CallStackDockWidget, TerminalDockWidget, PluginReportDockWidget
        ├── shaders/                # Precompiled SPIR-V GLSL 450 heatmap shaders
        ├── tutorial/               # TutorialDialog (--tutorial-mode)
        ├── views/                  # SourceCodeView*, Disassembly*, MemView*, MemoryProfiler*, ServerRack
        └── widgets/                # DifferentialGrid, MpiDiagnostics, MpiNetworkLog, HpcConsole*, ReferenceManual
```

### Command-Line Flag Reference (`src/core/utils/CliParser.hpp`)

| Flag / Argument | Mode | Description |
| :--- | :---: | :--- |
| `[target] [-- <args...>]` | Interactive / Headless | Opens an ELF binary, source file, or workspace directory and records `<args...>` as inferior program arguments. |
| `--headless` | Offscreen (`offscreen` QPA) | Runs GridLock without requiring an X11 or Wayland display server. |
| `--snapshot <file.png>` | Offscreen (`offscreen` QPA) | Renders the workspace window headlessly and exports a PNG screenshot to `<file.png>`. |
| `--verify-plugin <path>` | Headless Sandbox (`QCoreApplication`) | Runs `PluginVerifier::verifyBinary(<path>)` in an isolated sandbox (symbol hygiene, ABI v1.0 headers, regex/menu validation, dry-run lifecycle, and leak check), exiting `0` on `[PASS]` or `1` on `[FAIL]`. |
| `--tutorial-mode` | Interactive | Opens the interactive `TutorialDialog` launcher at startup. |
| `--test-mode` | Automated Runner | Executes `GridLockAutomationRunner` against the supplied positional target. |
| `--verbose` | Any | Enables verbose diagnostic logging on stderr and console streams. |

---

## 2. Core Subsystem Architecture

### Polymorphic Debugger Coordinators (`src/core/hpc/gdb/`, `src/core/hpc/dap/`)

`IBackendCoordinator` defines a backend-agnostic Qt signal/slot contract implemented by two coordinators:

1. **`GdbRankCoordinator` (`GdbRankCoordinator.cpp`, `GdbRankCommands.cpp`, `GdbRankInspection.cpp`, `GdbMiParser.cpp`)**:
   * **Local MPMD Launch**: Allocates `/tmp/gridlock_mi_<pid>_<seq>/r<i>.{in,out}` named pipes (`mkfifo`, `0600`, `O_RDWR | O_NONBLOCK`) and launches one `mpirun` job with `:`-separated `-np 1` GDB/MI3 clauses.
   * **Local Attach (`attachToProcesses`)**: Spawns one `gdb --interpreter=mi3` instance per selected `ProcessEntry` sorted by `mpiRank`, sends `-target-attach <pid>`, syncs cached breakpoints, and detaches cleanly via `-target-detach` on session stop.
   * **Remote Stubs (`attachToRemoteTargets`)**: Spawns `gdb` or `gdb-multiarch`, loads local DWARF info via `-file-exec-and-symbols`, and connects via `-target-select remote <endpoint>` or `-target-select extended-remote <endpoint>`.
   * **Execution Control, Silent Plugin Channels, Signals & Checkpoints**: Supports `nexti` (`-exec-next-instruction`), `Run to Cursor` (`-exec-until`), `Jump` (`-exec-jump`), `Force Early Return` (`-exec-return`), `haltAll` (`F6`), in-place `-gdb-set` variable/register writes, non-destructive `-break-enable`/`-break-disable`, temporary `-break-insert -t`, `rbreak`, `-break-commands`, `handle <SIG>` / `info handle` (token `210`), `complete` tab completion (token `211`), copy-on-write `checkpoint` / `info checkpoints` / `restart` / `delete checkpoint` (tokens `212` and `213`), dedicated silent plugin token sets (`m_pendingPluginEvals` and `m_pendingPluginMemReads`), client-side `{ident}` logpoints, hardware watchpoints (`-break-watch [-r|-a]`), and Process Record (`record full`).
2. **`DapCoordinator` (`DapCoordinator.cpp`, `DapCoordinatorRequests.cpp`)**:
   * Speaks JSON-RPC 2.0 (`Content-Length` framing) to `lldb-dap` (or `lldb-vscode`), mapping 0-based `rankId` to 1-based DAP `threadId`, supporting `launch`, `attach`, `setBreakpoints` (`condition`, `hitCondition`, `logMessage`), `dataBreakpointInfo` / `setDataBreakpoints`, `stackTrace`, `scopes`, `variables`, `evaluate`, `readMemory` (with `NativeMemoryReader` fast path for reads `> 1024` doubles), and `stepBack` / `reverseContinue`.

### Floating Diagnostic & Navigation Dialogs (`src/ui/dialogs/`)

* **`CommandPaletteDialog` + `SearchEverywhereEngine` (`search/`)**: Multi-scope Search Everywhere (`Double Shift`, `Ctrl+P`, `Ctrl+Shift+P`) with 6 tabbed scopes, CamelCase/acronym fuzzy scoring, 2-line workspace-relative rows, 35-line numbered context preview (`250`-line / `32 KB` mega-file cap), and `Small`/`Medium`/`Large`/`X-Large` size presets.
* **`PointerGraphDialog` (`graph/`)**: Topological Sugiyama DAG canvas preserving manual drag positions, with multi-root pinning, `"old -> new"` mutation badges, `Shift+Click` per-node width expansion, full-value hover tooltips, double-click pointer expansion, corner minimap, `/x` `/d` `/b` format overrides, and `2x` PNG / SVG export.
* **`ArrayPlotDialog` (`plotter/`)**: Multi-rank 1D curve overlays, `start:end:stride` slicing, `Log10` scaling, `Complex64`/`Complex128` decoding, 2D colormapped heatmaps, validated `splot`/`pm3d` `gnuplot` export, and virtualized 2D/3D C/Fortran tensor tables.
* **`PreferencesDialog` (`preferences/`)**: Modular per-page preferences (`>= 1040 px` minimum width clamped to available screen geometry) coordinated via a hierarchical `QTreeWidget` (`Appearance -> Toolbar` layout customizer, `Keybindings` `KeySequenceRecorderWidget`, `Plugins` `PluginSettingsPage`, `SlurmBatchParser`, etc.).
* **`ProgrammerCalculatorDialog` (`calculator/`), `SignalCatchpointDialog` & `CheckpointDialog` (`debugger/`)**: Synchronized multi-base/IEEE-754 calculator, POSIX/HPC signal matrix + catchpoints, and `/proc`-guarded copy-on-write fork checkpoints.

### Startup Performance, QSS Disk Cache & Lazy-Loading Architecture

* **Asynchronous Binary Launch & QSS Disk Cache (`ThemeManager`, `MainWindowExecution`)**: Compiled theme stylesheets from `Qt-Advanced-Stylesheets` are hashed and cached under `AppConfigLocation/acss`, and binary launch avoids synchronous disassembly/symbol stalls on the UI thread.
* **Deferred Bottom-Tab Initialization**: Hidden bottom-rail tabs (**`Package Manager`**, **`Terminal`**, and **`Reference Manual`**) initialize lazily on first activation or explicit lookup (`F1`, `Build Target`), while `SourceCodeView` routes multi-megabyte data files through a virtualized large-CSV viewer.
* **AST Knowledge Graph (`graphify-out/`)**: Developers and coding agents query `graphify` (`graphify query ...`) alongside local `README.md` guides to trace cross-module dependencies before modifying translation units.

> [!NOTE]
> `graphify-out/` is a local AST knowledge-graph cache ignored in `.gitignore`. Refresh it locally after adding, moving, or splitting files via `graphify extract . --code-only && graphify cluster-only . --no-viz`.

### Split `MainWindow` Architecture (`src/ui/MainWindow*.cpp`)

To keep every translation unit under 500 lines while maintaining fast incremental builds, `MainWindow` is partitioned into focused modules:
* `MainWindow.cpp` & `MainWindowLifecycle.cpp`: Construction, menu bar, preferences application, clean session/PTY reaping on exit, and preview fixtures.
* `MainWindowToolbar.cpp` & `MainWindowPerspectives.cpp`: `< 1,040 px` customizable `#MainToolbar` (`rebuildToolbarFromLayout`), `Launch Options` (`Config`) popover, `SOURCE | DEBUG | BINARY | MPI` perspectives, rank-count progressive disclosure, and `captureDocumentationScreenshots`.
* `MainWindowWorkspace.cpp` & `MainWindowDocks.cpp`: Central editor/disassembly splitters, right-side docks, bottom tool rail, lazy tab activation, floating dock redocking, and theme propagation.
* `MainWindowDebugger.cpp` & `MainWindowExecution.cpp`: `Debug` actions, non-linear execution (`nexti`, `Ctrl+F10`, PC jump, early return, `rbreak`), master `setFocusedRank()` sync hub, coordinator signal wiring, `/proc` and remote attach, and `+x`/build preflight checks.
* `MainWindowNavigation.cpp`, `MainWindowBinaryNav.cpp` & `MainWindowCohesion.cpp`: Session I/O, workspace PNG snapshots (`--snapshot`), `AddressSpaceNavBar` history, DWARF line/address sync, `F1` documentation lookup, and UI cohesion.
* `MainWindowPlugins.cpp`: Owns `PluginManager`, synchronizes `GdbRankCoordinator` lifecycle callbacks (`on_session_start`, `on_rank_stopped`, `on_session_end`), manages `#mainWindowPluginRestartBanner` (**Hot-Reload Now** / **Restart GridLock Now**), routes `PluginReportDockWidget` tabs and `emit_structured_event` payloads, and populates **Tools -> Plugins**.

### Native C99 Plugin ABI (`include/gridlock/plugin_api.h`)

GridLock exposes a frozen C99 binary interface (`GRIDLOCK_PLUGIN_ABI_MAJOR = 1`, `GRIDLOCK_PLUGIN_ABI_MINOR = 0`) paired with an optional header-only C++23 wrapper ([`include/gridlock/plugin_cpp_helpers.hpp`](../include/gridlock/plugin_cpp_helpers.hpp)) so external domain extensions inspect stopped MPI ranks and extend workstation UI surfaces without linking Qt6 or internal debugger libraries. Documentation is organized into four guides plus the scaffolding generator:
* [Plugins (User Guide)](Plugins.md): Multi-forge installation (GitHub, GitLab subgroups, Codeberg, Gitea, custom Git URLs), `plugins.toml` + `plugins-lock.toml` sync, `Version (<commit_short>)` provenance, `Update` / `Update All`, live **Hot-Reload Now**, workspace `SHA-256` trust approval, and `gridlock --verify-plugin <path>`.
* [Plugin Architecture, Lifecycle, Security & Build Pipeline](Plugin-Architecture-and-Lifecycle.md): Architecture diagrams, pre-`dlopen` trust gating, `PluginVerifier`, `hotReloadPlugins` (`STB_GNU_UNIQUE` symbol check + session replay), silent MI channels (`m_pendingPluginEvals` / `m_pendingPluginMemReads`), enforced resource quotas, and `PluginInstaller` internals.
* [C99 Plugin ABI & C++23 Wrapper Specification](Plugin-C99-ABI-Reference.md): Exact 64-bit struct byte offsets for all 9 ABI structs (`GridLockHostApi` `312 B`, `GridLockPluginDescriptor` `160 B`, `GridLockStopInfo` `64 B`, `GridLockVariableContext` `80 B`, `GridLockSurfaceContext` `80 B`, `GridLockCommandSpec` `56 B`, `GridLockThemeInfo` `212 B`, `GridLockTypeInfo` `288 B`, `GridLockContiguousRange` `424 B`), status/UI enums, all 28 `GridLockHostApi` callbacks, and `plugin_cpp_helpers.hpp` wrappers.
* [Plugin Authoring, UI Surfaces & Build Recipes](Plugin-Authoring-and-UI-Surfaces.md): [`scripts/init_plugin.lua`](../scripts/init_plugin.lua) scaffolding generator, zero-call `queryTypeInfo` / `resolveContiguousRange` usage, 5 context-menu surfaces, `PluginReportDockWidget` formats (`MARKDOWN`, `KEY_VALUE_TABLE`, `JSON_TREE`), `plugin.map` version scripts, `domain_mesh_probe` walkthrough, and `--verify-plugin` CI gating.

* **Scaffolding, Compiling & Verifying Out-of-Tree Plugins**:
  Generate a complete C++23 or C99 plugin project (with `plugin.map` symbol hiding for live hot-reload) via [`scripts/init_plugin.lua`](../scripts/init_plugin.lua), compile, and run the sandboxed verifier:
  ```bash
  lua scripts/init_plugin.lua --yes --name "My Mesh Probe" --lang c++23 --build meson --dir /tmp/my_mesh_probe
  meson setup /tmp/my_mesh_probe/build /tmp/my_mesh_probe -Dgridlock_include_dir="$PWD/include"
  ninja -C /tmp/my_mesh_probe/build
  ./builddir/gridlock --verify-plugin /tmp/my_mesh_probe/build/libgridlock_my_mesh_probe.so
  ```

---

## 3. Running the 24 Meson Test Suites and Developer Scripts

Delete stale `.gcda` coverage counters (if coverage was previously enabled) and run all 24 suites headlessly (unit test binaries automatically isolate `ConfigManager` into a temporary directory when `GRIDLOCK_TEST_CONFIG_DIR` is unset):

```bash
find builddir -name "*.gcda" -delete
meson test -C builddir --print-errorlogs
```

Every test target in `meson.build` runs with `QT_QPA_PLATFORM=offscreen`:

| # | Suite Name | Executable | Scope Tested |
| :--- | :--- | :--- | :--- |
| 1 | `coordinator_tests` | `gridlock_tests` | `GdbRankCoordinator` (including CPU-side mock `rocgdb` MI3 parsing and opt-in live GPU test), `ProcessScanner` (`/proc` & remote endpoint ranges), `DeadlockAnalyzer` |
| 2 | `lsp_tests` | `gridlock_lsp_tests` | `ClangdLspClient` completion, diagnostics, hover, definition, references, rename |
| 3 | `ui_tests` | `gridlock_ui_tests` | 64 offscreen end-to-end UI tests across `MainWindow`, customizable toolbar, Search Everywhere, Calculator, Array Plotter, Pointer Graph, Preferences (`PluginSettingsPage` hot-reload & Git installer), Signals/Catchpoints, GDB Readline, Non-Linear Execution, Breakpoint Lifecycle, CoW Checkpoints, and QSS caching |
| 4 | `config_tests` | `gridlock_config_tests` | `ConfigManager` (per-target `programArgs`, toolbar layouts, popup presets, plugin trust/enablement) and `SessionManager` round-trips |
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
| 21 | `mi_parser_tests` | `gridlock_mi_parser_tests` | GDB/MI3 tuple/list parsing, `@entry` filtering, silent plugin MI tokens, and C++ access-specifier unwrapping |
| 22 | `cliparser_tests` | `gridlock_cliparser_tests` | `CliParser` flag (`--verify-plugin`, `--snapshot`, `--headless`), positional target, and `-- <args...>` parsing |
| 23 | `plugin_abi_tests` | `gridlock_plugin_abi_tests` | Compile-time `sizeof`/`offsetof` `static_assert` layout gates for all 9 `plugin_api.h` structs, all 28 callbacks, and `plugin_cpp_helpers.hpp` |
| 24 | `plugin_manager_tests` | `gridlock_plugin_manager_tests` | `PluginManager`, `SHA-256` workspace trust, multi-forge `PluginManifest`/`PluginInstaller`, `PluginVerifier::verifyBinary`, silent `eval_expr`, zero-call DWARF ranges, and `hotReloadPlugins` |

### Developer Scripts (`scripts/README.md`), GPU Opt-In Suites, Coverage & Wiki Sync

For complete usage of every script under `scripts/` (including `init_plugin.lua`, `build_appimage.sh`, `capture_ui.sh`, `run_coverage.sh`, `sync_wiki.sh`, and `e2e_cli_suite.mjs`), see [`scripts/README.md`](../scripts/README.md):

* **Run a Single `QtTest` Slot in `gridlock_ui_tests`**:
  ```bash
  ninja -C builddir gridlock_ui_tests
  QT_QPA_PLATFORM=offscreen ./builddir/gridlock_ui_tests testWorkstationUiCohesionAndIntegratedDocViewer
  ```
* **Manual Developer CLI & Tutorial End-to-End Suite (`scripts/e2e_cli_suite.mjs`)**:
  ```bash
  ./scripts/e2e_cli_suite.mjs
  ```
* **Manual Opt-In ROCm / `rocgdb` Live GPU Kernel Probe (`GRIDLOCK_ENABLE_GPU_TESTS=1`)**:
  ```bash
  ninja -C builddir gridlock_tests
  GRIDLOCK_ENABLE_GPU_TESTS=1 QT_QPA_PLATFORM=offscreen ./builddir/gridlock_tests testLiveRocgdbHipKernelOnGpuIfAvailable
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

> [!IMPORTANT]
> `scripts/e2e_cli_suite.mjs` is strictly a manual local developer tool and must never be registered in `meson.build` (`meson test`) or invoked inside `.github/workflows/release.yml`.

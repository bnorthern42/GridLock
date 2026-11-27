# Changelog

All notable changes to the GridLock project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Hybrid MPI + OpenMP Fortran Debugging Tutorial & Example (`docs/Tutorial-Fortran-OpenMP.md`, `tutorial/stencil_omp.f90`, `meson.build`, `MainWindow`, `SearchEverywhereEngine`)**:
  - Added a runnable 2D `4 x 6` column-major Fortran MPI + OpenMP stencil example (`gfortran` / `ifx`) in `tutorial/stencil_omp.f90` with an optional Meson `stencil_omp_bin` target, in-app `TARGET:` / Search Everywhere registration, coordinate-encoded state (`rank * 1000 + j * 100 + i`), and a complete tutorial covering multi-rank + OpenMP thread navigation, `set scheduler-locking on/off`, 1-based Fortran slices, and column-major `ArrayPlotDialog` inspection.
- **Fortran (`gfortran` / `ifx`) Frame Detection, 1-Based Slicing & Column-Major Inspection (`LanguageExpressionHelper`, `GdbRankCoordinator`, `GdbMiParser`, `GdbRankInspection`, `ArrayPlotDialog`, `ArrayPlotCanvas`, `TensorSliceTableModel`, `PluginHostBridge`)**:
  - Added per-rank `SourceLanguage` tracking (`Unknown`, `C`, `Cpp`, `Fortran`) resolved from GDB/MI `-gdb-show language` and frame filename extensions (`.f`, `.for`, `.f90`, `.f95`, `.f03`, `.f08`, `.F90`), automatically refreshed on stops/frame switches and cleared on resume.
  - Added `LanguageExpressionHelper.hpp` for Fortran 1-based inclusive slice formatting (`vec(1:16)`, `mat(2:5, 1:4)`), Fortran base-address evaluation (`&(arr)` without C `[0]` subscripting), GDB Fortran array type/bounds parsing (`real(kind=8) (10,20)`, `integer(kind=4) (0:9,1:5)`), generalized N-D column-major vs. row-major indexing, and contiguous-range resolution in `PluginHostBridge`.
  - Updated `ArrayPlotDialog`, `ArrayPlotCanvas`, and `TensorSliceTableModel` to display a live `Fortran (1-based, column-major)` / `C/C++ (0-based, row-major)` badge, switch offset spinboxes and table headers to 1-based bounds on Fortran frames, parse Fortran `(start:end)` and `(r1:r2, c1:c2)` slices, and render 2D heatmaps and tensor tables in column-major order.

### Changed
- **Pruned Qt6 Dependencies, Optional `Qt6::Svg` Feature & Optional MPI Tutorial Targets (`meson.build`, `meson_options.txt`, `PointerGraphDialog`, `docs/Stability-Audit.md`)**:
  - Removed unused `Qt6::Qml` and `Qt6::OpenGLWidgets` modules from `meson.build` and moved `Qt6::Test` strictly to the test targets so the `gridlock` binary no longer requires test or QML/OpenGL packages on minimal HPC cluster images.
  - Added the `-Dsvg=auto|enabled|disabled` Meson feature option gating `Qt6::Svg` (`GRIDLOCK_HAS_QT_SVG`) with a clean PNG-only fallback in `PointerGraphDialog`, resolved the `Qt-Advanced-Stylesheets` Meson sandbox include warning, and made the `mpicc`/`mpicxx` tutorial targets optional (`required: false`).

### Fixed
- **Balanced-Brace GDB/MI3 Tuple & Frame Parsing (`GdbMiParser.hpp`, `GdbMiParser.cpp`, `tests/hpc/test_mi_parser.cpp`, `tests/ui/test_ui_cohesion.cpp`)**:
  - Replaced flat-brace regexes (`\{([^{}]+)\}` and `[^}]*`) across `-stack-list-variables` / `-stack-list-locals`, `*stopped` `frame={...}`, `-var-update` `changelist=[...]`, watchpoint stops, and hover replies (`100^done`) with a quote- and escape-aware balanced-delimiter scanner (`gridlock::mi::*`), preserving GDB 14+ `libstdc++` pretty-printed `std::vector`/`std::map` values, structs with brace-quoted strings, Fortran derived types, and multi-argument `*stopped` frames with nested `args=[{...}]`.

## [0.9.1] - 2026-11-26

### Added
- **Optional `qtermwidget6` Dependency & Built-In Qt6 Fallback Terminal (`meson_options.txt`, `meson.build`, `TerminalDockWidget`)**:
  - Added a `qtermwidget` Meson feature option (`auto` by default, checking `qtermwidget6` and `qtermwidget-qt6`) that automatically falls back to a built-in Qt6 `QPlainTextEdit` + `QProcess` interactive shell terminal (`HAVE_QTERMWIDGET6 == 0`) with ANSI stripping, copy/paste/clear support, and monospace enforcement so GridLock compiles cleanly on HPC clusters (such as TACC Stampede3) where `qtermwidget6` development headers are not installed.
- **`LocalSlurmBackend` Submission, Node Discovery & Multi-Rank `gdbserver` Auto-Attach (`LocalSlurmBackend`, `MainWindow`, `MainWindowBuild`, `MainWindowDebugger`, `MainWindowToolbar`, `SpackManager`)**:
  - Added `LocalSlurmBackend` implementing real Slurm batch execution and automatic debugger attachment: stages a per-rank `gdbserver --multi` wrapper (`SLURM_PROCID` / `PMI_RANK` / `OMPI_COMM_WORLD_RANK` bound to `base_port + rank`, default `2345`) inside the shared working directory (never node-local `/tmp`), submits via `sbatch --parsable`, polls `squeue -j <job_id> -h -o "%T"` from `PENDING` / `CONFIGURING` to `RUNNING`, expands allocated hostnames via `scontrol show hostnames`, establishes optional SSH `-L` local port-forwarding tunnels when `SshSettings::host` is configured, attaches `GdbRankCoordinator` in `extended-remote` mode with ranks paused at attach, and runs `scancel <job_id>` on debug stop or teardown.
- **Offline Slurm Pipeline, `#SBATCH` Parser & `HPC Integration` Persistence Tests (`tests/core/test_config.cpp`, `tests/ui/test_ui_cohesion.cpp`)**:
  - Added offline unit and headless UI regression tests using isolated `QSettings` and temporary `sbatch`, `squeue`, `scontrol`, and `scancel` shim scripts verifying `SlurmBatchParser` directive round-tripping, full 25-field `HPC Integration` `QSettings` persistence across `PreferencesDialog` restarts, compiled binary `{FILE}` resolution, multi-node `node01:2345..node02:2348` endpoint mapping, `sbatch` stderr reporting, and `scancel` cleanup.
- **`remote.toml` Target Preset Import/Export & Schema (`AttachProcessDialog`, `ProcessScanner`, `GdbRankCoordinator`, `docs/remote.schema.json`, `examples/remote.toml`)**:
  - Added `IMPORT REMOTE.TOML...` and `EXPORT...` actions to the **Remote Target (`gdbserver` / QEMU)** dialog tab (`ProcessScanner::parseRemoteTomlFile` / `exportRemoteTomlFile`), supporting string range and array `target.endpoints`, `symbol_binary`, `gdb_binary`, `protocol_mode` (`remote` / `extended-remote`), and `[paths]` `substitute` rules (`-gdb-set substitute-path`), with inline validation diagnostics, JSON Schema Draft 2020-12 (`docs/remote.schema.json`), and a documented example (`examples/remote.toml`).
- **Remote Target Dialog `QSettings` & Session Persistence (`AttachProcessDialog`, `MainWindow`, `ProcessScanner`)**:
  - Persisted `Endpoint(s)`, `Local Symbol Binary (ELF)`, `GDB Binary`, `Protocol Mode` (`remote` vs. `extended-remote`), path substitutions, and active tab selection both in memory across dialog reopens and across GridLock restarts under `RemoteTarget/` in `QSettings` (`RemoteTargetDialogState`).
- **Headless Remote Attach, `remote.toml`, QEMU `gdbstub` & Exited-Rank Regression Coverage (`tests/ui/test_ui_debugger.cpp`)**:
  - Added headless `QtTest` regression coverage (`testRemoteTargetPersistenceAndExitedRankGuard`, `testRemoteTargetQSettingsAndQemuStubAttach`, and `testRemoteTomlImportAndValidation`) verifying `remote.toml` import/export and malformed-file error handling, isolated `QSettings` round-trip persistence across restarts, single-rank QEMU system `gdbstub` (`localhost:1234` without a local ELF symbol binary) attach/detach, initial remote attach stop retention, tutorial tab dismissal, and exited-inferior inspection guards.

### Changed
- **`HPC / Cluster` Preferences Tab Layout & Remote Attach Defaults (`HpcSettingsPage`, `PreferencesDialog`, `ConfigManager`, `LocalSlurmBackend`, `GdbMiParser`)**:
  - Reorganized **Preferences -> HPC / Cluster** into two titled sections (**MPI Runtime & Rank Placement** and **Remote & Slurm Attach Defaults**) with top-aligned spacing, placed the bold red experimental Slurm warning label tightly beneath the opt-in checkbox inside a fixed-height container, added persisted **Base GDBServer Port** (`hpc/base_gdbserver_port`, default `2345`) and **Attach Retry Timeout (s)** (`hpc/attach_retry_timeout_sec`, default `15`) spinboxes wired into `LocalSlurmBackend` and `GdbMiParser`, and added a **Configure Slurm Script & Paths...** button jumping directly to **HPC Integration**.
- **Experimental Slurm File Menu Opt-In (`MainWindow`, `HpcSettingsPage`, `PreferencesDialog`, `ConfigManager`)**:
  - Hid **Submit Slurm Job...** from the top-level **File** menu by default and gated its live visibility behind an opt-in checkbox (`hpc/enable_slurm_menu`, default `false`) in **Preferences -> HPC / Cluster** accompanied by a bold red experimental warning (`Experimental, untested, not responsible for breakage`).
- **Meson `>= 1.3.0` Build Requirement & HPC Prerequisites (`meson.build`, `README.md`)**:
  - Added `meson_version : '>= 1.3.0'` to `project('GridLock', ...)` in `meson.build` so older system Meson releases fail immediately with a clear version diagnostic rather than `cpp_std=c++23` or `icx`/`icpx` compiler errors, and documented Meson `>= 1.3.0`, C++23 compiler (GCC `13+`, Clang `17+`, Intel `icpx` `2024+`), Qt 6, and GDB `>= 10.0` prerequisites (including user-local `pip` installation on HPC clusters) in `README.md`.
- **Documentation, Security Policy & Contributor Guide (`README.md`, `docs/HPC-Remote-and-Slurm.md`, `SECURITY.md`, `CONTRIBUTING.md`)**:
  - Clarified GridLock's role as an open-source graphical frontend for GDB (`GDB/MI`), added a `Why GridLock?` overview section, and marked Slurm job-step attachment and batch launch integration as experimental and untested on multi-node production clusters.
  - Updated `SECURITY.md` for the `>= 0.9.0` support line (documenting the native C99 plugin trust boundary, SHA-256 workspace pinning, and `--verify-plugin` checks) and expanded `CONTRIBUTING.md` with Google C++ Style Guide conventions, sub-800-line file limits, Meson/CMake build and headless test commands, and GitHub Wiki links.

### Fixed
- **Recursive Fuzzy Search & Auto-Expand in `PROJECT / SYMBOLS -> FILES` (`ProjectExplorerWidget`)**:
  - Replaced shallow `QFileSystemModel::setNameFilters` filtering with recursive project indexing (skipping `.git`, `builddir`, `build`, `node_modules`, `.cache`, `__pycache__`), case-insensitive fuzzy substring and subsequence matching across filenames and project-relative paths (also applied consistently across `SYMBOLS` and `STRINGS`), automatic expansion of matching ancestor folders, and clean collapse back to the root tree when the filter bar is cleared.
- **Live Slurm / QEMU Remote Attach Reliability (`MainWindowCohesion`, `MainWindowDebugger`, `LocalSlurmBackend`, `GdbRankCoordinator`, `GdbMiParser`)**:
  - Synced `#toolbarTargetSelector` when loading an external ELF executable via `MainWindow::loadBinaryTarget()` so `activeTargetBinaryPath()` and `Submit Slurm Job...` target the loaded binary instead of the startup tutorial.
  - Updated `gridlock_gdbserver_wrap.sh` to prioritize MPI launcher rank variables (`OMPI_COMM_WORLD_RANK`, `PMIX_RANK`, `PMI_RANK`, `MV2_COMM_WORLD_RANK`) ahead of `SLURM_PROCID` and bind `0.0.0.0:${PORT}`, added whitespace-tolerant `squeue -o %N` fallback to `scontrol show job`, added bounded `-target-select` startup retries during `srun`/`mpirun` `gdbserver` bind windows, and sent RSP `monitor exit` on Slurm debug stop so `gdbserver --multi` ranks terminate cleanly alongside `scancel`.
- **Slurm `{FILE}` Target Binary Resolution, `#SBATCH` Directive Parser & `HPC Integration` Persistence (`MainWindowBuild`, `SlurmBatchParser`, `HpcSettingsPages`, `PreferencesDialog`, `ConfigManager`)**:
  - Fixed `MainWindow::onSubmitSlurmJob()` to compile and validate the active target ELF executable (or prompt for a compiled binary when none is loaded) instead of substituting `m_currentFile` (`.cpp` source path) into `{FILE}` / `%%FILE%%`.
  - Fixed `SlurmBatchParser` to distinguish `-n` / `--ntasks` from `--ntasks-per-node` without multiplying `nodes * ntasks`, parse quoted `#SBATCH` values containing spaces, preserve `--error` (`-e`), `--chdir` (`-D`), `--exclusive`, and `--mem-per-cpu`, emit `--mem` only when non-empty, and shell-quote `"{FILE}"` so paths containing spaces never split.
  - Expanded `SlurmSettings` in `ConfigManager` to persist all `HPC Integration` fields (`jobName`, `account`, `qos`, `wallTime`, `cpusPerTask`, `memory`, `gpuType`, `constraint`, `mpiLauncher`, `modules`, `outputLog`, `errorLog`, `mailType`, `mailUser`, `scriptPath`, `sbatchPath`, `squeuePath`, `scontrolPath`, plus `partition`, `nodes`, `tasksPerNode`, `requestGpus`, `gpusPerNode`, `spackRoot`, and `scriptTemplate`) in `QSettings` with bidirectional `Script Preview` synchronization.
- **Remote Debugging (`gdbserver` / QEMU) Initial Attach Stop (`GdbRankCoordinator`, `GdbMiParser`, `MainWindowBuild`)**:
  - Kept attached remote ranks paused at their initial stop frame on `-target-select remote` / `extended-remote` (including unknown-source loader frames such as `_start` in `ld-linux.so`) while applying existing user breakpoints instead of auto-resuming fast-exiting targets before inspection, and dismissed unmodified startup welcome/tutorial tabs on attach so active frames and disassembly are visible immediately.
- **Exited Inferior Diagnostics & Server Rack Retention (`GdbMiParser`, `GdbRankCoordinator`, `GdbRankCommands`, `MainWindowExecution`, `MainWindowPerspectives`)**:
  - Updated `=thread-group-exited` handling to immediately transition ranks to `Exited`, clear queued post-stop commands, and gate register, stack, and variable polling (`-data-list-register-values`, `-stack-list-frames`, `-stack-list-variables`) on `stopped` state so exited ranks no longer emit `Error: No registers.` or `Error: The program is not being run.` and remain visible as `Exited` in the Server Rack and status bar until explicitly stopped or restarted.

## [0.9.0] - 2026-11-25

### Added
- **Frozen C99 Plugin ABI (`v1.0`) & Header-Only C++23 Wrappers (`include/gridlock/plugin_api.h`, `include/gridlock/plugin_cpp_helpers.hpp`, `tests/core/test_plugin_abi.cpp`)**:
  - Added binary-stable C99 plugin ABI `v1.0` (`GRIDLOCK_PLUGIN_ABI_MAJOR = 1u`, `GRIDLOCK_PLUGIN_ABI_MINOR = 0u`) with 16-byte `(struct_size, abi_major, abi_minor, reserved_flags)` headers on all 9 standard-layout POD structs, explicit `GRIDLOCK_CALL` calling convention, forward-compatible `void* reserved[8]` expansion slots, 28 `GridLockHostApi` callbacks, and compile-time `sizeof` / `offsetof` `static_assert` layout locks.
  - Added optional header-only `noexcept` C++23 convenience wrappers (`gridlock::plugin`) providing `std::expected` expression evaluation, interactive input prompts, RAII host byte buffers, typed `readPod<T>` / `readBytes` reads, and theme queries with zero Qt dependencies.
- **Zero-Inferior-Call DWARF / MI Introspection (`PluginHostBridge`, `include/gridlock/plugin_api.h`)**:
  - Added `query_type_info` (`GridLockTypeInfo`) and `resolve_contiguous_range` (`GridLockContiguousRange`) host callbacks and C++23 helpers (`gp::queryTypeInfo`, `gp::resolveContiguousRange`) to resolve C arrays (`T[N]`), `std::array`, `std::vector`, `std::span`, and `std::valarray` across `libstdc++` and `libc++` without invoking inferior functions.
- **PluginManager & Host Bridge (`src/core/plugins/PluginManager.*`, `PluginHostBridge*.cpp`, `ConfigManager*`)**:
  - Added dual `UserGlobal` (`~/.config/gridlock/plugins/`) and `Workspace` (`<workspace>/.gridlock/plugins/`) shared-library discovery, pre-`dlopen` SHA-256 workspace trust gating (`[plugins.trusted_workspace_binaries]`), POSIX world-writable and symlink-escape checks, callback-depth unload deferral (`m_callbackDepth`), FIFO stop-event queueing (`m_pendingStops`), and local `process_vm_readv` target memory reads.
- **Decentralized Multi-Forge Installer & Manifests (`PluginInstaller`, `PluginManifest`, `PluginSettingsPage`)**:
  - Added shell-free `QProcess` plugin installation supporting `owner/repo` and `github:` / `gitlab:` (including nested subgroups) / `codeberg:` shorthand as well as custom `https://` and `git@` URLs across GitHub, GitLab, Codeberg, Gitea, and self-hosted Git servers.
  - Added declarative `plugins.toml` manifest syncing, reproducible user-global `~/.config/gridlock/plugins-lock.toml` commit and `SHA-256` binary pinning, per-row **Update** and toolbar **Update All** actions, and `Version (<commit>)` provenance display with full tooltips.
- **Live Hot-Reload, Conformance Verifier & Scaffolding Generator (`PluginManager`, `PluginVerifier`, `CliParser`, `scripts/init_plugin.lua`)**:
  - Added in-place **Hot-Reload Now** (`PluginManager::hotReloadPlugins`) with Linux `STB_GNU_UNIQUE` (`'u'`) symbol detection and active MPI session `on_session_start` replay.
  - Added sandboxed post-build conformance verification (`PluginVerifier::verifyBinary`) and the headless `gridlock --verify-plugin <path.so>` CLI flag validating dynamic symbol hygiene, ABI headers, action/regex registrations, dry-run lifecycle hooks, and zero leaked `alloc_bytes` heap blocks.
  - Added interactive and `--yes` CLI Lua plugin scaffolding generator (`scripts/init_plugin.lua`) emitting C++23 or C99 starter sources, `plugin.map` linker version scripts, and Meson, CMake, or Make build files.
- **Workstation UI Extension Surfaces (`MainWindowPlugins`, `PluginReportDockWidget`, `VariablesDockWidget`, `SourceCodeView`, `CallStackDockWidget`, `ServerRackView`)**:
  - Added hierarchical `Tools -> Plugins` submenus (up to 4 segments with shortcut collision checks), clickable status-bar badges, context-menu `Plugin Actions` across all 5 surfaces (`Variables`, `CodeEditor`, `CallStack`, `Breakpoints`, `RankList`), inline editor stop annotations, live `ThemeManager` token synchronization (`on_theme_changed`), and declarative `PluginReportDockWidget` tabs (`MARKDOWN`, `KEY_VALUE_TABLE`, `JSON_TREE`).
- **Plugin Documentation, Guides & Reference Example (`docs/`, `examples/plugins/domain_mesh_probe/`, `scripts/README.md`)**:
  - Added end-user plugin guide (`docs/Plugins.md`), three-volume C99 ABI developer encyclopedia (`docs/Plugin-Architecture-and-Lifecycle.md`, `docs/Plugin-C99-ABI-Reference.md`, `docs/Plugin-Authoring-and-UI-Surfaces.md`), `scripts/README.md`, semantic GitHub Alerts and palette swatches across `README.md` and `docs/`, and the reference out-of-tree plugin (`examples/plugins/domain_mesh_probe/`).

### Changed
- **Silent Plugin Expression Channel (`GdbRankCoordinator`, `GdbMiParser`, `PluginHostBridge`)**:
  - Routed plugin `eval_expr` queries through a dedicated silent MI token channel (`m_pendingPluginEvals` / `pluginExpressionReady`) so internal plugin probes never pollute the user's **Evaluator** tab history.
- **Preferences & Installed Plugin Table Layout (`PreferencesDialog`, `PluginSettingsPage`)**:
  - Widened `PreferencesDialog` and refined `PluginSettingsPage` so all 8 **Installed** table columns, `Version (<commit>)` cells, and `Update` / `Update All` / `Install` buttons fit cleanly without horizontal scrolling or label clipping.
- **Release Version & Post-`v0.9.0` Roadmap Alignment (`meson.build`, `README.md`, `docs/ROADMAP.md`, `scripts/sync_wiki.sh`)**:
  - Bumped GridLock's application version to `0.9.0`, aligned post-`v0.9.0` roadmap milestones (`v0.10.0`, `v0.11.0`, and pre-`v1.0.0` target framing) in `README.md` and `docs/ROADMAP.md`, and updated `scripts/sync_wiki.sh` to mirror all 14 documentation pages including the plugin guides to the GitHub Wiki.

### Fixed
- **Code-Editor Symbol Extraction & Variable Type Resolution (`SourceCodeViewInteraction`, `MainWindowPlugins`)**:
  - Excluded trailing method-call segments (`(?!\s*\()`) from code-editor hover and context-menu symbol extraction so expressions like `domain.swap(next)` resolve to `domain` rather than a member function pointer, and populated `type_utf8` from `VariableTreeModel` when available.
- **Mermaid Diagram Syntax & Wiki Sync Reliability (`docs/Plugin-Architecture-and-Lifecycle.md`, `.github/workflows/sync-wiki.yml`)**:
  - Fixed `stateDiagram-v2` Mermaid transition label syntax (replacing unescaped `->` and `::` tokens inside transition descriptions) and hardened `.github/workflows/sync-wiki.yml` with workflow concurrency and pull-rebase retries before pushing wiki updates.

## [0.8.81] - 2026-11-23

### Fixed
- **Floating Desktop Window Decorations (`MainWindow`, `main.cpp`)**:
  - Restored native OS and window-manager decorations by default (stopping unconditional `QT_WAYLAND_DISABLE_WINDOWDECORATION=1` export on Wayland and keeping `Qt::FramelessWindowHint` unset on `MainWindow`) so floating desktop environments (GNOME, KDE Plasma, X11, and Windows) render standard titlebars, drag-to-move handles, edge resize borders, and window buttons by default while keeping tiling Wayland compositors such as Niri unaffected.

### Added
- **Menu-Bar Window Controls Option (`PreferencesDialog`, `ConfigManager`, `MainWindow`)**:
  - Added **Show Window Controls in Menu Bar** in `Preferences -> Appearance` (`ConfigManager::showMenuBarWindowControls`, default `true`, persisted under `[ui] show_menubar_window_controls` in `~/.config/gridlock/config.toml`), mounting compact `Minimize`, `Maximize/Restore`, and `Close` corner buttons alongside empty-space `startSystemMove()` drag and double-click maximize support on `menuBar()`.

## [0.8.8] - 2026-11-22

### Added
- **Customizable Top Toolbar & Hierarchical Preferences (`PreferencesDialog`, `ConfigManager`, `MainWindowToolbar`)**:
  - Added a hierarchical `QTreeWidget` category selector in `PreferencesDialog` (`Appearance -> Toolbar` expanded by default) and moved UI chrome sizing options under `Appearance`.
  - Added `Preferences -> Appearance -> Toolbar` (`Main Toolbar Layout & Spacing`) to reorder toolbar controls, hide or restore optional items, insert fixed `24px` gaps (`spacer`), insert right-aligning springs (`flex_spacer`, ignored when trailing), insert vertical dividers (`separator`), and reset to the default `< 1,040 px` layout.
  - Added `Config` (inline `Launch Options` popover for `--args`, working directory, and `Stop at main()`) and `Cont` (`Continue`) text labels, split build/step dropdown compartments, source-level stepping (`Step Over F10`, `Step Into F11`, `Step Out Shift+F11`), and a vector Search Everywhere trigger icon.
- **Multi-Scope Search Everywhere & Popup Scale Presets (`CommandPaletteDialog`, `SearchEverywhereEngine`, `ConfigManager`)**:
  - Upgraded global search to Search Everywhere (`Double Shift` for `All`, `Ctrl+P` for `Files`, `Ctrl+Shift+P` for `Actions`) with tabbed scopes (`All | Files | Symbols | Text | Actions | Ranks` via click or `Tab` / `Shift+Tab`), CamelCase and acronym fuzzy ranking, empty-query Recent Files & Actions, an `ESC` close button, and `Enter` / `Ctrl+Enter` split-editor opening.
  - Separated the top tab and control bar cleanly above the search input, rendered 2-line result rows with workspace-relative sub-labels, expanded the preview pane to 35 numbered context lines (capped at `250` lines / `32 KB` for mega-files), and added persisted `Small` (`760x440`), `Medium` (`840x500`), `Large` (`940x560`, default), and `X-Large` (`1080x640`) size presets with a live `S/M/L/XL` header toggle.
- **Topological Pointer & Struct Graph Enhancements (`PointerGraphDialog`)**:
  - Added layered Sugiyama DAG auto-layout preserving manual drag positions across stops, incremental multi-root pinning and unpinning, mouse-wheel zoom (`0.25x` to `2.5x`), an interactive corner minimap, per-node field suppression (`+N hidden`), horizontal/vertical card orientation toggles, `/x` `/d` `/b` scalar format overrides, dynamic alias splitting/merging, and `2x` PNG / vector SVG export.
  - Added `Shift+Click` node width expansion (up to `900px`), un-elided full-value hover tooltips, working double-click pointer dereference expansion, and `SourceCodeView` right-click graph handoff.
- **Multi-Rank & Scientific Array Plotter Extensions (`ArrayPlotDialog`)**:
  - Added multi-rank 1D overlay curves for halo/ghost-cell comparison, `start:end:stride` slicing, `Log10` Y and colorbar scaling, interactive drag-box zoom, scalar time-series history across stops, `Complex64` / `Complex128` decoding (`Magnitude`, `Phase`, `Real`, `Imaginary`), validated 2D `splot` / `pm3d` `gnuplot` export with stderr reporting, and virtualized 2D/3D tensor tables (`[Nx, Ny, Nz]` C row-major vs. Fortran column-major).
- **Progressive Disclosure, Unified Multi-Rank Scope & Live Editing (`MainWindowPerspectives`, `MainWindowExecution`, `VariablesDockWidget`, `RegisterView`, `ServerRackView`)**:
  - Added non-destructive `SOURCE | DEBUG | BINARY | MPI` perspectives without force-redocking floating panels, automatic collapsing of multi-rank chrome when `ranks == 1`, an explicit `All Ranks` vs. `Rank k` execution scope toggle, `Pause All Ranks` (`F6`), a master `setFocusedRank()` sync hub across all docks and dialogs, `All Ranks` expression evaluation broadcast, and `ServerRackView` `DEADLOCK` / `EXITED` badges with state filtering.
  - Added live in-place variable and register editing (`-gdb-set`), a copyable `Inspect Full Value...` popover, live expression autocomplete, a `CodeMinimap` visibility toggle in `Preferences -> Editing` with a rebindable shortcut in `Keybindings`, proportional UI typography and multi-language vector icons in `ProjectExplorerWidget`, and CLI positional target plus `-- <program_args...>` parsing.
- **Developer E2E Suite, Architectural Guides & Screenshots (`scripts/e2e_cli_suite.mjs`, `docs/`, `src/**/README.md`, `tests/**/README.md`)**:
  - Added a zero-dependency Node.js CLI and tutorial end-to-end test runner (`scripts/e2e_cli_suite.mjs`, manual developer-only), per-subdirectory `README.md` guides across `src/` and `tests/`, Graphify AST knowledge graph integration, and refreshed `v0.8.8` workstation, Search Everywhere, Pointer Graph, and Toolbar Preferences screenshots.

### Performance
- **Fast Binary Launch & Virtualized Large-Data Editor (`MainWindowExecution`, `ElfBinaryInspector`, `SourceCodeView`)**:
  - Eliminated synchronous startup stalls when launching target binaries and added a virtualized large-CSV viewer that bypasses heavy syntax highlighting and word-wrapping on multi-megabyte data files.

### Fixed
- **Per-Target Arguments, Terminal PTY Cleanup & Editor Indentation (`ConfigManager`, `TerminalDockWidget`, `SourceCodeView`, `MainWindow`)**:
  - Unified configuration storage under `~/.config/gridlock/` with persistent `Recent Sessions`, scoped `programArgs` per target and per workspace instead of leaking globally across binaries, and isolated unit test binaries into temporary config directories.
  - Fixed `Clear Terminal` in `TerminalDockWidget` to wipe scrollback and print a clean prompt without triggering Zsh `Ctrl+L` variable-expansion messages, fixed oversized right-click terminal context menus (`Copy`, `Paste`, `Select All`, `Clear Terminal`), added `Ctrl+Shift+C`/`V` and smart `Ctrl+C`/`V`, and cleanly reaped embedded PTYs on exit.
  - Enforced exact 4-column soft `Tab` / `Shift+Tab` (`Backtab`) indentation and monospace pixel `setTabStopDistance()` in `SourceCodeView`, bound `Ctrl+F` to the inline editor find bar, and switched split editors to a real `QSplitter`.
  - Ensured extensionless targets compile with `+x` permissions and pre-launch executability checks, blocked stale-binary launches on build errors, validated hex/NOP instruction patches with `Revert All Patches`, replaced persistent `QKeySequenceEdit` cells in `Keybindings` with an opaque `KeySequenceRecorderWidget` (`ShortcutItemDelegate`) with light-theme contrast, removed the accidental checkable checkbox on `Debug -> Run Target`, fixed `DisassemblyView` double context menus and `MemView` cursor offsets, and cleanly reaped active debug sessions and orphaned `/dev/shm/psm3_shm` segments on exit.

### Changed
- **Sub-500-Line Modular Refactoring (`src/`, `tests/`)**:
  - Reorganized `src/core/hpc/` (`gdb/`, `dap/`, `lsp/`, `memory/`, `analysis/`, `system/`), `src/ui/dialogs/` (`plotter/`, `graph/`, `calculator/`, `debugger/`, `project/`, `search/`, `preferences/`), and `tests/` (`ui/`, `dap/`, `hpc/`, `core/`), splitting large UI and core manager translation units (`MainWindow*`, `ConfigManager*`, `SourceCodeView*`, `MemView*`, `MemoryProfilerWidget*`, `Disassembly*`, `ProjectExplorer*`, `HpcConsole*`, `DocsetManager*`, `DapCoordinator*`, `ElfBinaryInspector*`) under 500 lines.

## [0.8.7] - 2026-11-20

### Added
- **Execution Power Tools & Non-Linear Control** (`MainWindowDebugger`, `SourceCodeView`, `DisassemblyView`, `GdbRankCoordinator`, `DapCoordinator`):
  - Instruction step-over (`nexti` / `Ctrl+Shift+F10` / `-exec-next-instruction`).
  - Run to Cursor (`Ctrl+F10` / `-exec-until`) from both source lines and disassembly addresses.
  - Draggable gutter PC arrow and `Set Next Statement / Jump...` (`-exec-jump`) guarded by lexical scope hazard checks.
  - `Force Early Return...` (`-exec-return`) from the `Debug` menu and `CALL STACK` context menu.
- **Non-Destructive Breakpoint Lifecycle, Temporary/Regex Breakpoints & Command Lists** (`ConditionalBreakpointDialog`, `SourceCodeView`, `GdbRankCoordinator`):
  - Non-destructive enable/disable toggling (`-break-enable` / `-break-disable`) rendered as hollow gutter rings while preserving condition, ignore count, rank filter, and hit commands.
  - Temporary one-shot breakpoints (`tbreak` / `-break-insert -t`) with dashed cyan `T` markers that auto-clear on hit.
  - Function-symbol regex breakpoints (`Debug -> Break on Function Regex (rbreak)...`).
  - Multi-line GDB hit command scripts (`-break-commands`).
- **Signal Disposition Matrix & Catchpoints** (`SignalCatchpointDialog`, `GdbRankCoordinator`, `tutorial/catchpoint_exceptions.cpp`):
  - `Debug -> Signals & Catchpoints...` (`Ctrl+Alt+S`) with a filterable POSIX/HPC signal `Stop` / `Print` / `Pass` disposition matrix (`handle <SIG>`) and `HPC Quiet Defaults` / `Trap Faults` presets.
  - C++ `throw`, `rethrow`, and `catch` regex catchpoints, Linux syscall catchpoints (`mmap`, `openat`, `clone3`, `ioctl`), and `dlopen` / `dlclose` catchpoints.
  - New 4-rank C++ MPI tutorial showcase (`tutorial/catchpoint_exceptions.cpp`).
- **Copy-on-Write Fork Checkpoints** (`CheckpointDialog`, `GdbRankCoordinator`):
  - `Debug -> Process Checkpoints...` (`Ctrl+Alt+K`) supporting GDB `checkpoint`, `info checkpoints`, `restart <id>`, and `delete checkpoint <id>`.
  - Pre-fork `/proc/[pid]/fd` and `/proc/[pid]/maps` safety scanner (`Safe`, `Warning`, and `BlockedOrUnsafe`) with expandable diagnostics and explicit confirmation prompts for active IPC sockets, pipes, shared memory, RDMA verbs, and GPU device mappings.
- **Productivity, Visualization & Console Ergonomics** (`CommandPaletteDialog`, `ProgrammerCalculatorDialog`, `ArrayPlotDialog`, `PointerGraphDialog`, `GdbConsoleWidget`):
  - Unified fuzzy Command Palette (`Ctrl+Shift+P` / `Ctrl+P` with `>`, `@`, and `#` prefixes).
  - Floating Programmer's Calculator (`Ctrl+Alt+C`) with synchronized `HEX` / `DEC` / `OCT` / `BIN`, IEEE-754 `float` / `double`, `8/16/32/64-bit` width, `BSWAP`, interactive 64-bit bit-toggle grid, radix-aware keypad, and right-click handoff across views.
  - Live auto-refreshing 1D/2D Array & Field Plotter (`Ctrl+Alt+G`) with `Viridis` / `Plasma` / `Cool-Warm` / `Grayscale` colormaps, `NaN` / `Inf` flags, previous-stop ghost curve overlay, and CSV / PNG / `gnuplot` export.
  - Persistent auto-updating Pointer Graph roots with `"old -> new"` field-level mutation diffs.
  - GDB console readline ergonomics (`~/.config/gridlock/gdb_history`, `Up` / `Down` draft preservation, `Ctrl+R` incremental reverse search, and `Tab` `complete` popup).
- **Documentation, Screenshots & Quick Reference** (`docs/Keyboard-Shortcuts-and-Reference.md`, `docs/screenshots/v087_*.png`, `scripts/sync_wiki.sh`):
  - Dedicated Keyboard Shortcuts and Quick Reference guide, Tutorial 8 (`catchpoint_exceptions.cpp`), five `v0.8.7` tool screenshots, and updated Wiki sync metadata.

### Performance
- **Startup Latency Optimization** (`ThemeManager`, `ClangdLspClient`, `MainWindowWorkspace`, `MainWindowDocks`, `HpcConsoleWidget`, `TerminalDockWidget`, `ReferenceManualWidget`):
  - On-disk QSS stylesheet caching under `AppConfigLocation/acss` and single-pass theme initialization.
  - Asynchronous package manager probing and `clangd` launch moved off the synchronous startup path.
  - Deferred initialization of inactive `Terminal`, `Package Manager`, and `Reference Manual` bottom tabs until first activation.

### Fixed
- **Dialog Visual Polish & Layout** (`ArrayPlotDialog`, `ProgrammerCalculatorDialog`, `CheckpointDialog`):
  - Reset `QPainter` brush to `Qt::NoBrush` before drawing 1D plot curves so `NaN` anomaly markers no longer flood-fill the plot interior, and widened `ArrayPlotDialog` toolbar controls to prevent checkbox clipping.
  - Escaped literal `&` mnemonics in `ProgrammerCalculatorDialog` (`Bitwise && Arithmetic Expression Evaluator`) and `CheckpointDialog` (`Rescan && Refresh`).
  - Disabled table word-wrapping and adjusted column widths in `CheckpointDialog` so file locations, timestamps, notes, and `Restore` / `Delete` buttons render without truncation.

## [0.8.66] - 2026-11-19

### Fixed
- **AppImage Qt6 Platform Plugins (`wayland` & `xcb`):** Bundled `libqwayland*.so` (`wayland-decoration-client`, `wayland-graphics-integration-client`, `wayland-shell-integration`), `xcbglintegrations`, `qt.conf`, and `libxcb-cursor.so.0` (with fixed-point recursive `ldd` plugin dependency collection and a build-time verification gate) so `GridLock-0.8.66-x86_64.AppImage` launches cleanly on both native Wayland compositors and X11/XWayland systems without requiring `libxcb-cursor0` on the host.

### Added
- **Documentation:** Added sanitized screenshots and usage documentation in `README.md` and `docs/USER_GUIDE.md` for **Attach to Running Process (`/proc` MPI job grouping)** and **Remote GDB Target (`target remote` / `extended-remote` for `qemu-user -g` and `gdbserver`)**.

## [0.8.65] - 2026-11-18

### Added
- **Attach to Running Process (`Ctrl+Alt+P`, `ProcessScanner`, `AttachProcessDialog`):** Added a Linux `/proc` process scanner with automatic OpenMPI, MPICH/PMI, PMIx, and SLURM MPI world-rank grouping (`MPI Job (N ranks)`), `kernel.yama.ptrace_scope` permission diagnostics with a 1-click `sysctl` clipboard fix, and single-PID or full-MPI-job attach via GDB/MI3 (`-target-attach`) and DAP (`attach`).
- **Remote Target (`target remote` / `extended-remote`) Support (`ProcessScanner`, `GdbRankCoordinator`, `AttachProcessDialog`):** Added a `Remote Target (gdbserver / QEMU)` tab supporting direct connections to `qemu-user -g <port>` stubs, `gdbserver` instances inside VMs, and multi-rank `host:port..port` ranges or comma-separated `host:port` endpoints with local ELF DWARF symbol loading (`-file-exec-and-symbols`) and `gdb-multiarch` selection.
- **GitHub Community Standards & Automated Wiki Sync (`ACCESSIBILITY.md`, `.github/`, `scripts/sync_wiki.sh`):** Added `ACCESSIBILITY.md`, structured YAML issue forms (`bug_report.yml`, `feature_request.yml`, `accessibility.yml`, `config.yml`), a standardized pull request template, and an automated GitHub Wiki sync workflow with generated `_Sidebar.md` and `_Footer.md`.

### Changed & Fixed
- **Documentation Screenshots (`MainWindowCohesion`, `scripts/capture_ui.sh`, `docs/screenshots/`):** Refreshed all 19 workstation, dialog, and theme screenshots to reflect current UI state with real MPI tutorial source files (`tutorial/deadlock_ring.cpp`) and line-anchored breakpoint popovers, while keeping normal `meson test` runs side-effect-free.
- **Repository & Test Suite Cleanup (`tests/`, `.gitignore`):** Removed obsolete top-level directories (`docker/`, `assets/`, `scratch/`, `.jules/`, `packaging/`), untracked runtime `.gridlock/` state in `.gitignore`, consolidated redundant and meta-linting test targets (`26 -> 22` Meson suites), and stripped leftover `if constexpr (requires ...)` test scaffolding.

## [0.8.6] - 2026-11-15

### Added
- **Visual Library & Heap Memory Profiler (`MemoryProfilerWidget`, `MemView`):** Added a `LIBRARY MEMORY MAP` view inside `MemView` combining `/proc/<pid>/smaps` virtual memory region attribution with a lightweight `LD_PRELOAD` caller-attribution tracker (`malloc`/`calloc`/`realloc`/`free` unwinding past `libstdc++` `operator new` with `addr2line` symbol resolution), an interactive 2D treemap and proportional stacked bar, a sortable and filterable breakdown table, a `⤢ POP OUT` / `⤡ DOCK` standalone window toggle, and five configurable color schemes (`Distinct`, `Viridis`, `Plasma`, `Ocean`, `HighContrast`) in `Preferences -> Appearance` and the profiler toolbar.
- **Inline Variable Ghost Values (`SourceCodeView`, `EditorTabManager`, `ConfigManager`):** Added muted italic inline variable values at the end of visible source lines when paused at a breakpoint or stack frame, resolving both local scalar variables and dotted/arrow member chains (such as `domain_cfg.solver.rel_tolerance`) while suppressing opaque `{...}` placeholders, with a toggle in `Preferences -> Editing`.
- **Non-Suspending Logpoints & Breakpoint Hit Ignore Counts (`ConditionalBreakpointDialog`, `GdbRankCoordinator`, `DapCoordinator`):** Added breakpoint log messages (`{expr}` expression evaluation streamed directly to `PROGRAM OUTPUT` without pausing ranks or stalling MPI barriers, rendered with an amber/cyan diamond gutter marker) and hit ignore counts (`-break-insert -i N` / `-break-after` and DAP `hitCondition`).
- **Interactive Pointer & Struct Graph Visualizer (`PointerGraphDialog`, `VariablesDockWidget`):** Added an interactive 2D pointer and struct graph canvas (`Variables` context menu and `Tools -> Pointer & Struct Graph Visualizer...`) with draggable struct cards, automatic 1-hop composite and pointer expansion, directed cubic Bezier edges, layered DAG auto-layout, null-pointer (`0x0`) guards, and cycle/shared-alias detection.
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
- **Unified Preferences UI:** Added a multi-page Kate/KDevelop-style sidebar `PreferencesDialog` (Edit -> Preferences) to manage Appearance, Editing, Behavior, Debugger tools, HPC/Cluster settings, SLURM integration, and Docsets.
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

[Unreleased]: https://github.com/bnorthern42/GridLock/compare/v0.9.1...HEAD
[0.9.1]: https://github.com/bnorthern42/GridLock/compare/v0.9.0...v0.9.1
[0.9.0]: https://github.com/bnorthern42/GridLock/releases/tag/v0.9.0

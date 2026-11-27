# GridLock Stability & Dependency Audit

**Scope:** Strictly read-only technical audit of `src/`, `include/`, `meson.build`, `meson_options.txt`, and `tests/` on `master` (GridLock `v0.9.1`).  
**Goal:** Catalog fragile external boundaries, build/packaging hazards on HPC clusters, file complexity hotspots, and test coverage gaps into a prioritized, actionable hardening checklist.

---

## 1. Executive Risk Summary

| Priority | Subsystem | Core Finding | Impact | Primary Locations |
| :---: | :--- | :--- | :--- | :--- |
| **High** | **GDB/MI Parsing** | Non-balanced regex `\{([^{}]+)\}` for `201^done,variables=[...]` and `[^}]*` inside `*stopped` `frame={...}` | Silently drops variables containing nested `{...}` (GDB 14+ `libstdc++` pretty-printers, C++ structs, Fortran derived types) and fails to extract `func`/`addr` when `args=[{...}]` precedes them | `src/core/hpc/gdb/GdbMiParser.cpp:225`, `src/core/hpc/gdb/GdbMiParser.cpp:477` |
| **High** | **MPI Launch & Env** | Unconditional `/usr/lib64/openmpi/lib` prepend to `LD_LIBRARY_PATH`, OpenMPI-specific `--oversubscribe` / `--hostfile` / `--map-by node`, and node-local `/tmp/gridlock_mi_*` FIFOs | Shadows MPICH/Intel MPI/Spack `libmpi.so` on HPC clusters, breaks non-OpenMPI `mpiexec` flags, and prevents multi-node `mpiexec` FIFO launches | `src/core/hpc/gdb/GdbRankCoordinator.cpp:441`, `src/core/hpc/gdb/GdbRankCoordinator.cpp:521-544`, `src/ui/dialogs/project/ProjectWizard.cpp:175-186` |
| **High** | **Build / Qt6 Deps** | `meson.build` unconditionally requires unused Qt6 `Qml` and `OpenGLWidgets`, plus `Test` for the main binary, and uses an out-of-sandbox `include_directories` path for `Qt-Advanced-Stylesheets` | Breaks builds on minimal headless HPC cluster Qt6 installs and will become a hard error in future Meson releases | `meson.build:11`, `meson.build:32`, `meson.build:933-945` |
| **High** | **LSP & DAP Framing** | Duplicate LSP clients (`LspCoordinator` vs. `ClangdLspClient`), unbounded `m_buffer` / `Content-Length` reads, deadlock on `Content-Length: 0` in `LspCoordinator`, hardcoded `--compile-commands-dir=build`, and release `qDebug()` DAP sniffer logs | Memory exhaustion or permanent framing stall on corrupted server output, broken hover on Meson `builddir/`, and noisy/sensitive DAP payload logging in release builds | `src/core/hpc/lsp/LspCoordinator.cpp:45,144-153`, `src/core/utils/ClangdLspClient.cpp:419-443`, `src/core/hpc/dap/DapCoordinator.cpp:126,205-258` |
| **Medium** | **Slurm & Teardown** | `LocalSlurmBackend` destructor does not `scancel` active jobs on abnormal exit, staged `.gridlock/slurm_staging/` files are never pruned, `DapCoordinator` calls blocking `QProcess::execute("scancel")` on the GUI thread, and SSH `-L` assumes contiguous free local ports | Orphaned Slurm allocations and remote `gdbserver` daemons on crash/close, GUI freezes on slow Slurm controllers, and SSH forward failures on shared login nodes | `src/core/hpc/system/LocalSlurmBackend.cpp:77-88,244-250,648-660`, `src/core/hpc/dap/DapCoordinator.cpp:171` |
| **Medium** | **GDB/MI Boundary Leakage** | Direct MI command writes and regex parsing outside `src/core/hpc/gdb/` (`DeadlockAnalyzer`, `PluginHostBridge`, `DebugCommands`), plus unescaped quote regex on hover (`100`) and auto-continue on missing `fullname` | Duplicate MI parsing paths drift over GDB versions; hover truncates quoted strings; stripped or Fortran/Odin/Zig runtime stops can be silently skipped | `src/core/hpc/analysis/DeadlockAnalyzer.cpp:152`, `src/core/plugins/PluginHostBridge.cpp:633-649`, `src/core/hpc/gdb/GdbMiParser.cpp:279,453` |
| **Medium** | **Plugin C ABI Boundary** | `PluginManager` invokes plugin `init`/`on_session_end`/`shutdown` without `try/catch` boundary guards, omits `struct_size` upper-bound and `reserved_flags == 0` checks, and does not auto-reclaim leaked `alloc_bytes` blocks on failed init | A throwing C++ plugin crashes GridLock via `std::terminate()`; malformed descriptors with nonzero `reserved_flags` pass validation; failed-init allocations leak | `src/core/plugins/PluginManager.cpp:477-501,531-544` |
| **Medium** | **Complexity Ceiling** | 11 C++ source/header/test files already exceed the `800`-line `agent.md` ceiling (up to 921 lines), and 23 additional files sit between `650` and `799` lines | Higher regression risk, harder reviewability, and frequent ceiling friction during feature work | See Section 2 Watchlist |
| **Low** | **Docs vs. `--nx` Mismatch** | `GdbRankCoordinator` always launches GDB with `--nx`, while `README.md:17` states GridLock preserves user `.gdbinit` setups and pretty-printers | Confuses users expecting custom `.gdbinit` pretty-printers (Rust, Zig, Eigen, PETSc) to load automatically | `src/core/hpc/gdb/GdbRankCoordinator.cpp:232,261,514,541`, `README.md:17` |

---

## 2. File Size & Complexity Watchlist (`>= 650` Lines)

Baseline inventory across `src/`, `include/`, and `tests/` (`agent.md` ceiling: strictly `< 800` lines).

### Files Exceeding the `800`-Line Ceiling (11 Files)

| Lines | File | Recommended Decomposition |
| :---: | :--- | :--- |
| **921** | `src/ui/dialogs/graph/PointerGraphLayout.cpp` | Split Sugiyama rank/crossing-reduction pass from edge routing & SVG/QPainter geometry helpers |
| **907** | `src/core/utils/ClangdLspClient.cpp` | Extract JSON-RPC transport framing into a shared `LspTransport` and separate heuristic C++ scope completion |
| **880** | `src/ui/views/SourceCodeView.hpp` | Move inline helper structs/slots into `SourceCodeView.cpp` or a dedicated `SourceCodeGutterTypes.hpp` |
| **865** | `tests/ui/test_ui_aux.cpp` | Split by domain (build/terminal vs. editor/theme tests) |
| **857** | `tests/ui/test_ui.cpp` | Split static ELF/disassembly tests into a dedicated UI test translation unit |
| **854** | `src/core/utils/ClangdLspNavigation.cpp` | Split symbol/definition navigation from rename/reference workspace-edit application |
| **836** | `src/ui/views/DisassemblyAnalysis.cpp` | Separate CFG basic-block graph builder from instruction annotation/xref extraction |
| **833** | `tests/hpc/test_coordinator.cpp` | Move ROCm/GPU MI parsing tests into `tests/hpc/test_rocgdb.cpp` |
| **813** | `src/core/models/VariableTreeModel.cpp` | Extract value formatting / type-badge helpers into a small `VariableFormatters.hpp` |
| **811** | `src/core/managers/ThemeManager.cpp` | Split built-in palette definitions (`ThemePalettes.cpp`) from QSS template rendering/caching |
| **807** | `src/ui/dialogs/project/ProjectSettingsDialog.cpp` | Extract per-tab form builders into dedicated helper functions/files |

### Files Approaching the Ceiling (`650` to `799` Lines: 23 Files)

| Lines | File | Category |
| :---: | :--- | :--- |
| 798 | `src/ui/dialogs/plotter/ArrayPlotCanvas.cpp` | Caution (2 lines below ceiling) |
| 798 | `tests/ui/test_ui_viz_and_ux.cpp` | Caution (2 lines below ceiling) |
| 796 | `src/ui/dialogs/debugger/ConditionalBreakpointDialog.cpp` | Caution |
| 795 | `tests/core/test_plugin_manager.cpp` | Caution |
| 792 | `tests/ui/test_ui_debugger.cpp` | Caution |
| 790 | `tests/core/test_plugin_abi.cpp` | Caution |
| 790 | `src/ui/dialogs/preferences/PluginSettingsPage.cpp` | Caution |
| 788 | `src/core/plugins/PluginHostBridge.cpp` | Caution |
| 787 | `src/ui/MainWindowPlugins.cpp` | Caution |
| 783 | `src/ui/dialogs/debugger/AttachProcessDialog.cpp` | Caution |
| 781 | `src/core/plugins/PluginInstaller.cpp` | Caution |
| 751 | `src/core/hpc/system/LocalSlurmBackend.cpp` | Watch |
| 747 | `src/ui/dialogs/graph/PointerGraphDialog.cpp` | Watch |
| 729 | `src/ui/GdbConsoleWidget.cpp` | Watch |
| 720 | `src/ui/MainWindowToolbar.cpp` | Watch |
| 709 | `tests/ui/test_ui_cohesion.cpp` | Watch |
| 704 | `src/core/plugins/PluginManager.cpp` | Watch |
| 696 | `include/gridlock/plugin_api.h` | Frozen ABI Header (do not split) |
| 676 | `src/ui/MainWindowBuild.cpp` | Watch |
| 672 | `src/ui/widgets/ReferenceManualWidget.cpp` | Watch |
| 654 | `src/core/plugins/PluginManifest.cpp` | Watch |
| 652 | `tests/ui/test_ui_plugins.cpp` | Watch |
| 652 | `src/ui/MainWindowDocks.cpp` | Watch |

---

## 3. Subsystem Hardening Checklists

### 3.1 GDB/MI Protocol & Pretty-Printer Fragility

- [x] **Replace flat regexes with a real balanced-brace & quote-aware GDB/MI3 tuple scanner (`GdbMiParser.hpp` / `GdbMiParser.cpp`)**:
  - Replaced `varTupleRe("\\{([^{}]+)\\}")` (`201^done,variables=[...]` / `locals=[...]`), `frame=\{[^}]*...` (`*stopped`), `changelist` `\{([^}]+)\}`, watchpoint `[^}]*`, and hover `100^done` unescaped-quote regexes with quote- and escape-aware linear-time balanced-delimiter scanning in `src/core/hpc/gdb/GdbMiParser.hpp` (`gridlock::mi::*`).
- [ ] **Eliminate out-of-parser MI traffic**:
  - Route `src/core/hpc/analysis/DeadlockAnalyzer.cpp:152` (`-stack-list-frames 0 3`) through `GdbRankCoordinator::requestStackAndThreads` / `stackTraceUpdated` exclusively so untokenized `-stack-list-frames` replies cannot collide with other stack queries.
  - Route `src/core/plugins/PluginHostBridge.cpp:633-649` (`-var-create` / `-var-delete` type lookup) through a coordinator helper method instead of directly regex-scraping `gdbOutputReceived`.
  - Move `src/core/commands/DebugCommands.cpp:43` (`-interpreter-exec console "catch signal SIGFPE"`) onto `IBackendCoordinator::setSignalDisposition(...)`.
- [ ] **Harden frame-stop filtering for non-C++ / DWARF5 targets**:
  - `src/core/hpc/gdb/GdbMiParser.cpp:279-280` auto-continues when `currentFile` is empty or `"??"` unless the stop reason matches a narrow allowlist. Ensure breakpoint hits in stripped frames, OpenMP outlined frames (`_omp_fn.0`), and Fortran/Odin/Zig runtime frames pause cleanly in `DisassemblyView` instead of silently resuming.
- [ ] **Clarify and make `.gdbinit` / pretty-printer loading opt-in**:
  - `GdbRankCoordinator.cpp:232,261,514,541` pass `--nx`, which protects MI parsing from arbitrary user `.gdbinit` scripts, while `README.md:17` advertises `.gdbinit` preservation. Add an explicit **Preferences -> Debugger** toggle (**Load user `.gdbinit` / Python pretty-printers**, default off) and align `README.md` with the actual flag behavior.

### 3.2 MPI Launchers, Slurm, & Cluster Environment

- [x] **Remove unconditional OpenMPI `LD_LIBRARY_PATH` injection**:
  - Replaced the unconditional `/usr/lib64/openmpi/lib:/usr/lib/openmpi/lib:` prepend in `src/core/hpc/gdb/GdbRankCoordinator.cpp` with `GdbRankCoordinator::resolveMpiLdLibraryPath(...)`: user MPI stacks (`MPI_ROOT`, `I_MPI_ROOT`, `MPICH_DIR`, `OPENMPI_DIR`, `CRAY_MPICH_DIR`, existing `LD_LIBRARY_PATH` entries containing `mpi`/`spack`, or non-system `mpirun`/`mpiexec` paths) are preserved untouched, and existing system OpenMPI directories are appended (never prepended) only when system OpenMPI is active.
- [ ] **Make MPI launcher flags implementation-aware (`OpenMPI` vs. `MPICH` / `Intel MPI` / `srun`)**:
  - `src/core/hpc/gdb/GdbRankCoordinator.cpp:523-525` and `src/ui/dialogs/project/ProjectWizard.cpp:136-186` hardcode OpenMPI flags (`--oversubscribe`, `--hostfile`, `--map-by node`, `--tag-output`). Detect launcher family (`mpirun --version`) or expose a launcher preset (`OpenMPI`, `MPICH / Intel MPI`, `Custom`) so `-f` / `-ppn` / `-l` are emitted for Hydra/Intel MPI and users have a raw-flags escape hatch.
- [ ] **Relocate or gate MPMD FIFO temp directories and add crash cleanup**:
  - `src/core/hpc/gdb/GdbRankCoordinator.cpp:521` places rank FIFOs in node-local `/tmp/gridlock_mi_<pid>_<seq>`, which fails if `mpiexec` spawns ranks on remote hosts via `--hostfile`. On startup, sweep stale `/tmp/gridlock_mi_*` directories whose owning PID is no longer alive; for multi-node launches, steer users to `gdbserver --multi` / `LocalSlurmBackend` rather than local FIFOs.
  - Document and optionally expose a clean-environment helper for HPC fabric IPC remnants (e.g., `/dev/shm/psm3_shm.*`, `FI_PROVIDER=tcp` fallback) when MPI jobs abort abnormally.
- [x] **Make `DapCoordinator` and `LocalSlurmBackend` destructor Slurm cancellation non-blocking**:
  - Replaced blocking `QProcess::execute("scancel", ...)` in `src/core/hpc/dap/DapCoordinator.cpp` with `DapCoordinator::cancelSlurmJobDetached(...)` (`QProcess::startDetached`), and updated `LocalSlurmBackend::~LocalSlurmBackend()` in `src/core/hpc/system/LocalSlurmBackend.cpp` to dispatch detached `scancel` when `m_activeJobId > 0` on destructor teardown.
- [ ] **Remaining `LocalSlurmBackend` staging cleanup & SSH port probing**:
  - Prune staged files in `.gridlock/slurm_staging/` once a job terminates, and probe free ephemeral local ports (`QTcpServer` bind-to-0) for SSH `-L 127.0.0.1:<local>:<remote>` forwards (`src/core/hpc/system/LocalSlurmBackend.cpp:648-660`) instead of assuming `effBasePort + r` is locally free on shared login nodes.

### 3.3 LSP / DAP & External Tool Integration

- [ ] **Consolidate duplicate LSP clients (`LspCoordinator` + `ClangdLspClient`)**:
  - `src/core/hpc/lsp/LspCoordinator.cpp` and `src/core/utils/ClangdLspClient.cpp` now share `gridlock::core::JsonRpcFramer` (`src/core/utils/JsonRpcFramer.hpp`) for bounded `Content-Length` transport; full client lifecycle consolidation remains optional follow-up work.
- [x] **Enforce framing safety, `compile_commands.json` discovery, & DAP trace gating**:
  - **Fixed `Content-Length: 0` deadlock & added strict upper bounds** across `LspCoordinator`, `ClangdLspClient`, and `DapCoordinator` via shared `JsonRpcFramer` (`16 MiB` max frame payload, `32 MiB` max rolling receive buffer, `16 KiB` header cap, case-insensitive `Content-Length:` parsing, and clean stream recovery on malformed/oversized headers).
  - **Replaced hardcoded `--compile-commands-dir=build`** in `LspCoordinator.cpp` with `setCompileCommandsDir(...)` and `resolveCompileCommandsDir(...)` (checking explicit override, `builddir/`, `build/`, and project root, omitting `--compile-commands-dir` when none exist).
  - **Gated verbose DAP protocol logging**: `[DAP Sniffer]` TX/RX JSON dumps in `src/core/hpc/dap/DapCoordinator.cpp` are now disabled by default and enabled only when `GRIDLOCK_DAP_TRACE=1` is set.
- [ ] **Remaining LSP/DAP resilience follow-ups**:
  - Prune stale entries in `ClangdLspClient::m_completedResponses`, add a bounded crash-restart backoff when `clangd` crashes, pre-verify the configured LSP binary exists before `QProcess::start`, and add request timeout expiration in `DapCoordinator`.

### 3.4 C ABI Plugin Surface (`include/gridlock/plugin_api.h` & `src/core/plugins/`)

- [x] **Guard all host-to-plugin ABI calls with `try/catch` boundaries**:
  - Wrapped `gridlock_plugin_init_v1`, `on_session_start`, `on_rank_stopped`, `on_session_end`, `on_theme_changed`, and `shutdown` invocations in `src/core/plugins/PluginManager.cpp` with `try { ... } catch (const std::exception& e) ... catch (...)` boundaries so exceptions from third-party C++ plugins never propagate across `extern "C"` frames.
- [x] **Tighten `GridLockPluginDescriptor` validation**:
  - `src/core/plugins/PluginManager.cpp` enforces `sizeof(GridLockPluginDescriptor) <= desc.struct_size <= kMaxSupportedPluginDescriptorSize` (`+256` bytes) and `desc.reserved_flags == 0`, rejecting malformed descriptors cleanly and unloading the shared library.
- [x] **Guarantee per-plugin `alloc_bytes` reclamation on failed init and unload**:
  - Tracked active host-allocated pointers per `PluginBindingContext` inside `PluginHostBridge` (`src/core/plugins/PluginHostBridge.hpp`) and wired `PluginHostBridge::reclaimPluginAllocations(...)` into both `teardownUncommitted` (failed/throwing init) and `shutdownAndUnloadHandle` (unload/hot-reload).
- [x] **Preserve existing ABI strengths & add sanitized GDB/MI trace export**:
  - Preserved the 16-byte versioned header layout locks (`tests/core/test_plugin_abi.cpp`), pre-`dlopen` workspace SHA-256 trust gating, `ScopedCallbackDepth` re-entrancy deferral, and Linux `STB_GNU_UNIQUE` hot-reload banner, and added `GdbMiTranscriptRecorder` (`src/core/hpc/gdb/GdbMiTranscriptRecorder.hpp`), **Tools -> Export Sanitized GDB/MI Trace...**, and `--export-mi-trace <file>` for deterministic cluster bug reproduction.

### 3.5 `meson.build`, Qt6 Modules & HPC Cluster Packaging

- [ ] **Remove unused Qt6 modules and split test-only / optional modules** (`meson.build:11`):
  - **Remove `Qml` and `OpenGLWidgets`**: verified zero references across `src/`. Removing both eliminates unnecessary `qt6-qtdeclarative` and OpenGL/EGL package requirements on headless/minimal HPC cluster builds.
  - **Split `Test` into a separate `qt6_test_dep`** (`dependency('qt6', modules: ['Test'], required: get_option('tests'))`) so building the main `gridlock` binary never requires `Qt6Test`.
  - **Make `Svg` and `Sql` optional Meson features** (`-Dqtsvg=auto`, `-Dqtsql=auto`):
    - `Qt6::Svg` is used solely in `src/ui/dialogs/graph/PointerGraphDialog.cpp:730` for optional SVG export.
    - `Qt6::Sql` (`QSQLITE`) is used solely in `DocsetManager` for offline Dash `.docset` indexing.
- [ ] **Fix the `Qt-Advanced-Stylesheets` Meson sandbox violation** (`meson.build:32`):
  - Replace direct `include_directories('external/Qt-Advanced-Stylesheets/src')` (which emits a Meson warning stating it will become a hard error in a future release) with either a Meson `subproject()` / CMake module lookup (`CMAKE_PREFIX_PATH`) or an explicit `external_include_dir` Meson option / `-I` compiler flag from `pkg-config`/CMake.
- [ ] **Make C/C++ tutorial build targets optional when MPI compilers are absent** (`meson.build:933-945`):
  - Guard `mpi_mm_bin` (`mpicc`) and `catchpoint_exceptions_bin` (`mpicxx`) with `find_program(..., required: false)` just like `stencil_omp_bin` (`meson.build:947`), so `ninja -C builddir` succeeds even if an MPI compiler module is not loaded during the build step.

---

## 4. Test Coverage Gaps & Recommended PR Gates

### Identified Coverage Gaps in `tests/` (24 Current Suites)

| Area | Current State | Missing Regression Tests |
| :--- | :--- | :--- |
| **GDB/MI Parser** | `test_mi_parser.cpp` + `test_coordinator.cpp` cover clean MI3 records, ROCm lanes, and Fortran helpers | Malformed/truncated MI lines; GDB 14+/16+ `201^done,variables=[...]` with nested `{...}` pretty-printer values; `*stopped` with nested `args=[{...}]` before `func`/`addr`; hover `100^done` with escaped quotes `\"`; stripped frame stops without `fullname` |
| **MPI & Slurm Launchers** | `test_config.cpp` + `test_ui_cohesion.cpp` cover offline `sbatch`/`squeue`/`scontrol`/`scancel` shims | Non-OpenMPI `mpiexec` (MPICH / Intel MPI) flag generation without `--oversubscribe`/`--map-by node`; SSH `-L` local port collision fallback; `LocalSlurmBackend` abnormal teardown `scancel` verification |
| **LSP & DAP Transport** | `test_lsp.cpp` + 8 `tests/dap/` suites cover happy-path framing and commands | `Content-Length: 0` / negative / oversized (`> 64 MiB`) framing recovery; missing `clangd` / `lldb-dap` binary startup; mid-session LSP/DAP crash and restart backoff |
| **C ABI Plugins** | `test_plugin_abi.cpp` + `test_plugin_manager.cpp` cover ABI layout, SHA-256 trust, and verifier leaks | Non-`noexcept` C++ plugin throwing during `init`/`shutdown`; nonzero `reserved_flags` rejection; failed-init `alloc_bytes` leak reclamation |
| **Build Matrix** | Default local build has full Qt6 + `qtermwidget6` + `mpif90` | CI matrix job testing minimal HPC configuration (`-Dqtermwidget=disabled`, no `mpif90`, and once split, no `Qml`/`OpenGLWidgets`/`Svg`) |

### Recommended Diagnostic & PR Gate Improvements

- [x] **Sanitized MI Session Trace Export**: Implemented `GdbMiTranscriptRecorder` (`4,096` lines / `2 MiB` ring buffer), deterministic path/user/host/env redaction, **Tools -> Export Sanitized GDB/MI Trace...**, and `--export-mi-trace <file>` so cluster users can attach reproducible bug reports across GDB 12/14/16+, OpenMPI, MPICH, and Slurm versions.
- [x] **Contributor & PR Gate Policy (`CONTRIBUTING.md`, `.github/pull_request_template.md`, `.github/ISSUE_TEMPLATE/bug_report.yml`)**: Documented the reproducer-first sanitized GDB/MI trace policy for unreproducible cluster/hardware bugs, mandatory failing-before / passing-after regression tests (`QT_QPA_PLATFORM=offscreen meson test`), `< 800`-line C++ file ceiling, and frozen `include/gridlock/plugin_api.h` compatibility rules.
- [ ] **Golden GDB/MI Fixture Test Suite (`tests/hpc/fixtures/`) & Minimal-HPC CI Matrix**:
  - Store real captured GDB/MI3 transcripts from GDB 12/14/16, `gfortran`, `ifx`, `rocgdb`, and `libstdc++`/`libc++` pretty-printers and replay them deterministically on every PR.
  - Enforce automated GitHub Actions checks for `< 800`-line file growth and minimal-dependency headless HPC builds (`-Dqtermwidget=disabled -Dsvg=disabled`).

# GridLock IDE Roadmap

## PHASE 1: Debugger Stability

* [x] Breakpoint sync: Sync gutter-selected breakpoints to GDB.
* [x] Rank Control: Implement individual rank-pausing (using specific GDB thread-apply/interrupt commands).
* [x] Layout Persistence: Ensure `QPlainTextEdit`/ViewportMargins are robust against layout regressions.
* [x] GDB Process Manager: Robust teardown sequence to prevent dangling zombie processes.

## PHASE 2: Variable Watcher (The "CLion Clone")

* [x] Expression Evaluator: Handle arbitrary C++ expressions (e.g., `calc + 5`).
* [x] Tooltips: Hover-variables in the code editor to see values.
* [x] Differential View: Dynamic variable tracking via GDB `-var-update`.

## PHASE 3: Advanced Debugger

* [x] MemView: Hex dump viewer for raw pointers (similar to `mcu-debug/memview`).
* [x] Register Viewer: Real-time tracking of CPU registers (EAX/RAX, RSP, etc.).
* [x] Conditional Breakpoints: Add logic for hit counts and conditions.
* [x] Floating-Point Exception (FPE) Trapper: Catch NaNs and Infinities.
* [x] Deadlock Detector (Barrier/Wait Analyzer): Flag hanging MPI processes.
* [x] Value-Change Visual Highlighting: Variables and registers flash when values change.
* [x] Raw Memory Export (CSV/Bin) & Multi-Rank Console Broadcasting.

## PHASE 4: IDE Experience & Productization

* [x] **Toolbar Robustness:** Fully decouple execution logic (Run, Continue, Step, Pause, Terminate) from UI lambdas; implement a Command Pattern for toolbar actions.
* [x] **Editor Tabs:** Support opening multiple source files simultaneously via a tabbed editor interface.
* [x] **Project Support:** Add a file explorer/project manager to handle entire repositories and directories.
* [x] **Asset/Branding Pipeline:** Integrate official app icon (`.ico`/.icns/.png), splash screen, and high-DPI resource handling using Qt Resource System (`.qrc`).
* [x] **Shortcut Manager:** Implement global IDE keybindings (Vim-style or CLion-style).
* [x] **Session Persistence:** Save/load debug configurations (binary arguments, custom GDB paths, watchlists).
* [x] **Deployment:** Create standardized install targets (`meson install`), desktop entries (`.desktop` file), and directory structure for cross-platform distribution.

## PHASE 5: The Polyglot Core (DAP Refactor)

* [x] **Refactor `DebuggerBackend`:** Transform your abstract `DebuggerBackend` into a `DapBackend`.
* [x] **Standardized Launch:** Implement the startup sequence for all language sessions:
  * [x] GridLock spawns the adapter process.
  * [x] GridLock performs a TCP handshake.
  * [x] GridLock sends a standard JSON-RPC `launch` command.

## PHASE 6: Zero-Copy Memory & WIP Visualizer ✅

* [x] **Zero-Copy Native Memory Engine (`process_vm_readv`):** Bypass standard DAP/GDB string evaluation overhead for large arrays by reading directly from kernel memory.
* [x] **Stride Security & Bounds Validation:** Implement an expression validation engine with overflow protection to prevent malicious or invalid matrix strides from causing out-of-bounds segfaults in the frontend.
* [x] **Hardware-Accelerated Vulkan (`QVulkanWindow`) UI Integration** (Deferred to post-AppImage release)
* [x] **Heatmap UI Controls and DAP Expression Evaluation Bridge** (Deferred to post-AppImage release)

## PHASE 6.5: Workspace Configuration & TDD Infrastructure ✅

* [x] **Project-Scoped Settings (`.gridlock/settings.toml`):** Implement per-project configuration files to manage execution parameters (binary path, rank count, MPI args) without polluting the global environment or the user's home directory.
* [x] **Workspace Metadata (`.gridlock/workspace.toml`):** Persist project-specific workspace state (open files, layout, session history) isolated to the project root.
* [x] **Dockerized TDD Pipeline:** Establish a `docker-compose`-based sandbox with `SYS_PTRACE` capabilities and `seccomp:unconfined` to guarantee reliable `ptrace` and `process_vm_readv` execution for CI and local testing.
* [x] **Coverage Reporting in Docker:** Integrate `lcov`-based code coverage generation into the Docker pipeline via `scripts/run_coverage.sh`.

## PHASE 7: Cross-Language Variable Inspector

* [ ] **Unified Grid:** Utilize the standard `VariablesRequest` enforced by DAP so `DifferentialGrid` works for all languages.
* [ ] **The "Dream" Feature:** Enable simultaneous debugging of different language runtimes (e.g., C++, Python, Node.js) paused at the same logical simulation timestamp, with the `DifferentialGrid` tracking memory addresses and objects in parallel.

## PHASE 8: Alternative Debugger Backends

By leveraging the MI2 protocol (and our future DAP architecture), our `DebuggerBackend` can abstract the underlying engine, allowing seamless switching between GNU GDB, LLDB, and GPU-specific debuggers.

* [ ] **LLVM/LLDB Integration:** Support `lldb-mi` or `lldb-dap` to provide native macOS support and better Clang AST parsing for modern C++.
* [ ] **GPU Debugging (CUDA/ROCm):** Support `cuda-gdb` (NVIDIA) and `rocgdb` (AMD) as selectable engines for inspecting GPU wavefronts and device VRAM.
* [ ] **Backend Selection UI:** Extend `DebuggerSettingsPage` with a "Debugger Engine" dropdown (GNU GDB, LLDB, CUDA-GDB, ROCGDB).
* [ ] **Dynamic UI Capabilities:** Ensure the GridLock UI dynamically enables/disables features based on the selected backend (e.g., hiding the "Registers" view if a specific backend doesn't map them the same way, or showing a "Wavefronts" tab for ROCm).

## PHASE 9: The Plugin Marketplace (Productionization)

* [ ] **Dynamic Loading:** Replace hardcoded language support with a `plugins/` directory system.

## PHASE 10: Advanced Cluster Lifecycle & Detached Sessions

* [ ] TOML-based session persistence for pending cluster jobs.
* [ ] Background `squeue` polling for seamless DAP re-attachment.
* [ ] Asynchronous `scancel` guillotine for safe UI teardowns and zombie process mitigation.

## PHASE 11: Deployment & AppImage Packaging ✅

* [x] Configure `CQtDeployer` and `appimagetool` to bundle the Qt6, Wayland, XCB, and Vulkan runtime dependencies.
* [x] Ensure the native `process_vm_readv` memory extraction engines and `lldb-dap` adapters remain fully functional within the AppImage mount.
* [x] Verify project-scoped `.gridlock/` configuration directories are handled correctly when GridLock is launched from the AppImage mount path.
* [x] Create `.desktop` file and application icon integration for standard Linux desktop environments.
* [x] Set up GitHub Actions CI/CD to automatically build and attach `.AppImage` binaries to GitHub Releases using artifact uploaders.

## Field Testing & Validation (Remote HPC Workflows)

> These items cannot be exercised in a local CI/CD pipeline. Each must be
> validated against a real HPC login node before marking complete.

### SSH Subsystem

* [ ] **Successful Authentication:** Connect to a cluster login node using an Ed25519/RSA key configured in `Preferences → HPC Integration → SSH Key Path` and confirm the HPC Console shows a live prompt.
* [ ] **Invalid Key Handling:** Supply a wrong or missing key path; verify GridLock surfaces a human-readable error in the HPC Console rather than hanging or crashing.
* [ ] **Connection Timeout:** Configure an unreachable host; confirm the SSH process respects a reasonable timeout (≤ 30 s) and the UI remains responsive throughout.
* [ ] **Reconnect Flow:** After a successful connection is dropped (e.g., network interruption), verify the "⟳ Refresh Packages" button attempts a new connection cleanly without leaking zombie processes.

### Spack Integration

* [ ] **`spack find` Parsing:** Run a refresh on a node where Spack is correctly sourced; confirm the package list populates in `SpackManager` and the count label is accurate.
* [ ] **Unsourced Spack Environment:** Connect to a node where `spack` is not on `$PATH`; verify GridLock emits a clear `[ERR]` message (e.g., `command not found`) instead of silently showing an empty list.
* [ ] **Package Filter:** With a non-trivial package list loaded, type a partial name into the search bar; confirm the filter updates in real time without a remote round-trip.
* [ ] **Large Package Lists (> 500 entries):** Confirm the `QPlainTextEdit` block limit (`setMaximumBlockCount`) prevents excessive memory growth and the scroll-to-bottom behaviour still works.

### SLURM Batch Submission

* [ ] **`{FILE}` Token Replacement:** Open a source file in the editor, trigger "Submit SLURM Job", and confirm the generated script replaces `{FILE}` with the correct absolute path before being passed to `sbatch`.
* [ ] **`sbatch` Execution:** Verify the job is accepted by the scheduler and a Job ID is echoed back to the HPC Console (e.g., `Submitted batch job 123456`).
* [ ] **GPU Request Flag:** Enable "Request GPUs" with `gpusPerNode = 2`, submit a job, and inspect the generated `#SBATCH --gres=gpu:2` directive in the script that reaches the scheduler.
* [ ] **Partition / Nodes / Tasks Propagation:** Change partition to a non-default value in `Preferences → HPC Integration`; verify the SLURM script header reflects the new values on the next submission.
* [ ] **Submission Failure Handling:** Submit to a non-existent partition; confirm the stderr from `sbatch` is surfaced in the HPC Console and the IDE does not freeze.

### MPI Edge Cases

* [ ] **Strict Node Affinity:** Enable `Strict Node Affinity` in `Preferences → HPC / Cluster`; run a 4-rank job and confirm the MPI runtime pins ranks to the specified host set from the Hosts File.
* [ ] **Custom Environment Variables:** Populate `MPI Environment Variables` with a `KEY=VALUE` pair; confirm the variable is visible inside the MPI ranks (e.g., via `getenv()` in the test program output).
* [ ] **Hosts File Path:** Point `MPI Hosts File` at a valid multi-node hostfile; verify `mpirun` picks up the correct `-hostfile` argument and distributes ranks across nodes.
* [ ] **Rank Count Propagation:** Set Default Rank Count to 8 in `Preferences → Debugger`; launch a session and confirm 8 GDB instances are spawned and all appear in the `ServerRackView`.

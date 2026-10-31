# GridLock User Workflow Guide

![GridLock Workspace Overview](screenshots/workspace_overview.png)

## 1. Launching & Stepping an MPI Target

1. **Select Target & Rank Count (`-np`) on the Main Toolbar:**
   * Choose a built-in MPI target (`inspection_demo (MPI)`, `deadlock_ring (MPI)`) or select `Open ELF / Binary...` in the **TARGET** selector (`#toolbarTargetSelector`).
   * Set the desired number of MPI processes (`1..64`) in the **RANKS (-np)** spin box (`#toolbarRankSpin`).
2. **Start Execution (`▶ Run Target`):**
   * When `STATUS: IDLE`, only `▶ Run Target` is enabled; session-only controls (`Continue`, `Step Inst`, `Pause Rank`, `Stop`, `Terminate Session`) are muted to prevent invalid commands.
   * Clicking `▶ Run Target` spawns the configured MPI ranks in **Server Rack**, pauses Rank 0 at `main` (`0x00401120`), highlights the entry line in **SourceCodeView** with an emerald `▶` marker, and enables all stepping controls.
3. **Step Instructions (`↷ Step Inst`):**
   * Each click on `↷ Step Inst` advances the active rank's instruction pointer (`rip`), synchronously updating **DisassemblyView** (Linear, CFG Graph, and Decompiler), **RegisterView**, **Server Rack**, **Memory View**, and **SourceCodeView**.
4. **Stop or Terminate (`■ Stop` / `✕ Terminate Session`):**
   * Resets the session state to `STATUS: IDLE`, clears the active execution highlight, and disables session-only toolbar buttons.

---

## 2. Two-Lane Vector Gutter & Cursor-Anchored Breakpoint Popover

![Cursor-Anchored Breakpoint Popover](screenshots/breakpoint_popover.png)

* **Two-Lane Vector Gutter (`SourceCodeView`):** The left editor gutter separates debugger badges (left `20px` vector icon lane) from right-aligned line numbers so breakpoint dots (`●`), conditional badges (`?`), and the active execution arrow (`▶`) never obscure line numbers.
* **Live Gutter Hover Preview:** Hovering over any line in the gutter highlights the row and displays a translucent ghost breakpoint preview circle before clicking.
* **Cursor-Anchored Conditional Breakpoint Popover (`ConditionalBreakpointDialog`):** Right-clicking any gutter line opens a frameless popover anchored directly beneath the gutter dot:
  * **Header Context:** Displays `Line <N> Breakpoint` alongside a monospaced preview of the target C/C++ statement.
  * **Suspend Scope:** Choose between **All MPI Ranks** or **Focused Rank Only** to isolate rank-specific conditions.
  * **Live `clangd` LSP Virtual-Buffer Expression Completion:** Typing a condition (such as `rank == 0`, `data[i].`, or `send_buf.`) splices the expression into a virtual C++ translation unit at line `N` and queries `clangd` over JSON-RPC 2.0 for context-aware struct member and local variable completions.
  * **Quick Actions:** Press `Enter` (`Done`) to save the condition or click **Remove Breakpoint** to clear it.

---

## 3. Bottom Tool-Window Icon Rail & Linked Contextual Tabs

![Bottom Tool-Window Rail — Debug Mode](screenshots/tool_rail_debug_tabs.png)

To eliminate tab clutter and nested header bars, the bottom workstation dock features a **`36px` Left Vertical Tool-Window Icon Rail (`#bottomToolWindowRail`)** paired with **Linked Contextual Sub-Tabs**:

* **`Debug` Mode:** Filters the bottom tab bar to **GDB Console**, **Memory View**, and **Evaluator** for low-level process inspection.
* **`MPI` Mode:** Filters the bottom tab bar to **MPI Diagnostics** and **Package Manager**:
  * **Flattened Single-Row `MPI Diagnostics` Header:** Integrates the `DEADLOCK ANALYZER` and `NETWORK LOG` mode selector buttons (`#mpiDiagViewDeadlockBtn`, `#mpiDiagViewNetworkBtn`), live rank status summary, and `Export Report` action into a single `26px` header strip with zero nested tab bars.
* **`Terminal` Mode:** Focuses the embedded fixed-pitch PTY **Terminal** (`QTermWidget`).
* **`Docs` Mode:** Focuses the offline **Reference Manual** (`Zeal` docsets & system `man` pages).

![Bottom Tool-Window Rail — MPI Diagnostics Mode](screenshots/tool_rail_mpi_diagnostics.png)

---

## 4. Multi-Distro HPC & System Package Manager Search (`"mpi"` Walkthrough)

![Package Manager — MPI Table View](screenshots/package_manager_mpi_search.png)

The **Package Manager** tab (`HpcConsoleWidget`) provides an integrated software discovery workstation across both HPC cluster environments and standard Linux workstations:

1. **Automatic Multi-Backend Discovery:** Automatically detects installed package managers in priority order: **Spack** (`spack`), **Arch Pacman** (`pacman`), **Fedora/RHEL DNF** (`dnf`), **Debian/Ubuntu APT** (`apt`/`dpkg-query`), **openSUSE Zypper** (`zypper`), **RPM** (`rpm`), and **EasyBuild** (`eb`).
2. **Searching for `"mpi"` Packages:**
   * Select `MPI` on the left vertical tool-window rail and open the **Package Manager** tab.
   * Type `"mpi"` into the search bar (`#packageSearchInput`) or click **Refresh**.
   * Filter by installation state (**All**, **Installed Only**, **Available**) or switch backends via the **Backend** selector.
3. **Three Switchable Presentation Views (`Table` | `List` | `Raw`):**
   * **Table View (`Table`):** Displays sortable columns (`Package Name`, `Version`, `Arch`, `Source / Repo`, `Status`, `Summary`) with emerald badges for `Installed` packages (`openmpi`, `openmpi-devel`, `hypre`, `fftw-mpi`, `hdf5-openmpi`) and amber badges for `Available` packages (`mpich`, `scalapack`).
   * **List View (`List`):** Renders rich scannable cards showing package title, version badge, repository tag, installation status pill, and full summary description.
   * **Raw View (`Raw`):** Displays the verbatim monospace CLI stdout stream for diagnostic auditing.

![Package Manager — MPI List View](screenshots/package_manager_list_view.png)

---

## 5. SLURM Batch Script Auto-Detection & Smart Build System

![Preferences — 16-Directive SLURM Batch Auto-Detection](screenshots/preferences_slurm_autodetect.png)

### 16-Directive `#SBATCH` Auto-Configuration (`Edit -> Preferences -> HPC / Cluster`)
* Click **Browse / Load Batch Script...** or paste an existing SLURM submission script into the **SLURM Batch Script Auto-Detector** inside `PreferencesDialog`.
* GridLock automatically parses 16 standard `#SBATCH` directives (`--job-name`, `--partition`, `--account`, `--qos`, `--nodes`, `--ntasks`, `--ntasks-per-node`, `--cpus-per-task`, `--gres`, `--mem`, `--time`, `--constraint`, `--reservation`, `--chdir`, `--output`, `--error`) plus `module load <modules>` commands and populates the corresponding cluster launcher fields immediately.

### Smart Project Build System (`Build -> Build Target (F7)` / `Clean` / `Rebuild`)
* GridLock inspects the active target's project root to detect the appropriate build toolchain automatically:
  * **Meson (`meson.build`):** Dispatches `ninja -C builddir` (and `ninja -C builddir -t clean`).
  * **CMake (`CMakeLists.txt`):** Dispatches `cmake --build build --parallel` (and `cmake --build build --target clean`).
  * **GNU Make (`Makefile`):** Dispatches `make -j$(nproc)` (and `make clean`).
  * **Standalone MPI Source (`SingleFileMPI`):** Compiles standalone `.cpp`/`.c` MPI sources directly via `mpicxx -g -O0` (or `g++ -g -O0` fallback).

---

## 6. Switching Workspace Perspectives (`Alt+1` / `Alt+2` / `Alt+3`)

* **`CODE` (`Alt+1`):** Side-by-side C/C++ source editor (`45%`) and assembly/CFG view (`55%`) for source-level and instruction-level stepping.
* **`REVERSE` (`Alt+2`):** Collapses the source editor and expands **DisassemblyView** in **Split CFG + Decompiler** mode across `100%` of the center stage.
* **`MPI` (`Alt+3`):** Activates the **MPI Diagnostics** console (`Deadlock Analyzer` & `Network Log`) and expands the bottom console height.
* **Maximize Bottom Console (`Ctrl+M`):** Toggles the bottom console dock between standard height (`260px`) and maximized reading height (`68%`).

![GridLock Reverse Engineering Perspective](screenshots/cfg_decompiler_split.png)

---

## 7. Binary Inspection, Xrefs, Live Patching & Offline Documentation (`F1`)

![GridLock MPI Diagnostics](screenshots/mpi_diagnostics_docsets.png)

* **Navigate by Address or Symbol (`Ctrl+G`):** Enter any hex address (`0x00401149`) or symbol name (`sym.main`) in the toolbar seek bar to synchronize `DisassemblyView` and `MemView`.
* **Inspect Cross-References (`X`):** Press `X` on any instruction in `DisassemblyView` to open `XrefInspectorDialog`, listing all incoming callers/branches (`XREFS TO`) and outgoing calls/jumps (`XREFS FROM`).
* **Patch Instructions & Bytes (`Ctrl+Shift+P` / `Ctrl+E`):** Assemble replacement instructions or edit raw hex bytes in `MemView` and export a patched executable binary via `MainWindow::exportPatchedBinary`.
* **Instant Offline Documentation (`F1`):** Press `F1` on any MPI call (`MPI_Init`, `MPI_Send`, `MPI_Recv`, `MPI_Barrier`, `MPI_Allreduce`) or C/POSIX symbol to open its documentation in the **Reference Manual** tab.


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

## 2. Switching Workspace Perspectives (`Alt+1` / `Alt+2` / `Alt+3`)

Use the perspective buttons on the right side of the main toolbar (or keyboard shortcuts) to tailor the layout to your current task:

* **`CODE` (`Alt+1`):** Side-by-side C/C++ source editor (`45%`) and assembly/CFG view (`55%`) for source-level and instruction-level stepping.
* **`REVERSE` (`Alt+2`):** Collapses the source editor and expands **DisassemblyView** in **Split CFG + Decompiler** mode across `100%` of the center stage so complex control-flow graphs and reconstructed C/C++ pseudocode have maximum horizontal space.
* **`MPI` (`Alt+3`):** Activates the **MPI Diagnostics** console (`Deadlock Analyzer` & `Network Log`) and expands the bottom console height to inspect collective barriers, point-to-point message queues, and circular wait-for cycles.
* **Maximize Bottom Console (`Ctrl+M`):** Toggles the bottom console dock between standard height (`260px`) and maximized reading height (`68%`) for inspecting long manual pages or large memory dumps.

![GridLock Reverse Engineering Perspective](screenshots/cfg_decompiler_split.png)

---

## 3. Binary Inspection, Xrefs & Live Patching

* **Navigate by Address or Symbol (`Ctrl+G`):** Enter any hex address (`0x00401149`) or symbol name (`sym.main`) in the toolbar seek bar to synchronize `DisassemblyView` and `MemView`. Use `<` / `>` (`Alt+Left` / `Alt+Right`) to traverse your seek history.
* **Inspect Cross-References (`X`):** Press `X` on any instruction in `DisassemblyView` to open `XrefInspectorDialog`, listing all incoming callers/branches (`XREFS TO`) and outgoing calls/jumps (`XREFS FROM`).
* **Patch Instructions & Bytes (`Ctrl+Shift+P` / `Ctrl+E`):** Press `Ctrl+Shift+P` in `DisassemblyView` to assemble a replacement instruction (`nop`, `ret`, `xor eax, eax`, `jmp 0x401178`) or `Ctrl+E` in `MemView` to edit raw hex bytes. Patched bytes are written back to the in-memory ELF64 image and can be exported as an executable binary via `MainWindow::exportPatchedBinary`.

---

## 4. MPI Deadlock Analysis & Offline Documentation (`F1`)

![GridLock MPI Diagnostics](screenshots/mpi_diagnostics_docsets.png)

* **Deadlock Cycle Highlighting:** When a circular wait-for dependency is detected across MPI ranks, GridLock highlights the cycle in the **Deadlock Analyzer** table, badges the deadlocked rank cards in **Server Rack**, and renders crimson `DEADLOCK CYCLE` headers on the corresponding basic blocks in the 2D Control Flow Graph.
* **Instant Offline Documentation (`F1`):** Press `F1` on any MPI call (`MPI_Init`, `MPI_Send`, `MPI_Recv`, `MPI_Barrier`, `MPI_Allreduce`) or C/POSIX symbol to open its documentation in the **Reference Manual** tab, powered by auto-discovered Zeal `.docset` bundles and system `man` pages.

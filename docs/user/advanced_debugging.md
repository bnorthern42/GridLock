# 🚀 Power-User Manual: Advanced Debugging

GridLock provides specialized tools for diagnosing complex HPC anomalies, race conditions, and memory corruptions without compromising UI responsiveness.

## 🗂️ Lazy-Loaded Variables Grid

Debugging parallel applications often means inspecting multi-gigabyte numerical arrays and deep pointer hierarchies. To ensure the IDE remains lightning-fast, the **Variables Dock** utilizes a strict **Lazy-Loading** architecture.

When execution pauses, GridLock will only fetch the top-level variable scopes. To inspect massive numerical arrays or deep pointers, you must **explicitly expand the nodes** in the UI (by clicking the chevron next to a variable). This on-demand fetching guarantees that your workspace will never freeze due to heavy data payloads, even when exploring complex nested structures across hundreds of ranks.

## 📝 Wayland-Native LSP Tooltips

Context is everything. GridLock integrates deeply with Clangd to provide rich, semantic hovers directly within your editor.

By hovering your cursor over any variable or function symbol in the **Source Code View**, a markdown-rendered tooltip will instantly appear displaying the symbol's signature, documentation, and live debugger evaluation. Because GridLock is a Wayland-native application, these overlays are composited cleanly—even on tiling window managers like Sway or Niri—without the flickering or translucent tearing common in legacy X11 applications.

## 🛑 Deadlock Detector (Barrier/Wait Analyzer)

MPI deadlocks are notoriously difficult to track. GridLock's Deadlock Detector passively analyzes the call stacks of all ranks. 

If multiple ranks are stalled in `MPI_Wait`, `MPI_Barrier`, or `MPI_Bcast` for longer than the configured threshold, the **MPI Diagnostics** dock flashes red and provides a dependency graph showing exactly which rank is holding the resource.

## 💥 Floating-Point Exception (FPE) Trapper

Catching `NaN` or `Inf` at the exact moment of creation is critical in scientific computing.

1. Open the **MPI Diagnostics** dock.
2. Toggle the **Enable FPE Traps** switch.
3. GridLock will inject `#pragma` or environment-level signals (e.g., `FE_INVALID | FE_DIVBYZERO`) into the debuggee.
4. The IDE will halt execution on the exact instruction that generated the bad float, highlighting it in the Assembly view.

## 🚦 Conditional Breakpoints via Expressions

Don't pause every iteration of a billion-step loop. Use conditional breakpoints.

1. Press `Alt + B` to set a standard breakpoint.
2. Right-click the red gutter icon (or use the command palette).
3. Enter a valid expression (e.g., `i == 500000 && local_rank == 2`).
4. The breakpoint indicator will turn **blue**, signifying a condition is attached.

## 🔍 Deep Memory Inspection & Matrix Export

When simple variable watching isn't enough, GridLock provides direct access to the raw physical memory of your MPI processes.

* **Instant Hex Inspection:** If you encounter a raw pointer or memory address in your Variables Grid or LSP Tooltip, simply **double-click the hex address**. This will instantly open the **Memory View** dock, anchored exactly to that block of memory for byte-level inspection.
* **Matrix Export:** To dump massive contiguous blocks of memory for offline data analysis, highlight a block in the Memory View and click **Export Matrix...**. You can export directly to **CSV** (for human-readable floats) or **Binary** (for pure raw bytes).

## ✨ Value-Change Visual Highlighting

Keep track of mutations without manual inspection. In both the **Variables Dock** and the **Registers Dock**:

*   When a value changes between execution steps, the cell briefly flashes **Yellow**.
*   This highlighting works globally across all expanded structs and objects, allowing you to instantly spot memory mutations as you step through your numerical solvers.

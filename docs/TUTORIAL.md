# GridLock Hands-On Tutorial

This tutorial walks through the 7 bundled MPI debugging showcase programs in `tutorial/`, followed by step-by-step guides for comparing process memory across ranks (`DIFF RANKS`), exporting raw memory slices (`EXPORT`), looking up offline man pages and Zeal docsets (`F1`), and managing system/HPC packages (`PACKAGE MANAGER`).

![GridLock Workspace Overview](screenshots/workspace_overview.png)

---

## Section 1: Building & Selecting the 7 Bundled MPI Examples

GridLock ships with 7 self-contained C++ MPI programs in `tutorial/` that exercise common distributed bugs, memory layouts, and telemetry views:

| Example Source | Toolbar `TARGET:` Entry | Primary Bug / Feature Demonstrated | Recommended Breakpoint |
| :--- | :--- | :--- | :--- |
| `tutorial/deadlock_ring.cpp` | `deadlock_ring (MPI)` | 8-rank synchronous `MPI_Send` ring deadlock | Line 34 (`MPI_Send`) |
| `tutorial/inspection_demo.cpp` | `inspection_demo (MPI)` | Deeply nested structs & rank-filtered conditional breakpoints | Line 19 (`data[i].coords.x = ...`) |
| `tutorial/collective_mismatch.cpp` | `collective_mismatch (MPI)` | Even ranks call `MPI_Bcast` while odd ranks hit `MPI_Barrier` | Line 46 (`if (rank % 2 == 0)`) |
| `tutorial/ghost_cell_drift.cpp` | `ghost_cell_drift (MPI)` | 1D Jacobi stencil exchange (`MPI_Sendrecv`) with `NaN` injection on step 3 | Line 69 (`stencil_center = ...`) |
| `tutorial/heap_imbalance.cpp` | `heap_imbalance (MPI)` | Quadratically scaled per-rank heap allocation & recursion depth | Line 41 (`leaf_checksum = ...`) |
| `tutorial/tag_mismatch_stall.cpp` | `tag_mismatch_stall (MPI)` | Point-to-point tag mismatch (`send_tag = 100` vs `expected_tag = 200`) | Line 53 (`MPI_Send`) / Line 61 (`MPI_Recv`) |
| `tutorial/simd_struct_watch.cpp` | `simd_struct_watch (MPI)` | 16-byte aligned `SimdMatrixPacket` (`0xCAFE...`) & raw memory export | Line 60 (`MPI_Allreduce`) |

### Compiling & Launching from the Toolbar or CLI

1. **Directly from the GridLock Toolbar:**
   * Launch GridLock (`./builddir/gridlock`).
   * Open the **`TARGET:`** dropdown on the top toolbar and select any of the 7 showcase entries (for example, `collective_mismatch (MPI)` or `ghost_cell_drift (MPI)`).
   * Set **`RANKS (-np):`** to **`8`** (or **`4`**).
   * Click **`⚙ Build`** (or press **`Ctrl+B`**) to compile `tutorial/<name>.cpp` into `tutorial/<name>` with `mpicxx -g -O0` inside the integrated terminal, or click **`▶ Run Target`** to let GridLock compile on demand and launch the session automatically.
2. **Manual Command-Line Compilation:**
   ```bash
   for src in tutorial/deadlock_ring.cpp tutorial/inspection_demo.cpp \
              tutorial/collective_mismatch.cpp tutorial/ghost_cell_drift.cpp \
              tutorial/heap_imbalance.cpp tutorial/tag_mismatch_stall.cpp \
              tutorial/simd_struct_watch.cpp; do
       mpicxx -g -O0 "$src" -o "${src%.cpp}"
   done
   ```

---

## Section 2: Walkthroughs for the 7 MPI Showcase Examples

### Example 1 (`tutorial/deadlock_ring.cpp`): Diagnosing an 8-Rank Circular `MPI_Send` Cycle

`tutorial/deadlock_ring.cpp` sets up a ring topology where every rank sends a 16,384-integer buffer (`send_buf`) to `next_rank = (rank + 1) % size` before posting the matching `MPI_Recv` from `prev_rank = (rank + size - 1) % size`:

```cpp
std::vector<int> send_buf(16384, rank * 10 + 1);
std::vector<int> recv_buf(16384, 0);

MPI_Send(send_buf.data(), static_cast<int>(send_buf.size()), MPI_INT,
         next_rank, tag, MPI_COMM_WORLD);
MPI_Recv(recv_buf.data(), static_cast<int>(recv_buf.size()), MPI_INT,
         prev_rank, tag, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
```

Because the 64 KB buffer exceeds the typical MPI eager threshold, every rank blocks inside `MPI_Send` waiting for its neighbor to enter `MPI_Recv`.

1. Select **`deadlock_ring (MPI)`** in **`TARGET:`**, set **`RANKS (-np):`** to **`8`**, and click **`▶ Run Target`**.
2. Step or continue past `MPI_Init` until execution blocks inside `MPI_Send` at line 34.
3. Check **`SERVER RACK`** on the right sidebar: all 8 rank slivers (`R0`–`R7`) indicate a stalled communication state.
4. Click **`MPI`** on the bottom tool rail (`Alt+3`) to open **`MPI Diagnostics`**. The **`DEADLOCK ANALYZER`** tab displays the circular wait-for chain (`Rank 0 -> Rank 1 -> ... -> Rank 7 -> Rank 0`).
5. In the **`Disassembly`** pane (`MAP (CFG)` mode), inspect the `[R0..R7]` badges clustered on the basic block calling `MPI_Send`. Replace the `MPI_Send` / `MPI_Recv` pair with `MPI_Sendrecv`, save (`Ctrl+S`), and rebuild (`⚙ Build`).

![GridLock MPI Diagnostics & Deadlock Analyzer](screenshots/mpi_diagnostics_docsets.png)

---

### Example 2 (`tutorial/inspection_demo.cpp`): Rank-Filtered Conditional Breakpoints & Struct Expansion

`tutorial/inspection_demo.cpp` populates an array of nested structs (`DeeplyNested data[3]`) with rank-dependent values:

```cpp
for (int i = 0; i < 3; i++) {
    data[i].id = i;
    for (int j = 0; j < 5; j++) data[i].values[j] = (rank + 1) * (i * j);
    data[i].coords.x = (rank == 0) ? (i * 1.1f) : (i * 1.5f);
    data[i].coords.y = (rank == 0) ? (i * 2.2f) : (i * 2.5f);
    data[i].coords.z = (rank == 0) ? (i * 3.3f) : (i * 3.5f);
}
```

1. Select **`inspection_demo (MPI)`** in **`TARGET:`** to open `tutorial/inspection_demo.cpp`.
2. Right-click the editor gutter on **line 19** (`data[i].coords.x = ...`) to open the **Conditional Breakpoint Popover**.

![Cursor-Anchored Breakpoint Popover](screenshots/breakpoint_popover.png)

3. Set **Suspend Scope** to **Focused Rank Only** (or **All MPI Ranks**) and type `data[i].coords.` in the **Condition** field. `ClangdLspClient` queries `clangd` and suggests `x`, `y`, and `z`. Complete the expression as:
   ```cpp
   rank == 3 && data[i].coords.x > 0.0f
   ```
4. Press **`Enter`** and click **`▶ Run Target`**. Execution skips iteration `i == 0` (where `coords.x == 0.0f`) and pauses on iteration `i == 1` when `data[1].coords.x` evaluates to `1.5f`.
5. Expand `data` in the top-right **`VARIABLES`** tab to inspect `data[0]`, `data[1]`, and `data[2]`, including nested `values[5]` and `coords.{x,y,z}` fields.

---

### Example 3 (`tutorial/collective_mismatch.cpp`): Catching `MPI_Bcast` vs. `MPI_Barrier` Divergence

`tutorial/collective_mismatch.cpp` branches on rank parity (`rank % 2 == 0`): even ranks call `MPI_Bcast` while odd ranks execute `MPI_Barrier(MPI_COMM_WORLD)`.

```cpp
if (rank % 2 == 0) {
    cfg.rank_branch_tag = 100 + rank;
    MPI_Bcast(&cfg.broadcast_seed, 1, MPI_INT, 0, MPI_COMM_WORLD);
} else {
    cfg.rank_branch_tag = 200 + rank;
    MPI_Barrier(MPI_COMM_WORLD);
}
```

1. Select **`collective_mismatch (MPI)`** in **`TARGET:`**, set **`RANKS (-np):`** to **`8`**, and set a breakpoint on **line 46** (`if (rank % 2 == 0)`).
2. Click **`▶ Run Target`** and step once (`↷ Step Inst`) into the branch.
3. Observe the **`Disassembly`** pane in **`MAP (CFG)`** mode: even ranks (`[R0, R2, R4, R6]`) follow the true edge into the basic block calling `MPI_Bcast`, while odd ranks (`[R1, R3, R5, R7]`) follow the false edge into the basic block calling `MPI_Barrier`.
4. Open **`MPI Diagnostics`** in the bottom dock to inspect the mismatched collective call primitives across ranks.

---

### Example 4 (`tutorial/ghost_cell_drift.cpp`): Tracking 1D Stencil Halo Corruption & `NaN` Propagation

`tutorial/ghost_cell_drift.cpp` performs a 5-step 1D Jacobi heat stencil exchange (`MPI_Sendrecv`) with periodic ring neighbors. On iteration `step == 3`, `faulty_rank` (Rank 2) injects `std::numeric_limits<double>::quiet_NaN()` into its right ghost boundary:

```cpp
if (step == 3 && rank == faulty_rank) {
    send_right = std::numeric_limits<double>::quiet_NaN();
}
```

1. Select **`ghost_cell_drift (MPI)`** in **`TARGET:`** and set a breakpoint on **line 69** (`stencil_center = domain[domain.size() / 2];`).
2. Switch to the top-right **`WATCH`** tab (`DifferentialGrid`) and add `step`, `left_ghost`, `right_ghost`, and `stencil_center`.
3. Click **`▶ Run Target`** and continue across timesteps:
   * On `step == 0..2`, all ranks show finite temperature values.
   * On `step == 3`, Rank 3's `left_ghost` turns `nan` immediately after receiving Rank 2's corrupted boundary value.
   * On `step == 4`, the `nan` value spreads into neighboring interior stencil cells.

---

### Example 5 (`tutorial/heap_imbalance.cpp`): Monitoring Per-Rank `STACK` & `HEAP` Telemetry

`tutorial/heap_imbalance.cpp` scales each rank's `std::vector<double> heap_buffer` quadratically with `(rank + 1) * (rank + 1)` and recurses to a rank-dependent stack depth `target_stack_depth = 2 + rank * 2` inside `recurseWithStackFrame()`.

1. Select **`heap_imbalance (MPI)`** in **`TARGET:`** and set a breakpoint on **line 41** (`const double leaf_checksum = ...`).
2. Click **`▶ Run Target`** with **`RANKS (-np):`** set to **`8`**.
3. Click through **`R0`** to **`R7`** in the **`SERVER RACK`** dock while watching the bottom status bar ribbon (`ARCH: x86_64 | MPI RANK: ... | STACK: ... | HEAP: ... | RAM: ... | CPU: ...`):
   * Higher ranks report deeper stack usage (`STACK`) from additional recursive frames and larger heap allocations (`HEAP`).
   * You can switch between absolute memory units (`MB / GB`) and percentages (`%`) in **`Edit -> Preferences -> Debugger`** (`Status Bar Telemetry Format`).

---

### Example 6 (`tutorial/tag_mismatch_stall.cpp`): Diagnosing Point-to-Point Tag Mismatches

In `tutorial/tag_mismatch_stall.cpp`, Rank 0 sends a `TelemetryPacket` to Rank 1 with `send_tag = 100`, whereas Rank 1 calls `MPI_Recv` waiting on `expected_tag = 200`:

```cpp
// Rank 0 (line 53):
MPI_Send(&pkt, sizeof(TelemetryPacket), MPI_BYTE, 1, send_tag /* 100 */, MPI_COMM_WORLD);

// Rank 1 (line 61):
MPI_Recv(&pkt, sizeof(TelemetryPacket), MPI_BYTE, 0, expected_tag /* 200 */, MPI_COMM_WORLD, &status);
```

1. Select **`tag_mismatch_stall (MPI)`** in **`TARGET:`**, set breakpoints on **line 53** and **line 61**, and click **`▶ Run Target`**.
2. Add `send_tag`, `expected_tag`, and `pkt.payload_tag` to the **`WATCH`** tab (`DifferentialGrid`) or inspect them in **`VARIABLES`** when switching between Rank 0 and Rank 1.
3. Open **`MPI Diagnostics`** to see Rank 1 blocked in `MPI_Recv` waiting on tag `200` while Rank 0 sent tag `100` and Ranks 2–7 have already reached `MPI_Finalize`.

---

### Example 7 (`tutorial/simd_struct_watch.cpp`): Inspecting Aligned Matrix & Packet Buffers in `MemView`

`tutorial/simd_struct_watch.cpp` populates a 16-byte aligned `SimdMatrixPacket` containing a 4-byte signature (`{0xCA, 0xFE, rank, world_size}`), a packet ID, a 64-bit lane flag word, and a 4x4 float matrix:

```cpp
struct alignas(16) SimdMatrixPacket {
    uint8_t magic[4];       // {0xCA, 0xFE, rank, world_size}
    uint32_t packet_id;     // 0x1000 + rank
    uint64_t flags;         // 0xABCD000000000000ULL | rank
    float matrix[4][4];     // 4x4 row-major transformation matrix
    double lane_Norms[4];   // Per-row Euclidean norm accumulator
};
```

1. Select **`simd_struct_watch (MPI)`** in **`TARGET:`**, set a breakpoint on **line 60** (`MPI_Allreduce`), and click **`▶ Run Target`**.
2. In the **`VARIABLES`** tab, expand `packet` to inspect `magic`, `packet_id`, `flags`, and `matrix[0..3][0..3]`.
3. Open **`MEMORY VIEW`** in the bottom dock and type `&packet` (or its hex address) into **`Addr:`** to inspect the raw 16-byte aligned header starting with `CA FE`.

---

## Section 3: Comparing Memory Differences Across Ranks (`MemView` -> `DIFF RANKS`)

When debugging distributed simulations (`ghost_cell_drift.cpp`, `simd_struct_watch.cpp`, or `inspection_demo.cpp`), you can compare raw process memory between any two ranks directly in **`MEMORY VIEW`** (`MemView`):

1. **Open Memory View:**
   * Click **`Debug`** on the bottom tool rail and select the **`MEMORY VIEW`** tab.
2. **Configure Address, Length, and Data Type:**
   * In **`Addr:`**, enter a symbol reference or hex address (such as `&packet`, `&domain[0]`, `&matrix[0]`, or `0x404000`).
   * In **`Bytes:`**, select or enter the byte count to read (for example, `256`).
   * In **`Type:`**, pick how values are formatted (`Hex`, `Int32`, `Float64`, etc.).
3. **Select Ranks and Diff (`DIFF RANKS`):**
   * Set **`Base:`** to **`0`** and **`Target:`** to **`2`** (or any rank pair you want to compare).
   * Click **`DIFF RANKS`**. `MemoryDiffer` reads the memory window from both rank processes via `process_vm_readv` and highlights every byte offset where Rank 2 diverges from Rank 0.

---

## Section 4: Exporting Raw Memory & Hex Dumps (`MemView` -> `EXPORT`)

While paused at a breakpoint, you can save the currently inspected memory slice to disk for offline verification, regression testing, or comparison across runs:

1. Pause execution at a breakpoint (such as line 60 of `tutorial/simd_struct_watch.cpp`) and load the target buffer in **`MEMORY VIEW`** (`Addr:` + `Bytes:`).
2. Click the **`EXPORT`** button on the right side of the **`MEMORY VIEW`** toolbar.
3. Choose an output file path in the save dialog to write the captured memory range (`Addr` through `Addr + Bytes`) to disk.
4. If you have modified instructions (`Ctrl+Shift+P` in `DisassemblyView`) or raw bytes (`Ctrl+E` in `MemView`), you can also write out a full patched ELF executable via **`File -> Export Patched Binary`**.

---

## Section 5: Offline Man Pages, Assembly Reference & Zeal Docsets (`F1` & `Preferences -> Docsets`)

GridLock includes an offline documentation viewer (**`Reference Manual`** / `ReferenceManualWidget`) so you can look up C/C++ library functions, MPI routines, Linux system calls, and assembly instructions without leaving the debugger:

1. **Instant Contextual Lookup (`F1`):**
   * Place the cursor on any C/C++ call, `MPI_*` routine (`MPI_Sendrecv`, `MPI_Bcast`, `MPI_Allreduce`), system call, or x86_64/ARM64 assembly mnemonic (`movsd`, `endbr64`, `je`, `callq`) in `SourceCodeView` or `DisassemblyView` and press **`F1`**.
   * GridLock opens the **`Docs`** category on the bottom tool rail and displays the matching page from your indexed Zeal `.docset` bundles or system `man` pages.
2. **Configuring & Indexing Docsets (`Edit -> Preferences -> Docsets`):**
   * Open **`Edit -> Preferences (Ctrl+,)`** and select **`Docsets`** in the left sidebar.
   * Add or browse custom Zeal docset directories (defaulting to `~/.local/share/Zeal/Zeal/docsets`) and download or re-index C, C++, and MPI `.docset` archives (`DocsetManager` queries the SQLite `searchIndex` table directly).

---

## Section 6: Using the Integrated Package Manager (`PACKAGE MANAGER` Dock)

The **`PACKAGE MANAGER`** tab (`HpcConsoleWidget`) lets you search, inspect, and install system and HPC debugging dependencies without switching to an external terminal:

![Package Manager — MPI Table View](screenshots/package_manager_mpi_search.png)

1. **Open the Package Manager:**
   * Click **`MPI`** on the bottom tool rail and select the **`PACKAGE MANAGER`** tab.
2. **Search System & HPC Backends:**
   * GridLock detects installed package managers automatically (`pacman`, `dnf`, `apt`, `zypper`, `rpm`, `spack`, `conan`, and `eb`).
   * Type a query such as `gdb`, `lldb`, `openmpi`, `mpich`, `clangd`, `valgrind`, or `zeal` into the search bar and press **`Enter`** (or click **`Refresh`**).
3. **Filter & Switch Views (`Table` | `List` | `Raw`):**
   * Filter packages by **All**, **Installed Only**, or **Available**, and switch between sortable **Table** columns, compact **List** cards, or **Raw** CLI output.

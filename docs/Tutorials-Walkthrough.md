# Tutorials Walkthrough

The `tutorial/` directory ships **7 primary C++ MPI showcase programs** (pre-populated in the main toolbar **`TARGET:`** dropdown and bundled into `GridLock-0.8.66-Tutorials-x86_64.AppImage`) plus **5 focused C/C++ micro-demos** accessible from `--tutorial-mode` (`TutorialDialog`) or `Open ELF / Binary...`.

Every walkthrough below gives the exact compiler command, recommended `-np` rank count, verified source line numbers, and the exact GridLock panels to inspect.

---

## Part 1: The 7 Primary MPI Showcase Programs

### Lab 1: Circular Point-to-Point Deadlock (`tutorial/deadlock_ring.cpp`)

* **Bug Demonstrated**: $N$-rank circular blocking `MPI_Send` stall (`Rank 0 -> Rank 1 -> ... -> Rank N-1 -> Rank 0`) with a 64 KB (`16,384 * sizeof(int)`) payload exceeding MPI eager buffering thresholds.
* **Compile Manually (or let `▶ Run Target` compile automatically)**:
  ```bash
  mpicxx -g -O0 tutorial/deadlock_ring.cpp -o tutorial/deadlock_ring
  ```
* **Reproduce & Diagnose in GridLock**:
  1. Select **`deadlock_ring (MPI)`** in **`TARGET:`** and set **`RANKS (-np):`** to **`8`**.
  2. Click **`▶ Run Target`**. Because lines `34-35` call `MPI_Send(send_buf.data(), 16384, MPI_INT, next_rank, tag, MPI_COMM_WORLD)` before line `36` (`MPI_Recv`), all 8 ranks block inside `MPI_Send`.
  3. Click **`⏸ Pause Rank`** (or set a breakpoint at **line `34`** before running).
  4. Press **`Alt+3`** (`MPI` perspective) and inspect **`MPI Diagnostics -> DEADLOCK ANALYZER`**:
     * Header badge reads **`STATUS: CYCLE DETECTED`**.
     * Every rank (`Rank 0 .. Rank 7`) shows `MPI_Send` stalled at `deadlock_ring.cpp:34`.
  5. Switch to **`REVERSE`** (`Alt+2`) to see `[R0-R7 (8)]` and **`[DEADLOCK]`** badges stacked on the `MPI_Send` basic block.
  6. **Fix**: Replace the sequential `MPI_Send` + `MPI_Recv` calls at lines `34-37` with `MPI_Sendrecv`, save (`Ctrl+S`), rebuild (`Ctrl+B`), and re-run.

---

### Lab 2: Nested Structs, Conditional Breakpoints & `clangd` Completion (`tutorial/inspection_demo.cpp`)

* **Behavior Demonstrated**: Recursive inspection of `struct DeeplyNested data[3]` (lines `4-10`, containing `int id`, `double values[5]`, and nested anonymous `coords { float x, y, z; }`) where Rank 0 assigns `i * (1.1f, 2.2f, 3.3f)` while Ranks `1..N-1` assign `i * (1.5f, 2.5f, 3.5f)` at **lines `19-21`**.
* **Compile**:
  ```bash
  mpicxx -g -O0 tutorial/inspection_demo.cpp -o tutorial/inspection_demo
  ```
* **Inspect in GridLock**:
  1. Select **`inspection_demo (MPI)`** in **`TARGET:`** and set **`RANKS (-np):`** to **`4`** (or **`8`**).
  2. **Right-click** the gutter at **line `19`** (`data[i].coords.x = ...`) to open `ConditionalBreakpointDialog`:
     * Set **`Suspend:`** to **`All`** (or select **`Rank 3`** in `Server Rack` and pick **`Rank / Thread`**).
     * Type `rank == 3 && data[i].` into **`Condition:`** to trigger live `clangd` member completion (`coords`, `id`, `values`), complete it to `rank == 3 && data[i].coords.x > 0.0f`, and click **`Done`**.
  3. Also left-click **line `23`** (`printf(...)`) so all ranks stop after populating `data[0..2]`.
  4. Click **`▶ Run Target`**, then **`⏭ Continue`** (`F5`) to reach line `23`.
  5. Expand `data` -> `[1]` -> `coords` in **`Variables`**, and add `data[1].coords.x` and `data[2].values[4]` to **`WATCH`** (`DifferentialGrid`): Rank 0's `data[1].coords.x` (`1.1`) highlights in light red as the minority outlier against Ranks `1..N-1` (`1.5`).

---

### Lab 3: Collective Branch Divergence (`tutorial/collective_mismatch.cpp`)

* **Bug Demonstrated**: Rank-parity branch mismatch on `MPI_COMM_WORLD`: at **line `47`** (`if (rank % 2 == 0)`), even ranks (`0, 2, 4, 6`) enter `MPI_Bcast` (**line `50`**) while odd ranks (`1, 3, 5, 7`) take the `else` branch into `MPI_Barrier` (**line `60`**).
* **Compile**:
  ```bash
  mpicxx -g -O0 tutorial/collective_mismatch.cpp -o tutorial/collective_mismatch
  ```
* **Reproduce & Diagnose in GridLock**:
  1. Select **`collective_mismatch (MPI)`** and set **`RANKS (-np):`** to **`8`**.
  2. Set a breakpoint at **line `47`** (`if (rank % 2 == 0)`) or run directly and pause once stalled.
  3. Open **`MPI Diagnostics`** (`Alt+3`): `DeadlockAnalyzer` flags **`STATUS: COLL DIVERGENCE`** (`Collective Divergence: Rank 0 in MPI_Bcast vs Rank 1 in MPI_Barrier`).
  4. Add `cfg.rank_branch_tag` and `cfg.broadcast_seed` to **`WATCH`** (`100 + rank` on even ranks vs `200 + rank` on odd ranks; `0x4242` = `16962` on Rank 0 vs `0` elsewhere).
  5. Press **`Space`** in `DisassemblyView` to view the 2D CFG: even rank badges sit on the `MPI_Bcast` basic block while odd rank badges sit on the `MPI_Barrier` basic block.

---

### Lab 4: Silent `NaN` Halo Injection & Stencil Drift (`tutorial/ghost_cell_drift.cpp`)

* **Bug Demonstrated**: 5-step periodic 1D 3-point Jacobi stencil (`for (int step = 0; step < 5; ++step`, line `43`) where `faulty_rank` (`Rank 2` when `-np >= 4`) injects `std::numeric_limits<double>::quiet_NaN()` into `send_right` on `step == 3` (**lines `48-50`**), poisoning `Rank 3`'s `left_ghost` via `MPI_Sendrecv` (**lines `53-55`**) and propagating `nan` into interior `domain` cells.
* **Compile**:
  ```bash
  mpicxx -g -O0 tutorial/ghost_cell_drift.cpp -o tutorial/ghost_cell_drift
  ```
* **Reproduce & Diagnose in GridLock**:
  1. Select **`ghost_cell_drift (MPI)`** and set **`RANKS (-np):`** to **`8`** (or **`4`**).
  2. Right-click the gutter at **line `69`** (`stencil_center = domain[domain.size() / 2];`), set **`Ignore:`** to **`3`** hits (so execution runs steps `0, 1, 2` uninterrupted and pauses on `step == 3`), and click **`Done`**.
  3. Add `step`, `left_ghost`, `right_ghost`, and `stencil_center` to **`WATCH`** (`DifferentialGrid`) and click **`▶ Run Target`**.
  4. On `step == 3`, **`WATCH`** immediately shades `Rank 3`'s `left_ghost` (`nan`) in light red against the numeric values on the other ranks. Click **`⏭ Continue`** (`F5`) once more (`step == 4`) to watch `nan` spread into interior stencil elements.
  5. Open **`Memory View`** (`Alt+4`), enter `domain.data()` in **`Addr:`**, set **`Bytes:`** to `64` and **`Type:`** to `Double`, and click **`Diff Ranks`** (`Base: 0`, `Target: 3`) to inspect the IEEE-754 `0x7ff8000000000000` quiet-`NaN` bytes.

---

### Lab 5: Per-Rank Heap & Stack-Depth Imbalance (`tutorial/heap_imbalance.cpp`)

* **Bug Demonstrated**: Quadratic heap imbalance (`element_count = 4096 * (rank + 1) * (rank + 1)` at **lines `55-56`**, from 32 KB on Rank 0 up to 2,048 KB on Rank 7) paired with linear stack recursion imbalance (`target_stack_depth = 2 + rank * 2` at **lines `62-63`**, calling `recurseWithStackFrame` with a 256-byte `double stack_scratch[32]` array per frame).
* **Compile**:
  ```bash
  mpicxx -g -O0 tutorial/heap_imbalance.cpp -o tutorial/heap_imbalance
  ```
* **Reproduce & Diagnose in GridLock**:
  1. Select **`heap_imbalance (MPI)`**, set **`RANKS (-np):`** to **`8`**, and set a breakpoint at **line `41`** (`const double leaf_checksum = ...` at the recursion leaf).
  2. Click **`▶ Run Target`**.
  3. Click through **`RANK 0`** to **`RANK 7`** in **`Server Rack`** while watching:
     * **`CALL STACK`**: `Rank 0` shows 2 `recurseWithStackFrame` frames; `Rank 7` shows 16 frames.
     * **Bottom Status Bar**: `STACK` and `HEAP` grow quadratically/linearly from Rank 0 to Rank 7.
     * **`Memory View -> LIBRARY MEMORY MAP`**: Select different ranks in the profiler's rank dropdown (with `Track Heap Callers (LD_PRELOAD)` enabled) to compare `Heap / Call Site` attribution across ranks.

---

### Lab 6: Point-to-Point Tag Mismatch Stall (`tutorial/tag_mismatch_stall.cpp`)

* **Bug Demonstrated**: Rank 0 sends `struct TelemetryPacket pkt` (56 bytes) to Rank 1 with `send_tag = 100` (**line `53`**), while Rank 1 posts `MPI_Recv` from Rank 0 expecting `expected_tag = 200` (**line `61`**). Worker ranks `2..N-1` finish local work and park at `MPI_Finalize` (**line `73`**).
* **Compile**:
  ```bash
  mpicxx -g -O0 tutorial/tag_mismatch_stall.cpp -o tutorial/tag_mismatch_stall
  ```
* **Reproduce & Diagnose in GridLock**:
  1. Select **`tag_mismatch_stall (MPI)`** and set **`RANKS (-np):`** to **`4`**.
  2. Set breakpoints at **line `53`** (`MPI_Send`) and **line `61`** (`MPI_Recv`), or click **`▶ Run Target`** and then **`⏸ Pause Rank`** once Rank 1 hangs.
  3. Focus **`RANK 0`** vs **`RANK 1`** in **`Server Rack`** and inspect `send_tag` (`100`) vs `expected_tag` (`200`) in **`Variables`** (or right-click `pkt` -> **`Visualize in Pointer / Struct Graph...`** to inspect `TelemetryPacket` fields).

---

### Lab 7: Aligned SIMD Structs, `Diff Ranks` & Binary Export (`tutorial/simd_struct_watch.cpp`)

* **Behavior Demonstrated**: Populates `struct alignas(16) SimdMatrixPacket` (`112` bytes, lines `21-27`: `uint8_t magic[4]` = `{0xCA, 0xFE, rank, size}`, `uint32_t packet_id`, `uint64_t flags`, `float matrix[4][4]`, `double lane_Norms[4]`), computes `local_trace` (**line `60`**), and reduces `global_trace` via `MPI_Allreduce` (**line `62`**).
* **Compile**:
  ```bash
  mpicxx -g -O0 tutorial/simd_struct_watch.cpp -o tutorial/simd_struct_watch
  ```
* **Inspect in GridLock**:
  1. Select **`simd_struct_watch (MPI)`**, set **`RANKS (-np):`** to **`4`**, and set breakpoints at **line `62`** (`MPI_Allreduce`) and **line `64`** (`std::printf`).
  2. Click **`▶ Run Target`**. When paused at line `62`, add `local_trace` and `packet.packet_id` to **`WATCH`**.
  3. Open **`Memory View`** (`HEX DUMP`), enter `&packet` in **`Addr:`**, set **`Bytes:`** to `112`, and click **`Read`**:
     * Byte offsets `0x00..0x01` show `CA FE`, followed by rank ID (`0x02`) and world size (`0x03`).
     * Set **`Base:`** to `0` and **`Target:`** to `1`, then click **`Diff Ranks`** to highlight every rank-dependent header and matrix byte in red.
     * Set **`Type:`** to `Float`, click **`Export`**, and save a `.csv` or `.bin` dump of the packet.
  4. Click **`⏭ Continue`** (`F5`) to stop at line `64` and verify `global_trace` matches across all ranks.

---

## Part 2: Auxiliary Tutorial Files & `--tutorial-mode`

Launch `./builddir/gridlock --tutorial-mode` to open the modal **`Interactive Tutorials`** launcher (`TutorialDialog`), or open these 5 standalone targets directly:

| Source File | Compile Command | Ranks (`-np`) | Key Lines & Purpose |
| :--- | :--- | :--- | :--- |
| `tutorial/deadlock_demo.c` | `mpicc -g -O0 tutorial/deadlock_demo.c -o tutorial/deadlock_demo` | `2` | Lines `25` & `28`: Mutual synchronous send (`MPI_Ssend`) deadlock between Rank 0 and Rank 1. |
| `tutorial/register_demo.cpp` | `mpicxx -g -O0 tutorial/register_demo.cpp -o tutorial/register_demo` | `1` or `2` | Lines `24-31`: Inline x86 assembly (`mov` / `add` into `%eax`); step with **`↷ Step Inst`** while watching `rax` mutate in **`Registers`**. |
| `tutorial/memview_demo.cpp` | `mpicxx -g -O0 tutorial/memview_demo.cpp -o tutorial/memview_demo` | `1` or `2` | Line `25`: Allocates 256-byte heap block filled with `0xAA`, `0xBB`, and `"Hello, MemView!"` at `+16`; read `memory_block` in **`Memory View`**. |
| `tutorial/memview_diff_demo.cpp` | `mpicxx -g -O0 tutorial/memview_diff_demo.cpp -o tutorial/memview_diff_demo` | `2` | Line `30`: `int array[10]` where Rank 1 sets `array[5] = 999` (line `16`); inspect `array` (`40` bytes) with **`Diff Ranks`** (`0` vs `1`). |
| `tutorial/mpi_mm.c` | `mpicc -g -O0 tutorial/mpi_mm.c -o builddir/mpi_mm` *(built by default in Meson)* | `3` or `4` | Lines `84, 99, 114, 130`: Master/worker (`FROM_MASTER=1`, `FROM_WORKER=2`) `500x500` dense `double` matrix multiplication. |

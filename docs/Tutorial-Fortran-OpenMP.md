# Hybrid MPI + OpenMP Fortran Debugging Tutorial

This hands-on tutorial demonstrates how to debug a hybrid **MPI + OpenMP Fortran** scientific code ([`tutorial/stencil_omp.f90`](../tutorial/stencil_omp.f90)) in GridLock. You will practice navigating both MPI ranks and OpenMP worker threads, stepping a single OpenMP thread cleanly with `scheduler-locking`, evaluating 1-based Fortran slice expressions, and inspecting column-major 2D arrays in GridLock's **Array & Field Plotter** (`Ctrl+Alt+G`).

---

## 1. Overview & Prerequisites

`stencil_omp.f90` allocates two `4 x 6` double-precision (`real(real64)`) Fortran arrays, `field` and `updated`, on every MPI rank. Each element is initialized to an unmistakable coordinate-encoded value:

```fortran
field(i, j) = real(rank * 1000 + j * 100 + i, real64)
```

| Rank | `field(1:4, 1)` (Col 1) | `field(1:4, 2)` (Col 2) | `field(1:4, 3)` (Col 3) |
| :--- | :--- | :--- | :--- |
| **Rank 0** | `101, 102, 103, 104` | `201, 202, 203, 204` | `301, 302, 303, 304` |
| **Rank 1** | `1101, 1102, 1103, 1104` | `1201, 1202, 1203, 1204` | `1301, 1302, 1303, 1304` |

Inside an `!$omp parallel default(none)` region, OpenMP threads compute a 5-point stencil over the interior elements `updated(2:3, 2:5)` and accumulate `local_sum`, followed by an `MPI_Reduce` to Rank 0.

### Required Tools & Recommended Debug Flags

* **GDB >= 12** (for complete DWARF5 Fortran array descriptor and OpenMP thread frame support).
* **MPI Launcher**: `mpirun` or `mpiexec` (OpenMPI, MPICH, or Intel MPI).
* **Fortran Compiler Flags**:
  * **GNU Fortran (`gfortran` / `mpif90`)**:
    ```bash
    mpif90 -g -O0 -fopenmp -Wall -Wextra -fcheck=bounds,do,mem stencil_omp.f90 -o stencil_omp
    ```
  * **Intel Fortran (`ifx` / `mpiifx`)**:
    ```bash
    mpiifx -g -O0 -qopenmp -warn all stencil_omp.f90 -o stencil_omp
    ```

> [!IMPORTANT]
> Always build with `-g -O0` when stepping through OpenMP regions so thread-private variables (`tid`, `i`, `j`) and array descriptors are not optimized into registers or folded away.

---

## 2. Building & Launching in GridLock

### Local Multi-Rank Launch (`mpirun`)

1. Build the tutorial executable from the repository root:
   * **Automatic Meson target** (built automatically by `ninja -C builddir` whenever `mpif90` is on `PATH`, producing `builddir/stencil_omp`), or compile directly into `tutorial/`:
   ```bash
   mpif90 -g -O0 -fopenmp -Wall -Wextra -fcheck=bounds,do,mem tutorial/stencil_omp.f90 -o tutorial/stencil_omp
   ```
   *(For Intel `ifx`: `mpiifx -g -O0 -qopenmp -warn all tutorial/stencil_omp.f90 -o tutorial/stencil_omp`.)*
2. Launch GridLock with `OMP_NUM_THREADS=4` exported in your shell environment (so each MPI rank spawns 4 OpenMP worker threads):
   ```bash
   OMP_NUM_THREADS=4 gridlock
   ```
3. Select **`stencil_omp (Fortran MPI+OpenMP)`** from the **`TARGET:`** dropdown (or open [`tutorial/stencil_omp.f90`](../tutorial/stencil_omp.f90) via **File -> Open Source File...** and load `tutorial/stencil_omp` or `builddir/stencil_omp` via **TARGET: -> Open ELF / Binary...**).
4. Set **RANKS (-np):** on the top toolbar to **`2`** (giving `2 MPI ranks x 4 OpenMP threads = 8` concurrent workers).

### Optional Slurm Batch Launch

To run on a Slurm cluster with `2` tasks and `4` CPUs per task:
1. Enable **Show "Submit Slurm Job..." in File menu** in **Edit -> Preferences -> HPC / Cluster** (experimental).
2. Open **Edit -> Preferences -> HPC Integration**, set **Tasks/Node** to `2`, **CPUs / Task** to `4`, and export `OMP_NUM_THREADS=$SLURM_CPUS_PER_TASK` inside the batch script template.
3. Use **File -> Submit Slurm Job...** to submit and attach to the allocated compute node(s).

---

## 3. Multi-Rank & OpenMP Thread Navigation

1. In `stencil_omp.f90`, click the left gutter to place a breakpoint on line **`51`** (inside the `!$omp do` loop where `updated(i, j)` is assigned) and a second breakpoint on line **`60`** (`call MPI_Reduce(...)`).
2. Click **`▶ Run Target`** (`Alt+R`). Both MPI ranks stop inside the parallel region.
3. Inspect the **Server Rack** (bottom-right) to switch between **`RANK 0`** and **`RANK 1`**:
   * On **`RANK 0`**, `field(1, 1)` is `101.0`.
   * On **`RANK 1`**, `field(1, 1)` is `1101.0`.
4. Open the **Threads** tab in the right-hand dock (alongside **Call Stack** and **Variables**):
   * You will see 4 active threads per rank: Thread 1 (the main thread) and 3 OpenMP worker threads running inside the compiler-outlined frame (for example, `stencil_omp._omp_fn.0` under `gfortran`).
   * Click between threads in the **Threads** dock to inspect thread-private scalars (`tid`, `i`, `j`) alongside shared arrays (`field`, `updated`). Notice that each worker thread holds a distinct `tid` (`0..3`) and column index `j` (`2..5`).

---

## 4. Stepping Without Thread Jumping (`scheduler-locking`)

When multiple OpenMP threads execute the same `!$omp do` loop concurrently, pressing **Step Over (`F10`)** allows sibling threads to hit the same breakpoint and steal debugger focus.

To step a single OpenMP worker cleanly line by line:

1. Select the thread you want to trace in the **Threads** dock (for example, the thread where `tid == 1`).
2. Open the **GDB Console** tab at the bottom of GridLock and lock the GDB thread scheduler to the active thread:
   ```gdb
   set scheduler-locking on
   ```
3. Press **Step Over (`F10`)** several times to step through `i = 2` and `i = 3` on that worker alone without other threads advancing.
4. To inspect `tid` across every OpenMP thread on the focused rank in one command, run:
   ```gdb
   thread apply all print tid
   ```
5. Before continuing to the implicit OpenMP barrier at `!$omp end do` or `MPI_Reduce` on line **`60`**, unlock the scheduler so sibling threads can reach the barrier (otherwise the locked thread will wait forever at `!$omp end parallel`):
   ```gdb
   set scheduler-locking off
   ```

> [!WARNING]
> Always run `set scheduler-locking off` (or `set scheduler-locking step`) before continuing past an `!$omp barrier`, `!$omp end do`, or `!$omp end parallel` directive so sibling OpenMP threads are allowed to join the barrier.

---

## 5. Inspecting 1-Based Column-Major Fortran Arrays

When GDB stops inside `stencil_omp.f90`, GridLock detects `SourceLanguage::Fortran` via `-gdb-show language` (with `.f90` extension fallback) and switches array inspection to **Fortran (1-based, column-major)** mode.

### 1-Based Fortran Slices in Watch & Evaluator

In the **Variables / Watch** dock or **Evaluator** tab, evaluate native 1-based inclusive Fortran slices:
* `field(1:4, 2)` -> displays Column 2 on Rank 0: `(201, 202, 203, 204)` (or `(1201, 1202, 1203, 1204)` on Rank 1).
* `updated(2:3, 2:5)` -> displays the `2 x 4` updated interior sub-matrix.

### Column-Major Verification in `ArrayPlotDialog` (`Ctrl+Alt+G`)

1. Press **`Ctrl+Alt+G`** (or choose **Tools -> Array & Field Plotter...**) while paused at line **`60`** (`call MPI_Reduce(...)`).
2. Confirm the footer badge displays **`Fortran (1-based, column-major)`**, the memory-order selector is set to **`Fortran Col`**, and the offset spinbox lower bound is **`1`**.
3. Enter **`field`** in **Expr:**, select **`double`**, set **`Ny = 4`** (rows) and **`Nx = 6`** (columns), and click **Plot / Refresh**.
4. Switch to the **Tensor Table** view:
   * Row and column headers are 1-based: `(1)..(4)` and `(1)..(6)`.
   * Cell `(1, 1)` shows `101` at linear offset `[0]`.
   * Cell `(4, 1)` shows `104` at linear offset `[3]`.
   * Cell `(1, 2)` shows **`201`** at linear offset **`[4]`** (`nrows = 4`), confirming contiguous column-major storage where consecutive rows occupy adjacent memory addresses.
5. Switch the **Rank** dropdown from **`Rank 0`** to **`Rank 1`** (or click **`RANK 1`** in the **Server Rack**) before stepping over `MPI_Reduce`: cell `(1, 2)` updates to **`1201`**, letting you verify per-rank stencil output before the collective reduction combines `local_sum` into `global_sum`.

# 🌐 Cluster & Remote Execution (HPC Workflows)

GridLock is designed to run locally on your Wayland workstation while seamlessly orchestrating and monitoring parallel execution state across remote supercomputing clusters.

## 🔍 Managing Parallel Execution State

Debugging an application running across hundreds of distributed nodes requires surgical precision. GridLock provides unified tools to monitor the health of the entire cluster while easily zooming into the state of a single rank.

### The Scope Dropdown: Isolating MPI Ranks
When a parallel job halts, you often only care about the state of the specific rank that triggered a crash or hit a breakpoint. 

Instead of overwhelming your network by fetching data for the entire cluster simultaneously, use the **Scope Dropdown** located at the top of the main toolbar. 
* Selecting a specific rank (e.g., `Rank 42`) instantly isolates your current view. 
* The Source Code Editor, Variables Grid, and Call Stack will immediately synchronize to reflect *only* the memory and execution context of that specific rank. 
* This allows you to inspect isolated anomalies smoothly without waiting for unnecessary data payloads from healthy ranks.

### Rank Diagnostics: LEDs and Timers
To provide a macro-level view of your cluster's health, GridLock features a real-time dashboard displaying a grid of diagnostic LEDs representing every active MPI rank in your allocation.

* **Execution State:** A green LED indicates a rank is running, while a yellow/red LED indicates a rank is paused or halted.
* **Execution Timers:** Each rank features a live execution timer. These timers are invaluable for spotting **load imbalances** and silent desyncs. 
* *Example:* If you notice that Rank 0's timer has stopped (indicating it hit a synchronization barrier) while Rank 1's timer continues to run for several minutes, you have instantly identified a severe compute imbalance or a potential deadlock without needing to read a single line of code.

## 🔑 Configuring SSH Keys for Remote Login Nodes

GridLock uses native SSH multiplexing to communicate with remote login nodes.

1. Navigate to **Preferences > HPC Settings**.
2. Add a new **Remote Host** (e.g., `login.cluster.edu`).
3. Provide the path to your private key (e.g., `~/.ssh/id_ed25519`).

> [!IMPORTANT]
> Password-based interactive SSH is **not supported** for automated debugging. You must configure passwordless key-based authentication.

## 📦 Spack Manager

Scientific software relies heavily on [Spack](https://spack.io/). GridLock integrates a native Spack environment manager.

1. Open the **HPC Console** dock.
2. Navigate to the **Spack** tab.
3. GridLock queries `spack env list` on the remote node.
4. Select your target environment. GridLock automatically injects `spack env activate <env>` into the pre-launch environment variables for the remote backend.

## 🚀 SLURM Batch Submission

You rarely run MPI jobs on a login node. GridLock natively understands SLURM's `sbatch` and `srun`.

In your Run Configuration, you can define a SLURM template:

```bash
#!/bin/bash
#SBATCH --job-name=gridlock_debug
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=8
#SBATCH --gpus=4
#SBATCH --time=00:30:00

srun --mpi=pmix {FILE}
```

*   **The `{FILE}` Token**: GridLock automatically replaces the `{FILE}` token with the compiled debuggee executable path.
*   **GPU Requests**: Modify the `#SBATCH --gpus=X` directive in the UI to dynamically allocate required resources.
*   GridLock automatically captures the resulting Job ID and attaches the debugger to the spawned `srun` instances across all nodes.

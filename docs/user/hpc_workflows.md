# 🌐 Cluster & Remote Execution (HPC Workflows)

GridLock is designed to run locally on your Wayland workstation while controlling execution seamlessly on remote supercomputing clusters.

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
4. Select your target environment. GridLock automatically injects `spack env activate <env>` into the pre-launch environment variables for the GDB backend.

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
*   GridLock automatically captures the resulting Job ID and attaches the DAP coordinator to the spawned `srun` instances across all nodes.

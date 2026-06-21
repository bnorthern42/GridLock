# SLURM & Spack Integration

Deploying and debugging applications on large-scale supercomputers requires interfacing with job schedulers and environment management systems. GridLock natively integrates these workflows into the IDE.

## Remote SSH Targets

To debug applications running on a remote cluster without forwarding heavy X11/Wayland sessions, GridLock supports remote debugging via SSH.

* **Configuration**: In the Target Configuration menu, define the remote hostname, user, and SSH key path. 
* **Execution**: GridLock orchestrates the launch of `gdbserver` or a headless debugging coordinator on the remote node and connects the local GUI over a secure, port-forwarded channel.

## SLURM Batch Job Management

For compute clusters managed by SLURM, GridLock can generate, submit, and monitor batch jobs directly from the UI.

* **Bash Templates**: You can define customizable bash templates (e.g., specifying `#SBATCH --nodes=4`, QoS queues, and GPU allocations) within the IDE.
* **Submission Workflow**: GridLock injects the debugger launch commands into your template and submits the job via `sbatch`.
* **Monitoring**: The IDE monitors the job queue via `squeue`. Once the job is allocated and begins executing, GridLock automatically attaches the UI to the allocated compute nodes.

## Integrated Spack GUI

HPC environments often rely on [Spack](https://spack.io/) to manage complex software dependency graphs. GridLock provides an integrated Spack GUI to manage your build and runtime environments.

* **Environment Browsing**: Browse available Spack environments, installed packages, and compiler toolchains directly within the IDE.
* **Module Loading**: Visually select the required Spack modules for your debugging session. GridLock automatically sources the Spack environment and loads the necessary modules (equivalent to `spack load <pkg>`) before launching the MPI runtime, ensuring your binary links against the correct shared libraries.

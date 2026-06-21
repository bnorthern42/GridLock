# GridLock - A High-Performance MPI Graphical Debugger

[![Build Status](https://github.com/bnorthern42/GridLock/actions/workflows/build.yml/badge.svg)](https://github.com/bnorthern42/GridLock/actions)

## Overview
GridLock is a specialized Integrated Development Environment (IDE) built entirely around the complexities of debugging parallel MPI (Message Passing Interface) applications. It acts as a graphical frontend to GDB/MI, allowing developers to inspect state, track variable execution, and visualize real-time x86_64 disassembly across multiple independent MPI ranks concurrently.

## Disclaimer / WIP Warning
**GridLock is currently in an Alpha/WIP state.** It is a personal project intended for research use. Use at your own risk. Features are subject to change and stability is not guaranteed.

## Prerequisites
To compile and run GridLock, you must have the following dependencies installed on your system:
- **C++23 Compiler:** (GCC 13+ or Clang 16+)
- **Build System:** Meson and Ninja
- **UI Framework:** Qt6 (Widgets, Core, Gui)
- **MPI Implementation:** MPICH or OpenMPI
- **Debugger Backend:** GDB (GNU Debugger) with MI3 support
- **Remote Target Server:** `gdbserver`
- **Language Server:** `clangd` (for semantic code intelligence and LSP support)

## Build Instructions
1. Clone the repository:
   ```bash
   git clone git@github.com:bnorthern42/GridLock.git
   cd GridLock
   ```
2. Setup the build directory using Meson:
   ```bash
   meson setup build
   ```
3. Compile the project using Ninja:
   ```bash
   ninja -C build
   ```
4. Run the executable:
   ```bash
   ./build/gridlock
   ```
   To run GridLock in test mode (useful for running tests without a physical MPI backend):
   ```bash
   ./build/gridlock --test-mode
   ```

## Key Features
- **Multi-Rank State Inspection:** Seamlessly monitor and step through multiple MPI processes simultaneously.
- **Conditional Breakpoints:** `Ctrl+Click` the gutter line numbers in the code editor to insert conditional breakpoints (e.g., `i == 5`). If a breakpoint already exists, `Ctrl+Click` updates its condition, while a regular click toggles it off.
- **Live Register View:** A dedicated `Registers` tab continuously tracks and updates CPU register states in real-time alongside execution.
- **MemView Hex Dump:** Inspect raw memory chunks by supplying variable names or absolute pointer addresses directly into GDB, presented in a clean Hex/ASCII format.
- **Semantic Hover Tooltips:** Powered by Clangd and GDB/MI, hover over any active variable in the editor to instantly see its live value.
- **HPC Orchestration:** Submit and track SLURM batch jobs directly from the IDE using customizable bash templates (with built-in GPU request configurations).
- **Remote Environment Management:** Integrated Spack GUI frontend acting as an HPC console, allowing you to browse, search, and monitor packages running on remote clusters.
- **Remote SSH Configuration:** Configure secure access keys, target hostnames, and remote environments centrally from the Preferences dialog.


## Example Workflow
1. Load a target MPI source file (e.g., `mpi_mm.c`).
2. Set a breakpoint by clicking the gutter.
3. Click **▶ Run Target** in the main toolbar.
4. Your program hits line 83 and pauses.
5. You open the Memory tab, type `&offset` (or the raw hex address `0x7fffffffc498`), and hit Read Memory.
6. It instantly pulls 256 bytes of raw application memory directly from GDB and formats it into a beautiful, classic Hex/ASCII grid.

## Roadmap
For upcoming features and architectural milestones, please see our [ROADMAP.md](ROADMAP.md).

## Contributing
Please see [CONTRIBUTING.md](CONTRIBUTING.md) for information on setting up the environment and submitting issues. All community interactions must adhere to our [Code of Conduct](CODE_OF_CONDUCT.md).

## Acknowledgments & Attributions
This project makes use of the excellent [toml++](https://github.com/marzer/tomlplusplus) library by mark gillard for parsing TOML configuration files.

GridLock was also inspired by and built upon the ideas from the following projects:
- **[GNU DDD](https://www.gnu.org/software/ddd/)**: For pioneering visual data display in debugging.
- **[KDbg](https://www.kdbg.org/)**: For early inspiration on KDE/Qt-based GDB frontend wrappers.
- **[gdbgui](https://www.gdbgui.com/)**: For demonstrating the power of browser-based debug visualization, which inspired our native `DifferentialGrid`.
- **[Zeal](https://zealdocs.org/)**: For the open-source `.docset` standard and offline documentation workflows that power our Reference Manual.

We also want to give a brief nod to the **Spack**, **SLURM**, and **OpenMPI** communities for the HPC tools GridLock orchestrates.

## License
This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.


## Other

GridLock currently supports debugging a single file open/edit at a time and does not yet support debugging hybrid MPI/OpenMP programs. These are active goals for the future.
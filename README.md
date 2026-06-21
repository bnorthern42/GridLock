# GridLock - A High-Performance MPI Graphical Debugger

[![Build Status](https://github.com/bnorthern42/GridLock/actions/workflows/build.yml/badge.svg)](https://github.com/bnorthern42/GridLock/actions)

GridLock is a Wayland-native, graphical GDB/MI frontend specifically designed for numerical methods and parallel MPI (Message Passing Interface) applications. It provides a specialized environment tailored to the complexities of parallel computing, allowing developers to inspect state, track execution, and visualize real-time x86_64 disassembly across multiple independent MPI ranks concurrently.

> [!WARNING]
> **GridLock is currently in an Alpha/WIP state.** It is a personal project intended for research use. Use at your own risk. Features are subject to change and stability is not guaranteed.

---

## Core Capabilities

### Multi-Rank Debugging
* **Multi-Rank State Inspection:** Monitor and step through multiple independent MPI processes simultaneously within a unified interface.
* **Visual Conditional Breakpoints:** Define, insert, and update conditional breakpoints directly in the code editor's gutter.
* **Live Register View:** A dedicated CPU registers panel tracks and updates register states in real-time alongside target execution.
* **Memory Hex/ASCII Dump:** Inspect raw memory chunks by supplying variable names or absolute pointer addresses directly to GDB, rendered in a structured Hex/ASCII format.

### HPC Orchestration
* **SLURM Job Integration:** Submit and track SLURM batch jobs directly from the IDE using customizable bash templates, with built-in support for GPU allocation.
* **Spack Environment Integration:** Browse, search, and monitor Spack packages running on remote clusters through an integrated graphical frontend.
* **Remote Environment & SSH Management:** Centrally configure hostnames, access keys, and remote execution environments from the preferences dialog.

### Developer Experience
* **Semantic Hover Tooltips:** Powered by a `clangd` language server backend and GDB/MI to instantly view live variable values on hover.
* **Offline Reference Manual:** Access local documentation and system man pages via an integrated viewer that supports the open-source `.docset` standard.

---

## Getting Started

### Prerequisites

To compile and run GridLock, ensure the following dependencies are installed on your system:

* **C++23 Compiler:** GCC 13+ or Clang 16+
* **Build System:** Meson and Ninja
* **UI Framework:** Qt6 (Widgets, Core, and Gui modules)
* **MPI Implementation:** MPICH or OpenMPI
* **Debugger Backend:** GDB (GNU Debugger) with MI3 support
* **Remote Target Server:** `gdbserver`
* **Language Server:** `clangd` (for semantic code intelligence and LSP support)

### Building the Project

1. **Clone the repository:**
   ```bash
   git clone git@github.com:bnorthern42/GridLock.git
   cd GridLock
   ```

2. **Configure the build directory:**
   ```bash
   meson setup build
   ```

3. **Compile the application:**
   ```bash
   ninja -C build
   ```

4. **Install the application (System-wide):**
   ```bash
   sudo meson install -C build
   ```

### Running the Application

* **Standard Execution:**
  To run GridLock under normal operating conditions:
  ```bash
  ./build/gridlock
  ```

* **Test Mode:**
  To run GridLock in test mode, which simulates the MPI environment without requiring a physical MPI backend:
  ```bash
  ./build/gridlock --test-mode
  ```

---

## Known Limitations & Roadmap

### Known Limitations
* **Single-File Scope:** GridLock currently supports debugging and editing a single file at a time.
* **No Hybrid Parallelism Support:** There is currently no support for debugging hybrid MPI/OpenMP applications.

### Future Roadmap
For upcoming features, architectural milestones, and proposed implementations, please refer to [ROADMAP.md](ROADMAP.md).

---

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
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

## Roadmap
For upcoming features and architectural milestones, please see our [ROADMAP.md](ROADMAP.md).

## Contributing
Please see [CONTRIBUTING.md](CONTRIBUTING.md) for information on setting up the environment and submitting issues. All community interactions must adhere to our [Code of Conduct](CODE_OF_CONDUCT.md).

## License
This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

# Contributing to GridLock

Thank you for your interest in contributing to GridLock! We are aiming to build a high-performance, professional-grade MPI graphical debugger.

## Development Environment Setup

To contribute to GridLock, you need to configure the development environment.

### 1. System Requirements
- **C++23 Compiler:** GCC 13+ or Clang 16+
- **Meson & Ninja:** Ensure the latest stable releases are installed.
- **Qt6:** Development packages for `Core`, `Gui`, `Widgets`, and `Test`.
- **MPI:** OpenMPI or MPICH development headers.
- **GDB:** GNU Debugger with MI3 support.

### 2. Building the Project
Clone the repository and set up the build environment using Meson:
```bash
git clone git@github.com:bnorthern42/GridLock.git
cd GridLock
meson setup build
ninja -C build
```

### 3. Running Tests
We enforce testing for core architectural changes. Before opening a Pull Request, ensure all tests pass:
```bash
meson test -C build --print-errorlogs
```

## Reporting Bugs
GridLock is a complex state machine that interfaces heavily with GDB/MI and MPI execution states. When reporting a bug, it is critical that we receive an exact replication of the environment variables and the backend output.

Please use the [ISSUE_TEMPLATE.md](ISSUE_TEMPLATE.md) format when submitting bug reports. 

Make sure to include:
1. Your exact **MPI Implementation Version** (e.g., OpenMPI 4.1.5).
2. The **Number of Ranks** you were debugging at the time of failure.
3. The raw **GDB/MI Output Logs** (which can be scraped from the `Compiler Terminal` view).

## Code Style
GridLock aims to maintain a strict, professional C++23 codebase. Please match the existing code style, utilize modern C++ patterns (e.g., smart pointers, `auto`, `std::unordered_map`), and thoroughly comment complex GDB/MI parsing logic.

Thank you for helping stabilize GridLock!

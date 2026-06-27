# GridLock - High-Performance MPI Graphical Debugger (v0.5.2)

![Build Status](https://img.shields.io/github/actions/workflow/status/bnorthern42/GridLock/release.yml?style=for-the-badge)
![C++23](https://img.shields.io/badge/C++-23-blue.svg?style=for-the-badge&logo=c%2B%2B)
![Qt6](https://img.shields.io/badge/Qt-6-41CD52.svg?style=for-the-badge&logo=qt)
![Wayland/Niri](https://img.shields.io/badge/Wayland-Niri_Native-orange.svg?style=for-the-badge)
![Meson/Ninja](https://img.shields.io/badge/Build-Meson%20|%20Ninja-blue.svg?style=for-the-badge)
![LLDB/DAP](https://img.shields.io/badge/Backend-LLDB%20|%20DAP-purple.svg?style=for-the-badge)

GridLock is a Wayland-native, Qt6 graphical MPI debugger powered by the Debug Adapter Protocol (DAP). It provides a highly specialized environment tailored to the complexities of parallel computing, allowing developers to inspect state, track execution, and visualize real-time state across multiple independent MPI ranks concurrently.

> [!WARNING]  
> **GridLock is currently in an Beta/WIP state.** It is a personal project intended for research use. Features are subject to change and stability is not guaranteed.

---

## 🚀 Key Features

* **Lazy-Loading DAP Variable Trees:** Dynamically explore complex structs, pointers, and arrays efficiently without locking the UI.
* **Integrated HPC Console & Hex Dump:** Seamlessly read raw memory via base64 DAP chunks, with 1:1 hardware-to-UI Hex dump synchronization.
* **Vim-Style Chorded Shortcuts:** Leverage advanced command patterns (`Alt+B`, `Ctrl+W`) tailored for rapid iteration without mouse dependency.
* **Project-Scoped Configuration (`.gridlock/`):** Each project maintains its own `.gridlock/settings.toml` to store execution parameters (binary path, rank count, MPI arguments) and `.gridlock/workspace.toml` for layout state. This isolates project configuration from the global environment and the user's home directory.
* **TOML Session Persistence:** Save and instantly restore robust HPC debug profiles (binary arguments, OpenMPI rank counts, watchlists).
* **Multi-Rank State Inspection:** Step through multiple independent MPI processes simultaneously within a unified, responsive interface.
* **Semantic Hover Tooltips:** Powered by `clangd` language server and DAP to instantly view live variable values on hover.
* **HPC Orchestration (SLURM & Spack):** Submit remote batch jobs and monitor Spack environments right from the IDE.
* **Deadlock Detector (Barrier/Wait Analyzer):** Automatically parses `-stack-list-frames` to flag active MPI ranks blocked in collective synchronization.
* **Floating-Point Exception (FPE) Trapper:** Halts execution instantly on divergent NaNs or infinities.
* **Conditional Breakpoint Expressions:** Dynamically halt loops using evaluable C++ statements (e.g., `i == 100 && rank == 0`).
* **Value-Change Visual Highlighting:** Fast identification of mutated variables and registers across steps via instant color shifts.
* **Raw Memory Export & Multi-Rank Broadcasts:** Deep hardware state inspection and mass cluster commanding.
* **Zero-Copy Memory Pipelines:** Extract large array states directly from the target kernel via lightning-fast `process_vm_readv` bypasses.
* **Memory Diff Engine:** Simultaneous, zero-copy memory extraction (`process_vm_readv`) across multiple MPI ranks with visual byte-level discrepancy highlighting.
* **Session Bookmarking:** Serialize active breakpoints, watched variables, and UI states to lightweight TOML files for persistent workspaces.

> ⚠️ **EXPERIMENTAL FEATURE (USE AT YOUR OWN RISK):** SLURM job execution is currently in a basic, feature-incomplete state. While GridLock can submit jobs to the cluster, advanced lifecycle management—such as detached session rehydration, automatic `squeue` polling, and automated `scancel` cleanup—is not yet implemented. Long-running or pending jobs may require manual management via the terminal to prevent burning allocation hours.

---

## 🛠️ Getting Started

### Prerequisites
* **C++23 Compiler:** GCC 13+ or Clang 16+
* **Build System:** Meson and Ninja
* **UI Framework:** Qt6 (Widgets, Core, and Gui modules)
* **MPI Implementation:** MPICH or OpenMPI
* **Debugger Backend:** DAP-compliant debug adapter (e.g., `lldb-vscode` or `lldb-dap`)
* **Language Server:** `clangd`

### Building from Source

```bash
git clone git@github.com:bnorthern42/GridLock.git
cd GridLock
meson setup build
ninja -C build
sudo meson install -C build
```

### Running the IDE

```bash
# Standard execution
./build/gridlock

# Test mode (simulates MPI/DAP environment)
./build/gridlock --test-mode

# Tutorial mode (experimental, WIP feature for testing debugger integrations)
# Note: This feature currently only works when building locally and is not supported in the AppImage.
./build/gridlock --tutorial-mode
```

### Deployment & AppImage Packaging

GridLock explicitly relies on **`CQtDeployer`** (replacing `linuxdeploy`) to package and deploy native binaries. This guarantees proper bundling of all Qt6 Wayland and XCB QPA payloads, ensuring native Wayland support without crashing on missing libraries. AppImages are deployed automatically via GitHub Actions standard artifact pipelines.

### Testing with Docker

> [!IMPORTANT]
> **`docker-compose up` is the officially supported method for running the test suite.** Host kernel hardening (e.g., `ptrace` scope restrictions, missing `seccomp` overrides) will cause tests that rely on `ptrace` and `process_vm_readv` to fail silently or be blocked outright. Do not rely on `meson test` alone to validate the full pipeline.

To run the full test suite in a correctly provisioned sandbox:

```bash
docker-compose up --build
```

The Docker environment is configured with `SYS_PTRACE` capabilities and `seccomp:unconfined`, ensuring that the LLDB/DAP adapter, `process_vm_readv` memory extraction, and `ptrace`-based syscall interception all function as they would on a permissive development kernel.

---

## 📚 Documentation

The GridLock documentation is divided into two primary suites:

* 🧑‍💻 **[User Documentation](docs/user/README.md):** Guides on layout, MPI workflow, advanced debugging, and cluster configuration.
* 🛠️ **[Developer Documentation](docs/developer/README.md):** Architectural overviews, build instructions, Wayland QPA specifics, and testing pipelines.

---

## 🗺️ Roadmap Snapshot

GridLock's development is broken into iterative phases. For a detailed breakdown, see [ROADMAP.md](ROADMAP.md).

* **Phase 1–5:** ✅ Complete — Stability, UI, Advanced Debugger, IDE Experience, DAP Refactor
* **Phase 6:** ✅ Complete — Zero-Copy Multi-Rank Memory Diff Engine & Session State Bookmarking
* **Phase 6.5:** ✅ Complete — Workspace Configuration (`.gridlock/`) & Dockerized TDD Pipeline
* **Phase 11:** ✅ Complete — Deployment & AppImage Packaging via CQtDeployer
* **Phase 7:** Upcoming — Cross-Language Variable Inspector
* **Phase 8:** Upcoming — Alternative Debugger Backends
* **Phase 9:** Upcoming — The Plugin Marketplace
* **Phase 10:** Upcoming — Advanced Cluster Lifecycle & Detached Sessions

---

## 🤝 Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for environment setup and issue submission. All community interactions must adhere to our [Code of Conduct](CODE_OF_CONDUCT.md).

## 🏆 Attributions

GridLock orchestrates OpenMPI, SLURM, and Spack, relying on `tomlplusplus` for configuration. It draws inspiration from GNU DDD, KDbg, gdbgui, and Zeal.

## 📄 License

Licensed under the MIT License. See [LICENSE](LICENSE).

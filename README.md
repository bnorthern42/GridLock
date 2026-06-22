# GridLock - High-Performance MPI Graphical Debugger

![Build Status](https://img.shields.io/github/actions/workflow/status/bnorthern42/GridLock/build.yml?branch=main&style=for-the-badge)
![C++23](https://img.shields.io/badge/C++-23-blue.svg?style=for-the-badge&logo=c%2B%2B)
![Qt6](https://img.shields.io/badge/Qt-6-41CD52.svg?style=for-the-badge&logo=qt)
![Wayland/Niri](https://img.shields.io/badge/Wayland-Niri_Native-orange.svg?style=for-the-badge)
![Meson/Ninja](https://img.shields.io/badge/Build-Meson%20|%20Ninja-blue.svg?style=for-the-badge)
![LLDB/DAP](https://img.shields.io/badge/Backend-LLDB%20|%20DAP-purple.svg?style=for-the-badge)

GridLock is a Wayland-native, Qt6 graphical MPI debugger powered by the Debug Adapter Protocol (DAP). It provides a highly specialized environment tailored to the complexities of parallel computing, allowing developers to inspect state, track execution, and visualize real-time state across multiple independent MPI ranks concurrently.

> [!WARNING]  
> **GridLock is currently in an Alpha/WIP state.** It is a personal project intended for research use. Features are subject to change and stability is not guaranteed.

---

## 🚀 Key Features

* **Lazy-Loading DAP Variable Trees:** Dynamically explore complex structs, pointers, and arrays efficiently without locking the UI.
* **Integrated HPC Console & Hex Dump:** Seamlessly read raw memory via base64 DAP chunks, with 1:1 hardware-to-UI Hex dump synchronization.
* **Vim-Style Chorded Shortcuts:** Leverage advanced command patterns (`Alt+B`, `Ctrl+W`) tailored for rapid iteration without mouse dependency.
* **TOML Session Persistence:** Save and instantly restore robust HPC debug profiles (binary arguments, OpenMPI rank counts, watchlists).
* **Multi-Rank State Inspection:** Step through multiple independent MPI processes simultaneously within a unified, responsive interface.
* **Semantic Hover Tooltips:** Powered by `clangd` language server and DAP to instantly view live variable values on hover.
* **HPC Orchestration (SLURM & Spack):** Submit remote batch jobs and monitor Spack environments right from the IDE.

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
```

---

## 🗺️ Roadmap Snapshot

GridLock's development is broken into iterative phases. For a detailed breakdown, see [ROADMAP.md](ROADMAP.md).

* **Phase 1-4:** (Complete) Stability, UI Foundations, Advanced Debugger, IDE Experience
* **Phase 5:** (Complete) The Polyglot Core (DAP Refactor)
* **Phase 6:** (In Progress) High-Performance Memory Visualizations (Zero-Copy pipelines, OpenGL Heatmaps, Stride Security)
* **Phase 7:** Cross-Language Variable Inspector
* **Phase 8:** Alternative Debugger Backends
* **Phase 9:** The Plugin Marketplace
* **Phase 10:** Advanced Cluster Lifecycle & Detached Sessions

---

## 🤝 Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for environment setup and issue submission. All community interactions must adhere to our [Code of Conduct](CODE_OF_CONDUCT.md).

## 🏆 Attributions

GridLock orchestrates OpenMPI, SLURM, and Spack, relying on `tomlplusplus` for configuration. It draws inspiration from GNU DDD, KDbg, gdbgui, and Zeal.

## 📄 License

Licensed under the MIT License. See [LICENSE](LICENSE).
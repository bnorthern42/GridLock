# Contributing to GridLock

Thank you for your interest in contributing to GridLock (`v0.9.1`). GridLock is an open-source C++23 and Qt6 graphical debugger frontend for GDB (`GDB/MI3`) and LLDB (`DAP`) built for multi-rank MPI debugging and ELF64 binary inspection.

---

## 1. Getting Started & Documentation Wiki

Before making changes, review the architecture and extension guides on the [GridLock GitHub Wiki](https://github.com/bnorthern42/GridLock/wiki):

* [**Installation and Building**](https://github.com/bnorthern42/GridLock/wiki/Installation-and-Building): Distro packages (Arch, Ubuntu/Debian, Fedora), `Qt-Advanced-Stylesheets` submodule setup, and Meson/Ninja targets.
* [**Developer Architecture and Testing**](https://github.com/bnorthern42/GridLock/wiki/Developer-Architecture-and-Testing): Modular layout, CLI flags, coordinator architecture, and all 24 Meson test suites.
* [**Using Plugins in GridLock**](https://github.com/bnorthern42/GridLock/wiki/Plugins): End-user multi-forge plugin installation, `plugins.toml` / `plugins-lock.toml` sync, and workspace `SHA-256` trust.
* [**Plugin Architecture, Lifecycle & Security (Vol. 1)**](https://github.com/bnorthern42/GridLock/wiki/Plugin-Architecture-and-Lifecycle): Host ownership, pre-`dlopen` verification, `PluginVerifier`, `hotReloadPlugins`, and resource quotas.
* [**C99 Plugin ABI & C++23 Wrappers (Vol. 2)**](https://github.com/bnorthern42/GridLock/wiki/Plugin-C99-ABI-Reference): Frozen 64-bit struct layouts, status/UI enums, all 28 `GridLockHostApi` callbacks, and `plugin_cpp_helpers.hpp`.
* [**Plugin Authoring, UI Surfaces & Build Recipes (Vol. 3)**](https://github.com/bnorthern42/GridLock/wiki/Plugin-Authoring-and-UI-Surfaces): `scripts/init_plugin.lua` scaffolding, zero-call DWARF inspection, 5 context-menu surfaces, and `--verify-plugin` CI gating.

---

## 2. Building & Running Tests

### Build From Source (`Meson >= 1.3.0` + `Ninja`, C++23 Compiler)

Requires **Meson `>= 1.3.0`**, **Ninja**, **Qt 6**, and a C++23 compiler (**GCC `13+`**, **Clang `17+`**, or Intel oneAPI **`icpx` `2024+`**).

```bash
git clone --recurse-submodules https://github.com/bnorthern42/GridLock.git
cd GridLock

cmake -S external/Qt-Advanced-Stylesheets -B external/Qt-Advanced-Stylesheets/build \
      -GNinja -DQT_VERSION_MAJOR=6 -DCMAKE_BUILD_TYPE=Release
ninja -C external/Qt-Advanced-Stylesheets/build

meson setup builddir
ninja -C builddir
```

### Run Automated Test Suites & Plugin Verification

All 24 Meson test suites run headlessly under `QT_QPA_PLATFORM=offscreen` and isolate configuration into temporary directories automatically:

```bash
# Run all 24 Meson test suites headlessly:
QT_QPA_PLATFORM=offscreen meson test -C builddir --print-errorlogs

# Run the headless workstation UI test suite directly:
QT_QPA_PLATFORM=offscreen ./builddir/gridlock_ui_tests

# Verify a compiled native C99/C++23 plugin binary in an isolated sandbox:
./builddir/gridlock --verify-plugin /path/to/libmy_plugin.so
```

---

## 3. Core Contribution Standards (Human & AI-Assisted PRs)

### 3.1 Reproducer-First Policy for Cluster & Hardware Bugs
Maintainers do not have local access to every HPC cluster configuration (such as NVIDIA H200 / Grace Hopper nodes, Cray PMI, proprietary MPI stacks, custom GPU `rocgdb` / `cuda-gdb` builds, or site-specific Slurm setups). Bug reports and PRs targeting environments that cannot be reproduced locally **must** attach:

1. An exported **Sanitized GDB/MI Trace** generated via **Tools -> Export Sanitized GDB/MI Trace...** (or `./gridlock --export-mi-trace /path/to/trace.mi.log`), which automatically redacts home/work/scratch paths, usernames, hostnames, and `-gdb-set environment` values while preserving exact MI syntax.
2. Exact debugger version output (`gdb --version`, `rocgdb --version`, or `lldb-dap --version`).
3. MPI implementation, version, and launcher command line (for example, `mpirun --version` / `srun --version` and relevant flags).

This allows maintainers to replay the exact wire sequence deterministically inside `GdbMiParser` and launcher unit tests.

### 3.2 Mandatory Regression Test Coverage
Every bug fix or parser/launcher change **must** add or update a deterministic unit test or headless UI test (`QT_QPA_PLATFORM=offscreen meson test -C builddir --print-errorlogs`) that fails before the patch and passes after it. Pull requests for bug fixes without regression tests will not be merged.

### 3.3 Small, Auditable Diffs & Architectural Boundaries
* Keep each pull request focused on a single concern; avoid sprawling multi-subsystem rewrites or speculative refactors.
* Preserve subsystem boundaries: keep GDB/MI parsing inside `src/core/hpc/gdb/GdbMiParser.*`, JSON-RPC framing inside `src/core/utils/JsonRpcFramer.hpp`, and MPI/Slurm command/environment builders inside their dedicated core classes (`GdbRankCoordinator`, `LocalSlurmBackend`) rather than scattering regexes or shell logic across UI widgets.
* Verify root cause before proposing a fix; do not submit blind or unverified patches.

### 3.4 Code Hygiene & Strict File-Length Ceiling (`< 800` Lines)
* **Google C++ Style Guide**: Core and UI code uses **C++23** (`-Wall -Wextra -Wpedantic`), `#pragma once`, strict `const` correctness, `override` on virtual overrides, in-class scalar initialization, and Qt parent-child or `std::unique_ptr` ownership.
* **Strict `< 800`-Line Ceiling (`agent.md`)**: Every C++ source (`.cpp`) and header (`.hpp`) file must stay strictly under `800` lines (targeting `< 500` lines per cohesive module). Before adding code to a file approaching `800` lines, extract cohesive helpers into a dedicated translation unit or header.
* **Zero Compiler Warnings**: All targets (`gridlock` and every test suite) must build cleanly under `-Wall -Wextra -Wpedantic`.
* **Documentation Prose Rules**: Write clear, direct technical prose without em-dashes, en-dashes, or marketing filler. Update the corresponding per-directory `README.md` whenever adding, moving, or splitting files.

### 3.5 Frozen Plugin C ABI Stability (`include/gridlock/plugin_api.h`)
* Native plugin ABI headers (`include/gridlock/plugin_api.h`) use strict **C99** with zero C++ standard library or Qt dependencies.
* Every callback uses `GRIDLOCK_CALL`, fixed-width integers (`int32_t`, `uint32_t`, `uint64_t`), and 16-byte `(struct_size, abi_major, abi_minor, reserved_flags)` version headers at offset `0`.
* **Never reorder, remove, or change the signature of existing function pointers** in `GridLockHostApi` or `GridLockPluginDescriptor`. Lock every struct `sizeof` and `offsetof` with `static_assert` in `tests/core/test_plugin_abi.cpp`.
* Extensions for experimental languages (such as Odin, Zig, or Rust) or domain-specific visualizers should target the versioned C ABI (`gridlock_plugin_init_v1`) wherever possible rather than adding heavy language-specific dependencies to core.

---

## 4. Pull Request Workflow

1. **Branch off `master`** with a descriptive branch name (for example, `feat/...` or `fix/...`) and keep commits focused and atomic.
2. **Verify locally before opening a PR**: Run `ninja -C builddir`, `QT_QPA_PLATFORM=offscreen meson test -C builddir --print-errorlogs`, and `QT_QPA_PLATFORM=offscreen ./builddir/gridlock_ui_tests`, confirming zero warnings and zero test failures.
3. **Complete the PR template**: Fill out `.github/pull_request_template.md`, including root cause, sanitized GDB/MI trace or reproducer (for debugger/cluster changes), regression tests, and `.png` screenshots (`bash scripts/capture_ui.sh`) for UI changes.
4. **Bug reports**: Open an issue using `.github/ISSUE_TEMPLATE/bug_report.yml` with your GridLock version, cluster/OS environment, `gdb --version`, MPI launcher details, and an attached **Sanitized GDB/MI Trace**.

Thank you for helping improve GridLock.

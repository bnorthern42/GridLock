# Contributing to GridLock

Thank you for your interest in contributing to GridLock (`v0.9.0`). GridLock is an open-source C++23 and Qt6 graphical debugger frontend for GDB (`GDB/MI3`) and LLDB (`DAP`) built for multi-rank MPI debugging and ELF64 binary inspection.

---

## 1. Getting Started & Documentation Wiki

Before making changes, review the architecture and extension guides on the [GridLock GitHub Wiki](https://github.com/bnorthern42/GridLock/wiki):

* [**Installation and Building**](https://github.com/bnorthern42/GridLock/wiki/Installation-and-Building): Distro packages (Arch, Ubuntu/Debian, Fedora), `Qt-Advanced-Stylesheets` submodule setup, and Meson/Ninja targets.
* [**Developer Architecture and Testing**](https://github.com/bnorthern42/GridLock/wiki/Developer-Architecture-and-Testing): Sub-500-line modular layout, CLI flags, coordinator architecture, and all 24 Meson test suites.
* [**Using Plugins in GridLock**](https://github.com/bnorthern42/GridLock/wiki/Plugins): End-user multi-forge plugin installation, `plugins.toml` / `plugins-lock.toml` sync, and workspace `SHA-256` trust.
* [**Plugin Architecture, Lifecycle & Security (Vol. 1)**](https://github.com/bnorthern42/GridLock/wiki/Plugin-Architecture-and-Lifecycle): Host ownership, pre-`dlopen` verification, `PluginVerifier`, `hotReloadPlugins`, and resource quotas.
* [**C99 Plugin ABI & C++23 Wrappers (Vol. 2)**](https://github.com/bnorthern42/GridLock/wiki/Plugin-C99-ABI-Reference): Frozen 64-bit struct layouts, status/UI enums, all 28 `GridLockHostApi` callbacks, and `plugin_cpp_helpers.hpp`.
* [**Plugin Authoring, UI Surfaces & Build Recipes (Vol. 3)**](https://github.com/bnorthern42/GridLock/wiki/Plugin-Authoring-and-UI-Surfaces): `scripts/init_plugin.lua` scaffolding, zero-call DWARF inspection, 5 context-menu surfaces, and `--verify-plugin` CI gating.

---

## 2. Building & Running Tests

### Build From Source (`Meson` + `Ninja`)

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
# Run all 24 Meson test suites:
meson test -C builddir --print-errorlogs

# Run the headless workstation UI test suite directly:
QT_QPA_PLATFORM=offscreen ./builddir/gridlock_ui_tests

# Verify a compiled native C99/C++23 plugin binary in an isolated sandbox:
./builddir/gridlock --verify-plugin /path/to/libmy_plugin.so
```

---

## 3. Code Style & Architectural Standards

* **Google C++ Style Guide**:
  * Core and UI code uses **C++23** (`-Wall -Wextra -Wpedantic`), `#pragma once`, strict `const` correctness, `override` on virtual overrides, in-class scalar initialization, and Qt parent-child or `std::unique_ptr` ownership.
  * Native plugin ABI headers (`include/gridlock/plugin_api.h`) use strict **C99** with zero C++ standard library or Qt dependencies.
* **Strict File-Length Discipline (`< 800` Lines)**:
  * Keep every source and header file under `800` lines (targeting `< 500` lines per cohesive module). Split new docks, dialogs, views, or core subsystems into dedicated translation units rather than growing monolithic files.
* **Frozen C99 Plugin ABI (`v1.0`) Rules**:
  * Every callback uses `GRIDLOCK_CALL`, fixed-width integers (`int32_t`, `uint32_t`, `uint64_t`), and 16-byte `(struct_size, abi_major, abi_minor, reserved_flags)` version headers at offset `0`.
  * Never shift existing field offsets or remove slots; lock every struct `sizeof` and `offsetof` with `static_assert` in `tests/core/test_plugin_abi.cpp`, and keep zero Qt types in `plugin_api.h`.
* **Documentation Prose Rules**:
  * Write clear, direct technical prose without em-dashes, en-dashes, or marketing filler. Update the corresponding per-directory `README.md` whenever adding, moving, or splitting files.

---

## 4. Pull Request Expectations

1. **Branch off `master`** with a descriptive branch name (for example, `feat/...` or `fix/...`) and keep commits focused and atomic.
2. **Pass all checks with zero compiler warnings**: Verify `ninja -C builddir`, `meson test -C builddir --print-errorlogs`, and `QT_QPA_PLATFORM=offscreen ./builddir/gridlock_ui_tests` before opening a PR.
3. **Attach workstation screenshots for UI changes**: For any UI, widget, layout, or theme change, generate and attach actual `.png` captures (`bash scripts/capture_ui.sh`) in the pull request description.
4. **Bug reports**: Open an issue using the structured templates under `.github/ISSUE_TEMPLATE/` and include your MPI implementation/version, rank count, and relevant GDB/MI output logs.

Thank you for helping improve GridLock.

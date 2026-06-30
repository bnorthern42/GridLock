# Changelog

All notable changes to the GridLock project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.6.0] - 2026-06-30

### Added
- **Integrated Interactive Terminal:** Real embedded terminal leveraging `qtermwidget`, providing a full PTY-backed interactive shell inside the IDE as `TerminalDockWidget`.
- **Centralized `VersionManager`:** The `meson.build` file is now the single source of truth for the project version. A `VersionConfig.hpp.in` template is configured at build time, exposing `GRIDLOCK_VERSION_MAJOR`, `GRIDLOCK_VERSION_MINOR`, and `GRIDLOCK_VERSION_PATCH` constants to all C++ translation units via the `VersionManager` API.
- **DAP Exception Handling:** The DAP lifecycle now catches fatal target exceptions (e.g., `SIGFPE`, segfaults). When `DapCoordinator` receives a stopped event with `reason: "exception"`, it emits a `targetCrashed(QString reason)` signal. `MainWindow` connects this signal to display a clean, non-blocking warning dialog rather than leaving the UI in a hung state.
- **Project-Local Configuration Overrides:** `ConfigManager` now implements a two-tier resolution hierarchy. On startup and session load, it checks for `<project_root>/.gridlock/workspace.toml` first. If present, values in that file override global XDG settings (`~/.config/gridlock/config.toml`), enabling fully reproducible, project-portable debug configurations.
- **Automated Stylesheet Asset Bundling:** `Qt-Advanced-Stylesheets` theme assets are now compiled directly into a Qt Resource (`.qrc`) file via a Meson `custom_target`. The generated `acss_resources.qrc` is processed by `qt.compile_resources()`, embedding all style assets into the binary for flawless zero-dependency deployment.
- **Persistent Layout Restoration:** `MainWindow` now saves and restores `QMainWindow::saveGeometry()`, `QMainWindow::saveState()`, and individual dock widget positions between sessions using `QSettings`. The layout is persisted on close and fully restored on the next launch.

### Changed
- **Terminal Architecture Separation:** The old monolithic pseudo-terminal was split into two distinct docks: `MpiNetworkLogWidget` (read-only, captures DAP/GDB stdout streams for MPI Diagnostics) and `TerminalDockWidget` (a live, fully interactive shell via `qtermwidget`).
- **`BreakpointManager` Scoping:** The `BreakpointManager` is now scoped to the active Workspace Directory. All breakpoint paths are stored as absolute paths relative to the project root. This prevents global breakpoint accumulation across projects and eliminates the flood of `<PENDING>` GDB/MI traffic that occurred when stale paths from other workspaces were sent to the debugger on session load.
- **`ProjectWizardDialog` Persistent Memory:** The Project Wizard now automatically re-populates all fields (binary path, arguments, MPI rank count, working directory) with the values from the last session. Fields are persisted via `QSettings` and restored on the next time the wizard is opened.
- **CI Pipeline Overhaul:** The GitHub Actions `release.yml` pipeline was updated to build all Qt6-native subproject dependencies from source on older Ubuntu runners. This includes `lxqt-build-tools`, `qtermwidget`, and `Qt-Advanced-Stylesheets` (pinned to their `2.0.0` tags), ensuring a reproducible and hermetic build environment.

### Fixed
- **Wayland/Niri Dock Regressions:** Resolved compositor-specific regressions affecting dock widget dragging and floating window management under Wayland compositors, specifically the Niri tiling compositor.
- **`mpi_mm.c` Divide-by-Zero Crash:** Fixed a fatal divide-by-zero crash in the MPI matrix multiplication tutorial (`tutorial/mpi_mm.c`) that occurred when the program was executed in singleton mode (rank count of 1) without any worker ranks, causing an integer division by zero when computing the row distribution.

## [0.5.3] - 2026-06-28

### Changed
- **Tooltip Rendering**: Removed heavy LSP Markdown tooltips and the `HoverWidget` to improve UI performance and stability. Reverted to a lightweight, custom `QLabel` plain-jane tooltip for GDB variable evaluations.
- **Tutorial Subsystem**: Decoupled tutorial E2E tests from the main build pipeline, removing them from `meson.build` to migrate to a separate repository.

### Fixed
- **Wayland Tooltip Glitches**: Fixed tooltips rendering as blank white boxes on Wayland compositors by refactoring them to use `Qt::ToolTip | Qt::FramelessWindowHint` without translucent background attributes.
- **Hover Jitter**: Implemented debouncing on `SourceCodeView` to eliminate rapid-fire GDB requests and UI flickering when moving the mouse cursor over the same variable.
- **Variable Tree Performance**: Eliminated eager DAP variable fetches, significantly improving responsiveness and eliminating I/O bottlenecks when stepping through code.

## [0.5.2] - 2026-06-27

### Added
- **Tutorial Mode**: Initial scaffolding for `--tutorial-mode` (Note: This feature is currently a Work In Progress / WIP).

### Fixed
- **DAP Execution**: Fixed DAP `stopOnEntry` stalling the debugger on the assembly entry point instead of hitting user code.
- **LSP Handshake**: Fixed Clangd LSP handshake failing due to a missing `initialized` notification.
- **UI Lifecycle**: Fixed application windowing lifecycle duplicating windows at startup when launching dialogs.

## [0.5.1] - 2026-06-24
- **Global Configuration Engine:** Implemented a new TOML-based `ConfigManager` that persistently stores toolchain paths (`lldb-dap`, `gdb`, `clangd`) and default MPI launch arguments in `~/.config/gridlock/config.toml`.
- **Unified Preferences UI:** Added a comprehensive, Kate/KDevelop-style sidebar `PreferencesDialog` (Edit -> Preferences) to manage Appearance, Editing, Behavior, Debugger tools, HPC/Cluster settings, SLURM integration, and Docsets.
- **Project Creation Wizard:** Replaced the disjointed "New Project" menu with a linear, multi-page `QWizard`. Includes dynamic MPI launch string generation with quick-toggles for common OpenMPI flags (e.g., `--bind-to core`, `--map-by node`, `--oversubscribe`).
- **Recent Sessions (MRU):** Added a "Recent Sessions" submenu under the File menu, tracking the last 5 loaded workspaces dynamically.
- **Headless CI Support:** Added `xvfb` (X Virtual FrameBuffer) to the GitHub Actions pipeline to allow Qt GUI and DAP parser tests to run successfully on headless Ubuntu runners.

### Changed
- **LSP Toolchain:** `LspCoordinator` now dynamically resolves the `clangd` binary path from the user's global configuration rather than relying on the system `PATH`.
- **Architecture:** Moved away from hardcoded configuration values; MPI execution flags and debugger paths are now injected directly from the `ConfigManager` singleton into the backend coordinators.

### Fixed
- **Workspace State Refresh:** Fixed a critical state bug where loading a new session or workspace failed to visually update the `ProjectExplorerWidget`. The file tree now correctly re-roots upon the `sessionLoaded` signal.
- **Vulkan CI Crash:** Completely purged lingering `QVulkanInstance` boilerplate from `src/main.cpp` that was causing `SIGABRT` compilation failures in environments lacking Vulkan headers.
- **Test Suite Linker Errors:** Fixed a Meson dependency issue where `gridlock_lsp_tests` failed to link against the newly implemented `ConfigManager.cpp`.
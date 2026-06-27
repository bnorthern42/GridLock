# Changelog

All notable changes to the GridLock project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
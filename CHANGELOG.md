## [0.5.0] - 2026-06-23

### Added
- **Multi-Rank Memory Diff Engine**: Implemented simultaneous, zero-copy memory extraction via `process_vm_readv`, complete with byte-level visual discrepancy highlighting across MPI ranks.
- **Session State Bookmarking**: Added a native `SessionManager` that serializes active breakpoints, watched variables, and UI states to lightweight `.toml` workspaces.
- **Automated CI/CD**: Established an automated GitHub Actions pipeline for testing, packaging, and AppImage deployment.

### Changed
- **Architectural Pivot**: Completely purged explicit Vulkan dependencies and the Domain Heatmap feature to ensure pure CPU-bound deployment across all HPC distributions and reduce technical debt.
- **CLI Polish**: Re-wired `QCommandLineParser` to properly output "GridLock v0.5.0" and highlight core feature sets in the help string.

### Fixed
- **GDB Protocol Leakage**: Implemented strict stream filtering to suppress raw MI protocol records (e.g., `^done`, `*stopped`) from bleeding into the user-facing console.
- **Array Extraction Crash**: Injected `-gdb-set max-value-size unlimited` into the backend coordinator initialization sequence to prevent GDB from halting when parsing massive remote arrays.
- **Accessibility**: Fixed a bug where dark-mode placeholder text in input fields lacked contrast by overriding `QPalette::PlaceholderText`.

### Removed
- Removed the `DomainHeatmapWidget`, Vulkan Fragment Shaders, and all associated rendering logic.

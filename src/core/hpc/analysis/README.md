# HPC Analysis & Mock Backends (`src/core/hpc/analysis/`)

Automated MPI collective and point-to-point deadlock analyzer alongside a deterministic in-memory `HpcBackend` mock for offline UI testing.

## Contents

- `DeadlockAnalyzer.hpp` (`101` lines):
  - Defines `DeadlockInfo` (`rankId`, `blockedFunction`, `file`, `line`, `reason`, `isCollectiveMismatch`).
  - Declares `DeadlockAnalyzer` (`QObject`) and its `lastDiagnosticAlert` property.
- `DeadlockAnalyzer.cpp`:
  - Inspects stopped rank stack frames (`-stack-list-frames 0 3`).
  - Flags blocking MPI/PMPI routines (`MPI_Barrier`, `MPI_Recv`, `MPI_Wait`, `MPI_Allreduce`).
  - Detects point-to-point wait cycles and collective divergence across paused ranks.
- `MockHpcBackend.hpp` (`50` lines):
  - Declares `MockHpcBackend` inheriting `HpcBackend` for deterministic offline cluster simulation.
  - Exposes `setFakePackages()` and `setNextSubmitFails()` hooks for repeatable failure testing.
- `MockHpcBackend.cpp`:
  - Emits simulated Spack package lists (`fetchPackages()`), SLURM job IDs (`submitSlurmJob()`), and `scancel` results asynchronously via `QTimer::singleShot`.

## Expected Behavior & Invariants

- **Verified Frame Preconditions**:
  - `DeadlockAnalyzer` evaluates collective mismatch and wait cycles strictly from paused ranks containing valid caller frame metadata (`-stack-list-frames 0 3`).
  - Ranks halted inside non-MPI code never emit false-positive `DeadlockInfo` records (`tests/core/test_advanced_features.cpp`).
- **Queued Asynchronous Mock Signals**:
  - `MockHpcBackend` schedules signal emission (`packagesReady`, `slurmJobSubmitted`, `commandFinished`) through Qt's event loop (`QTimer::singleShot`) rather than firing synchronously inside request slots.
  - Fault-injection toggles (`setNextSubmitFails(true)`) deterministically simulate daemon errors without sleeping.
- **Offline CI Execution**:
  - Both classes operate deterministically in `QT_QPA_PLATFORM=offscreen` environments with zero network, SLURM daemon, or MPI cluster access.
  - Run `meson test -C builddir advanced_features_tests ui_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Signal & UI Decoupling:
  - Keep `DeadlockAnalyzer` decoupled from `QtWidgets`; publish diagnostics through Qt signals and `lastDiagnosticAlert` so UI panels (`MpiDiagnosticsWidget`) and headless tests share one contract.
  - Keep `GdbRankCoordinator` and `RankState` forward-declared in `DeadlockAnalyzer.hpp` to avoid pulling heavy backend headers into UI widgets.
- Mock Async Semantics:
  - Never replace `QTimer::singleShot` in `MockHpcBackend` with direct synchronous callbacks, because `HpcConsoleWidget` and `SpackManager` expect queued event-loop completion.
- Ongoing Maintenance:
  - Add inline comments explaining blocking primitive sets, wait-edge construction, and collective divergence rules whenever extending heuristics.
  - Maintain strict `< 950` line limits per file, update `meson.build` if adding files, and update this `README.md`.

# GridLock Test Suite (`tests/`)

Domain-organized Qt Test suites spanning headless `MainWindow` UI workflows (`ui/`), DAP protocol coverage (`dap/`), GDB/MI and native HPC tooling (`hpc/`), and core config/security utilities (`core/`).

## Contents

- `ui/`:
  - Headless Qt integration and UX regression suite (`gridlock_ui_tests` / Meson target `ui_tests`, 64 QTest slots).
  - Covers docking, views, dialogs, Search Everywhere, visual cohesion, and screenshot captures.
- `dap/`:
  - Nine standalone `DapCoordinator` suites (`dap_parser_tests`, `dap_lifecycle_tests`, `dap_commands_tests`, `dap_breakpoints_tests`, `dap_stacktrace_tests`, `dap_variables_tests`, `dap_evaluate_tests`, `dap_memory_tests`, `dap_native_bridge_tests`).
- `hpc/`:
  - Seven backend/system test suites (`coordinator_tests`, `mi_parser_tests`, `sanitizer_tests`, `lsp_tests`, `hpc_memory_tests`, `memory_export_tests`, `obfuscator_tests`).
- `core/`:
  - Five infrastructure suites (`cliparser_tests`, `config_tests`, `docset_tests`, `stride_security_tests`, `advanced_features_tests`).
- `../scripts/e2e_cli_suite.mjs` (plus `../scripts/lib/e2e_helpers.mjs`):
  - Zero-dependency Node.js end-to-end CLI runner driving `./builddir/gridlock` externally across all CLI flags, every `tutorial/` source (single-rank and `ranks = 2`), synthetic crash/args/5 MB CSV stress targets, XDG config unification, Recent Sessions persistence, and `/dev/shm/psm3_shm*` + process teardown hygiene.

## Expected Behavior & Invariants

- **Build & Execution Commands**:
  - Build default targets plus UI test binary (`build_by_default : false`):
    - `ninja -C builddir && ninja -C builddir gridlock_ui_tests`
  - Run all 22 Meson targets headlessly:
    - `meson test -C builddir --print-errorlogs` (Meson sets `QT_QPA_PLATFORM=offscreen` on every test target).
  - Run the UI executable directly:
    - `QT_QPA_PLATFORM=offscreen ./builddir/gridlock_ui_tests`
  - Run the standalone Node.js CLI E2E suite:
    - `./scripts/e2e_cli_suite.mjs --verbose`
    - `./scripts/e2e_cli_suite.mjs --filter tutorial/inspection_demo` (run matching subset; add `--keep-tmp` to retain the temp sandbox directory).
- **Config & Environment Isolation**:
  - Config-mutating tests isolate paths in `QTemporaryDir` via `GRIDLOCK_TEST_CONFIG_DIR` or RAII-restored `XDG_CONFIG_HOME` wrappers so live `~/.config/gridlock/` state is never altered.
  - When launching local OpenMPI smoke binaries in restricted sandboxes, isolate `FI_PROVIDER` (e.g., `FI_PROVIDER=tcp`) as needed to avoid fabric probe stalls.
- **Zero Cluster Dependency**:
  - All 22 suites execute deterministically offline without live SSH hosts or SLURM daemons.

## Maintainer & Agent Notes

- Build & Registration Sync:
  - Update `meson.build` whenever adding, moving, or splitting test files (`*_test_src`, `*_moc_headers`, `sources :` in `qt6.compile_moc`, and `test(...)`).
  - Remember that `gridlock_ui_tests` uses `build_by_default : false`, so rebuild it explicitly (`ninja -C builddir gridlock_ui_tests`) before direct runs.
- Include & Working Directory Paths:
  - Use `#include "../../src/..."` inside test files located under `tests/<subdir>/`.
  - Meson executes tests with CWD set to `builddir/`, so runtime source-tree inspections (`../src/...`, `../docs/...`) resolve relative to `builddir/`.
- Cleanliness & Size Limits:
  - Delete temporary compiled tutorial binaries under `tutorial/` before staging commits.
  - Keep every `.hpp`/`.cpp` file under `< 950` lines and update the matching subdirectory `README.md` on test changes.

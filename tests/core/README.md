# Core Infrastructure & Security Tests (`tests/core/`)

Standalone unit and integration tests for CLI parsing, TOML/`QSettings` configuration, offline Dash/Zeal docsets, stride bounds security, and MPI deadlock/trap features.

## Contents

- `test_cliparser.cpp` (`cliparser_tests`):
  - Tests `CliParser` flags (`--test-mode`, `--tutorial-mode`, `--verbose`, `--headless`, `--snapshot`) and pre-`QApplication` offscreen QPA detection (uses `#include "test_cliparser.moc"`).
- `test_config.hpp`, `test_config.cpp` (`config_tests`):
  - Tests `ConfigManager` TOML workspace round-tripping, corrupt-file fallbacks, clamped validation bounds, SLURM/SSH/terminal defaults, and isolated `GRIDLOCK_TEST_CONFIG_DIR` sandboxing.
- `test_docset.hpp`, `test_docset.cpp` (`docset_tests`):
  - Exercises `DocsetManager` SQLite `docSet.dsidx` indexing, symbol search ranking, and HTML doc entry path resolution.
- `test_stride_security.hpp`, `test_stride_security.cpp` (`stride_security_tests`):
  - Tests `MemoryBoundsValidator` `size_t`/`uintptr_t` overflow guards, oversized stride rejection, out-of-bounds read checks, and null node guards.
- `test_advanced_features.hpp`, `test_advanced_features.cpp` (`advanced_features_tests`):
  - Verifies `DeadlockAnalyzer` blocking MPI call patterns, `SIGFPE` trap configuration, conditional breakpoint emission, and variable delta highlighting.

## Expected Behavior & Invariants

- **Strict Config Sandboxing**:
  - `test_config.cpp` saves and restores `GRIDLOCK_TEST_CONFIG_DIR` around `QTemporaryDir` instances so test runs never touch `~/.config/gridlock/config.toml`.
- **Fast Sub-Second Targets**:
  - All 5 targets link minimal `QtCore` / `QtSql` / `toml++` dependencies without instantiating `MainWindow`.
- **Security Regression Discipline**:
  - Any array slicing or memory offset bug in `plotter/` or `memory/` must add a failing bounds case to `test_stride_security.cpp` alongside the fix.
- **Verification**:
  - Run `meson test -C builddir cliparser_tests config_tests docset_tests stride_security_tests advanced_features_tests --print-errorlogs`.

## Maintainer & Agent Notes

- MOC & Meson Build Integration:
  - `test_cliparser.cpp` defines its `QObject` class inside the `.cpp` file, so `meson.build` must preserve `sources : 'tests/core/test_cliparser.cpp'` in `qt6.compile_moc(...)`.
  - Add new non-GUI unit suites (`SearchEverywhereEngine`, `ElfBinaryInspector`, `SessionManager`) under `tests/core/` with matching Meson targets in `meson.build`.
- Path & Environment Rules:
  - Use `#include "../../src/..."` relative includes across all files in `tests/core/`.
  - Always clean up temporary directories and restore `GRIDLOCK_TEST_CONFIG_DIR` / `XDG_CONFIG_HOME` using RAII guards on early test exits.
- Maintenance Discipline:
  - Keep every test `.hpp`/`.cpp` file strictly under `< 950` lines.
  - Avoid inline C++ test generation via string streams (`QTextStream`); use physical test fixtures instead.
  - Update this `README.md` whenever adding, moving, or retiring core infrastructure test suites.

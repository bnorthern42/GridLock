# Core Managers (`src/core/managers/`)

Application-wide persistence, theming, keyboard chord routing, offline documentation, session state, and cluster environment console managers.

## Contents

- `ConfigManager.hpp` (`356`):
  - Singleton facade and typed settings PODs: `DebuggerSettings`, `HpcSettings`, `SshSettings`, `SlurmSettings`, `TerminalSettings`, `ProjectSettings`, and `BreakpointConfig`.
- `ConfigManager.cpp` (`300`):
  - Singleton lifecycle, workspace switching, `QSettings`-backed debugger/HPC/SSH/SLURM/docset PODs, live editor/treemap toggles, and custom shortcut overrides.
- `ConfigManagerToml.cpp` (`212`):
  - Global `~/.config/gridlock/config.toml` bootstrap (`XDG_CONFIG_HOME` / `GRIDLOCK_TEST_CONFIG_DIR`), legacy migration, `saveGlobalConfig()`, toolchain paths, syntax colors, `[ui]` monitor routing, and `TerminalSettings`.
- `ConfigManagerWorkspace.cpp` (`481`):
  - Per-workspace `.gridlock/workspace.toml` and `.gridlock/settings.toml` persistence (`ProjectSettings`, target-scoped `programArgs`/`setProgramArgs` via `[project.target_args]` and in-memory map without global `QSettings` leakage, breakpoints, `BreakpointConfig`), workspace-scoped engine/theme/disassembly/telemetry overrides, and `Recent Sessions`.
- `ThemeManager.hpp` (`122`), `ThemeManager.cpp` (`822`):
  - Singleton managing `QtAdvancedStylesheet` palettes, workstation chrome QSS overlays, font sizing, and `themeChanged` broadcasts.
- `ShortcutManager.hpp` (`99`), `ShortcutManager.cpp`:
  - Singleton keyboard chord state machine and `ActionBinding` registry handling single-key shortcuts (`F5`, `F10`, `F11`) and multi-stroke chords (`Ctrl+W, H`).
- `DocsetManager.hpp` (`178`), `DocsetManager.cpp` (`468`), `DocsetManagerIndex.cpp` (`487`):
  - Singleton lifecycle, bundle metadata accessors, symbol normalization, and built-in dark-slate HTML5 manuals for MPI-3.1, System V AMD64 ABI, GDB/MI, x86_64 instructions, and system man pages (`DocsetManager.cpp`).
  - Zeal/Dash directory discovery, SQLite `ATTACH DATABASE` (`docSet.dsidx`) indexing, `searchIndex`/`ZTOKEN` symbol queries, `.docset` HTML dark-reader styling, and concurrent workspace header scanning (`DocsetManagerIndex.cpp`, tested in `tests/core/test_docset.cpp`).
- `SessionManager.hpp` (`104`), `SessionManager.cpp`:
  - Serializes `SessionState` (watches, `file:line` breakpoints, selected ranks) to `.toml` files and maintains MRU session lists.
- `SpackManager.hpp` (`94`), `SpackManager.cpp`:
  - Base `QWidget` controller binding an `HpcBackend` into Spack/SLURM console views (extended by `HpcConsoleWidget`).

## Expected Behavior & Invariants

- **Test Config Isolation**:
  - `ConfigManager` evaluates `GRIDLOCK_TEST_CONFIG_DIR` and `XDG_CONFIG_HOME` before resolving config paths so test runs never alter live `~/.config/gridlock/` state.
- **Safe QAction Pointers**:
  - `ShortcutManager` tracks live menu actions via `QPointer<QAction>` and validates callbacks on window destruction.
- **Translation Unit Line Budget**:
  - All `ConfigManager*.cpp` (`300`, `212`, `396` lines) and `DocsetManager*.cpp` (`468`, `487` lines) splits sit strictly `< 500` lines; `ThemeManager.cpp` (`822` lines) sits below `< 950` lines.
- **Verification**:
  - Run `meson test -C builddir config_tests docset_tests ui_tests --print-errorlogs` after modifying persistence, docset lookups, shortcuts, or theme overlays.

## Maintainer & Agent Notes

- Persistence & Config Isolation:
  - Add new persisted fields to a typed struct in `ConfigManager.hpp`, implement global TOML serializers in `ConfigManagerToml.cpp` or workspace TOML serializers in `ConfigManagerWorkspace.cpp`, and test round-trips plus corrupt-file fallbacks in `tests/core/test_config.cpp`.
  - Never let test cases touch `~/.config/gridlock/`; verify `GRIDLOCK_TEST_CONFIG_DIR` and `XDG_CONFIG_HOME` overrides remain honored on every new save path.
- Build Sync & Dependency Discipline:
  - Keep `ConfigManager`, `DocsetManager`, and `SessionManager` decoupled from `MainWindow` so `config_tests` and `docset_tests` compile cleanly without full UI linkage.
  - Keep split manager units (`ConfigManager*.cpp`, `DocsetManager*.cpp`) strictly `< 500` lines, register every new unit across all standalone test targets in `meson.build`, and update this `README.md`.

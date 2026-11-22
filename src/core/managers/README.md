# Core Managers (`src/core/managers/`)

Application-wide persistence, theming, keyboard chord routing, offline documentation, session state, and cluster environment console managers.

## Contents

- `ConfigManager.hpp` (`349`), `ConfigManager.cpp` (`914`):
  - Unified TOML + `QSettings` manager handling `~/.config/gridlock/config.toml` (`XDG_CONFIG_HOME` / `GRIDLOCK_TEST_CONFIG_DIR`), `.gridlock/workspace.toml`, and `.gridlock/settings.toml`.
  - Defines typed settings PODs: `DebuggerSettings`, `HpcSettings`, `SshSettings`, `SlurmSettings`, `TerminalSettings`, `ProjectSettings`, and `BreakpointConfig`.
- `ThemeManager.hpp` (`122`), `ThemeManager.cpp` (`822`):
  - Singleton managing `QtAdvancedStylesheet` palettes, workstation chrome QSS overlays, font sizing, and `themeChanged` broadcasts.
- `ShortcutManager.hpp` (`99`), `ShortcutManager.cpp`:
  - Singleton keyboard chord state machine and `ActionBinding` registry handling single-key shortcuts (`F5`, `F10`, `F11`) and multi-stroke chords (`Ctrl+W, H`).
- `DocsetManager.hpp` (`177`), `DocsetManager.cpp` (`934`):
  - Scans Dash/Zeal docset folders, queries SQLite `docSet.dsidx` symbol indices, and resolves offline HTML entries (`tests/core/test_docset.cpp`).
- `SessionManager.hpp` (`104`), `SessionManager.cpp`:
  - Serializes `SessionState` (watches, `file:line` breakpoints, selected ranks) to `.toml` files and maintains MRU session lists.
- `SpackManager.hpp` (`94`), `SpackManager.cpp`:
  - Base `QWidget` controller binding an `HpcBackend` into Spack/SLURM console views (extended by `HpcConsoleWidget`).

## Expected Behavior & Invariants

- **Test Config Isolation**:
  - `ConfigManager` evaluates `GRIDLOCK_TEST_CONFIG_DIR` and `XDG_CONFIG_HOME` before resolving config paths so test runs never alter live `~/.config/gridlock/` state.
- **Safe QAction Pointers**:
  - `ShortcutManager` tracks live menu actions via `QPointer<QAction>` and validates callbacks on window destruction.
- **Critical Line Ceiling Watch**:
  - `DocsetManager.cpp` (`934` lines) and `ConfigManager.cpp` (`914` lines) sit right below `< 950` lines; split new serializers or SQLite/HTML parsers before adding major functionality.
- **Verification**:
  - Run `meson test -C builddir config_tests docset_tests ui_tests --print-errorlogs` after modifying persistence, docset lookups, shortcuts, or theme overlays.

## Maintainer & Agent Notes

- Persistence & Config Isolation:
  - Add new persisted fields to a typed struct in `ConfigManager.hpp` and test TOML round-trips plus corrupt-file fallbacks in `tests/core/test_config.cpp`.
  - Never let test cases touch `~/.config/gridlock/`; verify `GRIDLOCK_TEST_CONFIG_DIR` and `XDG_CONFIG_HOME` overrides remain honored on every new save path.
- UI Metrics & Shortcuts:
  - Route UI chrome metrics and theme tokens through `ThemeManager` instead of hardcoding hex colors inside views/dialogs.
  - Ensure `ShortcutManager` keybinding defaults remain conflict-free and propagate overrides immediately to `PreferencesDialog` and `#MainToolbar` tooltips.
- Dependency & Size Discipline:
  - Keep `ConfigManager`, `DocsetManager`, and `SessionManager` decoupled from `MainWindow` so `config_tests` and `docset_tests` compile cleanly without full UI linkage.
  - Update this `README.md` and `meson.build` before expanding `DocsetManager.cpp` (`934`) or `ConfigManager.cpp` (`914`) past safe limits.

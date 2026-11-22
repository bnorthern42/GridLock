# Headless UI & UX Test Suite (`tests/ui/`)

Headless QtWidgets integration and UX regression suite (`gridlock_ui_tests` / Meson target `ui_tests`, 64 test methods) verifying docking, views, dialogs, search, and visual cohesion.

## Contents

- `test_ui.hpp`:
  - `TestUi` QTest fixture declaring all 64 private test slots alongside shared setup/teardown helpers.
- `test_ui.cpp` (`857` lines):
  - Core `MainWindow` bootstrapping, splitter/docking geometry, source gutter/bookmarks, CFG minimap, rank badges, and code-style checks.
- `test_ui_aux.cpp` (`944` lines):
  - Preferences, `ThemeManager`, unified `XDG_CONFIG_HOME` persistence, `ProjectExplorerWidget`, `TerminalDockWidget` clipboard shortcuts (`Ctrl+Shift+C`/`Ctrl+Shift+V`), and large CSV/file regression checks.
- `test_ui_capture.cpp`:
  - Offscreen documentation screenshot generator (`GRIDLOCK_CAPTURE_SCREENSHOTS`), tool dialog renders, and toolbar dropdown captures (`GRIDLOCK_SAVE_TOOLBAR_SHOT`).
- `test_ui_cohesion.cpp`:
  - Two-tier console chrome consistency, F1 docset routing, toolbar/dock theme alignment, and runtime checks reading `../src/ui/views/DisassemblyAnalysis.cpp`.
- `test_ui_debugger.cpp`:
  - Execution scope toggling, dynamic statement stepping, project wizard/settings, COW checkpoints, signal catchpoints, and keybindings verification (`GRIDLOCK_VERIFY_KEYBINDINGS_PNG`).
- `test_ui_viz_and_ux.cpp`:
  - Search Everywhere (`Double-Shift`, `Ctrl+P`, `Tab` cycling, `Esc` dismissal), `KeySequenceRecorderWidget` (`GRIDLOCK_VERIFY_KEYBINDS_RECORDER_PNG`), `#MainToolbar` width budget (`GRIDLOCK_VERIFY_TOOLBAR_PNG`), array plotter, and pointer graph tests.

## Expected Behavior & Invariants

- **Direct & Meson Execution**:
  - Build with `ninja -C builddir gridlock_ui_tests`.
  - Run via `meson test -C builddir ui_tests --print-errorlogs` or `QT_QPA_PLATFORM=offscreen ./builddir/gridlock_ui_tests`.
  - Targeted single-slot runs work directly (e.g., `./builddir/gridlock_ui_tests testSearchEverywherePalette`).
- **Optional Screenshot Env Vars**:
  - Leave screenshot env vars unset in normal runs; set `GRIDLOCK_CAPTURE_SCREENSHOTS=1`, `GRIDLOCK_SAVE_TOOLBAR_SHOT=1`, `GRIDLOCK_VERIFY_TOOLBAR_PNG`, `GRIDLOCK_VERIFY_KEYBINDS_RECORDER_PNG`, `GRIDLOCK_VERIFY_PROJECT_TREE_PNG`, or `GRIDLOCK_VERIFY_KEYBINDINGS_PNG` on demand for visual audits.
- **Config Isolation Hygiene**:
  - Any slot modifying `ConfigManager` or `ShortcutManager` must guard/restore `XDG_CONFIG_HOME` and `GRIDLOCK_TEST_CONFIG_DIR`.

## Maintainer & Agent Notes

- **Critical Line Ceiling Warning (`944 / 950`)**:
  - `test_ui_aux.cpp` sits at `944` lines (right beneath the `< 950` limit).
  - Add new UX checks to `test_ui_viz_and_ux.cpp` or split a new `test_ui_*.cpp` translation unit registered in `ui_test_src` inside `meson.build`.
- Include & CWD Rules:
  - `#include` directives inside `tests/ui/` use `../../src/...`, whereas runtime `QFile` checks execute from `builddir/` (`../src/...`).
- Artifact & Workspace Cleanup:
  - Remove any temporary tutorial executables compiled during test runs (`tutorial/collective_mismatch`, `tutorial/inspection_demo`, etc.) before committing.
  - Keep this `README.md` synchronized whenever adding new `test_ui_*.cpp` splits or visual verification hooks.

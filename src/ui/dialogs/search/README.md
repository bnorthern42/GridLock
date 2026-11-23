# Search Everywhere & Command Palette (`src/ui/dialogs/search/`)

Multi-scope global Search Everywhere and fuzzy command palette overlay triggered via Double-Shift, `Ctrl+P`, or `Ctrl+Shift+P`.

## Contents

- `CommandPaletteDialog.hpp` (`166` lines):
  - Defines `PaletteItem` metadata (`File`, `Symbol`, `Text`, `Action`, `Rank`).
  - Declares `CommandPaletteDialog` entry points (`openSearchEverywhere`, `openFileSearch`, `openActionPalette`).
  - Exposes scope tabs (`All | Files | Symbols | Text | Actions | Ranks`), recent items state, and preview widget hooks.
- `CommandPaletteDialog.cpp`:
  - Implements power-prefix parsing (`>`, `@`, `#`, `/`) across modes.
  - Ranks results via CamelCase/acronym fuzzy matching in `SearchEverywhereEngine`.
  - Populates Recent Files and Recent Actions lists whenever the query input is empty.
  - Integrates an `ESC` keycap close pill button directly inside the top search bar.
  - Installs child event filters and renders a live syntax-highlighted split preview pane.

## Expected Behavior & Invariants

- **Trigger Modes & `Tab` Scope Cycling**:
  - Double-Shift (two Shift taps `<= 350 ms` in `MainWindow`) opens `All` mode, `Ctrl+P` opens `Files` mode, and `Ctrl+Shift+P` opens `Actions` mode.
  - Clicking scope pills or pressing `Tab` / `Shift+Tab` switches active scopes predictably.
- **Unconditional `Esc` Dismissal**:
  - Pressing `Esc` from the search box, scope bar, results list, or preview pane, or clicking the `ESC` pill button, closes the palette cleanly (`tests/ui/test_ui_viz_and_ux.cpp`).
- **Enter & Ctrl+Enter Navigation**:
  - `Enter` executes or opens the selected match immediately; `Ctrl+Enter` opens file/symbol hits in a split pane when supported.
- **Bounded Interactive Latency**:
  - File indexing, symbol scanning, and grep caps run via bounded `SearchEverywhereEngine` routines.
- **Verification**:
  - Run `meson test -C builddir ui_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Scoring & Indexing Ownership:
  - Keep workspace file scanning, symbol indexing, grep caps, and CamelCase fuzzy scoring strictly inside `src/core/utils/SearchEverywhereEngine.{hpp,cpp}` rather than duplicating logic inside `CommandPaletteDialog.cpp`.
- UI Selectors & Event Filters:
  - Preserve `CommandPaletteDialog` child `objectName()` values, integrated `ESC` pill button wiring, and event filter precedence verified in `tests/ui/test_ui_viz_and_ux.cpp`.
  - Keep `Esc` dismissal unconditional even when the results list or syntax preview pane currently holds keyboard focus.
- Theme & Size Hygiene:
  - Keep both `.hpp` and `.cpp` well below `< 950` lines and maintain Light/Dark theme alignment through `ThemeManager`.
  - Update this `README.md` whenever adding new search scopes, keybinding triggers, or preview formats.

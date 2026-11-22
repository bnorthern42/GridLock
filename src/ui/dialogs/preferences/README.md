# Workstation Preferences (`src/ui/dialogs/preferences/`)

Modular `PreferencesDialog` shell with a hierarchical `QTreeWidget` category tree paired with per-category settings pages and shared form/chrome helpers.

## Contents

- `PreferencesDialog.hpp` (`93`), `PreferencesDialog.cpp` (`669`), `PreferencesDialogChrome.cpp` (`228`):
  - Dialog coordinator owning the hierarchical `QTreeWidget` category selector (`Appearance -> Toolbar` expanded by default), vertical `QScrollArea` page wrappers, main toolbar layout editor (`#preferencesToolbarPage`), `Search Everywhere Popup Size` selector inside `Appearance`, `preferencesChanged` signaling, and `ThemeManager` QSS/palette/icon chrome.
- `PreferencesFormHelpers.hpp` (`126`):
  - Shared form row builders, file/directory browse rows, section headers, and monospace font discovery helpers.
- `AppearanceSettingsPage.hpp` (`54`), `AppearanceSettingsPage.cpp` (`227`):
  - Theme preset selector, global UI font sizing, editor font family/size controls, and general UI chrome layout controls.
- `DebuggerSettingsPage.hpp` (`46`), `DebuggerSettingsPage.cpp` (`139`):
  - GDB/LLDB binary paths, backend engine mode, MPI launcher defaults, and MI console logging.
- `EditingBehaviorPages.hpp` (`72`), `EditingBehaviorPages.cpp` (`206`):
  - `EditingSettingsPage` (tab width, rainbow brackets, large-file thresholds) and `BehaviorSettingsPage`.
- `HpcSettingsPages.hpp` (`104`), `HpcSettingsPages.cpp` (`340`), `SlurmBatchParser.cpp` (`241`):
  - `HpcSettingsPage`, `HpcIntegrationSettingsPage`, and bidirectional `#SBATCH` batch script parser/generator.
- `KeybindingsSettingsPage.hpp` (`108`), `KeybindingsSettingsPage.cpp` (`201`), `ShortcutItemDelegate.cpp` (`201`):
  - Shortcut table, opaque centered `KeySequenceRecorderWidget` (`QFrame` + `QLabel` + clear button), and `ShortcutManager` binding persistence.
- `TerminalDocsetsPages.hpp` (`63`), `TerminalDocsetsPages.cpp` (`203`):
  - `DocsetSettingsPage` (Dash/Zeal docset registry) and `TerminalSettingsPage` (shell, scrollback, colors).

## Expected Behavior & Invariants

- **Translation Unit Headroom (`< 950` Lines)**:
  - Keep every file well below `< 950` lines by placing separate settings categories in dedicated page files.
- **Hierarchical Category Tree (`Appearance -> Toolbar`)**:
  - Top-level categories expand by default in `#preferencesCategoryTree` (`headerHidden = true`, `indentation = 14`), and selecting either a top-level category or nested child item (`Toolbar`) switches `QStackedWidget` directly via `Qt::UserRole`.
- **Opaque Key Sequence Recorder**:
  - `KeySequenceRecorderWidget` paints an opaque vertically centered recorder widget without badge bleed-through (`tests/ui/test_ui_viz_and_ux.cpp`).
- **Selector & Persistence Parity**:
  - Preserve existing `QWidget::objectName()` strings, `QScrollArea` wrappers, page ordering, `preferencesChanged` signals, and `ConfigManager`/`ShortcutManager`/`ThemeManager` round-trips.
- **Verification**:
  - Run `meson test -C builddir ui_tests config_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Never consolidate separate pages back into `PreferencesDialog.cpp`; register new pages through the sidebar stack and update `meson.build` (`src_files`, `moc_headers`, `ui_test_src`, `ui_test_moc_headers`).
- Reuse `PreferencesFormHelpers.hpp` helpers instead of duplicating path-browse or spinbox boilerplate across pages.
- Verify Light/Dark theme chrome and `KeySequenceRecorderWidget` offscreen geometry via `GRIDLOCK_VERIFY_KEYBINDS_RECORDER_PNG` when modifying delegates.
- Update this `README.md` whenever adding, moving, or splitting settings page files.

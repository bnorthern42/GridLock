# UI Dialog Modules (`src/ui/dialogs/`)

Domain-organized modal, modeless, and anchored popover dialogs serving debugging configuration, scientific visualization, search, project setup, and workstation preferences.

## Contents

- `plotter/`:
  - `ArrayPlotDialog`, `ArrayPlotCanvas`, `ArrayPlotExport`, and `TensorSliceTableModel`.
  - Provides live 1D/2D array plotting, complex fields, multi-rank overlays, stats export, and virtualized 2D/3D tensor tables.
- `graph/`:
  - `PointerGraphDialog`, `PointerGraphLayout` (Sugiyama layered DAG solver), and `PointerGraphEvaluator`.
  - Visualizes live pointer and struct topologies with cycle/alias detection.
- `calculator/`:
  - `ProgrammerCalculatorDialog` and `CalculatorEngine`.
  - Syncs Hex/Dec/Oct/Bin inputs, 64-bit interactive bit toggles, endian swaps, and C-style expressions.
- `debugger/`:
  - `AttachProcessDialog`, `CheckpointDialog`, `ConditionalBreakpointDialog`, `SignalCatchpointDialog`, and `XrefInspectorDialog`.
- `project/`:
  - `ProjectSettingsDialog` (target settings, quick attach, new project, about modal) and multi-step `ProjectWizard`.
- `search/`:
  - `CommandPaletteDialog` for Double-Shift / `Ctrl+P` / `Ctrl+Shift+P` Search Everywhere and fuzzy action palette.
- `preferences/`:
  - Thin `PreferencesDialog` shell paired with modular per-category settings pages (`<= 400` lines per file).

## Expected Behavior & Invariants

- **Strict Domain Grouping**:
  - Zero dialog sources sit flat inside `src/ui/dialogs/`; every dialog resides inside its functional domain subdirectory.
- **Theme & `objectName()` Stability**:
  - All dialogs source colors and font metrics dynamically from `ThemeManager` and preserve stable `QWidget::objectName()` identifiers verified by `tests/ui/`.
- **Offscreen Headless Execution**:
  - Dialog constructors, show events, and layouts run cleanly under `QT_QPA_PLATFORM=offscreen` without hanging on native platform dialogs.
- **Verification**:
  - Run `meson test -C builddir ui_tests dap_commands_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Place new multi-file dialogs in an existing domain subdirectory (or introduce a clean new domain directory) instead of placing flat files under `src/ui/dialogs/`.
- Read the target subdirectory's `README.md` before modifying code inside `preferences/`, `plotter/`, `graph/`, `debugger/`, or `search/`.
- Keep `meson.build` (`src_files`, `moc_headers`, `ui_test_src`, `ui_test_moc_headers`, and `dap_commands_test_*`) synchronized on every file add, move, or split.
- Update both this index `README.md` and the affected subdirectory `README.md` when reorganizing dialog files.

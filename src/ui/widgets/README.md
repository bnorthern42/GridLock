# Workstation Support Widgets (`src/ui/widgets/`)

Reusable docked and overlay widgets for project browsing, vector file icons, inline search, expression watching, MPI diagnostics, HPC packages, and offline manuals.

## Contents

- `ProjectExplorerWidget.hpp` (`260`), `ProjectExplorerWidget.cpp` (`397`), `ProjectExplorerActions.cpp` (`382`):
  - Tri-mode explorer dock (`FILES | SYMBOLS | STRINGS`) UI chrome, directory tree model, theme styling, and keyboard/breadcrumb navigation (`ProjectExplorerWidget.cpp`).
  - File/folder CRUD context menus, Git status badge watcher, and ELF symbol/string extraction plus live filtering (`ProjectExplorerActions.cpp`).
- `FileIconProvider.hpp` (`47`), `FileIconProvider.cpp`:
  - High-DPI `@2x` theme-aware `QPainter` vector file and semantic folder icon provider (`QAbstractFileIconProvider`) with zero external icon font dependency.
- `DifferentialGrid.hpp` (`139`), `DifferentialGrid.cpp`:
  - 2D table comparing MPI ranks (rows) against watched expressions (columns) with cross-rank divergence highlighting.
- `ExpressionEvaluatorWidget.hpp` (`90`), `ExpressionEvaluatorWidget.cpp`:
  - Dockable interactive C/C++ expression evaluator across active cluster ranks.
- `HpcConsoleWidget.hpp` (`208`), `HpcConsoleWidget.cpp` (`470`), `HpcConsoleFormatting.cpp` (`293`):
  - Multi-distro package & HPC console construction, command execution, Spack signal handlers, and Slurm/SSH remote probing (`HpcConsoleWidget.cpp`).
  - Rich-text output banner rendering, package table/list parsing (`spack`, `pacman`, `dnf`, `apt`, `zypper`, `rpm`, `dpkg-query`, `eb`), filtering, and theme styles (`HpcConsoleFormatting.cpp`).
- `MpiDiagnosticsWidget.hpp` (`177`), `MpiDiagnosticsWidget.cpp`, `MpiNetworkLogWidget.hpp` (`120`), `MpiNetworkLogWidget.cpp`:
  - Automated MPI deadlock alert dashboard and 6-column P2P/collective network log trace table.
- `HoverWidget.hpp` (`66`), `HoverWidget.cpp`:
  - Floating markdown hover tooltip card for symbol inspection.
- `InlineFindBar.hpp` (`139`), `InlineFindBar.cpp`:
  - Compact `Ctrl+F` inline search bar attached to `QPlainTextEdit` viewports with `Escape` focus return.
- `ReferenceManualWidget.hpp` (`211`), `ReferenceManualWidget.cpp`:
  - Embedded `DocsetViewerWidget`, `ManPageViewerWidget`, and F1 reference browser.

## Expected Behavior & Invariants

- **Focus & `Esc` Discipline**:
  - `InlineFindBar` returns keyboard focus to its host editor on `Esc`; `HoverWidget` dismisses reliably without stealing active typing focus.
- **Translation Unit Line Budget**:
  - `ProjectExplorerWidget.cpp` (`397`), `ProjectExplorerActions.cpp` (`382`), `HpcConsoleWidget.cpp` (`470`), and `HpcConsoleFormatting.cpp` (`293`) all sit strictly `< 500` lines.
- **Uniform Header Chrome**:
  - Custom header strips (`MpiNetworkLogWidget`, `DifferentialGrid`, `ProjectExplorerWidget`) synchronize height and palette colors via `ThemeManager`.
- **Verification**:
  - Run `meson test -C builddir ui_tests mi_parser_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Keep `InlineFindBar` lightweight (`QtWidgets` only) because standalone test targets (`mi_parser_tests` in `meson.build`) link it directly.
- Note that `MainWindowWorkspace.cpp` currently includes `widgets/HpcConsoleWidget.cpp` directly while `HpcConsoleFormatting.cpp` is compiled as a standalone unit in `meson.build` (`src_files` and `ui_test_src`).
- Add new language/build extensions to `FileIconProvider` with clean vector `QPainter` geometry rather than bundling bitmap icons, and keep split widget units strictly `< 500` lines.

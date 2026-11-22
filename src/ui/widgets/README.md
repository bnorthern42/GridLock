# Workstation Support Widgets (`src/ui/widgets/`)

Reusable docked and overlay widgets for project browsing, vector file icons, inline search, expression watching, MPI diagnostics, HPC packages, and offline manuals.

## Contents

- `ProjectExplorerWidget.hpp` (`260`), `ProjectExplorerWidget.cpp` (`936`):
  - Tri-mode explorer dock (`FILES | SYMBOLS | STRINGS`) combining workspace directory trees, symbol search, extracted binary strings, and synchronized address seeking.
- `FileIconProvider.hpp` (`47`), `FileIconProvider.cpp`:
  - High-DPI `@2x` theme-aware `QPainter` vector file and semantic folder icon provider (`QAbstractFileIconProvider`) with zero external icon font dependency.
- `DifferentialGrid.hpp` (`139`), `DifferentialGrid.cpp`:
  - 2D table comparing MPI ranks (rows) against watched expressions (columns) with cross-rank divergence highlighting.
- `ExpressionEvaluatorWidget.hpp` (`90`), `ExpressionEvaluatorWidget.cpp`:
  - Dockable interactive C/C++ expression evaluator across active cluster ranks.
- `HpcConsoleWidget.hpp` (`207`), `HpcConsoleWidget.cpp` (`929`):
  - Extends `SpackManager` into a multi-distro package & HPC console supporting `spack`, `pacman`, `dnf`, `apt`, `zypper`, `rpm`, `dpkg-query`, and `eb` across Table, List, and Raw views.
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
- **Critical Line Ceiling Watch**:
  - `ProjectExplorerWidget.cpp` (`936` lines) and `HpcConsoleWidget.cpp` (`929` lines) sit directly below `< 950` lines; extract parser or model helpers before expanding either widget.
- **Uniform Header Chrome**:
  - Custom header strips (`MpiNetworkLogWidget`, `DifferentialGrid`, `ProjectExplorerWidget`) synchronize height and palette colors via `ThemeManager`.
- **Verification**:
  - Run `meson test -C builddir ui_tests mi_parser_tests --print-errorlogs`.

## Maintainer & Agent Notes

- Keep `InlineFindBar` lightweight (`QtWidgets` only) because standalone test targets (`mi_parser_tests` in `meson.build`) link it directly.
- Add new language/build extensions to `FileIconProvider` with clean vector `QPainter` geometry rather than bundling bitmap icons.
- Update this `README.md` and `meson.build` whenever splitting helpers out of `ProjectExplorerWidget.cpp` or `HpcConsoleWidget.cpp`.

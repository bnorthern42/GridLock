# Main Workstation UI (`src/ui/`)

Modular `MainWindow` workstation shell, multi-pane editor tab container, MI console widget, and domain UI subdirectories.

## Contents

- `MainWindow.hpp` (`492`), `MainWindowInternal.hpp` (`238`), `MainWindow.cpp` (`939`):
  - Core `QMainWindow` shell, session load/save persistence, coordinator teardown guards, and shared cross-module structs/helpers.
- `MainWindowWorkspace.cpp`, `MainWindowDocks.cpp`, `MainWindowPerspectives.cpp`:
  - Four-zone splitter/dock choreography, float/redock transitions, Code/Debug/Binary perspectives, typography scaling, and theme propagation.
- `MainWindowToolbar.cpp`, `MainWindowExecution.cpp`, `MainWindowDebugger.cpp`:
  - `#MainToolbar` creation, contextual HPC (`ranks > 1`) and `Binary` tool disclosure, execution scope dispatch, stepping workflows, and `setFocusedRank()`.
- `MainWindowBuild.cpp`, `MainWindowInspection.cpp`, `MainWindowNavigation.cpp` (`873`), `MainWindowCohesion.cpp`:
  - Pre-flight builds and attach flows, inspection pivots, address navigation history, F1 docset routing, ELF loading, Search Everywhere indexing, and cohesion audits.
- `EditorTabManager.hpp` (`221`), `EditorTabManager.cpp`:
  - Split `QTabWidget` source container routing `.csv`/`.tsv` files `>= 256 KB` to `CsvTablePreviewWidget` and files `>= 512 KB` to fast plain-text mode.
- `GdbConsoleWidget.hpp` (`221`), `GdbConsoleWidget.cpp`:
  - Multi-rank interactive GDB/MI console viewer, rank filter bar, and command sender (`tests/hpc/test_mi_parser.cpp`).
- Subdirectories: `dialogs/`, `docks/`, `views/`, `widgets/`, `shaders/`, `tutorial/`, `utils/`.

## Expected Behavior & Invariants

- **Critical Line Ceiling Watch**:
  - `MainWindow.cpp` (`939` lines) and `MainWindowNavigation.cpp` (`873` lines) sit close to `< 950` lines.
  - Route new slots to the matching domain `MainWindow*.cpp` unit or extract a cohesive helper file before expanding `MainWindow.cpp`.
- **Contextual Toolbar Width Budget**:
  - `#MainToolbar` hides/collapses specialized HPC (`ranks <= 1`) and reverse-engineering (outside `Binary`) controls so visible widgets fit comfortably under `1,040 px` (`tests/ui/test_ui_viz_and_ux.cpp`).
- **Zero Dead GUI Controls**:
  - Every `QAction`, menu item, and toolbar button must have an explicit `QObject::connect(...)` binding.
- **Verification**:
  - Run `ninja -C builddir gridlock_ui_tests && QT_QPA_PLATFORM=offscreen ./builddir/gridlock_ui_tests`.

## Maintainer & Agent Notes

- Split Architecture & Build Sync:
  - Check `MainWindowInternal.hpp` before adding shared widget pointers, timers, or helper functions across `MainWindow*.cpp` splits.
  - Register new `.cpp` splits or `Q_OBJECT` headers in both `src_files` / `moc_headers` and `ui_test_src` / `ui_test_moc_headers` in `meson.build`.
- Layering & Performance Guards:
  - Keep protocol parsing and numeric scoring inside `src/core/`; keep `MainWindow*.cpp` focused on Qt coordination and view updates.
  - Preserve large-file routing in `EditorTabManager` (`>= 256 KB` CSV/TSV preview, `>= 512 KB` raw plain text without highlighter or `clangd`).
- Ongoing Maintenance:
  - Update this `README.md` whenever adding, moving, or splitting top-level `src/ui/` modules or subdirectories.

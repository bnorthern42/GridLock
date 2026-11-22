# Main Workstation UI (`src/ui/`)

Modular `MainWindow` workstation shell, multi-pane editor tab container, MI console widget, and domain UI subdirectories.

## Contents

- `MainWindow.hpp` (`492`), `MainWindowInternal.hpp` (`240`), `MainWindow.cpp` (`432`):
  - Core `QMainWindow` shell construction, subview accessors, hover tooltip helper, menu bar setup, and shared cross-module structs/helpers.
- `MainWindowLifecycle.cpp` (`454`):
  - Backend coordinator binding (`setCoordinator`), preview fixture seeding, status-bar memory telemetry, `closeEvent()` session/geometry save and synchronous MPI/PTY/LSP teardown, and global `eventFilter()` (`Double Shift` and IDE shortcuts).
- `MainWindowNavigation.cpp` (`385`), `MainWindowBinaryNav.cpp` (`471`):
  - Source/session TOML I/O, recent workspaces menu, Search Everywhere and Calculator triggers, workspace PNG snapshots, tutorial compilation (`MainWindowNavigation.cpp`), plus `AddressSpaceNavBar`, `seekToAddressOrExpression()` history, lazy hidden-disassembly CFG/decompiler sync, `F12` symbol jumps, and synthetic ELF memory windows (`MainWindowBinaryNav.cpp`).
- `MainWindowWorkspace.cpp` (`483`), `MainWindowDocks.cpp` (`652`), `MainWindowPerspectives.cpp` (`534`):
  - Four-zone splitter/dock choreography, float/redock transitions, Code/Debug/Binary perspectives, typography scaling, and theme propagation.
- `MainWindowToolbar.cpp` (`533`), `MainWindowExecution.cpp` (`486`), `MainWindowDebugger.cpp` (`512`):
  - `#MainToolbar` creation, contextual HPC (`ranks > 1`) and `Binary` tool disclosure, pre-launch regular-executable validation (`validateTargetExecutableForLaunch`), execution scope dispatch, stepping workflows, and `setFocusedRank()`.
- `MainWindowBuild.cpp` (`496`), `MainWindowInspection.cpp` (`516`), `MainWindowCohesion.cpp` (`522`):
  - Pre-flight `-g -O0` compilation (resolving extensionless targets to sibling `.cpp`/`.c`/`.cc`/`.cxx` sources, skipping non-code data extensions, enforcing `0755` execute permissions, and caching `(path, mtime, size)` stamps to skip redundant `loadBinaryTarget` reloads), attach flows, inspection pivots, F1 docset routing, ELF loading, Search Everywhere indexing, and cohesion audits.
- `EditorTabManager.hpp` (`221`), `EditorTabManager.cpp`:
  - Split `QTabWidget` source container routing `.csv`/`.tsv` files `>= 256 KB` to `CsvTablePreviewWidget` and files `>= 512 KB` to fast plain-text mode.
- `GdbConsoleWidget.hpp` (`221`), `GdbConsoleWidget.cpp`:
  - Multi-rank interactive GDB/MI console viewer, rank filter bar, and command sender (`tests/hpc/test_mi_parser.cpp`).
- Subdirectories: `dialogs/`, `docks/`, `views/`, `widgets/`, `shaders/`, `tutorial/`, `utils/`.

## Expected Behavior & Invariants

- **Translation Unit Line Budget**:
  - Core shell and navigation units (`MainWindow.cpp`, `MainWindowLifecycle.cpp`, `MainWindowNavigation.cpp`, `MainWindowBinaryNav.cpp`) sit strictly `< 500` lines; `MainWindowDocks.cpp` (`652`) is the largest unit in `src/ui/` and all files must stay well below `< 950` lines.
  - Route new slots to the matching domain `MainWindow*.cpp` unit or extract a cohesive helper file rather than growing existing units past `550` lines.
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

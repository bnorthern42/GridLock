# Workstation Dock Widgets (`src/ui/docks/`)

Dockable debugger and terminal panels integrated into `MainWindow`'s right and bottom workstation zones.

## Contents

- `CallStackDockWidget.hpp` (`137` lines), `CallStackDockWidget.cpp`:
  - Combines an OS/OpenMP thread selector (`threadSelectorCombo`) with a 64-frame unwound call stack table (`callStackTable`).
  - Clicking any caller frame or switching active threads synchronizes `SourceCodeView`, `DisassemblyView`, `VariablesDockWidget`, and `RegisterView`.
- `VariablesDockWidget.hpp` (`156` lines), `VariablesDockWidget.cpp`:
  - Hierarchical local/watch tree wrapping `VariableTreeModel` and `QSortFilterProxyModel`.
  - Supports MPI rank switching, substring filtering, inline value editing, and lazy struct/pointer child expansion.
- `TerminalDockWidget.hpp` (`198` lines), `TerminalDockWidget.cpp` (`467` lines):
  - Embedded VT100/truecolor PTY terminal emulator wrapping `QTermWidget`.
  - Manages shell startup/teardown, monospace font sizing, and color-scheme updates.
  - Implements `sendCommand()` automation, scrollback buffers, compact `QMenu#terminalContextMenu` (`Copy`, `Paste`, `Select All`, `Clear Terminal` with clean newline prompt redraw), and clipboard `Ctrl+Shift+C` / `Ctrl+Shift+V` shortcuts.

## Expected Behavior & Invariants

- **Valid Frame Context Switching**:
  - `CallStackDockWidget` validates frame rows and instruction addresses before emitting frame/thread change signals so views never jump to unmapped `0x0` locations.
- **Deterministic PTY Lifecycle & Teardown**:
  - `TerminalDockWidget` cleanly closes its child PTY shell prior to widget destruction so offscreen tests and app shutdown never hang on orphaned shell processes.
- **Clipboard & Compact Context Menu Reliability**:
  - `Ctrl+Shift+C` and `Ctrl+Shift+V` intercept key events cleanly and interact directly with `QGuiApplication::clipboard()` without shell escape swallowing (`tests/ui/test_ui_aux.cpp`).
  - `clearTerminal()` clears scrollback/output, writes `\n` (never `\x0c`) when the PTY shell is active so `zsh`/`bash` prints a fresh prompt without triggering Zsh `Ctrl+L` variable-expansion widgets, and retains keyboard focus (`tests/ui/test_ui_aux.cpp`).
  - `enforceMonospaceOnChildren()` and `eventFilter()` skip `QMenu` and `isWindow()` popups so `#terminalContextMenu` preserves its compact `Qt::Popup` `sizeHint()` and renders `Copy`, `Paste`, `Select All`, and `Clear Terminal` with legible disabled contrast (`tests/ui/test_ui_viz_and_ux.cpp`).
- **Verification**:
  - Run `meson test -C builddir ui_tests --print-errorlogs` after modifying dock layouts, terminal PTY handling, or variable filtering.

## Maintainer & Agent Notes

- Coordinator Decoupling:
  - Keep GDB/MI and DAP parsing out of dock widgets; wire docks to `IBackendCoordinator` signals cleanly inside `MainWindowDocks.cpp` and `MainWindowInspection.cpp`.
- Terminal PTY & Clipboard Testing:
  - Test both keyboard clipboard shortcuts (`Ctrl+Shift+C`/`Ctrl+Shift+V`) and non-blocking PTY shutdown under `QT_QPA_PLATFORM=offscreen` whenever modifying `TerminalDockWidget`.
  - Guard optional `QTermWidget` feature flags cleanly so builds remain portable across supported Linux toolchains.
- Visual Metrics & File Ceiling:
  - Preserve `ThemeManager` font and dock-header palette propagation across every dock panel.
  - Keep all frame and variable selections bounds-checked against active coordinator lists.
  - Maintain strict `< 950` lines per file and keep Google C++ conventions (`#pragma once`, `override`, `m_camelCase`).
  - Update `meson.build` and this `README.md` whenever adding or splitting dock panels.

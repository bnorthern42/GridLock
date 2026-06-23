# Views and Docks

GridLock utilizes a dynamic `QSplitter`-based layout populated by custom Qt6 view classes and payload docks designed for specific data density.

## 🪟 Main UI Frame & Custom Views

| Component | Files | Description |
| :--- | :--- | :--- |
| **MainWindow** | `MainWindow.hpp`, `MainWindow.cpp` | The core `QMainWindow`. Bootstraps the layout, injects `ShortcutManager`, and manages dock visibility logic. |
| **ServerRackView** | `ServerRackView.hpp`, `ServerRackView.cpp` | Renders a real-time matrix of MPI ranks displaying halted/running state via block colors. |
| **SourceCodeView** | `SourceCodeView.hpp`, `SourceCodeView.cpp` | Inherits `QPlainTextEdit`. Implements line gutters, breakpoints, and LSP syntax highlighting. |
| **DisassemblyView** | `DisassemblyView.hpp`, `DisassemblyView.cpp` | Syncs DWARF instructions to the active `SourceCodeView` line, providing synchronized assembly views. |
| **MemView** | `MemView.hpp`, `MemView.cpp` | A high-performance hex editor interface rendering chunks via `process_vm_readv`. |
| **RegisterView** | `RegisterView.hpp`, `RegisterView.cpp` | Provides 64-bit CPU register states bound to the `DifferentialGrid` for mutation highlighting. |

## 🚢 Dock Payloads

| Component | Files | Description |
| :--- | :--- | :--- |
| **TerminalDock** | `TerminalDock.hpp`, `TerminalDock.cpp` | A standard interactive PTY overlay bound via `QProcess` to the system shell. |
| **VariablesDockWidget** | `VariablesDockWidget.hpp`, `VariablesDockWidget.cpp` | The visual container wrapping the `VariableTreeModel`. |
| **GdbConsoleWidget** | `GdbConsoleWidget.hpp`, `GdbConsoleWidget.cpp` | Provides a direct REPL interface to the target GDB rank with custom log filtering. |
| **DomainHeatmapWidget** | `DomainHeatmapWidget.hpp`, `DomainHeatmapWidget.cpp` | The `QOffscreenSurface` UI frame that hosts the raw Vulkan render instance. |
| **DomainHeatmapRenderer** | `DomainHeatmapRenderer.hpp`, `DomainHeatmapRenderer.cpp` | The Vulkan fragment/compute shader pipeline converting memory matrices into thermal maps. |

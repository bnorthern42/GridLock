# Widgets and Dialogs

This section catalogs the reusable micro-components and modal configuration windows across the application.

## 🧩 Reusable Widgets

| Component | Files | Description |
| :--- | :--- | :--- |
| **ProjectExplorerWidget** | `ProjectExplorerWidget.hpp`, `ProjectExplorerWidget.cpp` | Navigates the workspace file tree using `QFileSystemModel`. |
| **ExpressionEvaluatorWidget** | `ExpressionEvaluatorWidget.hpp`, `ExpressionEvaluatorWidget.cpp` | A one-line REPL for quick variable queries or DAP expression tests. |
| **ReferenceManualWidget** | `ReferenceManualWidget.hpp`, `ReferenceManualWidget.cpp` | Tabbed HTML viewer hosting Zeal/Dash docsets or system `man` pages. |
| **DifferentialGrid** | `DifferentialGrid.hpp`, `DifferentialGrid.cpp` | Signal-slot bridge widget used internally by Tables to flash colors (Yellow) upon rapid state change. |
| **MpiDiagnosticsWidget** | `MpiDiagnosticsWidget.hpp`, `MpiDiagnosticsWidget.cpp` | Displays the Deadlock graphical dependencies and enables/disables the FPE Trapper. |

## 💬 Popup Dialogs

| Component | Files | Description |
| :--- | :--- | :--- |
| **ProjectSettingsDialog** | `ProjectSettingsDialog.hpp`, `ProjectSettingsDialog.cpp` | A modal to modify local workspace `settings.toml` configurations (HPC nodes, rank counts). |
| **ProjectWizardDialog** | `ProjectWizardDialog.hpp`, `ProjectWizardDialog.cpp` | A setup tool for generating standard `meson.build` or CMake skeletons for new MPI projects. |
| **PreferencesDialog** | `PreferencesDialog.hpp`, `PreferencesDialog.cpp` | Global IDE settings such as dark/light themes, default shortcuts, and SSH remote targets. |
| **ConditionalBreakpointDialog** | `ConditionalBreakpointDialog.hpp`, `ConditionalBreakpointDialog.cpp` | An expression prompt enabling dynamic looping halts based on GDB boolean logic. |

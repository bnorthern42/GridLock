# Managers and Data Models

This document outlines the singleton managers and core data structures controlling the application's global state, settings, and command routing.

## 🛠️ State and Configuration Managers

| Component | Files | Description |
| :--- | :--- | :--- |
| **ConfigManager** | `ConfigManager.hpp`, `ConfigManager.cpp` | Parses `.gridlock/settings.toml` and `.gridlock/workspace.toml`. Handles disk I/O serialization of run profiles. |
| **SpackManager** | `SpackManager.hpp`, `SpackManager.cpp` | Invokes remote `spack env list`, allowing the user to select and inject Spack environments dynamically into target run parameters. |
| **ShortcutManager** | `ShortcutManager.hpp`, `ShortcutManager.cpp` | A global singleton applying Vim-style key sequences via an injected `QObject` event filter on the `MainWindow`. |
| **DocsetManager** | `DocsetManager.hpp`, `DocsetManager.cpp` | Wraps SQLite for Zeal/Dash offline HTML docsets, managing indexed keyword searches. |
| **ThemeManager** | `ThemeManager.hpp`, `ThemeManager.cpp` | Dynamically overrides `QPalette` colors, maintaining the global Catppuccin Mocha QSS application stylesheet. |
| **EditorTabManager** | `EditorTabManager.hpp`, `EditorTabManager.cpp` | Manages the tab lifecycle of open `SourceCodeView` documents, managing unsaved dirty states and external file reloads. |

## 📊 Data Models

| Component | Files | Description |
| :--- | :--- | :--- |
| **VariableTreeModel** | `VariableTreeModel.hpp`, `VariableTreeModel.cpp` | Implements `QAbstractItemModel`. Powers the lazy-loading hierarchy inside the VariablesDock for deep struct pointers and vectors. |

## ⚙️ Command Execution

| Component | Files | Description |
| :--- | :--- | :--- |
| **DebugCommands** | `DebugCommands.hpp`, `DebugCommands.cpp` | Implements the Command Pattern for actions like `StepOverCommand` and `ToggleBreakpointCommand`. Triggered by the `ShortcutManager`. |

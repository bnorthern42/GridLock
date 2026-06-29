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
| **VariableTreeModel** | `VariableTreeModel.hpp`, `VariableTreeModel.cpp` | Implements `QAbstractItemModel`. Powers the lazy-loading hierarchy inside the VariablesDock. |

### Lazy Loading Architecture (`VariableTreeModel`)
To prevent I/O blocking and massive payload lags, GridLock **no longer eager-fetches** all variable scopes upon a debugger stop event. Instead, the model implements Qt's dynamic lazy-loading pattern via `canFetchMore()` and `fetchMore()`. Variables are only requested from the DAP/GDB backend when the user physically expands a node in the UI.

*Example: Lazy-fetching DAP variables in `VariableTreeModel`:*
```cpp
bool VariableTreeModel::canFetchMore(const QModelIndex& parent) const {
    VariableNode* node = getNode(parent);
    // Return true if node indicates children exist but they haven't been loaded
    return node && (node->numChildren > 0 || node->variablesReference > 0) && !node->childrenLoaded;
}

void VariableTreeModel::fetchMore(const QModelIndex& parent) {
    if (!parent.isValid()) return;
    VariableNode* node = getNode(parent);
    
    if (node && (node->numChildren > 0 || node->variablesReference > 0) && !node->childrenLoaded) {
        node->childrenLoaded = true; // Prevent recursive or looped fetching
        
        if (auto dap = dynamic_cast<DapCoordinator*>(m_coordinator)) {
            if (node->variablesReference > 0) {
                // Asynchronously request the variables from the backend
                dap->requestVariables(m_currentRankId, node->variablesReference);
            }
        }
    }
}
```
## ⚙️ Command Execution

| Component | Files | Description |
| :--- | :--- | :--- |
| **DebugCommands** | `DebugCommands.hpp`, `DebugCommands.cpp` | Implements the Command Pattern for actions like `StepOverCommand` and `ToggleBreakpointCommand`. Triggered by the `ShortcutManager`. |

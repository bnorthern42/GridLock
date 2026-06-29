# 🖼️ UI Internals & Views

GridLock's UI is written entirely in Qt6 C++, leveraging a heavily customized widget architecture to achieve a modern, dense aesthetic without relying on QML overhead.

## 🪟 Layout Logic

*   **`MainWindow.cpp`**: Instantiates a central `QSplitter` that manages the three primary column views. It registers global event filters and mounts the `Qt-Advanced-Stylesheets` theming engine.

## 👁️ Core Views (The `QSplitter` Panes)

*   **`SourceCodeView.cpp`**: Inherits from `QPlainTextEdit`. Implements line number gutters, logical block-based breakpoint tracking (preventing visual drift), and overlays LSP `clangd` tooltips.
*   **`DisassemblyView.cpp`**: Renders raw instruction streams. Contains internal models to map DWARF instruction pointers back to the `SourceCodeView` lines.
*   **`ServerRackView.cpp`**: A custom `QWidget` that paints a grid of indicators representing the state of all active MPI ranks.
*   **`MemView.cpp`**: A specialized hex editor widget that interfaces directly with the `NativeMemoryReader` for infinite scrolling of raw process memory.
*   **`RegisterView.cpp`**: Displays x86_64/ARM CPU registers. Integrates with the `DifferentialGrid` to highlight state mutations.

## 🚢 Docks & Widgets

*   **`MpiDiagnosticsWidget.cpp`**: Renders dependency graphs to locate `MPI_Wait` deadlocks and toggles FPE trappers.
*   **`DifferentialGrid.cpp`**: The underlying engine for the Variables/Registers docks. Performs real-time state diffing to trigger yellow visual highlights when values change between execution steps.
*   **`TerminalDock.cpp`**: A `QProcess`-backed terminal emulator overlay.
*   **`VariablesDockWidget.cpp`**: Hosts the `VariableTreeModel` to display complex nested structs and pointers.
*   **`DomainHeatmapWidget.cpp`**: Utilizes a raw Vulkan surface to render 2D memory matrices (e.g., fluid dynamics arrays) in real-time.

## 🧠 Models & Managers

*   **`VariableTreeModel.cpp`**: Implements `QAbstractItemModel` for lazy-loading Deep GDB/MI struct evaluations.
*   **`Qt-Advanced-Stylesheets` (External)**: Replaces the legacy `ThemeManager`. It compiles and injects Material Design themes (`qt_material` / `dark_teal`) globally at runtime, allowing UI components to natively inherit SCSS/QSS rules without hardcoded styles.
    
    *Example: Instantiating the global stylesheet in `main.cpp`:*
    ```cpp
    acss::QtAdvancedStylesheet advancedStylesheet;
    QString stylesDir = QApplication::applicationDirPath() + "/styles";
    
    // Fallback to install path if running from build/
    if (!QDir(stylesDir).exists()) {
      stylesDir = "/usr/share/gridlock/styles"; 
    }
    
    advancedStylesheet.setStylesDirPath(stylesDir);
    advancedStylesheet.setOutputDirPath(QDir::tempPath() + "/gridlock_acss");
    advancedStylesheet.setCurrentStyle("qt_material");
    advancedStylesheet.setCurrentTheme("dark_teal");
    app.setStyleSheet(advancedStylesheet.styleSheet());
    ```
*   **`ShortcutManager.cpp`**: Intercepts `QEvent::KeyPress` globally. Implements a Vim-style chorded command pattern while safely skipping `QLineEdit` inputs.
*   **`EditorTabManager.cpp`**: Manages the open document lifecycle, dirty states, and integrates with the `SourceCodeView` instances.

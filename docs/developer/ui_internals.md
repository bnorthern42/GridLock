# 🖼️ UI Internals & Views

> **Last updated: v0.6.0**

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
*   **`TerminalDockWidget.cpp`** *(v0.6.0)*: A fully interactive embedded terminal backed by the `qtermwidget` library. Provides a real PTY (pseudo-terminal) session, replacing the old `QProcess`-based overlay. Declared as a Meson dependency via `dependency('qtermwidget6')`.
*   **`MpiNetworkLogWidget.cpp`** *(v0.6.0)*: A **read-only** log widget that captures and displays raw DAP/GDB stdout streams from the active debug session. Found under the **MPI Diagnostics** tab. This widget replaced the old "Network Log" portion of the monolithic pseudo-terminal.
*   **`VariablesDockWidget.cpp`**: Hosts the `VariableTreeModel` to display complex nested structs and pointers.
*   **`DomainHeatmapWidget.cpp`**: Utilizes a raw Vulkan surface to render 2D memory matrices (e.g., fluid dynamics arrays) in real-time.

## 🧠 Models & Managers

*   **`VariableTreeModel.cpp`**: Implements `QAbstractItemModel` for lazy-loading Deep GDB/MI struct evaluations.
*   **`Qt-Advanced-Stylesheets` (External Submodule)**: Replaces the legacy `ThemeManager`. It compiles and injects Material Design themes (`qt_material` / `dark_teal`) globally at runtime, allowing UI components to natively inherit SCSS/QSS rules without hardcoded styles.
    > **Note on modifying external submodules:** `Qt-Advanced-Stylesheets` is maintained upstream and imported as a git submodule. We do not modify its internal C++ source code. Any compiler warnings emitted by this library are suppressed at the build-system level via the wrapper `external/Qt-Advanced-Stylesheets/meson.build`.

    #### v0.6.0: Automated `.qrc` Asset Bundling

    Prior to v0.6.0, the application searched for the `styles/` directory at runtime relative to `applicationDirPath()`, which was fragile on installed or AppImage-packaged builds. In v0.6.0, all stylesheet assets are compiled directly into the binary via a generated Qt Resource file.

    **How it works in `meson.build`:**
    ```meson
    # 1. A custom_target copies/generates the acss_resources.qrc file
    acss_qrc = custom_target(
      'acss_resources_qrc',
      input  : acss_style_files,   # all .json, .qss, .svg assets from the submodule
      output : 'acss_resources.qrc',
      command: [generate_qrc_script, '@OUTPUT@', '@INPUT@']
    )

    # 2. qt.compile_resources() processes the .qrc into a linkable C++ object
    acss_resources = qt.compile_resources(sources: acss_qrc)

    # 3. The object is linked into the main executable target
    executable('gridlock', sources + acss_resources, ...)
    ```

    **Consequence for `main.cpp`:** The stylesheet initializer no longer needs a filesystem path fallback. Assets are accessed via the `:/` Qt resource path prefix:
    ```cpp
    acss::QtAdvancedStylesheet advancedStylesheet;
    // Assets are now embedded; setStylesDirPath points to the qrc prefix
    advancedStylesheet.setStylesDirPath(":/acss/styles");
    advancedStylesheet.setOutputDirPath(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/acss"
    );
    advancedStylesheet.setCurrentStyle("qt_material");
    advancedStylesheet.setCurrentTheme("dark_teal");
    app.setStyleSheet(advancedStylesheet.styleSheet());
    ```
*   **`ShortcutManager.cpp`**: Intercepts `QEvent::KeyPress` globally. Implements a Vim-style chorded command pattern while safely skipping `QLineEdit` inputs.
*   **`EditorTabManager.cpp`**: Manages the open document lifecycle, dirty states, and integrates with the `SourceCodeView` instances.

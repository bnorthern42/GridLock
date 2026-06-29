# Widgets and Dialogs

This section catalogs the reusable micro-components and modal configuration windows across the application.

## 🧩 Reusable Widgets

| Component | Files | Description |
| :--- | :--- | :--- |
| **ProjectExplorerWidget** | `ProjectExplorerWidget.hpp`, `ProjectExplorerWidget.cpp` | Navigates the workspace file tree using `QFileSystemModel`. Integrates a custom `FileIconProvider` to render Nerd Fonts dynamically. |
| **HoverWidget** | `HoverWidget.hpp`, `HoverWidget.cpp` | An overlay widget built to intercept LSP markdown. Uses `Qt::Popup` instead of `Qt::ToolTip` and avoids `WA_TranslucentBackground` to prevent visual tearing on Wayland compositors. |
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

## 🖌️ Rendering Infrastructure

### Nerd Font Icons (`FileIconProvider.cpp`)
To avoid blurry or miscolored static SVGs, GridLock paints Unicode glyphs directly onto `QPixmap` buffers using the integrated `SymbolsNerdFont-Regular.ttf`.

*Example: Mapping a Nerd Font hex code based on file type:*
```cpp
QIcon FileIconProvider::icon(const QFileInfo &info) const {
  QPixmap pixmap(16, 16);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);

  QFont font("Symbols Nerd Font", 12);
  painter.setFont(font);

  // Default color and icon
  QColor iconColor = QColor("#cdd6f4");
  QString unicodeStr = "\uf15b"; 

  if (info.isDir()) {
    unicodeStr = "\uf07b"; // Folder closed
    iconColor = QColor("#89b4fa"); // Blue
  } else if (info.suffix().toLower() == "cpp") {
    unicodeStr = "\ue61d"; // C++ logo
  }

  painter.setPen(iconColor);
  painter.drawText(pixmap.rect(), Qt::AlignCenter, unicodeStr);
  return QIcon(pixmap);
}
```

### Wayland UI Overlays (`HoverWidget.cpp`)
When displaying LSP hover data, `Qt::ToolTip` behaves unreliably across differing Wayland compositors (e.g., Sway vs. KDE KWin). We utilize `Qt::Popup` combined with `Qt::FramelessWindowHint` to guarantee standard window geometry while maintaining strict Z-ordering.

*Example: Initializing a safe Wayland overlay widget:*
```cpp
HoverWidget::HoverWidget(QWidget* parent) : QWidget(parent) {
    // Avoid Qt::ToolTip and WA_TranslucentBackground on Wayland
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_StyledBackground, true);
    
    // Components inherit from global Qt-Advanced-Stylesheets engine
    m_frame = new QFrame(this);
    m_textBrowser = new QTextBrowser(m_frame);
    m_textBrowser->setFrameShape(QFrame::NoFrame);
    
    // Auto-hiding logic based on cursor containment
    m_checkTimer = new QTimer(this);
    connect(m_checkTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            QRect expandedRect = geometry().adjusted(-20, -20, 20, 20);
            if (!expandedRect.contains(QCursor::pos())) {
                hide();
                m_checkTimer->stop();
            }
        }
    });
}
```

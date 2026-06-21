#include "ThemeManager.hpp"
#include <QFont>

namespace gridlock::ui {

QString ThemeManager::stylesheet() {
    return R"(
/* ────────────────────────────────────────────────────────────────────────────
   GridLock – Catppuccin Mocha global stylesheet
   Colours are defined as constants in ThemeManager.hpp.
   Immersive depth is created through background luminance differences,
   not hard borders.
   ──────────────────────────────────────────────────────────────────────────── */

/* ── Base ── */
QWidget {
    background-color: #1e1e2e;
    color: #cdd6f4;
    font-family: "Inter", "Noto Sans", sans-serif;
    font-size: 13px;
    selection-background-color: #cba6f7;
    selection-color: #1e1e2e;
}

/* ── Main Window ── */
QMainWindow {
    background-color: #1e1e2e;
}

QMainWindow::separator {
    background-color: #181825;
    width: 1px;
    height: 1px;
}

/* ── Menu Bar ── */
QMenuBar {
    background-color: #181825;
    color: #cdd6f4;
    border-bottom: 1px solid #313244;
    padding: 2px 4px;
}

QMenuBar::item {
    padding: 4px 10px;
    border-radius: 4px;
    background: transparent;
}

QMenuBar::item:selected {
    background-color: #313244;
    color: #cba6f7;
}

QMenu {
    background-color: #181825;
    color: #cdd6f4;
    border: 1px solid #313244;
    border-radius: 6px;
    padding: 4px 0;
}

QMenu::item {
    padding: 6px 24px 6px 12px;
    border-radius: 4px;
    margin: 2px 4px;
}

QMenu::item:selected {
    background-color: #313244;
    color: #cba6f7;
}

QMenu::separator {
    height: 1px;
    background: #313244;
    margin: 4px 8px;
}

/* ── Toolbar ── */
QToolBar {
    background-color: #181825;
    border-bottom: 1px solid #313244;
    spacing: 4px;
    padding: 3px 6px;
}

QToolBar::separator {
    background-color: #313244;
    width: 1px;
    margin: 4px 2px;
}

QToolButton, QToolBar QToolButton {
    background: transparent;
    color: #cdd6f4;
    border: none;
    border-radius: 5px;
    padding: 4px 10px;
}

QToolButton:hover {
    background-color: #313244;
}

QToolButton:pressed {
    background-color: #45475a;
}

/* ── Status Bar ── */
QStatusBar {
    background-color: #181825;
    color: #a6adc8;
    border-top: 1px solid #313244;
    font-size: 12px;
}

/* ── Dock Widgets — borderless, blends into mantle ── */
QDockWidget {
    color: #cdd6f4;
    titlebar-close-icon: none;
    titlebar-normal-icon: none;
}

QDockWidget::title {
    background-color: #181825;
    color: #7f849c;
    padding: 5px 10px;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    border-bottom: 1px solid #313244;
}

QDockWidget::title:focus {
    color: #cba6f7;
    border-bottom: 1px solid #cba6f7;
}

QDockWidget::close-button, QDockWidget::float-button {
    border: none;
    background: transparent;
    padding: 0px;
}

/* ── Splitter — invisible, 1–2 px, subtle hover ── */
QSplitter::handle {
    background-color: #181825;
}

QSplitter::handle:horizontal {
    width: 2px;
    background-color: #181825;
}

QSplitter::handle:vertical {
    height: 2px;
    background-color: #181825;
}

QSplitter::handle:hover {
    background-color: #45475a;
}

/* ── Tab Widget ── */
QTabWidget::pane {
    border: none;
    background-color: #1e1e2e;
}

QTabBar {
    background-color: #181825;
}

QTabBar::tab {
    background-color: #181825;
    color: #7f849c;
    padding: 6px 16px;
    border: none;
    border-bottom: 2px solid transparent;
    font-size: 12px;
}

QTabBar::tab:selected {
    background-color: #1e1e2e;
    color: #cdd6f4;
    border-bottom: 2px solid #cba6f7;
}

QTabBar::tab:hover:!selected {
    background-color: #252535;
    color: #cdd6f4;
}

/* ── Input Widgets ── */
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background-color: #313244;
    color: #cdd6f4;
    border: 1px solid transparent;
    border-radius: 5px;
    padding: 5px 8px;
    min-height: 26px;
    selection-background-color: #cba6f7;
    selection-color: #1e1e2e;
}

QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border-color: #cba6f7;
}

QLineEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover {
    border-color: #45475a;
}

QComboBox::drop-down {
    border: none;
    padding-right: 8px;
}

QComboBox QAbstractItemView {
    background-color: #313244;
    color: #cdd6f4;
    selection-background-color: #cba6f7;
    selection-color: #1e1e2e;
    border: 1px solid #45475a;
    border-radius: 5px;
    outline: none;
}

QSpinBox::up-button, QSpinBox::down-button,
QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    background-color: #45475a;
    border: none;
    border-radius: 2px;
}

QSpinBox::up-button:hover, QSpinBox::down-button:hover,
QDoubleSpinBox::up-button:hover, QDoubleSpinBox::down-button:hover {
    background-color: #585b70;
}

/* ── Monospace / code views (0px margins, terminal aesthetic) ── */
QPlainTextEdit, QTextEdit {
    background-color: #181825;
    color: #cdd6f4;
    border: none;
    font-family: "JetBrains Mono", "Fira Code", "Cascadia Code", monospace;
    font-size: 12px;
    line-height: 1.5;
    selection-background-color: #cba6f7;
    selection-color: #1e1e2e;
}

QTextBrowser {
    background-color: #181825;
    color: #cdd6f4;
    border: none;
    font-family: "JetBrains Mono", "Fira Code", "Cascadia Code", monospace;
    font-size: 12px;
    selection-background-color: #cba6f7;
    selection-color: #1e1e2e;
}

/* ── Scroll Bars ── */
QScrollBar:vertical {
    background: #181825;
    width: 8px;
    border: none;
    border-radius: 4px;
}

QScrollBar::handle:vertical {
    background: #45475a;
    min-height: 20px;
    border-radius: 4px;
}

QScrollBar::handle:vertical:hover {
    background: #585b70;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0px;
}

QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
    background: none;
}

QScrollBar:horizontal {
    background: #181825;
    height: 8px;
    border: none;
    border-radius: 4px;
}

QScrollBar::handle:horizontal {
    background: #45475a;
    min-width: 20px;
    border-radius: 4px;
}

QScrollBar::handle:horizontal:hover {
    background: #585b70;
}

QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0px;
}

QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
    background: none;
}

/* ── List/Tree/Table Widgets ── */
QListWidget, QTreeWidget, QTreeView, QTableWidget {
    background-color: #181825;
    color: #cdd6f4;
    border: none;
    alternate-background-color: #1e1e2e;
    gridline-color: #313244;
    outline: none;
    show-decoration-selected: 1;
}

QListWidget::item, QTreeWidget::item, QTreeView::item, QTableWidget::item {
    padding: 4px 8px;
    border-radius: 3px;
}

QListWidget::item:selected, QTreeWidget::item:selected, QTreeView::item:selected, QTableWidget::item:selected {
    background-color: #313244;
    color: #cba6f7;
}

QListWidget::item:hover:!selected, QTreeWidget::item:hover:!selected, QTreeView::item:hover:!selected,
QTableWidget::item:hover:!selected {
    background-color: #252535;
}

QHeaderView::section {
    background-color: #181825;
    color: #7f849c;
    padding: 4px 8px;
    border: none;
    border-bottom: 1px solid #313244;
    font-size: 11px;
}

/* ── Sidebar list (PreferencesDialog sidebar) ── */
QListWidget[role="sidebar"] {
    background-color: #181825;
    border-right: 1px solid #313244;
    font-size: 13px;
    outline: none;
}

QListWidget[role="sidebar"]::item {
    padding: 10px 18px;
    border-radius: 0px;
}

QListWidget[role="sidebar"]::item:selected {
    background-color: #313244;
    color: #cba6f7;
    border-left: 3px solid #cba6f7;
}

QListWidget[role="sidebar"]::item:hover:!selected {
    background-color: #252535;
}

/* ── Buttons ── */
QPushButton {
    background-color: #313244;
    color: #cdd6f4;
    border: none;
    border-radius: 5px;
    padding: 6px 14px;
    min-width: 64px;
    font-weight: 500;
}

QPushButton:hover {
    background-color: #45475a;
}

QPushButton:pressed {
    background-color: #585b70;
}

QPushButton:disabled {
    color: #45475a;
    background-color: #313244;
}

/* Primary action buttons — object name "primaryBtn" */
QPushButton#primaryBtn {
    background-color: #cba6f7;
    color: #1e1e2e;
    font-weight: 600;
}

QPushButton#primaryBtn:hover {
    background-color: #d4b9ff;
}

/* Dialog OK/Apply specific */
QDialogButtonBox QPushButton[text="OK"],
QDialogButtonBox QPushButton[text="Apply"] {
    background-color: #cba6f7;
    color: #1e1e2e;
    font-weight: 600;
}

QDialogButtonBox QPushButton[text="OK"]:hover,
QDialogButtonBox QPushButton[text="Apply"]:hover {
    background-color: #d4b9ff;
}

/* ── CheckBox / RadioButton ── */
QCheckBox {
    color: #cdd6f4;
    spacing: 8px;
}

QCheckBox::indicator {
    width: 16px;
    height: 16px;
    border: 1px solid #45475a;
    border-radius: 3px;
    background-color: #313244;
}

QCheckBox::indicator:checked {
    background-color: #cba6f7;
    border-color: #cba6f7;
}

QCheckBox::indicator:hover {
    border-color: #cba6f7;
}

/* ── Label — semantic variants ── */
QLabel {
    color: #cdd6f4;
    background: transparent;
}

/* Muted labels — e.g. form keys */
QLabel[role="muted"] {
    color: #a6adc8;
}

/* Section headings in settings pages */
QLabel[role="heading"] {
    font-size: 15px;
    color: #89b4fa;
    font-weight: 600;
}

/* Status label inside SpackManager / HPC console */
QLabel[role="status"] {
    color: #a6adc8;
    font-size: 11px;
}

/* Horizontal separator line widget */
QLabel[role="separator"] {
    background-color: rgba(255, 255, 255, 0.08);
    max-height: 1px;
    min-height: 1px;
}

/* ── Dialog ── */
QDialog {
    background-color: #1e1e2e;
    color: #cdd6f4;
}

/* ── Group Box ── */
QGroupBox {
    background-color: #1e1e2e;
    color: #89b4fa;
    border: 1px solid #313244;
    border-radius: 6px;
    margin-top: 12px;
    padding-top: 8px;
    font-weight: 600;
    font-size: 12px;
}

QGroupBox::title {
    subcontrol-origin: margin;
    subcontrol-position: top left;
    left: 10px;
    top: -6px;
    color: #89b4fa;
}

/* ── Progress Bar ── */
QProgressBar {
    background-color: #313244;
    color: #cdd6f4;
    border: none;
    border-radius: 4px;
    text-align: center;
    height: 8px;
}

QProgressBar::chunk {
    background-color: #cba6f7;
    border-radius: 4px;
}

/* ── Tooltip ── */
QToolTip {
    background-color: #313244;
    color: #cdd6f4;
    border: 1px solid #45475a;
    border-radius: 4px;
    padding: 4px 8px;
    font-size: 12px;
}
)";
}

void ThemeManager::applyGlobalTheme(QApplication& app) const {
    app.setStyleSheet(stylesheet());
    // Set a clean global sans-serif font for UI chrome.
    QFont uiFont("Inter");
    if (!uiFont.exactMatch()) {
        uiFont = QFont("Noto Sans");
    }
    if (!uiFont.exactMatch()) {
        uiFont = QFont("DejaVu Sans");
    }
    uiFont.setPointSize(13);
    app.setFont(uiFont);
}

} // namespace gridlock::ui

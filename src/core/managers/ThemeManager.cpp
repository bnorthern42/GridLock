/**
 * @file ThemeManager.cpp
 * @brief Implementation of the workstation design system QSS overlay and theme switcher.
 *
 * Compiles the base `qt_material` XML theme via QtAdvancedStylesheet and layers
 * GridLock's high-density dark-slate QSS rules on top so dock headers (24px),
 * toolbars (26px), and interactive controls (20-22px) maintain strict pixel
 * budgets across all panels.
 *
 * Palette Architecture:
 * - Primary Canvas: `#16181d`
 * - Secondary Surface / Dock Background: `#1b1e24`
 * - Sub-View Header Strip: `#21252b` with `#282c34` 1px borders
 * - Primary Accent (Focus / Active Tab): `#528bff`
 * - Secondary Accent (Execution / Telemetry): `#4ec9b0`
 */

#include "ThemeManager.hpp"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFont>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>

#include "QtAdvancedStylesheet.h"

namespace gridlock::core::managers {

/**
 * @brief Returns the process-wide ThemeManager singleton instance.
 * @return Reference to the static Meyer's singleton instance.
 */
ThemeManager& ThemeManager::instance() {
    static ThemeManager instance;
    return instance;
}

/**
 * @brief Constructs ThemeManager and allocates the underlying QtAdvancedStylesheet engine.
 */
ThemeManager::ThemeManager()
    : m_advancedStylesheet(std::make_unique<acss::QtAdvancedStylesheet>()) {}

/**
 * @brief Default destructor for ThemeManager (defined out-of-line for `std::unique_ptr` PImpl).
 */
ThemeManager::~ThemeManager() = default;

/**
 * @brief Returns the high-contrast Light Mode workstation QSS overlay when dark mode is disabled.
 *
 * Replaces dark-slate `#16181d` backgrounds and light `#d7dae0` text with crisp `#ffffff` /
 * `#f6f8fa` surfaces and `#1f2328` charcoal text so tables, trees, editors, and dock headers
 * never render unreadable white-on-white text in Light Mode.
 *
 * @return High-contrast Light Mode QSS stylesheet string.
 */
static QString getHighContrastLightQssOverlay() {
    return QStringLiteral(
        // Root window and modal dialog light-slate canvas with dark charcoal foreground text.
        "\n/* High-Contrast Light Workstation Design System */\n"
        "QMainWindow, QWidget#centralWidget, QDialog, QWizard { background: #f6f8fa; color: #1f2328; }\n"
        // Interactive 4px dock and splitter drag handles with royal-blue hover indicator.
        "QMainWindow::separator, QSplitter::handle { width: 4px; height: 4px; background: #d0d7de; }\n"
        "QMainWindow::separator:hover, QSplitter::handle:hover { background: #0969da; }\n"
        // Zero-margin embedded terminal emulator container protection.
        "QTermWidget, QTermWidget QWidget { padding: 0px; margin: 0px; border: none; }\n"
        // Dock container borders and 24px uppercase dock title headers.
        "QDockWidget { border: 1px solid #d0d7de; color: #1f2328; }\n"
        "QDockWidget::title {\n"
        "    background: #eaeef2; color: #1f2328; padding: 3px 8px; height: 24px;\n"
        "    min-height: 20px; max-height: 24px; border-bottom: 1px solid #d0d7de;\n"
        "    font-size: 11px; font-weight: 600; text-transform: uppercase;\n"
        "}\n"
        // Tabbed workspace and dock tab bars with 2px royal-blue active tab underline.
        "QTabWidget::pane, QTabWidget#EditorTabManager::pane { border: 0px; background: #ffffff; }\n"
        "QTabBar { background: #eaeef2; }\n"
        "QTabBar::tab {\n"
        "    background: #eaeef2; color: #57606a; padding: 3px 10px; height: 24px;\n"
        "    min-height: 20px; max-height: 24px; border-right: 1px solid #d0d7de;\n"
        "    border-bottom: 1px solid #d0d7de; font-size: 11px; font-weight: 600;\n"
        "}\n"
        // Active selected tab uses pure white surface and dark charcoal label text.
        "QTabBar::tab:selected { background: #ffffff; color: #1f2328; border-bottom: 2px solid #0969da; }\n"
        // Compact 20-22px push buttons and toolbar buttons with crisp #d0d7de borders.
        "QPushButton, QToolButton, QToolBar QToolButton {\n"
        "    height: 20px; min-height: 20px; max-height: 22px; padding: 1px 8px;\n"
        "    background: #f6f8fa; border: 1px solid #d0d7de; border-radius: 2px;\n"
        "    color: #1f2328; font-size: 10px; font-weight: 600;\n"
        "}\n"
        // Hover and disabled button states in Light Mode.
        "QPushButton:hover:enabled, QToolButton:hover { background: #eaeef2; border: 1px solid #0969da; color: #0969da; }\n"
        "QPushButton:disabled, QToolButton:disabled { background: #f6f8fa; color: #8c959f; border: 1px solid #d8dee4; }\n"
        // Single-line inputs, spinboxes, combo boxes, and filter bars with #1f2328 text on #ffffff.
        "QLineEdit, QSpinBox, QComboBox, #cutterSeekBar, #symbolFilterEdit, #stringFilterEdit, #watchExpressionInput {\n"
        "    height: 20px; min-height: 20px; max-height: 22px; padding: 0px 6px;\n"
        "    background: #ffffff; border: 1px solid #d0d7de; border-radius: 2px; color: #1f2328; font-size: 11px;\n"
        "}\n"
        // Text editors, browsers, list widgets, and hex dump viewports.
        "QTextEdit, QPlainTextEdit, QTextBrowser, QListWidget, #cutterHexDumpEdit {\n"
        "    border: none; background: #ffffff; color: #1f2328;\n"
        "}\n"
        // High-contrast tables and trees with #ffffff base, #f6f8fa alternate rows, and #1f2328 text.
        "QTreeView, QTableView, QTableWidget, #cutterRegisterTable, #symbolsTable, #stringsTable, #mpiNetworkLogTable {\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    background: #ffffff; alternate-background-color: #f6f8fa; color: #1f2328;\n"
        "    border: none; gridline-color: #d0d7de; selection-background-color: #ddf4ff; selection-color: #0969da;\n"
        "}\n"
        // Table and tree section headers capped at 20-22px height.
        "QHeaderView, QHeaderView::section, QTableCornerButton::section {\n"
        "    background-color: #eaeef2; color: #1f2328; border-right: 1px solid #d0d7de; border-bottom: 1px solid #d0d7de;\n"
        "    height: 20px; min-height: 20px; max-height: 22px; font-size: 10px; font-weight: 600;\n"
        "}\n"
        // Unified 24-26px sub-view header strips and bottom status bar in Light Mode.
        "QToolBar, QStatusBar, #cutterDisasmHeader, #cutterMemHeader, #projectModeBar, #watchToolbar, "
        "#evaluatorHeaderBar, #evaluatorPromptBar, #gdbConsoleHeaderBar, #gdbConsolePromptBar, "
        "#refManualHeaderBar, #mpiDiagnosticsHeaderBar, #mpiNetworkLogHeaderBar, #variablesHeaderBar {\n"
        "    background: #eaeef2; color: #1f2328; border-bottom: 1px solid #d0d7de; min-height: 24px; max-height: 26px;\n"
        "}\n"
        // Status bar telemetry badges, labels, and checkboxes use high-contrast #1f2328 foreground.
        "#cutterStatusTelemetry, #cutterDisasmBadge, #cutterDisasmStats, QLabel, QCheckBox { color: #1f2328; }\n"
    );
}

/**
 * @brief Returns the high-density dark-slate workstation QSS overlay appended to base themes.
 *
 * Enforces:
 * - `#16181d` primary canvas background and `#21252b` 24px sub-view headers.
 * - 4px interactive splitter handles with `#528bff` hover highlights.
 * - 20-22px height caps on QPushButton, QToolButton, QLineEdit, QSpinBox, and QComboBox.
 * - Monospace tabular typography for disassembly, hex dump, register, symbol, and string views.
 *
 * @return Complete QSS stylesheet string for the workstation chrome overlay.
 */
static QString getHighDensityQssOverlay(bool isDark = true) {
    if (!isDark) {
        return getHighContrastLightQssOverlay();
    }
    return QStringLiteral(
        // Root window canvas and 4px interactive dock/splitter resize handles.
        // Dark slate `#16181d` matches the primary code and disassembly background.
        "\n/* Dark High-Density Workstation Design System */\n"
        "QMainWindow, QWidget#centralWidget { background: #16181d; }\n"
        // Splitter and dock separators use a 4px grab target with a subtle 1px border.
        "QMainWindow::separator { width: 4px; height: 4px; background: #1e2228; border-left: 1px solid #282c34; }\n"
        "QSplitter::handle { width: 4px; height: 4px; background: #1e2228; border-left: 1px solid #282c34; }\n"
        "QSplitter::handle:horizontal { width: 4px; }\n"
        "QSplitter::handle:vertical { height: 4px; }\n"
        // Hovering any dock or splitter separator highlights the drag handle in royal blue.
        "QMainWindow::separator:hover, QSplitter::handle:hover { background: #528bff; }\n"
        // Embedded terminal emulator cell geometry protection preventing PTY stair-stepping.
        "QTermWidget, QTermWidget QWidget { padding: 0px; margin: 0px; border: none; }\n"
        // Dock container borders and compact 24px dock title bars.
        // Every dock widget has a 1px `#282c34` outline to separate adjacent panes cleanly.
        "QDockWidget { border: 1px solid #282c34; }\n"
        "QDockWidget::title {\n"
        "    background: #21252b;\n"
        "    color: #abb2bf;\n"
        "    padding: 3px 8px;\n"
        "    height: 24px;\n"
        "    min-height: 20px;\n"
        "    max-height: 24px;\n"
        "    border-bottom: 1px solid #2c313a;\n"
        "    font-size: 11px;\n"
        "    font-weight: 600;\n"
        "    text-transform: uppercase;\n"
        "}\n"
        // Tabbed dock and bottom console tab bar chrome with 2px royal-blue active indicator.
        // Zero-margin tab panes prevent double borders inside dock containers.
        "QTabWidget::pane { border: 0px; margin: 0px; padding: 0px; background: #16181d; }\n"
        "QTabWidget#EditorTabManager::pane { border: 0px; margin: 0px; padding: 0px; background: #16181d; }\n"
        "QTabBar { background: #181a1f; }\n"
        "QTabBar::tab {\n"
        "    background: #181a1f;\n"
        "    color: #abb2bf;\n"
        "    padding: 3px 10px;\n"
        "    height: 24px;\n"
        "    min-height: 20px;\n"
        "    max-height: 24px;\n"
        "    margin: 0px;\n"
        "    border-right: 1px solid #282c34;\n"
        "    border-bottom: 1px solid #2c313a;\n"
        "    font-size: 11px;\n"
        "    font-weight: 600;\n"
        "    text-transform: uppercase;\n"
        "}\n"
        // Selected tab uses the `#21252b` header surface and a 2px `#528bff` underline.
        "QTabBar::tab:selected {\n"
        "    background: #21252b;\n"
        "    color: #ffffff;\n"
        "    border-bottom: 2px solid #528bff;\n"
        "}\n"
        // Standard push button states (normal, hover, pressed/checked, disabled) capped at 22px height.
        // Muted `#323844` borders replace loud neon outlines across all control bars.
        "QPushButton, QPushButton:disabled, QPushButton:flat:disabled {\n"
        "    height: 20px;\n"
        "    min-height: 20px;\n"
        "    max-height: 22px;\n"
        "    padding: 1px 8px;\n"
        "    margin: 0px;\n"
        "    background: #252932;\n"
        "    border: 1px solid #323844;\n"
        "    border-radius: 2px;\n"
        "    color: #d7dae0;\n"
        "    font-size: 10px;\n"
        "    font-weight: 600;\n"
        "}\n"
        // Hovering an enabled push button subtly brightens the fill and border.
        "QPushButton:hover:enabled {\n"
        "    background: #2c323d;\n"
        "    border: 1px solid #3e4452;\n"
        "    color: #ffffff;\n"
        "}\n"
        // Active/checked push buttons display a 1px `#528bff` royal-blue accent border.
        "QPushButton:pressed, QPushButton:checked {\n"
        "    background: #2c3444;\n"
        "    border: 1px solid #528bff;\n"
        "    color: #ffffff;\n"
        "}\n"
        // Disabled push buttons recede into the dark background with muted `#5c6370` text.
        "QPushButton:disabled {\n"
        "    background: #181a1f;\n"
        "    color: #5c6370;\n"
        "    border: 1px solid #242830;\n"
        "}\n"
        // Compact single-line text inputs, spinboxes, and combo boxes capped at 22px height.
        "QLineEdit, QLineEdit:disabled, QSpinBox, QSpinBox:disabled, QComboBox, QComboBox:disabled {\n"
        "    height: 20px;\n"
        "    min-height: 20px;\n"
        "    max-height: 22px;\n"
        "    padding: 0px 6px;\n"
        "    margin: 0px;\n"
        "    background: #16181d;\n"
        "    border: 1px solid #323844;\n"
        "    border-radius: 2px;\n"
        "    color: #d7dae0;\n"
        "    font-size: 11px;\n"
        "}\n"
        // Focused input controls highlight their border in `#528bff` for keyboard accessibility.
        "QLineEdit:focus, QSpinBox:focus, QComboBox:focus {\n"
        "    border: 1px solid #528bff;\n"
        "    background: #1b1e24;\n"
        "}\n"
        // Code editors, disassembly views, documentation browsers, and tabular data grids.
        "QTextEdit, QPlainTextEdit, QTextBrowser, QListWidget {\n"
        "    border: none;\n"
        "    border-radius: 0px;\n"
        "    padding: 0px;\n"
        "    background: #16181d;\n"
        "    color: #d7dae0;\n"
        "}\n"
        // Tree and table views enforce monospace typography, alternating rows, and royal-blue selection.
        "QTreeView, QTableView, QTableWidget {\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    background: #16181d;\n"
        "    alternate-background-color: #181b20;\n"
        "    color: #d7dae0;\n"
        "    border: none;\n"
        "    padding: 0px;\n"
        "    gridline-color: #282c34;\n"
        "    selection-background-color: #264f78;\n"
        "    selection-color: #ffffff;\n"
        "}\n"
        // Compact 18px minimum row height maximizes visible rows in symbol and register tables.
        "QTreeView::item, QTableView::item {\n"
        "    padding: 1px 4px;\n"
        "    min-height: 18px;\n"
        "}\n"
        // Selected tree and table rows highlight in #264f78 royal blue with crisp white text.
        "QTreeView::item:selected, QTableView::item:selected, QTableWidget::item:selected {\n"
        "    background: #264f78;\n"
        "    color: #ffffff;\n"
        "}\n"
        // Table and tree header container background matches #16181d canvas so empty vertical
        // header space below the last row in DifferentialGrid has zero grey column mismatch.
        "QHeaderView, QHeaderView:vertical, QHeaderView:horizontal {\n"
        "    background-color: #16181d;\n"
        "    border: none;\n"
        "}\n"
        // Table and tree column header sections capped at 20-22px height with 1px #2c313a border.
        // Matches the #21252b sub-view control strip palette across all side and bottom panes.
        "QHeaderView::section {\n"
        "    background-color: #21252b;\n"
        "    color: #abb2bf;\n"
        "    padding: 2px 6px;\n"
        "    height: 20px;\n"
        "    min-height: 20px;\n"
        "    max-height: 22px;\n"
        "    border: 0px;\n"
        "    border-right: 1px solid #2c313a;\n"
        "    border-bottom: 1px solid #2c313a;\n"
        "    font-size: 10px;\n"
        "    font-weight: 600;\n"
        "}\n"
        // Top-left table corner button matches the #21252b header section surface.
        "QTableCornerButton::section {\n"
        "    background-color: #21252b;\n"
        "    border: 0px;\n"
        "    border-right: 1px solid #2c313a;\n"
        "    border-bottom: 1px solid #2c313a;\n"
        "}\n"
        // Slim 8px vertical and horizontal scrollbars with zero arrow buttons.
        // Uses #16181d track background and #2c323c rounded thumb handle (#3e4452 on hover).
        "QScrollBar:vertical {\n"
        "    background: #16181d;\n"
        "    width: 8px;\n"
        "    margin: 0px;\n"
        "    border: none;\n"
        "}\n"
        // Vertical scrollbar thumb handle with 24px minimum grab height.
        "QScrollBar::handle:vertical {\n"
        "    background: #2c323c;\n"
        "    min-height: 24px;\n"
        "    border-radius: 3px;\n"
        "}\n"
        // Brighten vertical scrollbar thumb handle on mouse hover.
        "QScrollBar::handle:vertical:hover {\n"
        "    background: #3e4452;\n"
        "}\n"
        // Suppress legacy top/bottom stepper arrow buttons on vertical scrollbars.
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {\n"
        "    height: 0px;\n"
        "    background: none;\n"
        "}\n"
        // Horizontal 8px scrollbar track matching dark-slate viewport background.
        "QScrollBar:horizontal {\n"
        "    background: #16181d;\n"
        "    height: 8px;\n"
        "    margin: 0px;\n"
        "    border: none;\n"
        "}\n"
        // Horizontal scrollbar thumb handle with 24px minimum grab width.
        "QScrollBar::handle:horizontal {\n"
        "    background: #2c323c;\n"
        "    min-width: 24px;\n"
        "    border-radius: 3px;\n"
        "}\n"
        // Brighten horizontal scrollbar thumb handle on mouse hover.
        "QScrollBar::handle:horizontal:hover {\n"
        "    background: #3e4452;\n"
        "}\n"
        // Suppress legacy left/right stepper arrow buttons on horizontal scrollbars.
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {\n"
        "    width: 0px;\n"
        "    background: none;\n"
        "}\n"
        // Top execution and navigation toolbar and tool buttons capped at 26-28px height.
        "QToolBar {\n"
        "    height: 26px;\n"
        "    max-height: 28px;\n"
        "    min-height: 24px;\n"
        "    padding: 1px 4px;\n"
        "    spacing: 4px;\n"
        "    background: #1e2228;\n"
        "    border-bottom: 1px solid #2c313a;\n"
        "}\n"
        // Toolbar buttons share the same 20-22px height and `#252932` slate surface as push buttons.
        "QToolButton, QToolBar QToolButton {\n"
        "    height: 20px;\n"
        "    min-height: 20px;\n"
        "    max-height: 22px;\n"
        "    padding: 1px 8px;\n"
        "    margin: 0px;\n"
        "    background: #252932;\n"
        "    border: 1px solid #323844;\n"
        "    border-radius: 2px;\n"
        "    color: #d7dae0;\n"
        "    font-size: 11px;\n"
        "    font-weight: 600;\n"
        "}\n"
        // Toolbar button hover state.
        "QToolButton:hover, QToolBar QToolButton:hover {\n"
        "    background: #2c323d;\n"
        "    border: 1px solid #3e4452;\n"
        "    color: #ffffff;\n"
        "}\n"
        // Toolbar button active/pressed/checked state.
        "QToolButton:pressed, QToolBar QToolButton:pressed, QToolButton:checked, QToolBar QToolButton:checked {\n"
        "    background: #2c3444;\n"
        "    border: 1px solid #528bff;\n"
        "    color: #ffffff;\n"
        "}\n"
        // Disabled toolbar buttons recede into the dark toolbar chrome with muted #5c6370 text.
        "QToolButton:disabled, QToolBar QToolButton:disabled {\n"
        "    background: #181a1f;\n"
        "    border: 1px solid #232730;\n"
        "    color: #5c6370;\n"
        "}\n"
        // Address seek bar (`#cutterSeekBar`) in the top toolbar uses monospace font and 22px height.
        "#cutterSeekBar {\n"
        "    background: #141619;\n"
        "    color: #d7dae0;\n"
        "    border: 1px solid #323844;\n"
        "    border-radius: 2px;\n"
        "    padding: 1px 6px;\n"
        "    min-height: 20px;\n"
        "    max-height: 22px;\n"
        "    font-size: 11px;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "}\n"
        // Focused address seek bar highlights border in royal blue.
        "#cutterSeekBar:focus {\n"
        "    border: 1px solid #528bff;\n"
        "    background: #181a1f;\n"
        "}\n"
        // Bottom status bar (`QStatusBar`) and live debugger telemetry strip (`#cutterStatusTelemetry`).
        "QStatusBar {\n"
        "    background: #181a1f;\n"
        "    color: #abb2bf;\n"
        "    border-top: 1px solid #282c34;\n"
        "    min-height: 22px;\n"
        "    max-height: 24px;\n"
        "    padding: 1px 8px;\n"
        "}\n"
        "QStatusBar::item { border: none; }\n"
        "#cutterStatusTelemetry {\n"
        "    color: #abb2bf;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 11px;\n"
        "    min-height: 18px;\n"
        "    padding: 1px 4px;\n"
        "}\n"
        // Unified 24-26px sub-view header and prompt strips across all workstation panels and bottom tabs.
        "#cutterDisasmHeader, #cutterMemHeader, #projectModeBar, #watchToolbar, "
        "#evaluatorHeaderBar, #evaluatorPromptBar, #gdbConsoleHeaderBar, #gdbConsolePromptBar, "
        "#refManualHeaderBar, #mpiDiagnosticsHeaderBar, #mpiNetworkLogHeaderBar, #variablesHeaderBar {\n"
        "    background: #21252b;\n"
        "    border-bottom: 1px solid #2c313a;\n"
        "    min-height: 24px;\n"
        "    max-height: 26px;\n"
        "}\n"
        // Disassembly header title badge and basic-block/edge metrics label.
        "#cutterDisasmBadge {\n"
        "    color: #abb2bf;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 11px;\n"
        "    font-weight: 600;\n"
        "}\n"
        "#cutterDisasmStats {\n"
        "    color: #7f848e;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 10px;\n"
        "}\n"
        // Disassembly mode toggle buttons (CFG, Decompiler, Split CFG+Decompiler).
        "#cutterCfgToggleBtn, #cutterDecompilerBtn, #cutterSplitViewBtn {\n"
        "    background: #252932;\n"
        "    color: #abb2bf;\n"
        "    border: 1px solid #323844;\n"
        "    border-radius: 2px;\n"
        "    padding: 1px 6px;\n"
        "    min-height: 18px;\n"
        "    max-height: 20px;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 9px;\n"
        "    font-weight: 600;\n"
        "}\n"
        // Checked state for Decompiler and Split CFG+Decompiler buttons.
        "#cutterDecompilerBtn:checked, #cutterSplitViewBtn:checked {\n"
        "    background: #2c3444;\n"
        "    color: #ffffff;\n"
        "    border: 1px solid #528bff;\n"
        "}\n"
        // Hover state for Disassembly header mode buttons.
        "#cutterCfgToggleBtn:hover, #cutterDecompilerBtn:hover, #cutterSplitViewBtn:hover {\n"
        "    background: #2c323d;\n"
        "    border: 1px solid #528bff;\n"
        "    color: #ffffff;\n"
        "}\n"
        // Hexdump, register table, symbol table, string table, and address-space ribbon selectors.
        "#cutterHexDumpEdit {\n"
        "    background: #16181d;\n"
        "    border: none;\n"
        "    border-top: 1px solid #282c34;\n"
        "}\n"
        "#cutterRegisterTable, #symbolsTable, #stringsTable, #mpiNetworkLogTable {\n"
        "    background: #181a1f;\n"
        "    border: none;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 11px;\n"
        "}\n"
        // Top virtual address-space map ribbon bar (`#AddressNavToolbar`).
        "#AddressNavToolbar {\n"
        "    background: #141619;\n"
        "    border-bottom: 1px solid #282c34;\n"
        "    min-height: 20px;\n"
        "    max-height: 22px;\n"
        "    padding: 0px;\n"
        "    spacing: 0px;\n"
        "}\n"
        // ProjectExplorer mode buttons (FILES | SYMBOLS | STRINGS) and Watch expression add button.
        "#explorerFilesModeBtn, #explorerSymbolsModeBtn, #explorerStringsModeBtn, #addWatchBtn {\n"
        "    background: #252932;\n"
        "    color: #abb2bf;\n"
        "    border: 1px solid #323844;\n"
        "    border-radius: 2px;\n"
        "    padding: 1px 6px;\n"
        "    min-height: 18px;\n"
        "    max-height: 20px;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 10px;\n"
        "    font-weight: 600;\n"
        "}\n"
        // Checked state for ProjectExplorer mode selector buttons.
        "#explorerFilesModeBtn:checked, #explorerSymbolsModeBtn:checked, #explorerStringsModeBtn:checked {\n"
        "    background: #2c3444;\n"
        "    color: #ffffff;\n"
        "    border: 1px solid #528bff;\n"
        "}\n"
        // Filter inputs for Symbols, Strings, and Watch expressions.
        "#symbolFilterEdit, #stringFilterEdit, #watchExpressionInput {\n"
        "    background: #141619;\n"
        "    color: #d7dae0;\n"
        "    border: 1px solid #323844;\n"
        "    border-radius: 2px;\n"
        "    padding: 1px 5px;\n"
        "    min-height: 18px;\n"
        "    max-height: 22px;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 10px;\n"
        "}\n"
        // Modal Dialog Dark-Slate Chrome & Content Cards.
        "QDialog, QWizard {\n"
        "    background: #16181d;\n"
        "    color: #d7dae0;\n"
        "}\n"
        // Dialog QGroupBox content cards with 1px solid #2c313a border.
        "QGroupBox {\n"
        "    background: #1b1e24;\n"
        "    border: 1px solid #2c313a;\n"
        "    border-radius: 3px;\n"
        "    margin-top: 8px;\n"
        "    padding-top: 10px;\n"
        "    color: #d7dae0;\n"
        "    font-weight: 600;\n"
        "}\n"
        // GroupBox title badge positioning.
        "QGroupBox::title {\n"
        "    subcontrol-origin: margin;\n"
        "    subcontrol-position: top left;\n"
        "    padding: 0px 6px;\n"
        "    color: #61afef;\n"
        "}\n"
        // Standardize 24px dialog footer action buttons inside QDialogButtonBox.
        "QDialogButtonBox QPushButton {\n"
        "    min-height: 22px;\n"
        "    max-height: 24px;\n"
        "    min-width: 72px;\n"
        "    padding: 2px 10px;\n"
        "}\n"
        // Sleek 28px-row dialog navigation sidebars with 2px solid #528bff active indicator.
        "QListWidget[role=\"sidebar\"] {\n"
        "    background: #1e2128;\n"
        "    color: #abb2bf;\n"
        "    border: none;\n"
        "    border-right: 1px solid #2c313a;\n"
        "    outline: none;\n"
        "    font-size: 11px;\n"
        "}\n"
        "QListWidget[role=\"sidebar\"]::item {\n"
        "    height: 28px;\n"
        "    padding: 2px 8px;\n"
        "    border-left: 2px solid transparent;\n"
        "}\n"
        "QListWidget[role=\"sidebar\"]::item:hover {\n"
        "    background: #252932;\n"
        "    color: #ffffff;\n"
        "}\n"
        "QListWidget[role=\"sidebar\"]::item:selected {\n"
        "    background: #262b36;\n"
        "    color: #ffffff;\n"
        "    border-left: 2px solid #528bff;\n"
        "    font-weight: 600;\n"
        "}\n"
        // Cohesive dark-slate popup menus and tooltips across all views.
        // Enforces #1e222a popup container surface with 1px #323844 structural border.
        "QMenu {\n"
        "    background: #1e222a;\n"
        "    color: #d7dae0;\n"
        "    border: 1px solid #323844;\n"
        "    padding: 3px 0px;\n"
        "}\n"
        // Standardize context menu item row padding and light-slate foreground text.
        "QMenu::item {\n"
        "    padding: 4px 20px;\n"
        "    color: #d7dae0;\n"
        "    background: transparent;\n"
        "}\n"
        // Highlight hovered/selected context menu actions with #264f78 accent fill.
        "QMenu::item:selected {\n"
        "    background: #264f78;\n"
        "    color: #ffffff;\n"
        "}\n"
        // Render subtle 1px horizontal divider lines between grouped menu actions.
        "QMenu::separator {\n"
        "    height: 1px;\n"
        "    background: #2c313a;\n"
        "    margin: 3px 6px;\n"
        "}\n"
        // Unify floating hover tooltips with the dark-slate popup chrome palette.
        "QToolTip {\n"
        "    background: #1e222a;\n"
        "    color: #d7dae0;\n"
        "    border: 1px solid #323844;\n"
        "    padding: 3px 6px;\n"
        "}\n"
    );
}

/**
 * @brief Synchronizes QApplication's base/text QPalette with the active dark or light mode.
 *
 * Ensures custom-painted widgets and QTableWidget viewports querying `QPalette::Base`
 * or `QPalette::Text` receive high-contrast dark (`#16181d` / `#d7dae0`) or light
 * (`#ffffff` / `#1f2328`) colors immediately on mode switches.
 *
 * @param isDark True for dark-slate palette; false for high-contrast light palette.
 */
static void applyApplicationPalette(bool isDark) {
    if (!qApp) {
        return;
    }
    QPalette pal = qApp->palette();
    if (isDark) {
        // Configure dark-slate base and light-slate text roles.
        pal.setColor(QPalette::Window, QColor(0x16, 0x18, 0x1d));
        pal.setColor(QPalette::WindowText, QColor(0xd7, 0xda, 0xe0));
        pal.setColor(QPalette::Base, QColor(0x16, 0x18, 0x1d));
        pal.setColor(QPalette::AlternateBase, QColor(0x18, 0x1b, 0x20));
        pal.setColor(QPalette::Text, QColor(0xd7, 0xda, 0xe0));
    } else {
        // Configure pure white base and high-contrast charcoal text roles for Light Mode.
        pal.setColor(QPalette::Window, QColor(0xf6, 0xf8, 0xfa));
        pal.setColor(QPalette::WindowText, QColor(0x1f, 0x23, 0x28));
        pal.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
        pal.setColor(QPalette::AlternateBase, QColor(0xf6, 0xf8, 0xfa));
        pal.setColor(QPalette::Text, QColor(0x1f, 0x23, 0x28));
    }
    qApp->setPalette(pal);
}

/**
 * @brief Initializes QtAdvancedStylesheet paths, loads user QSettings preferences, and applies QSS.
 */
void ThemeManager::initialize() {
    // Configure resource input directory and writable ACSS cache output path.
    const QString stylesDir = ":/styles";
    m_advancedStylesheet->setStylesDirPath(stylesDir);
    m_advancedStylesheet->setOutputDirPath(
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/acss");
    m_advancedStylesheet->setCurrentStyle("qt_material");

    // Read persisted theme name from QSettings, falling back to `dark_teal` if unavailable.
    QSettings s("gridlock", "debugger");
    QString theme = s.value("appearance/theme", "dark_teal").toString();
    if (!m_advancedStylesheet->themes().contains(theme) &&
        !m_advancedStylesheet->themes().contains(theme + ".xml")) {
        theme = "dark_teal";
    }
    m_darkMode = s.value("appearance/dark_mode", !theme.startsWith("light_")).toBool();

    const int uiFontSize = s.value("appearance/ui_font_size", 11).toInt();

    // Compile the XML palette variables and append the workstation QSS overlay.
    m_advancedStylesheet->setCurrentTheme(theme);
    m_advancedStylesheet->setThemeVariableValue("font_size", QString("%1pt").arg(uiFontSize));
    m_advancedStylesheet->updateStylesheet();
    applyApplicationPalette(m_darkMode);
    const QString qss = m_advancedStylesheet->styleSheet() + getHighDensityQssOverlay(m_darkMode);
    qApp->setStyleSheet(qss);

    // Synchronize the global QApplication font point size with user preferences.
    QFont appFont = qApp->font();
    appFont.setPointSize(uiFontSize);
    qApp->setFont(appFont);
}

/**
 * @brief Returns the list of themes discovered by QtAdvancedStylesheet.
 */
QStringList ThemeManager::getAvailableThemes() const {
    if (m_advancedStylesheet) {
        return m_advancedStylesheet->themes();
    }
    return QStringList();
}

/**
 * @brief Dynamically switches the active XML theme and reapplies the workstation QSS overlay.
 */
void ThemeManager::setTheme(const QString& themeName, bool isDark) {
    m_darkMode = isDark && !themeName.startsWith(QStringLiteral("light_"), Qt::CaseInsensitive);
    if (m_advancedStylesheet && !themeName.isEmpty()) {
        // Resolve optional `.xml` extension required by the stylesheet catalog.
        const QString themePath = m_advancedStylesheet->themes().contains(themeName + ".xml")
                                      ? themeName + ".xml"
                                      : themeName;
        m_advancedStylesheet->setCurrentTheme(themePath);
        m_advancedStylesheet->updateStylesheet();
        applyApplicationPalette(m_darkMode);
        qApp->setStyleSheet(m_advancedStylesheet->styleSheet() + getHighDensityQssOverlay(m_darkMode));
    }
    emit themeModeChanged(m_darkMode);
}

/**
 * @brief Switches between dark-slate (`true`) and high-contrast light (`false`) workstation palettes.
 */
void ThemeManager::setDarkMode(bool dark) {
    m_darkMode = dark;
    QSettings s("gridlock", "debugger");
    s.setValue("appearance/dark_mode", dark);
    applyApplicationPalette(m_darkMode);
    if (m_advancedStylesheet) {
        const QString fallbackTheme = dark ? QStringLiteral("dark_teal") : QStringLiteral("light_blue");
        QString theme = s.value("appearance/theme", fallbackTheme).toString();
        if (dark && theme.startsWith(QStringLiteral("light_"), Qt::CaseInsensitive)) {
            theme = QStringLiteral("dark_teal");
        } else if (!dark && theme.startsWith(QStringLiteral("dark_"), Qt::CaseInsensitive)) {
            theme = QStringLiteral("light_blue");
        }
        if (m_advancedStylesheet->themes().contains(theme) ||
            m_advancedStylesheet->themes().contains(theme + ".xml")) {
            const QString themePath = m_advancedStylesheet->themes().contains(theme + ".xml")
                                          ? theme + ".xml"
                                          : theme;
            m_advancedStylesheet->setCurrentTheme(themePath);
            m_advancedStylesheet->updateStylesheet();
        }
        qApp->setStyleSheet(m_advancedStylesheet->styleSheet() + getHighDensityQssOverlay(m_darkMode));
    } else if (qApp) {
        qApp->setStyleSheet(getHighDensityQssOverlay(m_darkMode));
    }
    emit themeModeChanged(m_darkMode);
}

/**
 * @brief Reloads both theme and UI font point size from QSettings without restarting.
 */
void ThemeManager::applyThemeAndFonts() {
    if (!m_advancedStylesheet) {
        return;
    }

    // Re-read the selected theme and validate against the available theme catalog.
    QSettings s("gridlock", "debugger");
    QString theme = s.value("appearance/theme", "dark_teal").toString();
    if (!m_advancedStylesheet->themes().contains(theme) &&
        !m_advancedStylesheet->themes().contains(theme + ".xml")) {
        theme = "dark_teal";
    }
    m_darkMode = s.value("appearance/dark_mode", !theme.startsWith("light_")).toBool();

    const QString themePath = m_advancedStylesheet->themes().contains(theme + ".xml")
                                  ? theme + ".xml"
                                  : theme;
    m_advancedStylesheet->setCurrentTheme(themePath);

    // Update the `font_size` stylesheet variable and regenerate the composite QSS.
    const int uiFontSize = s.value("appearance/ui_font_size", 11).toInt();
    m_advancedStylesheet->setThemeVariableValue("font_size", QString("%1pt").arg(uiFontSize));

    m_advancedStylesheet->updateStylesheet();
    applyApplicationPalette(m_darkMode);
    const QString qss = m_advancedStylesheet->styleSheet() + getHighDensityQssOverlay(m_darkMode);
    qApp->setStyleSheet(qss);

    // Apply the updated point size to the application font.
    QFont appFont = qApp->font();
    appFont.setPointSize(uiFontSize);
    qApp->setFont(appFont);
    emit themeModeChanged(m_darkMode);
}

} // namespace gridlock::core::managers

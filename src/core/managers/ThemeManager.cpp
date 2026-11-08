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
#include "ConfigManager.hpp"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFont>
#include <QPalette>
#include <QSettings>
#include <QStandardPaths>
#include <QWidget>

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
        // Dock container borders and uppercase dock title headers (>= 12px UI sans-serif).
        "QDockWidget { border: 1px solid #d0d7de; color: #1f2328; }\n"
        "QDockWidget::title {\n"
        "    background: #eaeef2; color: #1f2328; padding: 3px 8px; height: 26px;\n"
        "    min-height: 22px; max-height: 26px; border-bottom: 1px solid #d0d7de;\n"
        "    font-size: 12px; font-weight: 600; text-transform: uppercase;\n"
        "}\n"
        // Tabbed workspace and dock tab bars with 2px royal-blue active tab underline.
        "QTabWidget::pane, QTabWidget#EditorTabManager::pane { border: 0px; background: #ffffff; }\n"
        "QTabBar { background: #eaeef2; }\n"
        "QTabBar::tab {\n"
        "    background: #eaeef2; color: #57606a; padding: 4px 10px; height: 26px;\n"
        "    min-height: 22px; max-height: 26px; border-right: 1px solid #d0d7de;\n"
        "    border-bottom: 1px solid #d0d7de; font-size: 11px; font-weight: 600;\n"
        "}\n"
        // Active selected tab uses pure white surface and dark charcoal label text.
        "QTabBar::tab:selected { background: #ffffff; color: #1f2328; border-bottom: 2px solid #0969da; }\n"
        // Push buttons and toolbar buttons with crisp #d0d7de borders and 12px UI sans-serif font.
        "QPushButton, QToolButton, QToolBar QToolButton {\n"
        "    height: 22px; min-height: 20px; max-height: 24px; padding: 1px 8px;\n"
        "    background: #f6f8fa; border: 1px solid #d0d7de; border-radius: 3px;\n"
        "    color: #1f2328; font-size: 12px; font-weight: 600;\n"
        "}\n"
        // Hover and disabled button states in Light Mode.
        "QPushButton:hover:enabled, QToolButton:hover { background: #eaeef2; border: 1px solid #0969da; color: #0969da; }\n"
        "QToolButton:checked, QToolBar QToolButton:checked { background: #dafbe1; color: #1a7f37; border: 1px solid #2ea043; font-weight: 600; }\n"
        "QPushButton:disabled, QToolButton:disabled { background: #f6f8fa; color: #8c959f; border: 1px solid #d8dee4; }\n"
        // Single-line inputs, spinboxes, combo boxes, and filter bars with 12px UI sans-serif text.
        "QLineEdit, QSpinBox, QComboBox, #toolbarTargetSelector, #cutterSeekBar, #symbolFilterEdit, #stringFilterEdit, #watchExpressionInput {\n"
        "    height: 24px; min-height: 22px; max-height: 24px; padding: 0px 6px;\n"
        "    background: #ffffff; border: 1px solid #d0d7de; border-radius: 3px; color: #1f2328; font-size: 12px; font-weight: 500; font-style: normal;\n"
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
        // Table and tree section headers use 12px UI sans-serif typography.
        "QHeaderView, QHeaderView::section, QTableCornerButton::section {\n"
        "    font-family: 'Inter', sans-serif;\n"
        "    background-color: #eaeef2; color: #1f2328; border-right: 1px solid #d0d7de; border-bottom: 1px solid #d0d7de;\n"
        "    height: 22px; min-height: 22px; max-height: 24px; font-size: 12px; font-weight: 600;\n"
        "}\n"
        // Unified sub-view header strips and bottom status bar in Light Mode.
        "QStatusBar, #cutterMemHeader, #watchToolbar, #evaluatorHeaderBar, #evaluatorPromptBar, "
        "#gdbConsoleHeaderBar, #gdbConsolePromptBar, #refManualHeaderBar, #mpiDiagnosticsHeaderBar, "
        "#mpiNetworkLogHeaderBar, #variablesHeaderBar {\n"
        "    background: #eaeef2; color: #1f2328; border-bottom: 1px solid #d0d7de; min-height: 24px; max-height: 26px;\n"
        "}\n"
        "QToolBar, #cutterDisasmHeader, #projectModeBar {\n"
        "    background: #eaeef2; color: #1f2328; border-bottom: 1px solid #d0d7de; min-height: 28px; max-height: 30px;\n"
        "}\n"
        // Menus and modal dialog chrome use 13px UI sans-serif typography.
        "QMenuBar, QMenu, QDialog, QWizard { font-size: 13px; }\n"
        // Status bar telemetry badges, labels, and checkboxes use high-contrast 12px #1f2328 foreground.
        "#cutterStatusTelemetry, #cutterDisasmBadge, #cutterDisasmStats, QLabel, QCheckBox { color: #1f2328; font-size: 12px; }\n"
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
        // Dock container borders and 26px dock title bars in 12px UI sans-serif.
        // Every dock widget has a 1px `#282c34` outline to separate adjacent panes cleanly.
        "QDockWidget { border: 1px solid #282c34; }\n"
        "QDockWidget::title {\n"
        "    background: #21252b; color: #abb2bf; padding: 3px 8px; height: 26px;\n"
        "    min-height: 22px; max-height: 26px; border-bottom: 1px solid #2c313a;\n"
        "    font-size: 12px; font-weight: 600; text-transform: uppercase;\n"
        "}\n"
        // Tabbed dock and bottom console tab bar chrome with 2px royal-blue active indicator.
        // Zero-margin tab panes prevent double borders inside dock containers.
        "QTabWidget::pane { border: 0px; margin: 0px; padding: 0px; background: #16181d; }\n"
        "QTabWidget#EditorTabManager::pane { border: 0px; margin: 0px; padding: 0px; background: #16181d; }\n"
        "QTabBar { background: #181a1f; }\n"
        "QTabBar::tab {\n"
        "    background: #181a1f; color: #abb2bf; padding: 4px 10px; height: 26px;\n"
        "    min-height: 22px; max-height: 26px; margin: 0px; border-right: 1px solid #282c34;\n"
        "    border-bottom: 1px solid #2c313a; font-size: 11px; font-weight: 600; text-transform: uppercase;\n"
        "}\n"
        // Selected tab uses the `#21252b` header surface and a 2px `#528bff` underline.
        "QTabBar::tab:selected {\n"
        "    background: #21252b; color: #ffffff; border-bottom: 2px solid #528bff;\n"
        "}\n"
        // Standard push button states (normal, hover, pressed/checked, disabled) in 12px UI sans-serif.
        // Muted `#323844` borders replace loud neon outlines across all control bars.
        "QPushButton, QPushButton:disabled, QPushButton:flat:disabled {\n"
        "    height: 22px; min-height: 20px; max-height: 24px; padding: 1px 8px; margin: 0px;\n"
        "    background: #252932; border: 1px solid #323844; border-radius: 3px;\n"
        "    color: #d7dae0; font-size: 12px; font-weight: 600;\n"
        "}\n"
        // Hovering an enabled push button subtly brightens the fill and border.
        "QPushButton:hover:enabled {\n"
        "    background: #2c323d; border: 1px solid #3e4452; color: #ffffff;\n"
        "}\n"
        // Active/checked push buttons display a 1px `#528bff` royal-blue accent border.
        "QPushButton:pressed, QPushButton:checked {\n"
        "    background: #2c3444; border: 1px solid #528bff; color: #ffffff;\n"
        "}\n"
        // Disabled push buttons recede into the dark background with muted `#454c59` text.
        "QPushButton:disabled {\n"
        "    background: #14161b; color: #454c59; border: 1px solid #1e2128;\n"
        "}\n"
        // Single-line text inputs, spinboxes, and combo boxes in 12px UI sans-serif.
        "QLineEdit, QLineEdit:disabled, QSpinBox, QSpinBox:disabled, QComboBox, QComboBox:disabled, #toolbarTargetSelector {\n"
        "    height: 24px; min-height: 22px; max-height: 24px; padding: 0px 6px; margin: 0px;\n"
        "    background: #16181d; border: 1px solid #323844; border-radius: 3px;\n"
        "    color: #d7dae0; font-size: 12px; font-weight: 500; font-style: normal;\n"
        "}\n"
        // Focused input controls highlight their border in `#528bff` for keyboard accessibility.
        "QLineEdit:focus, QSpinBox:focus, QComboBox:focus {\n"
        "    border: 1px solid #528bff; background: #1b1e24;\n"
        "}\n"
        // Code editors, disassembly views, documentation browsers, and tabular data grids.
        "QTextEdit, QPlainTextEdit, QTextBrowser, QListWidget {\n"
        "    border: none; border-radius: 0px; padding: 0px; background: #16181d; color: #d7dae0;\n"
        "}\n"
        // Tree and table views enforce monospace data typography, alternating rows, and royal-blue selection.
        "QTreeView, QTableView, QTableWidget {\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    background: #16181d; alternate-background-color: #181b20; color: #d7dae0;\n"
        "    border: none; padding: 0px; gridline-color: #282c34;\n"
        "    selection-background-color: #264f78; selection-color: #ffffff;\n"
        "}\n"
        // Compact 18px minimum row height maximizes visible rows in symbol and register tables.
        "QTreeView::item, QTableView::item { padding: 1px 4px; min-height: 18px; }\n"
        // Selected tree and table rows highlight in #264f78 royal blue with crisp white text.
        "QTreeView::item:selected, QTableView::item:selected, QTableWidget::item:selected {\n"
        "    background: #264f78; color: #ffffff;\n"
        "}\n"
        // Table and tree header container background matches #16181d canvas so empty vertical
        // header space below the last row in DifferentialGrid has zero grey column mismatch.
        "QHeaderView, QHeaderView:vertical, QHeaderView:horizontal {\n"
        "    background-color: #16181d; border: none;\n"
        "}\n"
        // Table and tree column header sections use 12px UI sans-serif with 1px #2c313a border.
        // Matches the #21252b sub-view control strip palette across all side and bottom panes.
        "QHeaderView::section {\n"
        "    font-family: 'Inter', sans-serif; background-color: #21252b; color: #abb2bf;\n"
        "    padding: 2px 6px; height: 22px; min-height: 22px; max-height: 24px;\n"
        "    border: 0px; border-right: 1px solid #2c313a; border-bottom: 1px solid #2c313a;\n"
        "    font-size: 12px; font-weight: 600;\n"
        "}\n"
        // Top-left table corner button matches the #21252b header section surface.
        "QTableCornerButton::section {\n"
        "    background-color: #21252b; border: 0px; border-right: 1px solid #2c313a; border-bottom: 1px solid #2c313a;\n"
        "}\n"
        // Slim 8px vertical and horizontal scrollbars with zero arrow buttons.
        // Uses #16181d track background and #2c323c rounded thumb handle (#3e4452 on hover).
        "QScrollBar:vertical { background: #16181d; width: 8px; margin: 0px; border: none; }\n"
        // Vertical scrollbar thumb handle with 24px minimum grab height.
        "QScrollBar::handle:vertical { background: #2c323c; min-height: 24px; border-radius: 3px; }\n"
        // Brighten vertical scrollbar thumb handle on mouse hover.
        "QScrollBar::handle:vertical:hover { background: #3e4452; }\n"
        // Suppress legacy top/bottom stepper arrow buttons on vertical scrollbars.
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: none; }\n"
        // Horizontal 8px scrollbar track matching dark-slate viewport background.
        "QScrollBar:horizontal { background: #16181d; height: 8px; margin: 0px; border: none; }\n"
        // Horizontal scrollbar thumb handle with 24px minimum grab width.
        "QScrollBar::handle:horizontal { background: #2c323c; min-width: 24px; border-radius: 3px; }\n"
        // Brighten horizontal scrollbar thumb handle on mouse hover.
        "QScrollBar::handle:horizontal:hover { background: #3e4452; }\n"
        // Suppress legacy left/right stepper arrow buttons on horizontal scrollbars.
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; background: none; }\n"
        // Top execution and navigation toolbar accommodating 24px segmented perspective toggles.
        "QToolBar {\n"
        "    height: 30px; max-height: 32px; min-height: 28px; padding: 1px 4px; spacing: 4px;\n"
        "    background: #1e2228; border-bottom: 1px solid #2c313a;\n"
        "}\n"
        // Toolbar buttons use 12px UI sans-serif and `#252932` slate surface.
        "QToolButton, QToolBar QToolButton {\n"
        "    height: 22px; min-height: 20px; max-height: 24px; padding: 1px 8px; margin: 0px;\n"
        "    background: #252932; border: 1px solid #323844; border-radius: 3px;\n"
        "    color: #d7dae0; font-size: 12px; font-weight: 600;\n"
        "}\n"
        // Toolbar button hover state.
        "QToolButton:hover, QToolBar QToolButton:hover {\n"
        "    background: #2c323d; border: 1px solid #3e4452; color: #ffffff;\n"
        "}\n"
        // Toolbar button pressed and latched Running (:checked) states.
        "QToolButton:pressed, QToolBar QToolButton:pressed {\n"
        "    background: #2c3444; border: 1px solid #528bff; color: #ffffff;\n"
        "}\n"
        "QToolButton:checked, QToolBar QToolButton:checked {\n"
        "    background-color: #19362b; color: #4ec9b0; border: 1px solid #2ea043; font-weight: 600;\n"
        "}\n"
        // Disabled toolbar buttons recede into low-contrast dark chrome when session is IDLE.
        "QToolButton:disabled, QToolBar QToolButton:disabled {\n"
        "    background-color: #14161b; border: 1px solid #1e2128; color: #454c59;\n"
        "}\n"
        // Address seek bar (`#cutterSeekBar`) in the top toolbar uses 12px UI typography and 24px height.
        "#cutterSeekBar {\n"
        "    background: #141619; color: #d7dae0; border: 1px solid #323844; border-radius: 3px;\n"
        "    padding: 1px 6px; min-height: 22px; max-height: 24px; font-size: 12px;\n"
        "}\n"
        // Focused address seek bar highlights border in royal blue.
        "#cutterSeekBar:focus { border: 1px solid #528bff; background: #181a1f; }\n"
        // Bottom status bar (`QStatusBar`) and live debugger telemetry strip (`#cutterStatusTelemetry`).
        "QStatusBar {\n"
        "    background: #181a1f; color: #abb2bf; border-top: 1px solid #282c34;\n"
        "    min-height: 24px; max-height: 26px; padding: 1px 8px;\n"
        "}\n"
        "QStatusBar::item { border: none; }\n"
        "#cutterStatusTelemetry {\n"
        "    color: #abb2bf; font-size: 12px; min-height: 18px; padding: 1px 4px;\n"
        "}\n"
        // Unified sub-view header and prompt strips across all workstation panels and bottom tabs.
        "#cutterMemHeader, #watchToolbar, #evaluatorHeaderBar, #evaluatorPromptBar, "
        "#gdbConsoleHeaderBar, #gdbConsolePromptBar, #refManualHeaderBar, "
        "#mpiDiagnosticsHeaderBar, #mpiNetworkLogHeaderBar, #variablesHeaderBar {\n"
        "    background: #21252b; border-bottom: 1px solid #2c313a; min-height: 24px; max-height: 26px;\n"
        "}\n"
        "#cutterDisasmHeader, #projectModeBar {\n"
        "    background: #21252b; border-bottom: 1px solid #2c313a; min-height: 28px; max-height: 30px;\n"
        "}\n"
        // Disassembly header title badge and basic-block/edge metrics label in 12px UI sans-serif.
        "#cutterDisasmBadge { color: #abb2bf; font-size: 12px; font-weight: 600; }\n"
        "#cutterDisasmStats { color: #7f848e; font-size: 12px; }\n"
        // Unified 24px segmented Disassembly mode buttons (CFG, Decompiler, Split CFG+Decompiler).
        "#cutterCfgToggleBtn, #cutterDecompilerBtn, #cutterSplitViewBtn {\n"
        "    background: #21252b; color: #d7dae0; border: 1px solid #282c34; border-radius: 3px;\n"
        "    padding: 0px 10px; min-height: 24px; max-height: 24px; font-size: 12px; font-weight: 600;\n"
        "}\n"
        // Checked state for Disassembly segmented mode buttons uses theme accent fill and white text.
        "#cutterCfgToggleBtn:checked, #cutterDecompilerBtn:checked, #cutterSplitViewBtn:checked {\n"
        "    background: #528bff; color: #ffffff; border: 1px solid #528bff;\n"
        "}\n"
        // Hover state for Disassembly header mode buttons.
        "#cutterCfgToggleBtn:hover, #cutterDecompilerBtn:hover, #cutterSplitViewBtn:hover {\n"
        "    background: #282c34; border: 1px solid #528bff; color: #ffffff;\n"
        "}\n"
        // Hexdump, register table, symbol table, string table, and address-space ribbon selectors.
        "#cutterHexDumpEdit { background: #16181d; border: none; border-top: 1px solid #282c34; }\n"
        "#cutterRegisterTable, #symbolsTable, #stringsTable, #mpiNetworkLogTable {\n"
        "    background: #181a1f; border: none;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace; font-size: 12px;\n"
        "}\n"
        // Top virtual address-space map ribbon bar (`#AddressNavToolbar`).
        "#AddressNavToolbar {\n"
        "    background: #141619; border-bottom: 1px solid #282c34;\n"
        "    min-height: 20px; max-height: 22px; padding: 0px; spacing: 0px;\n"
        "}\n"
        // Left vertical 36px tool-window activity rail (`#bottomToolWindowRail`) background and 1px right border.
        "#bottomToolWindowRail { background: #181a1f; border-right: 1px solid #252932; }\n"
        // Default 28x28px checkable icon-rail tool buttons inside `#bottomToolWindowRail`.
        "#bottomToolWindowRail QToolButton { background: transparent; color: #9da5b4; border: 1px solid transparent; "
        "border-radius: 4px; min-width: 28px; max-width: 28px; min-height: 28px; max-height: 28px; padding: 2px; margin: 0px; }\n"
        // Hover highlight state for inactive tool-window rail buttons.
        "#bottomToolWindowRail QToolButton:hover { background: #21252b; color: #ffffff; border: 1px solid #323844; }\n"
        // Active checked state with left accent bar for the selected tool-window group.
        "#bottomToolWindowRail QToolButton:checked { background: #21252b; color: #61afef; border: 1px solid #528bff; border-left: 2px solid #528bff; }\n"
        // Unified 24px ProjectExplorer segmented mode buttons (FILES | SYMBOLS | STRINGS) and Watch add button.
        "#explorerFilesModeBtn, #explorerSymbolsModeBtn, #explorerStringsModeBtn {\n"
        "    background: #21252b; color: #d7dae0; border: 1px solid #282c34; border-radius: 3px;\n"
        "    padding: 0px 10px; min-height: 24px; max-height: 24px; font-size: 12px; font-weight: 600;\n"
        "}\n"
        "#addWatchBtn {\n"
        "    background: #21252b; color: #d7dae0; border: 1px solid #282c34; border-radius: 3px;\n"
        "    padding: 0px 8px; min-height: 22px; max-height: 24px; font-size: 12px; font-weight: 600;\n"
        "}\n"
        // Checked state for ProjectExplorer mode selector buttons uses theme accent fill and white text.
        "#explorerFilesModeBtn:checked, #explorerSymbolsModeBtn:checked, #explorerStringsModeBtn:checked {\n"
        "    background: #528bff; color: #ffffff; border: 1px solid #528bff;\n"
        "}\n"
        // Filter inputs for Symbols, Strings, and Watch expressions in 12px UI sans-serif.
        "#symbolFilterEdit, #stringFilterEdit, #watchExpressionInput {\n"
        "    background: #141619; color: #d7dae0; border: 1px solid #323844; border-radius: 3px;\n"
        "    padding: 1px 6px; min-height: 20px; max-height: 24px; font-size: 12px;\n"
        "}\n"
        // Modal Dialog Dark-Slate Chrome & Content Cards (13px dialog typography).
        "QDialog, QWizard { background: #16181d; color: #d7dae0; font-size: 13px; }\n"
        // Dialog QGroupBox content cards with 1px solid #2c313a border.
        "QGroupBox {\n"
        "    background: #1b1e24; border: 1px solid #2c313a; border-radius: 3px;\n"
        "    margin-top: 8px; padding-top: 10px; color: #d7dae0; font-size: 13px; font-weight: 600;\n"
        "}\n"
        // GroupBox title badge positioning.
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; padding: 0px 6px; color: #61afef; }\n"
        // Standardize dialog footer action buttons inside QDialogButtonBox in 13px UI sans-serif.
        "QDialogButtonBox QPushButton { min-height: 16px; max-height: 18px; min-width: 72px; padding: 4px 14px; margin: 0px; font-size: 13px; }\n"
        // Sleek dialog navigation sidebars with 3px solid #528bff active indicator and 13px typography.
        "QListWidget, QListWidget[role=\"sidebar\"] {\n"
        "    background: #1e2128; color: #abb2bf; selection-background-color: #264f78; selection-color: #ffffff;\n"
        "    border: none; border-right: 1px solid #2c313a; outline: none; font-size: 13px;\n"
        "}\n"
        "QListWidget[role=\"sidebar\"]::item { height: 28px; padding: 2px 8px; border-left: 2px solid transparent; }\n"
        // Style hovered sidebar list items with bright white text on dark-slate fill.
        "QListWidget::item:hover, QListWidget[role=\"sidebar\"]::item:hover {\n"
        "    background-color: #252932; color: #ffffff;\n"
        "}\n"
        // Keep selected list items bright white on #264f78 in both active and !active focus states.
        "QListWidget::item:selected, QListWidget::item:selected:active,\n"
        "QListWidget::item:selected:!active, QListWidget[role=\"sidebar\"]::item:selected,\n"
        "QListWidget[role=\"sidebar\"]::item:selected:active,\n"
        "QListWidget[role=\"sidebar\"]::item:selected:!active {\n"
        "    background-color: #264f78; color: #ffffff; border-left: 3px solid #528bff; font-weight: 600;\n"
        "}\n"
        // Cohesive 13px menu bar and dark-slate popup menus across all views.
        // Enforces #1e222a popup container surface with 1px #323844 structural border.
        "QMenuBar { font-size: 13px; }\n"
        "QMenu {\n"
        "    background: #1e222a; color: #d7dae0; border: 1px solid #323844; padding: 3px 0px; font-size: 13px;\n"
        "}\n"
        // Standardize context menu item row padding and light-slate foreground text.
        "QMenu::item { padding: 4px 20px; color: #d7dae0; background: transparent; }\n"
        // Highlight hovered/selected context menu actions with #264f78 accent fill.
        "QMenu::item:selected { background: #264f78; color: #ffffff; }\n"
        // Render subtle 1px horizontal divider lines between grouped menu actions.
        "QMenu::separator { height: 1px; background: #2c313a; margin: 3px 6px; }\n"
        // Unify floating hover tooltips with the dark-slate popup chrome palette in 12px font.
        "QToolTip {\n"
        "    background: #1e222a; color: #d7dae0; border: 1px solid #323844; padding: 3px 6px; font-size: 12px;\n"
        "}\n"
    );
}

/**
 * @brief Synchronizes QApplication's QPalette roles with the active ThemePalette tokens.
 *
 * Ensures standard Qt widgets and custom-painted viewports querying `QPalette::Window`,
 * `QPalette::Base`, `QPalette::AlternateBase`, `QPalette::Text`, `QPalette::WindowText`,
 * `QPalette::Button`, `QPalette::ButtonText`, or `QPalette::Highlight` inherit the active theme's
 * semantic palette tokens immediately on theme changes.
 *
 * @param tp Structured ThemePalette tokens for the active workstation theme.
 */
static void applyApplicationPalette(const ThemeManager::ThemePalette& tp) {
    if (!qApp) return;
    QPalette pal = qApp->palette();
    // Map semantic ThemePalette hex tokens to standard QPalette color roles across all ColorGroups.
    const QColor canvasCol(tp.canvasHex);
    const QColor headerCol(tp.headerHex);
    const QColor textCol(tp.textHex);
    const QColor accentCol(tp.accentHex);
    pal.setColor(QPalette::Window, tp.isDark ? canvasCol : headerCol);
    pal.setColor(QPalette::WindowText, textCol);
    pal.setColor(QPalette::Base, canvasCol);
    pal.setColor(QPalette::AlternateBase, headerCol);
    pal.setColor(QPalette::Text, textCol);
    pal.setColor(QPalette::Button, headerCol);
    pal.setColor(QPalette::ButtonText, textCol);
    // Preserve high-contrast white HighlightedText on theme accent Highlight across all ColorGroups.
    pal.setColor(QPalette::All, QPalette::Highlight, accentCol);
    pal.setColor(QPalette::All, QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    qApp->setPalette(pal);
}

/**
 * @brief Returns the 12 curated GridLock workstation theme identifiers (8 dark, 4 light).
 */
QStringList ThemeManager::availableThemeIds() const {
    return {
        QStringLiteral("gridlock_slate"), QStringLiteral("dark_teal"),
        QStringLiteral("tokyo_night"),    QStringLiteral("catppuccin_mocha"),
        QStringLiteral("gruvbox_dark"),   QStringLiteral("nord_dark"),
        QStringLiteral("one_dark_pro"),   QStringLiteral("dracula_dark"),
        QStringLiteral("daylight_light"), QStringLiteral("solarized_light"),
        QStringLiteral("catppuccin_latte"), QStringLiteral("one_light")
    };
}

/**
 * @brief Maps a theme identifier to its human-readable display title (zero underscores).
 */
QString ThemeManager::themeDisplayName(const QString& themeId) {
    const QString id = themeId.trimmed().toLower();
    if (id.contains(QStringLiteral("one_light"))) return QStringLiteral("One Light (Light)");
    if (id.contains(QStringLiteral("solarized"))) return QStringLiteral("Solarized Warm (Light)");
    if (id.contains(QStringLiteral("latte"))) return QStringLiteral("Catppuccin Latte (Light)");
    if (id.contains(QStringLiteral("daylight")) || id.startsWith(QStringLiteral("light"))) return QStringLiteral("Daylight Paper (Light)");
    if (id.contains(QStringLiteral("nord"))) return QStringLiteral("Nord");
    if (id.contains(QStringLiteral("one_dark"))) return QStringLiteral("One Dark Pro");
    if (id.contains(QStringLiteral("dracula"))) return QStringLiteral("Dracula");
    if (id.contains(QStringLiteral("tokyo"))) return QStringLiteral("Tokyo Night");
    if (id.contains(QStringLiteral("catppuccin"))) return QStringLiteral("Catppuccin Mocha");
    if (id.contains(QStringLiteral("gruvbox"))) return QStringLiteral("Gruvbox Industrial");
    if (id.contains(QStringLiteral("teal"))) return QStringLiteral("Obsidian Teal");
    return QStringLiteral("GridLock Slate");
}

/**
 * @brief Returns 5 hex swatch colors (canvas, header, border, accent, text) for a theme ID.
 */
QStringList ThemeManager::themeSwatchColors(const QString& themeId) {
    const QString id = themeId.trimmed().toLower();
    if (id.contains(QStringLiteral("one_light"))) {
        return {QStringLiteral("#fafafa"), QStringLiteral("#f0f0f1"), QStringLiteral("#d3d4d8"), QStringLiteral("#4078f2"), QStringLiteral("#383a42")};
    }
    if (id.contains(QStringLiteral("solarized"))) {
        return {QStringLiteral("#fdf6e3"), QStringLiteral("#eee8d5"), QStringLiteral("#d3cbb7"), QStringLiteral("#268bd2"), QStringLiteral("#586e75")};
    }
    if (id.contains(QStringLiteral("latte"))) {
        return {QStringLiteral("#eff1f5"), QStringLiteral("#e6e9ef"), QStringLiteral("#ccd0da"), QStringLiteral("#1e66f5"), QStringLiteral("#4c4f69")};
    }
    if (id.contains(QStringLiteral("daylight")) || id.startsWith(QStringLiteral("light"))) {
        return {QStringLiteral("#ffffff"), QStringLiteral("#eaeef2"), QStringLiteral("#d0d7de"), QStringLiteral("#0969da"), QStringLiteral("#1f2328")};
    }
    if (id.contains(QStringLiteral("nord"))) {
        return {QStringLiteral("#2e3440"), QStringLiteral("#3b4252"), QStringLiteral("#4c566a"), QStringLiteral("#88c0d0"), QStringLiteral("#eceff4")};
    }
    if (id.contains(QStringLiteral("one_dark"))) {
        return {QStringLiteral("#282c34"), QStringLiteral("#21252b"), QStringLiteral("#3e4451"), QStringLiteral("#61afef"), QStringLiteral("#abb2bf")};
    }
    if (id.contains(QStringLiteral("dracula"))) {
        return {QStringLiteral("#282a36"), QStringLiteral("#21222c"), QStringLiteral("#44475a"), QStringLiteral("#bd93f9"), QStringLiteral("#f8f8f2")};
    }
    if (id.contains(QStringLiteral("tokyo"))) {
        return {QStringLiteral("#1a1b26"), QStringLiteral("#24283b"), QStringLiteral("#2f3549"), QStringLiteral("#7aa2f7"), QStringLiteral("#c0caf5")};
    }
    if (id.contains(QStringLiteral("catppuccin"))) {
        return {QStringLiteral("#1e1e2e"), QStringLiteral("#181825"), QStringLiteral("#313244"), QStringLiteral("#89b4fa"), QStringLiteral("#cdd6f4")};
    }
    if (id.contains(QStringLiteral("gruvbox"))) {
        return {QStringLiteral("#1d2021"), QStringLiteral("#282828"), QStringLiteral("#3c3836"), QStringLiteral("#fabd2f"), QStringLiteral("#ebdbb2")};
    }
    if (id == QStringLiteral("obsidian_teal") || id.contains(QStringLiteral("teal"))) {
        return {QStringLiteral("#11161a"), QStringLiteral("#172026"), QStringLiteral("#22303a"), QStringLiteral("#00d2b4"), QStringLiteral("#d8e6eb")};
    }
    // Default GridLock Slate / Baseline Dark
    return {QStringLiteral("#16181d"), QStringLiteral("#21252b"), QStringLiteral("#282c34"), QStringLiteral("#528bff"), QStringLiteral("#d7dae0")};
}

/**
 * @brief Returns structured palette tokens for a given theme identifier.
 */
ThemeManager::ThemePalette ThemeManager::paletteForTheme(const QString& themeId) {
    const QString id = themeId.trimmed().toLower();
    const QStringList sw = themeSwatchColors(id);
    const bool isLight = id.contains(QStringLiteral("light")) || id.contains(QStringLiteral("latte"));
    ThemePalette p;
    if (sw.size() >= 5) {
        p.canvasHex = sw[0]; p.headerHex = sw[1]; p.borderHex = sw[2]; p.accentHex = sw[3]; p.textHex = sw[4];
    } else {
        p.canvasHex = QStringLiteral("#16181d"); p.headerHex = QStringLiteral("#21252b");
        p.borderHex = QStringLiteral("#282c34"); p.accentHex = QStringLiteral("#528bff"); p.textHex = QStringLiteral("#d7dae0");
    }
    p.isDark = !isLight;
    return p;
}

/**
 * @brief Dynamically interpolates UI font size, font family, and curated theme palette tokens into QSS.
 */
static QString buildDynamicWorkstationQss(const QString& baseQss, bool isDark, int uiFontSize,
                                          const QString& uiFontFamily, const QString& themeId) {
    QString overlay = getHighDensityQssOverlay(isDark);

    // Enforce >= 12px UI typography floor and scale up when user selects a larger UI font size.
    const int clampedUiSize = qBound(12, uiFontSize, 32);
    if (uiFontSize > 12) {
        overlay.replace(QStringLiteral("font-size: 12px;"), QStringLiteral("font-size: %1px;").arg(clampedUiSize));
        overlay.replace(QStringLiteral("max-height: 24px;"), QStringLiteral("max-height: %1px;").arg(qMax(24, clampedUiSize + 12)));
    }

    // Apply curated dark or light theme color tokens when a specialized palette is selected.
    const QString id = themeId.trimmed().toLower();
    const QStringList sw = ThemeManager::themeSwatchColors(id);
    if (isDark && sw.size() >= 5 && id != QStringLiteral("gridlock_slate")) {
        overlay.replace(QStringLiteral("#16181d"), sw[0], Qt::CaseInsensitive);
        overlay.replace(QStringLiteral("#21252b"), sw[1], Qt::CaseInsensitive);
        overlay.replace(QStringLiteral("#181a1f"), sw[1], Qt::CaseInsensitive);
        overlay.replace(QStringLiteral("#282c34"), sw[2], Qt::CaseInsensitive);
        overlay.replace(QStringLiteral("#2c313a"), sw[2], Qt::CaseInsensitive);
        overlay.replace(QStringLiteral("#528bff"), sw[3], Qt::CaseInsensitive);
        overlay.replace(QStringLiteral("#d7dae0"), sw[4], Qt::CaseInsensitive);
    } else if (!isDark && sw.size() >= 5 && !id.contains(QStringLiteral("daylight"))) {
        overlay.replace(QStringLiteral("#ffffff"), sw[0], Qt::CaseInsensitive);
        overlay.replace(QStringLiteral("#f6f8fa"), sw[0], Qt::CaseInsensitive);
        overlay.replace(QStringLiteral("#eaeef2"), sw[1], Qt::CaseInsensitive);
        overlay.replace(QStringLiteral("#d0d7de"), sw[2], Qt::CaseInsensitive);
        overlay.replace(QStringLiteral("#0969da"), sw[3], Qt::CaseInsensitive);
        overlay.replace(QStringLiteral("#1f2328"), sw[4], Qt::CaseInsensitive);
    }
    // Override qt_material's dark selection-color on QListView/QListWidget selected rows in light themes.
    if (!isDark && sw.size() >= 4) {
        const QString selFg = (QColor(sw[3]).lightness() < 165) ? QStringLiteral("#ffffff") : QStringLiteral("#16181d");
        overlay += QStringLiteral("\nQListView::item:selected, QListView::item:selected:focus, QListWidget::item:selected, QListWidget::item:selected:focus, QListWidget::item:selected:active, QListWidget::item:selected:!active { background-color: %1; selection-background-color: %1; color: %2; selection-color: %2; }\n").arg(sw[3], selFg);
    }

    if (!uiFontFamily.trimmed().isEmpty()) {
        overlay += QStringLiteral("\nQMenuBar, QMenu { font-family: '%1'; font-size: %2px; }\nQLabel, QPushButton, QComboBox, QLineEdit, QSpinBox { font-family: '%1'; font-size: %3px; }\n")
                       .arg(uiFontFamily.trimmed()).arg(qMax(13, clampedUiSize)).arg(clampedUiSize);
    }
    // Upgrade any sub-12px rules emitted by the upstream qt_material template before appending overlay.
    QString sanitizedBase = baseQss;
    for (int subPx : {8, 9, 10, 11}) {
        sanitizedBase.replace(QStringLiteral("font-size: %1px").arg(subPx), QStringLiteral("font-size: 12px"), Qt::CaseInsensitive);
    }
    return sanitizedBase + overlay;
}

/**
 * @brief Initializes QtAdvancedStylesheet paths, loads user preferences, and applies persisted theme.
 */
void ThemeManager::initialize() {
    const QString stylesDir = QStringLiteral(":/styles");
    m_advancedStylesheet->setStylesDirPath(stylesDir);
    m_advancedStylesheet->setOutputDirPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/acss"));
    m_advancedStylesheet->setCurrentStyle(QStringLiteral("qt_material"));
    loadPersistedTheme();
}

/** @brief Returns the list of curated workstation themes. */
QStringList ThemeManager::getAvailableThemes() const { return availableThemeIds(); }

/** @brief Applies a theme by identifier, automatically deriving dark/light polarity from the theme. */
void ThemeManager::applyTheme(const QString& themeName, bool persist) {
    const QString id = themeName.trimmed();
    const bool isLight = id.contains(QStringLiteral("light"), Qt::CaseInsensitive) ||
                         id.contains(QStringLiteral("latte"), Qt::CaseInsensitive);
    setTheme(id, !isLight, persist);
}

/**
 * @brief Dynamically switches the active theme, updates QSS overlay, and persists to settings.
 * @param themeName Internal theme identifier string (e.g. "gridlock_slate", "solarized_light").
 * @param isDark True for dark-slate polarity; false for high-contrast light polarity.
 * @param persist True to persist the selected theme to QSettings and ConfigManager.
 */
void ThemeManager::setTheme(const QString& themeName, bool isDark, bool persist) {
    if (themeName.trimmed().isEmpty()) return;
    m_activeThemeId = themeName.trimmed();
    const bool isLightTheme = m_activeThemeId.contains(QStringLiteral("light"), Qt::CaseInsensitive) ||
                              m_activeThemeId.contains(QStringLiteral("latte"), Qt::CaseInsensitive);
    m_darkMode = isDark && !isLightTheme;

    QSettings s(QStringLiteral("gridlock"), QStringLiteral("debugger"));
    const int uiFontSize = s.value(QStringLiteral("appearance/ui_font_size"), 11).toInt();
    const QString uiFontFamily = s.value(QStringLiteral("appearance/ui_font_family"), QString()).toString();

    static QMap<QString, QString> s_acssCache;
    QString baseAcss;
    if (m_advancedStylesheet) {
        const QString acssTheme = m_darkMode ? QStringLiteral("dark_teal") : QStringLiteral("light_blue");
        const QString cacheKey = acssTheme + QLatin1Char(':') + QString::number(uiFontSize);
        if (s_acssCache.contains(cacheKey)) {
            baseAcss = s_acssCache.value(cacheKey);
        } else if (m_advancedStylesheet->themes().contains(acssTheme) || m_advancedStylesheet->themes().contains(acssTheme + ".xml")) {
            m_advancedStylesheet->setCurrentTheme(m_advancedStylesheet->themes().contains(acssTheme + ".xml") ? acssTheme + ".xml" : acssTheme);
            m_advancedStylesheet->setThemeVariableValue("font_size", QString("%1pt").arg(uiFontSize));
            m_advancedStylesheet->updateStylesheet();
            baseAcss = m_advancedStylesheet->styleSheet();
            s_acssCache.insert(cacheKey, baseAcss);
        }
    }
    const ThemePalette activePal = paletteForTheme(m_activeThemeId);
    applyApplicationPalette(activePal);
    m_currentStyleSheet = buildDynamicWorkstationQss(baseAcss, m_darkMode, uiFontSize, uiFontFamily, m_activeThemeId);
    if (qApp) {
        const bool themeOrQssChanged = (qApp->property("gridlock_active_theme").toString() != m_activeThemeId) ||
                                       (qApp->styleSheet() != m_currentStyleSheet);
        qApp->setProperty("gridlock_active_theme", m_activeThemeId);
        qApp->setProperty("gridlock_dark_mode", m_darkMode);
        qApp->setProperty("gridlock_swatch_colors", themeSwatchColors(m_activeThemeId));
        if (themeOrQssChanged) {
            if (qApp->styleSheet() != m_currentStyleSheet) {
                qApp->setStyleSheet(m_currentStyleSheet);
            }
            applyApplicationPalette(activePal);
            for (QWidget* w : QApplication::allWidgets()) {
                if (w && w->metaObject()->indexOfMethod("reloadStyle()") >= 0) {
                    QMetaObject::invokeMethod(w, "reloadStyle", Qt::DirectConnection);
                }
            }
        }
    }
    if (persist) savePersistedTheme();
    emit themeModeChanged(m_darkMode);
}

/**
 * @brief Persists active theme identifier and typography settings to QSettings and ConfigManager.
 */
void ThemeManager::savePersistedTheme() const {
    if (m_activeThemeId.trimmed().isEmpty()) return;
    const QString id = m_activeThemeId.trimmed();
    QSettings s1(QStringLiteral("GridLock"), QStringLiteral("GridLockWorkstation"));
    s1.setValue(QStringLiteral("appearance/theme_id"), id);
    s1.setValue(QStringLiteral("appearance/theme"), id);
    s1.setValue(QStringLiteral("appearance/dark_mode"), m_darkMode);

    QSettings s2(QStringLiteral("GridLock"), QStringLiteral("Workstation"));
    s2.setValue(QStringLiteral("appearance/theme_id"), id);
    s2.setValue(QStringLiteral("appearance/theme"), id);
    s2.setValue(QStringLiteral("appearance/dark_mode"), m_darkMode);

    QSettings s3(QStringLiteral("gridlock"), QStringLiteral("debugger"));
    s3.setValue(QStringLiteral("appearance/theme_id"), id);
    s3.setValue(QStringLiteral("appearance/theme"), id);
    s3.setValue(QStringLiteral("appearance/dark_mode"), m_darkMode);

    gridlock::core::ConfigManager::instance().setThemeName(id);
}

/**
 * @brief Restores active theme from ConfigManager / QSettings, applies it, and returns the theme ID.
 * @return Restored active theme identifier.
 */
QString ThemeManager::loadPersistedTheme() {
    QString themeId = gridlock::core::ConfigManager::instance().getThemeName();
    if (themeId.trimmed().isEmpty() || themeId == QStringLiteral("gridlock_slate")) {
        QSettings s1(QStringLiteral("GridLock"), QStringLiteral("GridLockWorkstation"));
        QString sVal = s1.value(QStringLiteral("appearance/theme_id")).toString();
        if (!sVal.trimmed().isEmpty()) {
            themeId = sVal;
        } else {
            QSettings s2(QStringLiteral("GridLock"), QStringLiteral("Workstation"));
            sVal = s2.value(QStringLiteral("appearance/theme_id")).toString();
            if (!sVal.trimmed().isEmpty()) {
                themeId = sVal;
            } else {
                QSettings s3(QStringLiteral("gridlock"), QStringLiteral("debugger"));
                sVal = s3.value(QStringLiteral("appearance/theme_id"),
                                s3.value(QStringLiteral("appearance/theme"), QStringLiteral("gridlock_slate"))).toString();
                if (!sVal.trimmed().isEmpty()) themeId = sVal;
            }
        }
    }
    if (!themeId.trimmed().isEmpty()) {
        if (m_activeThemeId != themeId.trimmed() || m_currentStyleSheet.isEmpty()) {
            applyTheme(themeId, false);
        }
    }
    return m_activeThemeId;
}

/**
 * @brief Switches between dark-slate (`true`) and high-contrast light (`false`) workstation palettes.
 */
void ThemeManager::setDarkMode(bool dark) {
    m_darkMode = dark;
    const bool currentlyLight = m_activeThemeId.contains(QStringLiteral("light"), Qt::CaseInsensitive) ||
                                m_activeThemeId.contains(QStringLiteral("latte"), Qt::CaseInsensitive);
    if (!dark && !currentlyLight) m_activeThemeId = QStringLiteral("daylight_light");
    else if (dark && currentlyLight) m_activeThemeId = QStringLiteral("gridlock_slate");
    setTheme(m_activeThemeId, dark);
}

/**
 * @brief Reloads both theme and UI font point size from QSettings without restarting.
 */
void ThemeManager::applyThemeAndFonts() {
    QSettings s(QStringLiteral("gridlock"), QStringLiteral("debugger"));
    const QString savedTheme = s.value(QStringLiteral("appearance/theme"), QStringLiteral("gridlock_slate")).toString();
    const int uiFontSize = s.value(QStringLiteral("appearance/ui_font_size"), 11).toInt();
    const QString uiFontFamily = s.value(QStringLiteral("appearance/ui_font_family"), QString()).toString();
    applyTheme(savedTheme);
    if (qApp) {
        QFont appFont = qApp->font();
        if (!uiFontFamily.trimmed().isEmpty()) appFont.setFamily(uiFontFamily.trimmed());
        appFont.setPointSize(uiFontSize);
        qApp->setFont(appFont);
    }
}

} // namespace gridlock::core::managers

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
#include <QDir>
#include <QFont>
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
static QString getHighDensityQssOverlay() {
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
        // Dock container borders and compact 18-24px dock title bars.
        // Every dock widget has a 1px `#282c34` outline to separate adjacent panes cleanly.
        "QDockWidget { border: 1px solid #282c34; }\n"
        "QDockWidget::title {\n"
        "    background: #21252b;\n"
        "    color: #abb2bf;\n"
        "    padding: 3px 8px;\n"
        "    min-height: 18px;\n"
        "    max-height: 24px;\n"
        "    border-bottom: 1px solid #2c313a;\n"
        "    font-size: 11px;\n"
        "    font-weight: 600;\n"
        "}\n"
        // Tabbed dock and bottom console tab bar chrome with 2px royal-blue active indicator.
        // Zero-margin tab panes prevent double borders inside dock containers.
        "QTabWidget::pane { border: 0px; margin: 0px; padding: 0px; background: #1b1e24; }\n"
        "QTabBar { background: #16181d; }\n"
        "QTabBar::tab {\n"
        "    background: #16181d;\n"
        "    color: #9da5b4;\n"
        "    padding: 3px 10px;\n"
        "    min-height: 18px;\n"
        "    max-height: 24px;\n"
        "    margin: 0px;\n"
        "    border-right: 1px solid #282c34;\n"
        "    border-bottom: 1px solid #2c313a;\n"
        "    font-size: 11px;\n"
        "    font-weight: 600;\n"
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
        "    background: #1b1e24;\n"
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
        // Table and tree column header sections capped at 20-22px height with 1px #2c313a border.
        // Matches the #21252b sub-view control strip palette across all side and bottom panes.
        "QHeaderView::section {\n"
        "    background: #21252b;\n"
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
        // Unified 24-26px sub-view header strips across all workstation panels and bottom tabs.
        "#cutterDisasmHeader, #cutterMemHeader, #projectModeBar, #watchToolbar, "
        "#evaluatorHeaderBar, #refManualHeaderBar, #mpiDiagnosticsHeaderBar, #variablesHeaderBar {\n"
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
        "#cutterRegisterTable, #symbolsTable, #stringsTable {\n"
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
    );
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

    const int uiFontSize = s.value("appearance/ui_font_size", 11).toInt();

    // Compile the XML palette variables and append the workstation QSS overlay.
    m_advancedStylesheet->setCurrentTheme(theme);
    m_advancedStylesheet->setThemeVariableValue("font_size", QString("%1pt").arg(uiFontSize));
    m_advancedStylesheet->updateStylesheet();
    const QString qss = m_advancedStylesheet->styleSheet() + getHighDensityQssOverlay();
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
void ThemeManager::setTheme(const QString& themeName, bool /*isDark*/) {
    if (m_advancedStylesheet && !themeName.isEmpty()) {
        // Resolve optional `.xml` extension required by the stylesheet catalog.
        const QString themePath = m_advancedStylesheet->themes().contains(themeName + ".xml")
                                      ? themeName + ".xml"
                                      : themeName;
        m_advancedStylesheet->setCurrentTheme(themePath);
        m_advancedStylesheet->updateStylesheet();
        qApp->setStyleSheet(m_advancedStylesheet->styleSheet() + getHighDensityQssOverlay());
    }
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

    const QString themePath = m_advancedStylesheet->themes().contains(theme + ".xml")
                                  ? theme + ".xml"
                                  : theme;
    m_advancedStylesheet->setCurrentTheme(themePath);

    // Update the `font_size` stylesheet variable and regenerate the composite QSS.
    const int uiFontSize = s.value("appearance/ui_font_size", 11).toInt();
    m_advancedStylesheet->setThemeVariableValue("font_size", QString("%1pt").arg(uiFontSize));

    m_advancedStylesheet->updateStylesheet();
    const QString qss = m_advancedStylesheet->styleSheet() + getHighDensityQssOverlay();
    qApp->setStyleSheet(qss);

    // Apply the updated point size to the application font.
    QFont appFont = qApp->font();
    appFont.setPointSize(uiFontSize);
    qApp->setFont(appFont);
}

} // namespace gridlock::core::managers

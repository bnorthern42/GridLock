#include "ThemeManager.hpp"
#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>
#include <QFont>
#include "QtAdvancedStylesheet.h"

namespace gridlock::core::managers {

ThemeManager& ThemeManager::instance() {
    static ThemeManager instance;
    return instance;
}

ThemeManager::ThemeManager() : m_advancedStylesheet(std::make_unique<acss::QtAdvancedStylesheet>()) {
}

ThemeManager::~ThemeManager() = default;

static QString getHighDensityQssOverlay() {
    return QStringLiteral(
        "\n/* Dark High-Density Workstation Design System */\n"
        "QMainWindow, QWidget#centralWidget { background: #16181d; }\n"
        "QMainWindow::separator { width: 4px; height: 4px; background: #1e2228; border-left: 1px solid #282c34; }\n"
        "QSplitter::handle { width: 4px; height: 4px; background: #1e2228; border-left: 1px solid #282c34; }\n"
        "QSplitter::handle:horizontal { width: 4px; }\n"
        "QSplitter::handle:vertical { height: 4px; }\n"
        "QMainWindow::separator:hover, QSplitter::handle:hover { background: #528bff; }\n"
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
        "QTabBar::tab:selected {\n"
        "    background: #21252b;\n"
        "    color: #ffffff;\n"
        "    border-bottom: 2px solid #528bff;\n"
        "}\n"
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
        "QPushButton:hover:enabled {\n"
        "    background: #2c323d;\n"
        "    border: 1px solid #3e4452;\n"
        "    color: #ffffff;\n"
        "}\n"
        "QPushButton:pressed, QPushButton:checked {\n"
        "    background: #2c3444;\n"
        "    border: 1px solid #528bff;\n"
        "    color: #ffffff;\n"
        "}\n"
        "QPushButton:disabled {\n"
        "    background: #181a1f;\n"
        "    color: #5c6370;\n"
        "    border: 1px solid #242830;\n"
        "}\n"
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
        "QLineEdit:focus, QSpinBox:focus, QComboBox:focus {\n"
        "    border: 1px solid #528bff;\n"
        "    background: #1b1e24;\n"
        "}\n"
        "QTextEdit, QPlainTextEdit {\n"
        "    border: 1px solid #282c34;\n"
        "    border-radius: 0px;\n"
        "    padding: 0px;\n"
        "    background: #16181d;\n"
        "    color: #d7dae0;\n"
        "}\n"
        "QTreeView, QTableView, QTableWidget {\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    background: #181a1f;\n"
        "    border: none;\n"
        "    padding: 0px;\n"
        "    gridline-color: #282c34;\n"
        "}\n"
        "QTreeView::item, QTableView::item {\n"
        "    padding: 1px 4px;\n"
        "    min-height: 18px;\n"
        "}\n"
        "QHeaderView::section {\n"
        "    background: #21252b;\n"
        "    color: #abb2bf;\n"
        "    padding: 2px 6px;\n"
        "    height: 20px;\n"
        "    min-height: 20px;\n"
        "    max-height: 22px;\n"
        "    border: 0px;\n"
        "    border-right: 1px solid #282c34;\n"
        "    border-bottom: 1px solid #2c313a;\n"
        "    font-size: 10px;\n"
        "    font-weight: 600;\n"
        "}\n"
        "QToolBar {\n"
        "    height: 26px;\n"
        "    max-height: 28px;\n"
        "    min-height: 24px;\n"
        "    padding: 1px 4px;\n"
        "    spacing: 4px;\n"
        "    background: #1e2228;\n"
        "    border-bottom: 1px solid #2c313a;\n"
        "}\n"
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
        "QToolButton:hover, QToolBar QToolButton:hover {\n"
        "    background: #2c323d;\n"
        "    border: 1px solid #3e4452;\n"
        "    color: #ffffff;\n"
        "}\n"
        "QToolButton:pressed, QToolBar QToolButton:pressed, QToolButton:checked, QToolBar QToolButton:checked {\n"
        "    background: #2c3444;\n"
        "    border: 1px solid #528bff;\n"
        "    color: #ffffff;\n"
        "}\n"
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
        "#cutterSeekBar:focus {\n"
        "    border: 1px solid #528bff;\n"
        "    background: #181a1f;\n"
        "}\n"
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
        "#cutterDisasmHeader, #cutterMemHeader, #projectModeBar, #watchToolbar {\n"
        "    background: #21252b;\n"
        "    border-bottom: 1px solid #2c313a;\n"
        "    min-height: 24px;\n"
        "    max-height: 24px;\n"
        "}\n"
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
        "#cutterDecompilerBtn:checked, #cutterSplitViewBtn:checked {\n"
        "    background: #2c3444;\n"
        "    color: #ffffff;\n"
        "    border: 1px solid #528bff;\n"
        "}\n"
        "#cutterCfgToggleBtn:hover, #cutterDecompilerBtn:hover, #cutterSplitViewBtn:hover {\n"
        "    background: #2c323d;\n"
        "    border: 1px solid #528bff;\n"
        "    color: #ffffff;\n"
        "}\n"
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
        "#AddressNavToolbar {\n"
        "    background: #141619;\n"
        "    border-bottom: 1px solid #282c34;\n"
        "    min-height: 20px;\n"
        "    max-height: 22px;\n"
        "    padding: 0px;\n"
        "    spacing: 0px;\n"
        "}\n"
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
        "#explorerFilesModeBtn:checked, #explorerSymbolsModeBtn:checked, #explorerStringsModeBtn:checked {\n"
        "    background: #2c3444;\n"
        "    color: #ffffff;\n"
        "    border: 1px solid #528bff;\n"
        "}\n"
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
    );
}

void ThemeManager::initialize() {
    QString stylesDir = ":/styles";
    m_advancedStylesheet->setStylesDirPath(stylesDir);
    m_advancedStylesheet->setOutputDirPath(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/acss");
    m_advancedStylesheet->setCurrentStyle("qt_material");
    
    QSettings s("gridlock", "debugger");
    QString theme = s.value("appearance/theme", "dark_teal").toString();
    if (!m_advancedStylesheet->themes().contains(theme) && !m_advancedStylesheet->themes().contains(theme + ".xml")) {
        theme = "dark_teal";
    }
    
    int uiFontSize = s.value("appearance/ui_font_size", 11).toInt();

    m_advancedStylesheet->setCurrentTheme(theme);
    m_advancedStylesheet->setThemeVariableValue("font_size", QString("%1pt").arg(uiFontSize));
    m_advancedStylesheet->updateStylesheet();
    QString qss = m_advancedStylesheet->styleSheet() + getHighDensityQssOverlay();
    qApp->setStyleSheet(qss);

    QFont appFont = qApp->font();
    appFont.setPointSize(uiFontSize);
    qApp->setFont(appFont);
}

QStringList ThemeManager::getAvailableThemes() const {
    if (m_advancedStylesheet) {
        return m_advancedStylesheet->themes();
    }
    return QStringList();
}

void ThemeManager::setTheme(const QString& themeName, bool /*isDark*/) {
    if (m_advancedStylesheet && !themeName.isEmpty()) {
        QString themePath = m_advancedStylesheet->themes().contains(themeName + ".xml") ? themeName + ".xml" : themeName;
        m_advancedStylesheet->setCurrentTheme(themePath);
        m_advancedStylesheet->updateStylesheet();
        qApp->setStyleSheet(m_advancedStylesheet->styleSheet() + getHighDensityQssOverlay());
    }
}

void ThemeManager::applyThemeAndFonts() {
    if (!m_advancedStylesheet) return;

    QSettings s("gridlock", "debugger");
    QString theme = s.value("appearance/theme", "dark_teal").toString();
    if (!m_advancedStylesheet->themes().contains(theme) && !m_advancedStylesheet->themes().contains(theme + ".xml")) {
        theme = "dark_teal";
    }

    QString themePath = m_advancedStylesheet->themes().contains(theme + ".xml") ? theme + ".xml" : theme;
    m_advancedStylesheet->setCurrentTheme(themePath);
    
    int uiFontSize = s.value("appearance/ui_font_size", 11).toInt();
    m_advancedStylesheet->setThemeVariableValue("font_size", QString("%1pt").arg(uiFontSize));
    
    m_advancedStylesheet->updateStylesheet();
    QString qss = m_advancedStylesheet->styleSheet() + getHighDensityQssOverlay();
    qApp->setStyleSheet(qss);

    QFont appFont = qApp->font();
    appFont.setPointSize(uiFontSize);
    qApp->setFont(appFont);
}

} // namespace gridlock::core::managers

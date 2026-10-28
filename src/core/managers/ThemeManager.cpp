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
        "\n/* Dark High-Density Workstation Overlay */\n"
        "QMainWindow::separator { width: 1px; height: 1px; background: #2b2b2b; }\n"
        "QMainWindow::separator:hover { background: #3d424b; }\n"
        "QSplitter::handle { background: #2b2b2b; }\n"
        "QSplitter::handle:horizontal { width: 1px; }\n"
        "QSplitter::handle:vertical { height: 1px; }\n"
        "QSplitter::handle:hover { background: #3d424b; }\n"
        "QDockWidget { border: 1px solid #2b2b2b; }\n"
        "QDockWidget::title {\n"
        "    background: #1a1d21;\n"
        "    color: #d4d7dc;\n"
        "    padding: 3px 6px;\n"
        "    border-bottom: 1px solid #2b2b2b;\n"
        "    font-weight: 600;\n"
        "}\n"
        "QTabWidget::pane { border: 0px; margin: 0px; padding: 0px; }\n"
        "QTabBar::tab {\n"
        "    background: #1a1d21;\n"
        "    color: #9da5b4;\n"
        "    padding: 3px 8px;\n"
        "    margin: 0px;\n"
        "    border-right: 1px solid #2b2b2b;\n"
        "    border-bottom: 1px solid #2b2b2b;\n"
        "}\n"
        "QTabBar::tab:selected {\n"
        "    background: #23272e;\n"
        "    color: #ffffff;\n"
        "    border-bottom: 2px solid #009688;\n"
        "}\n"
        "QTreeView, QTableView, QTableWidget {\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    gridline-color: #2b2b2b;\n"
        "}\n"
        "QTreeView::item, QTableView::item {\n"
        "    padding: 1px 4px;\n"
        "    min-height: 18px;\n"
        "}\n"
        "QHeaderView::section {\n"
        "    background: #1a1d21;\n"
        "    color: #b0b8c4;\n"
        "    padding: 2px 6px;\n"
        "    border: 0px;\n"
        "    border-right: 1px solid #2b2b2b;\n"
        "    border-bottom: 1px solid #2b2b2b;\n"
        "}\n"
        "QToolBar {\n"
        "    background: #1a1d21;\n"
        "    border-bottom: 1px solid #2b2b2b;\n"
        "    spacing: 2px;\n"
        "    padding: 1px 4px;\n"
        "}\n"
        "#cutterSeekBar {\n"
        "    background: #141619;\n"
        "    color: #d4d7dc;\n"
        "    border: 1px solid #333842;\n"
        "    border-radius: 2px;\n"
        "    padding: 2px 6px;\n"
        "    min-height: 20px;\n"
        "    font-size: 11px;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "}\n"
        "#cutterSeekBar:focus {\n"
        "    border: 1px solid #569cd6;\n"
        "    background: #181a1f;\n"
        "}\n"
        "QStatusBar {\n"
        "    background: #181a1f;\n"
        "    color: #b0b8c4;\n"
        "    border-top: 1px solid #2b2b2b;\n"
        "    min-height: 24px;\n"
        "    padding: 2px 8px;\n"
        "}\n"
        "QStatusBar::item {\n"
        "    border: none;\n"
        "}\n"
        "#cutterStatusTelemetry {\n"
        "    color: #b0b8c4;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 11px;\n"
        "    min-height: 18px;\n"
        "    padding: 1px 4px;\n"
        "}\n"
        "#cutterDisasmHeader {\n"
        "    background: #1a1d21;\n"
        "    border-bottom: 1px solid #2b2b2b;\n"
        "}\n"
        "#cutterDisasmBadge {\n"
        "    color: #d4d7dc;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 11px;\n"
        "    font-weight: 600;\n"
        "}\n"
        "#cutterDisasmStats {\n"
        "    color: #858585;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 10px;\n"
        "}\n"
        "#cutterCfgToggleBtn {\n"
        "    background: #23272e;\n"
        "    color: #4ec9b0;\n"
        "    border: 1px solid #333842;\n"
        "    border-radius: 2px;\n"
        "    padding: 1px 8px;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 10px;\n"
        "}\n"
        "#cutterCfgToggleBtn:hover {\n"
        "    background: #2c313a;\n"
        "    border: 1px solid #569cd6;\n"
        "}\n"
        "#cutterMemHeader {\n"
        "    background: #1a1d21;\n"
        "    border-bottom: 1px solid #2b2b2b;\n"
        "}\n"
        "#cutterRegisterTable {\n"
        "    background: #181a1f;\n"
        "    border: none;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 11px;\n"
        "}\n"
        "#AddressNavToolbar {\n"
        "    background: #141619;\n"
        "    border-bottom: 1px solid #2b2b2b;\n"
        "    padding: 0px;\n"
        "    spacing: 0px;\n"
        "}\n"
        "#projectModeBar, #watchToolbar {\n"
        "    background: #1a1d21;\n"
        "    border-bottom: 1px solid #2b2b2b;\n"
        "}\n"
        "#explorerFilesModeBtn, #explorerSymbolsModeBtn, #addWatchBtn {\n"
        "    background: #23272e;\n"
        "    color: #9da5b4;\n"
        "    border: 1px solid #2b2b2b;\n"
        "    border-radius: 2px;\n"
        "    padding: 1px 6px;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 10px;\n"
        "    font-weight: 600;\n"
        "}\n"
        "#explorerFilesModeBtn:checked, #explorerSymbolsModeBtn:checked {\n"
        "    background: #1e3038;\n"
        "    color: #4ec9b0;\n"
        "    border: 1px solid #009688;\n"
        "}\n"
        "#symbolFilterEdit, #watchExpressionInput {\n"
        "    background: #141619;\n"
        "    color: #d4d7dc;\n"
        "    border: 1px solid #333842;\n"
        "    border-radius: 2px;\n"
        "    padding: 1px 5px;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 10px;\n"
        "}\n"
        "#symbolsTable {\n"
        "    background: #181a1f;\n"
        "    border: none;\n"
        "    font-family: 'JetBrains Mono', 'Fira Code', 'Monospace', monospace;\n"
        "    font-size: 11px;\n"
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

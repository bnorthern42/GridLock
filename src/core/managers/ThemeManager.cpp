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

void ThemeManager::initialize() {
    QString stylesDir = QApplication::applicationDirPath() + "/styles";
    if (!QDir(stylesDir).exists()) {
        stylesDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/styles"; // Fallback install path
    }
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
    qApp->setStyleSheet(m_advancedStylesheet->styleSheet());

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
        qApp->setStyleSheet(m_advancedStylesheet->styleSheet());
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
    qApp->setStyleSheet(m_advancedStylesheet->styleSheet());

    QFont appFont = qApp->font();
    appFont.setPointSize(uiFontSize);
    qApp->setFont(appFont);
}

} // namespace gridlock::core::managers

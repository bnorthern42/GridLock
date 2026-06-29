#include "ThemeManager.hpp"
#include <QApplication>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>
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
    
    m_advancedStylesheet->setCurrentTheme(theme);
    m_advancedStylesheet->updateStylesheet();
    qApp->setStyleSheet(m_advancedStylesheet->styleSheet());
}

QStringList ThemeManager::getAvailableThemes() const {
    if (m_advancedStylesheet) {
        return m_advancedStylesheet->themes();
    }
    return QStringList();
}

void ThemeManager::setTheme(const QString& themeName, bool /*isDark*/) {
    if (m_advancedStylesheet && !themeName.isEmpty()) {
        m_advancedStylesheet->setCurrentTheme(themeName);
        m_advancedStylesheet->updateStylesheet();
        qApp->setStyleSheet(m_advancedStylesheet->styleSheet());
    }
}

} // namespace gridlock::core::managers

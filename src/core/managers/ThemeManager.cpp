#include "ThemeManager.hpp"
#include <QStyleFactory>
#include <QApplication>
#include <QPalette>
#include <QColor>
#include <QStyle>

namespace gridlock::core::managers {

ThemeManager& ThemeManager::instance() {
    static ThemeManager instance;
    return instance;
}

QStringList ThemeManager::getAvailableThemes() const {
    return QStyleFactory::keys();
}

void ThemeManager::setTheme(const QString& styleName, bool isDark) {
    if (!styleName.isEmpty()) {
        QApplication::setStyle(QStyleFactory::create(styleName));
    }
    
    if (isDark) {
        QPalette darkPalette;
        darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::WindowText, Qt::white);
        darkPalette.setColor(QPalette::Base, QColor(42, 42, 42));
        darkPalette.setColor(QPalette::AlternateBase, QColor(66, 66, 66));
        darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
        darkPalette.setColor(QPalette::ToolTipText, Qt::white);
        darkPalette.setColor(QPalette::Text, Qt::white);
        darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
        darkPalette.setColor(QPalette::ButtonText, Qt::white);
        darkPalette.setColor(QPalette::BrightText, Qt::red);
        darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
        darkPalette.setColor(QPalette::HighlightedText, Qt::black);
        
        darkPalette.setColor(QPalette::Disabled, QPalette::Text, Qt::darkGray);
        darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, Qt::darkGray);
        darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, Qt::darkGray);

        QApplication::setPalette(darkPalette);
    } else {
        QApplication::setPalette(QApplication::style()->standardPalette());
    }
}

} // namespace gridlock::core::managers

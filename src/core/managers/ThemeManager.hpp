/**
 * @file ThemeManager.hpp
 * @brief Global stylesheet, dark-slate workstation chrome, and UI typography manager.
 *
 * Coordinates QtAdvancedStylesheet (`qt_material` XML palettes) with GridLock's
 * high-density workstation QSS overlay so all docks, headers, splitters, and
 * compact 20px controls maintain uniform visual metrics across runtime theme changes.
 */

#pragma once

#include <memory>

#include <QObject>
#include <QStringList>

namespace acss {
class QtAdvancedStylesheet;
} // namespace acss

namespace gridlock::core::managers {

/**
 * @brief Singleton manager controlling application-wide QSS themes and font sizing.
 */
class ThemeManager : public QObject {
    Q_OBJECT
public:
    /** @brief Returns the process-wide ThemeManager singleton instance. */
    static ThemeManager& instance();

    /** @brief Loads the persisted theme and font size from QSettings and applies the global QSS. */
    void initialize();

    /** @brief Returns the list of available QtAdvancedStylesheet theme names. */
    QStringList getAvailableThemes() const;

    /**
     * @brief Switches the active base theme and reapplies the high-density workstation QSS overlay.
     * @param themeName Name of the theme (with or without `.xml` suffix).
     * @param isDark True if the selected theme uses a dark palette.
     */
    void setTheme(const QString& themeName, bool isDark = true);

    /** @brief Re-reads theme and font preferences from QSettings and updates qApp immediately. */
    void applyThemeAndFonts();

private:
    ThemeManager();
    ~ThemeManager() override;

    std::unique_ptr<acss::QtAdvancedStylesheet> m_advancedStylesheet;
};

} // namespace gridlock::core::managers

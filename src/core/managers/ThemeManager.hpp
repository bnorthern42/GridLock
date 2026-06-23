#pragma once
#include <QObject>
#include <QStringList>

namespace gridlock::core::managers {

class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager& instance();
    
    QStringList getAvailableThemes() const;
    void setTheme(const QString& styleName, bool isDark);

private:
    ThemeManager() = default;
};

} // namespace gridlock::core::managers

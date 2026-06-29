#pragma once
#include <QObject>
#include <QStringList>
#include <memory>

namespace acss {
    class QtAdvancedStylesheet;
}

namespace gridlock::core::managers {

class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager& instance();
    
    void initialize();
    QStringList getAvailableThemes() const;
    void setTheme(const QString& themeName, bool isDark = true);

private:
    ThemeManager();
    ~ThemeManager();
    
    std::unique_ptr<acss::QtAdvancedStylesheet> m_advancedStylesheet;
};

} // namespace gridlock::core::managers

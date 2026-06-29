#pragma once
#include <QWidget>
#include <QFont>
#include <QStringList>

class QTermWidget;

namespace gridlock::ui {

class TerminalDockWidget : public QWidget {
    Q_OBJECT
public:
    explicit TerminalDockWidget(const QString& title, QWidget* parent = nullptr);
    
    // Test configuration wrappers
    void setTerminalFont(const QString& family, int pointSize);
    QFont terminalFont() const;
    
    void setColorScheme(const QString& scheme);
    QString colorScheme() const;
    
    void setScrollbackLines(int lines);
    int scrollbackLines() const;
    
    void setEnvironment(const QStringList& env);
    QStringList environment() const;

private:
    QTermWidget* m_termWidget;
    QString m_colorScheme;
    QStringList m_environment;
};

} // namespace gridlock::ui

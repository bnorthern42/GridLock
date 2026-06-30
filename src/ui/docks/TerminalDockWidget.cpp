#include "TerminalDockWidget.hpp"
#include <QVBoxLayout>
#include <QApplication>
#include <qtermwidget.h>
#include <QProcessEnvironment>
#include <QTimer>
#include "../../core/managers/ConfigManager.hpp"

namespace gridlock::ui {

TerminalDockWidget::TerminalDockWidget(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    Q_UNUSED(title);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_termWidget = new QTermWidget(0, this);
    const auto ts = gridlock::core::ConfigManager::instance().getTerminalSettings();
    m_termWidget->setShellProgram(ts.shellPath);
    
    // Set default environment
    QStringList env;
    env << "TERM=xterm-256color" << "COLORTERM=truecolor";
    setEnvironment(env);
    
    // Set default dark theme styling
    setColorScheme("DarkPastels");
    
    // Start terminal
    m_termWidget->startShellProgram();

    // Set font based on ConfigManager AFTER starting the shell, 
    // and delay it to the next event loop iteration. qtermwidget is notorious
    // for resetting/ignoring fonts applied before it has fully spun up its VT100 emulator.
    QTimer::singleShot(0, this, [this, ts]() {
        QFont font(ts.fontFamily, ts.fontSize);
        font.setStyleHint(QFont::Monospace);
        m_termWidget->setTerminalFont(font);
    });
    
    layout->addWidget(m_termWidget);
}

void TerminalDockWidget::setTerminalFont(const QString& family, int pointSize) {
    QFont font(family, pointSize);
    font.setStyleHint(QFont::Monospace);
    m_termWidget->setTerminalFont(font);
}

QFont TerminalDockWidget::terminalFont() const {
    return m_termWidget->getTerminalFont();
}

void TerminalDockWidget::setColorScheme(const QString& scheme) {
    m_colorScheme = scheme;
    m_termWidget->setColorScheme(scheme);
}

QString TerminalDockWidget::colorScheme() const {
    return m_colorScheme;
}

void TerminalDockWidget::setScrollbackLines(int lines) {
    m_termWidget->setHistorySize(lines);
}

int TerminalDockWidget::scrollbackLines() const {
    return m_termWidget->historySize();
}

void TerminalDockWidget::setEnvironment(const QStringList& env) {
    m_environment = env;
    m_termWidget->setEnvironment(env);
}

QStringList TerminalDockWidget::environment() const {
    return m_environment;
}

} // namespace gridlock::ui

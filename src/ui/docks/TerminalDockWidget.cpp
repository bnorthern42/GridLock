#include "TerminalDockWidget.hpp"
#include <QVBoxLayout>
#include <QApplication>
#include <qtermwidget.h>
#include <QProcessEnvironment>

namespace gridlock::ui {

TerminalDockWidget::TerminalDockWidget(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    Q_UNUSED(title);
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_termWidget = new QTermWidget(0, this);
    
    // Set default shell (or fallback to /bin/bash)
    QString shell = qEnvironmentVariable("SHELL");
    if (shell.isEmpty()) {
        shell = "/bin/bash";
    }
    m_termWidget->setShellProgram(shell);
    
    // Set default environment
    QStringList env;
    env << "TERM=xterm-256color" << "COLORTERM=truecolor";
    setEnvironment(env);
    
    // Set default dark theme styling
    setColorScheme("DarkPastels");
    
    // Attempt to set a developer-friendly font if available
    QFont font("FiraCode Nerd Font", 10);
    font.setStyleHint(QFont::Monospace);
    m_termWidget->setTerminalFont(font);
    
    // Start terminal
    m_termWidget->startShellProgram();
    
    layout->addWidget(m_termWidget);
}

void TerminalDockWidget::setTerminalFont(const QString& family, int pointSize) {
    QFont font(family, pointSize);
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

/**
 * @file TerminalDockWidget.cpp
 * @brief Implementation of the embedded terminal emulator dock widget.
 *
 * Implements shell initialization, VT100 typography management, delayed font
 * application, and terminal color scheme bindings wrapping QTermWidget.
 */

#include "TerminalDockWidget.hpp"
#include "../../core/managers/ConfigManager.hpp"

#include <QApplication>
#include <QFontDatabase>
#include <QFontInfo>
#include <QProcessEnvironment>
#include <QTimer>
#include <QVBoxLayout>
#include <qtermwidget.h>

namespace gridlock::ui {

/**
 * @brief Resolves a strict fixed-pitch monospace font, falling back to guaranteed system monospace fonts.
 * @param family Candidate font family name.
 * @param pointSize Desired point size.
 * @return Validated fixed-pitch QFont instance.
 */
static QFont resolveMonospaceFont(const QString& family, int pointSize) {
    QFont font(family, pointSize);
    font.setFixedPitch(true);
    font.setStyleHint(QFont::Monospace);
    if (!QFontInfo(font).fixedPitch()) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (pointSize > 0) {
            font.setPointSize(pointSize);
        }
        font.setFixedPitch(true);
        font.setStyleHint(QFont::Monospace);
        if (!QFontInfo(font).fixedPitch()) {
            static const QStringList fallbackFamilies = {
                QStringLiteral("Liberation Mono"),
                QStringLiteral("JetBrains Mono"),
                QStringLiteral("Hack")
            };
            for (const QString& fam : fallbackFamilies) {
                QFont candidate(fam, pointSize > 0 ? pointSize : 11);
                candidate.setFixedPitch(true);
                candidate.setStyleHint(QFont::Monospace);
                if (QFontInfo(candidate).fixedPitch()) {
                    font = candidate;
                    break;
                }
            }
        }
    }
    return font;
}

/**
 * @brief Constructs the terminal widget, launches shell, and configures environment.
 * @param title Dock window title string.
 * @param parent Optional parent widget.
 */
TerminalDockWidget::TerminalDockWidget(const QString& title, QWidget* parent)
    : QWidget(parent) {
    Q_UNUSED(title);

    // Layout without interior padding for edge-to-edge terminal display.
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Instantiate QTermWidget backend.
    m_termWidget = new QTermWidget(0, this);
    // Isolate terminal canvas from external stylesheet cascading padding/margins.
    m_termWidget->setStyleSheet(QStringLiteral(
        "QTermWidget, QTermWidget QWidget { padding: 0px; margin: 0px; border: none; font-family: monospace; }"));

    const auto ts = gridlock::core::ConfigManager::instance().getTerminalSettings();
    m_termWidget->setShellProgram(ts.shellPath);

    // Set default standard terminal environment parameters.
    QStringList env;
    env << "TERM=xterm-256color" << "COLORTERM=truecolor";
    setEnvironment(env);

    // Apply default dark theme color scheme.
    setColorScheme("DarkPastels");

    // Launch underlying shell process.
    m_termWidget->startShellProgram();

    // Enforce fixed-pitch monospace typography at startup.
    m_activeFont = resolveMonospaceFont(ts.fontFamily, ts.fontSize);

    // Set font based on ConfigManager AFTER starting the shell, 
    // and delay it to the next event loop iteration. qtermwidget is notorious
    // for resetting/ignoring fonts applied before it has fully spun up its VT100 emulator.
    QTimer::singleShot(0, this, [this]() {
        m_termWidget->setTerminalFont(m_activeFont);
        m_termWidget->update();
    });

    layout->addWidget(m_termWidget);
}

/**
 * @brief Configures terminal font family and point size with monospace fallback.
 * @param family Font family name.
 * @param pointSize Point size in points.
 */
void TerminalDockWidget::setTerminalFont(const QString& family, int pointSize) {
    QFont font(family, pointSize);
    font.setFixedPitch(true);
    font.setStyleHint(QFont::Monospace);
    m_requestedFont = font;
    m_activeFont = resolveMonospaceFont(family, pointSize);
    m_termWidget->setTerminalFont(m_activeFont);
    m_termWidget->update();
}

/**
 * @brief Custom font setter validating fixed-pitch metrics before applying.
 * @param family Candidate font family.
 * @param pointSize Font size.
 */
void TerminalDockWidget::setCustomFont(const QString& family, int pointSize) {
    setTerminalFont(family, pointSize);
}

/**
 * @brief Retrieves the active validated fixed-pitch terminal font.
 * @return Current fixed-pitch QFont instance.
 */
QFont TerminalDockWidget::activeTerminalFont() const {
    if (m_activeFont.fixedPitch() || QFontInfo(m_activeFont).fixedPitch()) {
        return m_activeFont;
    }
    return m_termWidget->getTerminalFont();
}

/**
 * @brief Retrieves the active terminal emulator font.
 * @return Current QFont instance.
 */
QFont TerminalDockWidget::terminalFont() const {
    if (!m_requestedFont.family().isEmpty()) {
        return m_requestedFont;
    }
    return activeTerminalFont();
}

/**
 * @brief Sets the active QTermWidget color scheme.
 * @param scheme Name of color scheme.
 */
void TerminalDockWidget::setColorScheme(const QString& scheme) {
    m_colorScheme = scheme;
    m_termWidget->setColorScheme(scheme);
}

/**
 * @brief Retrieves the current color scheme identifier.
 * @return Color scheme name.
 */
QString TerminalDockWidget::colorScheme() const {
    return m_colorScheme;
}

/**
 * @brief Sets the maximum scrollback buffer history size.
 * @param lines Number of history lines.
 */
void TerminalDockWidget::setScrollbackLines(int lines) {
    m_termWidget->setHistorySize(lines);
}

/**
 * @brief Retrieves the active scrollback history buffer capacity.
 * @return Line count.
 */
int TerminalDockWidget::scrollbackLines() const {
    return m_termWidget->historySize();
}

/**
 * @brief Injects custom environment variable strings into the terminal shell.
 * @param env Array of environment definitions.
 */
void TerminalDockWidget::setEnvironment(const QStringList& env) {
    m_environment = env;
    m_termWidget->setEnvironment(env);
}

/**
 * @brief Retrieves the environment array configured on the terminal.
 * @return List of environment variable strings.
 */
QStringList TerminalDockWidget::environment() const {
    return m_environment;
}

}  // namespace gridlock::ui

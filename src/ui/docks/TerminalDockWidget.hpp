/**
 * @file TerminalDockWidget.hpp
 * @brief Embedded VT100 / truecolor terminal emulator dock widget wrapping QTermWidget.
 *
 * Provides a native Linux shell terminal instance embedded into the bottom dock
 * zone with customizable monospace typography, color schemes, scrollback buffers,
 * and environment variable overrides.
 */

#pragma once

#include <QFont>
#include <QString>
#include <QStringList>
#include <QWidget>

class QTermWidget;

namespace gridlock::ui {

/**
 * @class TerminalDockWidget
 * @brief Embedded terminal emulator interface wrapping QTermWidget.
 */
class TerminalDockWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs the terminal dock, initializes shell program, and applies themes.
     * @param title Dock window title string.
     * @param parent Optional parent widget.
     */
    explicit TerminalDockWidget(const QString& title, QWidget* parent = nullptr);

    /**
     * @brief Sets the terminal emulator font family and point size.
     * @param family Monospace font family name (e.g. "JetBrains Mono").
     * @param pointSize Font size in points.
     */
    void setTerminalFont(const QString& family, int pointSize);

    /**
     * @brief Configures terminal font family and point size with strict monospace validation.
     * @param family Desired font family name.
     * @param pointSize Font size in points.
     */
    void setCustomFont(const QString& family, int pointSize);

    /**
     * @brief Retrieves the active terminal emulator font.
     * @return Current QFont instance.
     */
    QFont terminalFont() const;

    /**
     * @brief Retrieves the active validated monospace terminal font.
     * @return Validated fixed-pitch QFont instance.
     */
    QFont activeTerminalFont() const;

    /**
     * @brief Sets the active QTermWidget color scheme.
     * @param scheme Color scheme name (e.g. "DarkPastels").
     */
    void setColorScheme(const QString& scheme);

    /**
     * @brief Retrieves the current color scheme name.
     * @return Color scheme identifier string.
     */
    QString colorScheme() const;

    /**
     * @brief Configures maximum scrollback buffer history size in lines.
     * @param lines Number of historical output lines to retain.
     */
    void setScrollbackLines(int lines);

    /**
     * @brief Retrieves the active scrollback history buffer size.
     * @return Line count capacity.
     */
    int scrollbackLines() const;

    /**
     * @brief Injects custom environment variables into the terminal shell process.
     * @param env List of "KEY=VALUE" environment definitions.
     */
    void setEnvironment(const QStringList& env);

    /**
     * @brief Retrieves the environment variables configured for the terminal.
     * @return List of environment variable strings.
     */
    QStringList environment() const;

    /**
     * @brief Dispatches a shell command to the embedded QTermWidget session and appends a newline.
     * @param command Shell command string to execute in the terminal.
     */
    void sendCommand(const QString& command);

    /**
     * @brief Retrieves the last command sent to the embedded terminal session.
     * @return Trimmed command string most recently sent via sendCommand().
     */
    QString lastSentCommand() const { return m_lastSentCommand; }

private:
    QTermWidget* m_termWidget{nullptr};    ///< Underlying QTermWidget instance.
    QFont m_requestedFont;                 ///< User-configured terminal font setting.
    QFont m_activeFont;                    ///< Active validated monospace font.
    QString m_colorScheme;                 ///< Active color scheme name.
    QStringList m_environment;             ///< Shell process environment array.
    QString m_lastSentCommand;             ///< Most recent shell command sent via sendCommand().
};

}  // namespace gridlock::ui

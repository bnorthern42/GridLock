/**
 * @file TerminalDockWidget.hpp
 * @brief Embedded VT100 / truecolor terminal emulator dock widget wrapping QTermWidget.
 *
 * Provides a native Linux shell terminal instance embedded into the bottom dock
 * zone with customizable monospace typography, color schemes, scrollback buffers,
 * and environment variable overrides.
 */

#pragma once

#include <QElapsedTimer>
#include <QFont>
#include <QString>
#include <QStringList>
#include <QWidget>

class QMenu;
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
    /// Disconnects QTermWidget signals before destruction so shell exit cannot hit a dying dock.
    ~TerminalDockWidget() override;

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

    /**
     * @brief Updates the terminal working directory and cd's the live shell PTY when running.
     * @param dirPath Target directory (or file path whose parent directory will be used).
     */
    void setWorkingDirectory(const QString& dirPath);

    /**
     * @brief Returns the canonical working directory currently tracked by the terminal dock.
     * @return Canonical working directory path.
     */
    QString workingDirectory() const { return m_currentWorkingDir; }

    /**
     * @brief Verifies that QTermWidget and all internal display child widgets retain a fixed-pitch font.
     * @return True if every child widget inside TerminalDockWidget reports fixed-pitch typography.
     */
    bool areAllChildWidgetsFixedPitch() const;

    /**
     * @brief Lazily starts the underlying interactive shell PTY if it has not been launched yet.
     */
    void ensureShellStarted();

    /**
     * @brief Returns true once the underlying shell PTY has been started.
     */
    bool isShellStarted() const { return m_shellStarted; }

    /**
     * @brief Returns the active PTY shell OS PID, or 0 if no shell process is running.
     */
    int shellPid() const;

    /** @brief Gracefully exits and reaps the embedded PTY shell process (SIGTERM -> SIGKILL + QProcess::waitForFinished)
     *         so QTermWidget::Session::close() never logs 'did not die with SIGHUP'. */
    void shutdownTerminalSession();

    /** @brief Returns true when the embedded terminal currently has non-empty selected text. */
    bool hasSelection() const;
    /** @brief Copies currently selected terminal text to the system clipboard. */
    void copySelection();
    /** @brief Pastes current system clipboard text into the embedded terminal PTY. */
    void pasteFromClipboard();
    /** @brief Selects all visible and scrollback text in the embedded terminal. */
    void selectAllText();
    /** @brief Clears the embedded terminal screen buffer. */
    void clearTerminal();
    /** @brief Builds a themed right-click context menu with Copy, Paste, Select All, and Clear Terminal. */
    QMenu* createContextMenu(QWidget* parent = nullptr);
    /** @brief Returns the most recent text pasted into the terminal via pasteFromClipboard(). */
    QString lastPastedText() const { return m_lastPastedText; }

protected:
    /** @brief Lazily launches the interactive shell PTY when the Terminal tab is first shown. */
    void showEvent(QShowEvent* event) override;

    /** @brief Intercepts keyboard copy/paste shortcuts, ShortcutOverride, ContextMenu, and font/geometry events. */
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /** @brief Re-applies validated monospace typography and full-width geometry across QTermWidget and its children. */
    void enforceMonospaceOnChildren();
    /** @brief Displays the themed right-click terminal context menu at @p globalPos. */
    void showTerminalContextMenu(const QPoint& globalPos);

    QTermWidget* m_termWidget{nullptr};    ///< Underlying QTermWidget instance.
    QFont m_requestedFont;                 ///< User-configured terminal font setting.
    QFont m_activeFont;                    ///< Active validated monospace font.
    QString m_colorScheme;                 ///< Active color scheme name.
    QStringList m_environment;             ///< Shell process environment array.
    QString m_lastSentCommand;             ///< Most recent shell command sent via sendCommand().
    QString m_lastPastedText;              ///< Most recent clipboard payload pasted into the terminal.
    QString m_currentWorkingDir;           ///< Canonical active working directory for the shell PTY.
    bool m_applyingFontGuard{false};       ///< Reentrancy guard preventing recursive FontChange loops.
    bool m_shellStarted{false};            ///< True once m_termWidget->startShellProgram() has been invoked.
    bool m_pendingCdSync{false};           ///< Ensures active working directory is synced before the next shell command.
    int m_scrollbackLines{1000};           ///< Configured scrollback buffer capacity in lines.
    QElapsedTimer m_shellStartTimer;       ///< Tracks elapsed time since startShellProgram() for initial PTY readiness.
};

}  // namespace gridlock::ui

/**
 * @file GdbConsoleWidget.hpp
 * @brief Interactive multi-rank GDB MI console widget.
 *
 * Defines the GdbConsoleWidget class, providing an interactive terminal
 * view for GDB Machine Interface traffic, rank-based output filtering,
 * command execution, and formatted console log rendering.
 */

#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

namespace gridlock::ui {

/**
 * @class GdbConsoleWidget
 * @brief Terminal widget displaying input and output streams for GDB debugger instances.
 *
 * Supports switching between raw MI2 protocol streams and cleaned console
 * text, filtering logs by rank ID and text search patterns, and executing
 * arbitrary GDB commands against all or individual MPI ranks.
 */
class GdbConsoleWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs the GDB console widget.
     * @param parent Optional parent widget context.
     */
    explicit GdbConsoleWidget(QWidget* parent = nullptr);

    /**
     * @brief Virtual destructor for clean resource release.
     */
    ~GdbConsoleWidget() override = default;

    /**
     * @brief Resets the rank filter dropdown for a new debugging session.
     * @param count Number of MPI ranks in the session.
     */
    void resetRanks(int count);

    /**
     * @brief Formats a raw MI log string into human-readable console text.
     * @param rank MPI rank identifier.
     * @param text Raw MI record string.
     * @param isInput True if command originated from user, false if output.
     * @param showRaw If true, returns unscrubbed raw protocol text.
     * @return Formatted log string, or empty if suppressed.
     */
    static QString formatLogEntry(int rank, const QString& text, bool isInput,
                                  bool showRaw);

public slots:
    /**
     * @brief Appends asynchronous output received from GDB for a rank.
     * @param rank Target rank index.
     * @param output Received GDB output stream content.
     */
    void appendGdbOutput(int rank, const QString& output);

    /**
     * @brief Appends command input dispatched to GDB for a rank.
     * @param rank Target rank index.
     * @param input Command string sent to GDB stdin.
     */
    void appendGdbInput(int rank, const QString& input);

signals:
    /**
     * @brief Emitted when the user enters a GDB command in the input edit box.
     * @param rank Selected rank ID (-1 for broadcast to all ranks).
     * @param command Command string to execute.
     */
    void commandEntered(int rank, const QString& command);

private slots:
    void onClearClicked();
    void onCommandReturnPressed();
    void onFilterTextChanged(const QString& text);
    void onRankFilterChanged(int index);
    void onRawMiToggled(bool checked);

private:
    /**
     * @brief Re-evaluates active filter constraints and updates the console edit.
     */
    void applyFilter();

    QLineEdit* m_filterEdit = nullptr;
    QPushButton* m_clearButton = nullptr;
    QCheckBox* m_rawMiCheckbox = nullptr;
    QComboBox* m_rankCombo = nullptr;
    QPlainTextEdit* m_consoleEdit = nullptr;
    QLineEdit* m_commandEdit = nullptr;

    /// Internal structure recording individual GDB log events.
    struct LogEntry {
        int rank = 0;
        QString text;
        bool isInput = false;
    };
    QList<LogEntry> m_logs;
};

}  // namespace gridlock::ui

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
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

namespace gridlock::ui {

/**
 * @class GdbConsoleWidget
 * @brief Terminal widget displaying input and output streams for GDB debugger instances.
 *
 * Supports switching between raw MI2 protocol streams and cleaned console
 * text, filtering logs by rank ID and text search patterns, executing
 * arbitrary GDB commands against all or individual MPI ranks, persistent
 * command history (`Up`/`Down`), incremental reverse search (`Ctrl+R`), and
 * live `complete` tab completion (`Tab`).
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
     * @brief Synchronizes the rank selector dropdown count without clearing retained console logs.
     * @param count Active number of MPI ranks in the session.
     */
    void ensureRankCount(int count);

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

    /**
     * @brief Returns the pixel height of the top filter/target header strip.
     * @return Header bar height in pixels.
     */
    int headerBarHeight() const;

    /**
     * @brief Returns the pixel height of the bottom gdb> command prompt strip.
     * @return Prompt bar height in pixels.
     */
    int promptBarHeight() const;

    /** @brief Returns the interactive command input QLineEdit (`#gdbConsoleCommandEdit`). */
    QLineEdit* commandEdit() const { return m_commandEdit; }

    /** @brief Returns the loaded persistent command history list (oldest to newest, max 500 entries). */
    QStringList commandHistory() const { return m_history; }

    /** @brief Reloads command history from `ConfigManager::instance().gdbHistoryFilePath()`. */
    void loadCommandHistory();

    /** @brief Returns true when `(reverse-i-search)` mode (`Ctrl+R`) is currently active. */
    bool isReverseSearchActive() const { return m_reverseSearchActive; }

    /** @brief Returns the current `(reverse-i-search)` query string. */
    QString reverseSearchQuery() const { return m_reverseSearchQuery; }

    /** @brief Returns the currently matched history command in `(reverse-i-search)` mode. */
    QString reverseSearchMatch() const { return m_reverseSearchMatch; }

    /** @brief Returns the inline `(reverse-i-search)` banner label (`#gdbReverseSearchStrip`). */
    QLabel* reverseSearchLabel() const { return m_reverseSearchLabel; }

    /** @brief Returns the `Tab` completion popup list widget (`#GdbCompletionPopup`). */
    QListWidget* completionPopup() const { return m_completionPopup; }

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

    /**
     * @brief Appends interactive REPL evaluation output or sanitizer diagnostics for a rank.
     * @param rank Target MPI rank index (or -1 for broadcast diagnostics).
     * @param output Formatted debugger console response or error message.
     * @param isError True when @p output represents an error or security rejection.
     */
    void appendDebuggerConsoleOutput(int rank, const QString& output, bool isError = false);

    /**
     * @brief Receives live GDB `complete <prefix>` suggestions and applies common prefix or shows `#GdbCompletionPopup`.
     */
    void onConsoleCompletionsReady(const QString& prefix, const QStringList& completions);

signals:
    /**
     * @brief Emitted when the user enters a GDB command in the input edit box.
     * @param rank Selected rank ID (-1 for broadcast to all ranks).
     * @param command Command string to execute.
     */
    void commandEntered(int rank, const QString& command);

    /**
     * @brief Emitted when `Tab` is pressed and live GDB `complete <prefix>` suggestions are requested.
     */
    void completionRequested(const QString& prefix);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onClearClicked();
    void onCommandReturnPressed();
    void onFilterTextChanged(const QString& text);
    void onRankFilterChanged(int index);
    void onRawMiToggled(bool checked);

private:
    /** @brief Re-evaluates active filter constraints and updates the console edit. */
    void applyFilter();
    /** @brief Flushes coalesced pending console lines using sticky-scroll preservation. */
    void flushConsoleBatch();
    void recordCommandInHistory(const QString& command);
    void saveCommandHistory() const;
    void navigateHistory(int delta);
    void startOrCycleReverseSearch();
    void updateReverseSearchMatch(bool cycleOlder);
    void exitReverseSearch(bool restorePrior);
    void triggerTabCompletion();
    void applyCompletionCandidates(const QString& prefix, const QStringList& candidates);
    void acceptSelectedCompletion();
    void hideCompletionPopup();
    static QStringList offlineCompletionsForPrefix(const QString& prefix);

    QWidget* m_headerBar = nullptr;
    QWidget* m_promptBar = nullptr;
    QLineEdit* m_filterEdit = nullptr;
    QPushButton* m_clearButton = nullptr;
    QPushButton* m_sendButton = nullptr;
    QCheckBox* m_rawMiCheckbox = nullptr;
    QComboBox* m_rankCombo = nullptr;
    QPlainTextEdit* m_consoleEdit = nullptr;
    QLineEdit* m_commandEdit = nullptr;
    QLabel* m_reverseSearchLabel = nullptr;
    QListWidget* m_completionPopup = nullptr;
    class QTimer* m_flushTimer = nullptr;
    QStringList m_pendingBatch;

    QStringList m_history;
    int m_historyIndex = 0;
    QString m_pendingDraft;
    bool m_reverseSearchActive = false;
    QString m_reverseSearchQuery;
    QString m_reverseSearchMatch;
    QString m_reverseSearchPriorInput;
    int m_reverseSearchHistoryIdx = -1;
    bool m_awaitingLiveCompletion = false;

    /// Internal structure recording individual GDB MI and interactive REPL log events.
    struct LogEntry {
        int rank = 0;
        QString text;
        bool isInput = false;
        bool isRepl = false;
        bool isError = false;
    };
    QList<LogEntry> m_logs;
};

}  // namespace gridlock::ui

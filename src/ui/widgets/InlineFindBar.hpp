/**
 * @file InlineFindBar.hpp
 * @brief Compact floating inline text search bar attached to a QPlainTextEdit viewport.
 *
 * Provides non-intrusive editor and console text search (`Ctrl+F`) with live match
 * highlighting, match counter telemetry, forward/backward navigation (`Enter` /
 * `Shift+Enter`), case-sensitivity toggling, and `Escape` dismissal that returns
 * keyboard focus directly to the host editor without stealing focus to global bars.
 */

#pragma once

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QPair>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextEdit>
#include <QVector>
#include <QWidget>

namespace gridlock::ui {

/**
 * @class InlineFindBar
 * @brief Floating top-right search bar bound to a `QPlainTextEdit` instance.
 *
 * Computes all document occurrences of the search query, exposes `ExtraSelection`
 * highlight ranges so composite editors (`SourceCodeView`) can merge them with
 * active-line and diagnostic highlights, or applies them directly on standalone
 * `QPlainTextEdit` consoles (`GdbConsoleWidget`).
 */
class InlineFindBar : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs an inline find bar parented to @p editor.
     * @param editor Host `QPlainTextEdit` searched and highlighted by this bar.
     * @param externalSelectionSync When true, emits `matchesUpdated()` instead of calling `editor->setExtraSelections()` directly.
     */
    explicit InlineFindBar(QPlainTextEdit* editor, bool externalSelectionSync = false);

    /** @brief Destructor. */
    ~InlineFindBar() override = default;

    /**
     * @brief Shows the find bar, seeds query from any single-line selection, and focuses the input field.
     */
    void openAndFocus();

    /**
     * @brief Hides the find bar, clears match highlights, and restores keyboard focus to the host editor.
     */
    void closeBar();

    /**
     * @brief Advances to the next match (wrapping around at document end).
     * @return True if at least one match exists and was selected.
     */
    bool findNext();

    /**
     * @brief Moves to the previous match (wrapping around at document start).
     * @return True if at least one match exists and was selected.
     */
    bool findPrevious();

    /** @brief Sets the search query string programmatically and recomputes matches. */
    void setSearchText(const QString& text);

    /** @brief Returns the current search query text. */
    QString searchText() const;

    /** @brief Returns whether case-sensitive matching is enabled. */
    bool isCaseSensitive() const;

    /** @brief Enables or disables case-sensitive matching and recomputes matches. */
    void setCaseSensitive(bool enabled);

    /** @brief Returns the total number of matches in the host document for the current query. */
    int matchCount() const { return static_cast<int>(m_matches.size()); }

    /** @brief Returns the 0-based active match index, or -1 when no match is active. */
    int currentMatchIndex() const { return m_activeMatchIndex; }

    /** @brief Returns the search input `QLineEdit` (`#inlineFindEdit`). */
    QLineEdit* findEdit() const { return m_findEdit; }

    /** @brief Returns the `"Next"` button (`#inlineFindNextBtn`). */
    QPushButton* nextButton() const { return m_nextBtn; }

    /** @brief Returns the `"Previous"` button (`#inlineFindPrevBtn`). */
    QPushButton* previousButton() const { return m_prevBtn; }

    /** @brief Returns the `"Case Sensitive"` checkbox (`#inlineFindCaseCheck`). */
    QCheckBox* caseSensitiveCheckBox() const { return m_caseCheck; }

    /** @brief Returns `ExtraSelection` highlights for all matches and the active match. */
    QList<QTextEdit::ExtraSelection> matchSelections() const;

    /** @brief Refreshes dark/light chrome styling from the host editor palette and repositions the bar. */
    void updatePositionAndStyle();

signals:
    /** @brief Emitted whenever match highlights change or the bar opens/closes. */
    void matchesUpdated();

protected:
    /**
     * @brief Intercepts host editor resize events and `Ctrl+F` / `Enter` / `Shift+Enter` / `Esc` keystrokes.
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /** @brief Re-scans the host document for all occurrences of `searchText()` and selects the nearest match. */
    void recomputeMatches(bool preserveNearestToCursor);

    /** @brief Moves the host editor cursor to `m_matches[m_activeMatchIndex]` and updates telemetry. */
    void focusActiveMatch(bool moveEditorCursor);

    /** @brief Updates the `"k/N"` match count badge label. */
    void updateMatchLabel();

    QPlainTextEdit* m_editor = nullptr;
    QLineEdit* m_findEdit = nullptr;
    QLabel* m_matchLabel = nullptr;
    QPushButton* m_prevBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
    QCheckBox* m_caseCheck = nullptr;
    QPushButton* m_closeBtn = nullptr;
    QVector<QPair<int, int>> m_matches;
    int m_activeMatchIndex = -1;
    bool m_externalSelectionSync = false;
};

}  // namespace gridlock::ui

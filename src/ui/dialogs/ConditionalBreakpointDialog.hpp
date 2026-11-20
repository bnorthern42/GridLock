/**
 * @file ConditionalBreakpointDialog.hpp
 * @brief Cursor-anchored breakpoint configuration popover with rank suspend scope and predicate presets.
 *
 * Provides a frameless dark-slate callout popover anchored directly beneath a source gutter
 * breakpoint marker. Supports toggling breakpoint enablement, selecting multi-rank vs.
 * single-rank suspension scope, and applying MPI-aware boolean condition presets.
 */

#pragma once

#include <QDialog>
#include <QPoint>
#include <QString>
#include <QStringList>

#include <QTreeWidget>
#include <QVector>
#include "../../core/utils/ClangdLspClient.hpp"

class QCheckBox;
class QComboBox;
class QContextMenuEvent;
class QLabel;
class QLineEdit;
class QPaintEvent;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QShowEvent;
class QSpinBox;
class QTreeWidgetItem;

namespace gridlock::ui {

/**
 * @class LspCompletionPopup
 * @brief Sleek floating IntelliSense completion card with kind badge, identifier, and type signature.
 */
class LspCompletionPopup : public QTreeWidget {
    Q_OBJECT
public:
    explicit LspCompletionPopup(QWidget* parent = nullptr);
    void setItems(const QVector<core::ClangdLspClient::LspCompletionItem>& items);
    void positionBelow(const QPoint& globalTopLeft, int preferredWidth = 320);
    bool selectCurrent();
    void selectNext();
    void selectPrevious();

signals:
    void itemChosen(const QString& insertText);

protected:
    void keyPressEvent(QKeyEvent* event) override;
};

/**
 * @class ConditionalBreakpointDialog
 * @brief Cursor-anchored callout popover for configuring line breakpoint state, rank scope, and conditions.
 */
class ConditionalBreakpointDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Constructs the anchored breakpoint popover with an optional initial condition expression.
     * @param currentCondition Pre-existing predicate expression if editing, or empty for unconditional.
     * @param parent Optional parent widget.
     */
    explicit ConditionalBreakpointDialog(const QString& currentCondition = QString(),
                                         QWidget* parent = nullptr);

    /**
     * @brief Populates the popover header with the 1-based source line number and code preview snippet.
     * @param lineOneBased 1-based source line number where the breakpoint resides.
     * @param sourceSnippet Optional source statement snippet displayed in the elided title banner.
     */
    void setTargetLineContext(int lineOneBased, const QString& sourceSnippet = QString());

    /**
     * @brief Provides full source document context for virtual-buffer `clangd` LSP expression completion.
     * @param filePath Path to the source file being edited.
     * @param fullSourceText Complete text of the active source document.
     * @param lineOneBased 1-based line number of the conditional breakpoint.
     */
    void setSourceDocumentContext(const QString& filePath,
                                  const QString& fullSourceText,
                                  int lineOneBased);

    /**
     * @brief Queries ClangdLspClient for member or variable completions matching expr at the breakpoint line.
     * @param expr Partial C/C++ condition expression (e.g., "data[i]." or "send_buf.").
     * @return Ordered list of completion candidates.
     */
    QStringList completionsForExpression(const QString& expr) const;

    /**
     * @brief Appends or replaces the trailing member/identifier token in the condition input with completionLabel.
     * @param completionLabel Selected completion member or variable label (e.g., "coords").
     */
    void applyCompletionItem(const QString& completionLabel);

    /**
     * @brief Returns the formatted title summary banner (e.g., "Line 34 breakpoint — MPI_Send(...)").
     * @return Header summary text string.
     */
    QString targetLineSummaryText() const;

    /**
     * @brief Indicates whether the breakpoint remains enabled when closing the popover.
     * @return True if enabled and not marked for removal; false otherwise.
     */
    bool isBreakpointEnabled() const;

    /**
     * @brief Sets the enabled checkbox state for the breakpoint.
     * @param enabled True to keep breakpoint active; false to disable/remove.
     */
    void setBreakpointEnabled(bool enabled);

    /**
     * @brief Indicates whether the user explicitly clicked Remove Breakpoint (F9).
     * @return True if explicit removal was requested.
     */
    bool isRemoveRequested() const { return m_removeRequested; }

    /**
     * @brief Returns the active suspension scope ("All", "Rank", or "None").
     * @return Suspension scope identifier string.
     */
    QString suspendScope() const;

    /**
     * @brief Configures the radio selection for suspension scope ("All" or "Rank").
     * @param scope Scope identifier ("All" or "Rank").
     */
    void setSuspendScope(const QString& scope);

    /**
     * @brief Populates the condition input field with a quick-preset C/C++ MPI predicate expression.
     * @param expr Boolean predicate expression (e.g., "rank == 0").
     */
    void applyQuickPreset(const QString& expr);

    /**
     * @brief Retrieves the trimmed predicate condition string entered by the user.
     * @return Trimmed C/C++ boolean expression string.
     */
    QString condition() const;

    /**
     * @brief Alias accessor returning the trimmed predicate condition string.
     * @return Trimmed C/C++ boolean expression string.
     */
    QString getCondition() const { return condition(); }

    /** @brief Returns configured pass/ignore hit count (`0` = stop immediately). */
    int ignoreCount() const;
    /** @brief Sets the pass/ignore hit count spinbox value. */
    void setIgnoreCount(int count);
    /** @brief Returns true when Non-Suspending Logpoint mode is checked with a non-empty message. */
    bool isLogpoint() const;
    /** @brief Returns the trimmed Logpoint trace message (empty when Logpoint mode is disabled). */
    QString logMessage() const;
    /** @brief Populates the Logpoint checkbox and message field. */
    void setLogMessage(const QString& message);
    /** @brief Returns true when Temporary (one-shot tbreak) mode is checked. */
    bool isTemporary() const;
    /** @brief Updates the Temporary (one-shot tbreak) checkbox state. */
    void setTemporary(bool temporary);
    /** @brief Returns non-empty GDB command lines entered for `commands ... end`. */
    QStringList commands() const;
    /** @brief Populates the multi-line GDB hit commands editor. */
    void setCommands(const QStringList& commands);

    /**
     * @brief Positions the popover directly beneath a global gutter icon coordinate, clamped to screen bounds.
     * @param globalAnchorPos Global screen point at the bottom-center of the gutter breakpoint marker.
     */
    void positionNearAnchor(const QPoint& globalAnchorPos);

    /**
     * @brief Anchors the popover at globalAnchorPos and displays it immediately.
     * @param globalAnchorPos Global screen coordinate of the clicked breakpoint marker.
     */
    void showAtGlobalPos(const QPoint& globalAnchorPos);

signals:
    /** @brief Emitted when the Enabled checkbox is toggled non-destructively (not when Remove F9 is clicked). */
    void breakpointEnabledToggled(const QString& filePath, int lineOneBased, bool enabled);

protected:
    /**
     * @brief Renders the antialiased rounded dark-slate popover surface and top upward callout triangle.
     * @param event Paint event descriptor.
     */
    void paintEvent(QPaintEvent* event) override;

    /**
     * @brief Re-applies explicit cursor-anchored coordinates on show so window managers do not re-center.
     * @param event Show event descriptor.
     */
    void showEvent(QShowEvent* event) override;

    /**
     * @brief Consumes context menu events inside the popup so right-clicks never bubble to parent views.
     * @param event Context menu event descriptor.
     */
    void contextMenuEvent(QContextMenuEvent* event) override;

    /**
     * @brief Intercepts arrow keys and enter/tab from condition editor to navigate LSP popup.
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QLabel* m_titleLabel = nullptr;          ///< Header label displaying "Line <N> breakpoint".
    QCheckBox* m_enabledCheck = nullptr;     ///< Toggle checkbox enabling or non-destructively disabling the breakpoint.
    QCheckBox* m_temporaryCheck = nullptr;   ///< Toggle checkbox marking a one-shot temporary (`tbreak`) breakpoint.
    QCheckBox* m_suspendCheck = nullptr;     ///< Toggle checkbox controlling execution suspension.
    QRadioButton* m_suspendAllRadio = nullptr;   ///< Radio option suspending all MPI ranks.
    QRadioButton* m_suspendRankRadio = nullptr;  ///< Radio option suspending only the active rank/thread.
    QPushButton* m_makeDefaultBtn = nullptr; ///< Button persisting chosen suspend scope as default.
    QLineEdit* m_conditionEdit = nullptr;    ///< Single-line C/C++ boolean predicate input box.
    QSpinBox* m_ignoreCountSpin = nullptr;   ///< Hit pass/ignore count spinbox (0 = stop immediately).
    QCheckBox* m_logpointCheck = nullptr;    ///< Toggle enabling Non-Suspending Logpoint mode.
    QLineEdit* m_logMessageEdit = nullptr;   ///< Trace expression message for non-suspending Logpoints.
    QPlainTextEdit* m_commandsEdit = nullptr;///< Multi-line GDB `commands ... end` editor executed on breakpoint hit.
    LspCompletionPopup* m_completionPopup = nullptr; ///< Sleek floating IntelliSense completion card.
    QComboBox* m_presetCombo = nullptr;      ///< Dropdown selector offering common MPI rank/loop presets.
    QPushButton* m_removeBtn = nullptr;      ///< Left-aligned button removing the breakpoint (F9).
    QPushButton* m_cancelBtn = nullptr;      ///< Secondary button canceling popover changes cleanly.
    QPushButton* m_doneBtn = nullptr;        ///< Primary right-aligned confirmation button ("Done").
    QString m_sourceFilePath;                ///< Active file path for virtual-buffer LSP completion.
    QString m_fullSourceText;                ///< Full editor document text for virtual-buffer splicing.
    int m_targetLine = -1;                   ///< 1-based line number associated with this popover.
    int m_caretCenterX = 22;                 ///< Horizontal center offset (px) of top callout triangle.
    bool m_removeRequested = false;          ///< Latched true when Remove Breakpoint (F9) is clicked.
    bool m_hasExplicitAnchor = false;        ///< True once positionNearAnchor() has computed screen coords.
    QPoint m_anchoredGlobalPos;              ///< Cached global top-left coordinate for anchored display.
};

}  // namespace gridlock::ui

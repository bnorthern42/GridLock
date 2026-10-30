#pragma once

/**
 * @file SourceCodeView.hpp
 * @brief High-performance C/C++ source code editor and debugger gutter view.
 *
 * Implements SourceCodeView and CppSyntaxHighlighter for source navigation,
 * breakpoint toggling, active line stepping illumination, and variable hover inspection.
 */

#include <QWidget>
#include <QString>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QTimer>
#include <QPoint>
#include <QContextMenuEvent>
#include <QMouseEvent>

namespace gridlock::ui {

/**
 * @class CppSyntaxHighlighter
 * @brief Real-time syntax highlighter for C++ keywords, strings, types, and comments.
 */
class CppSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    /**
     * @brief Constructs the highlighter attached to a QTextDocument.
     * @param parent The parent text document.
     */
    explicit CppSyntaxHighlighter(QTextDocument *parent = nullptr);

protected:
    /**
     * @brief Highlights an individual block of text using regular expressions.
     * @param text The text chunk to highlight.
     */
    void highlightBlock(const QString &text) override;

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> highlightingRules;

    QRegularExpression commentStartExpression;
    QRegularExpression commentEndExpression;
    QTextCharFormat multiLineCommentFormat;
};

class LineNumberArea;

/**
 * @class SourceCodeView
 * @brief Monospace source code editor widget with line numbering, breakpoints, and hover inspection.
 */
class SourceCodeView : public QPlainTextEdit {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new SourceCodeView.
     * @param parent Optional parent widget.
     */
    explicit SourceCodeView(QWidget *parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~SourceCodeView() override = default;

    /**
     * @brief Sets source code content and highlights an initial active line.
     * @param code Full text of the source file.
     * @param activeLine Optional 1-based active line number.
     */
    void setSourceCode(const QString& code, int activeLine = -1);

    /**
     * @brief Sets the path of the currently open source file.
     * @param filePath Filesystem path.
     */
    void setCurrentFile(const QString& filePath) { m_currentFilePath = filePath; }

    /**
     * @brief Returns the path of the currently open source file.
     * @return File path string.
     */
    QString currentFile() const { return m_currentFilePath; }

    /**
     * @brief Highlights a specific 1-based line as the active execution pointer.
     * @param lineNumber 1-based line index.
     */
    void highlightCurrentLine(int lineNumber);

    /**
     * @brief Highlights a 1-based line as the active debugger execution line (or clears if <= 0).
     * @param lineOneBased 1-based source line number, or <= 0 to clear execution highlight.
     */
    void highlightExecutionLine(int lineOneBased);

    /**
     * @brief Returns the currently highlighted 1-based debugger execution line (-1 if none).
     * @return 1-based execution line number, or -1 when idle.
     */
    int executionLine() const { return m_executionLine; }

    /**
     * @brief Selects a 1-based source line, highlights it, and emits sourceLineSelected.
     * @param lineOneBased 1-based source line number.
     */
    void selectSourceLine(int lineOneBased);

    /**
     * @brief Returns the plain text contents of the source document.
     * @return Plain text string.
     */
    QString getPlainText() const { return toPlainText(); }

    /**
     * @brief Returns viewport margins configured for the line number area.
     * @return QMargins.
     */
    QMargins getViewportMargins() const { return viewportMargins(); }

    /**
     * @brief Returns the pixel width reserved for the dedicated left vector icon lane in the gutter.
     * @return Icon lane width in pixels (20px).
     */
    int gutterIconLaneWidth() const { return 20; }

    /**
     * @brief Returns the total pixel width of the two-lane left gutter (icon lane + right-aligned line numbers).
     * @return Gutter width in pixels (>= 48px).
     */
    int lineNumberAreaWidth() const;

    /**
     * @brief Returns the right viewport margin reservation in pixels (strictly 0).
     * @return Right viewport margin in pixels.
     */
    int rightViewportMargin() const { return viewportMargins().right(); }

    /**
     * @brief Indicates whether any 80-column right-margin fill or split band is active.
     * @return Always false for the uniform dark-slate editor canvas.
     */
    bool hasRightMarginFill() const { return false; }

    /**
     * @brief Sets the full set of active breakpoint lines.
     * @param bps Set of 1-based line numbers.
     */
    void setBreakpoints(const QSet<int>& bps);

    /**
     * @brief Returns the set of active 1-based breakpoint line numbers.
     * @return QSet<int> of 1-based breakpoint lines.
     */
    QSet<int> breakpoints() const;

    /**
     * @brief Checks whether a breakpoint is set on the given 1-based line number.
     * @param lineOneBased 1-based source line number.
     * @return True if an active breakpoint is registered on lineOneBased.
     */
    bool hasBreakpoint(int lineOneBased) const;

    /**
     * @brief Toggles a breakpoint at the given 1-based line.
     * @param lineNum 1-based line number.
     */
    void toggleBreakpoint(int lineNum);

    /**
     * @brief Sets or updates a conditional/rank-filtered breakpoint on a 1-based source line.
     * @param lineOneBased 1-based source line number.
     * @param condition Conditional expression (e.g., "rank == 0").
     */
    void setConditionalBreakpoint(int lineOneBased, const QString& condition);

    /**
     * @brief Returns the active C/C++ boolean condition attached to a 1-based breakpoint line.
     * @param lineOneBased 1-based source line number.
     * @return Trimmed condition expression, or empty string if unconditional/unset.
     */
    QString breakpointCondition(int lineOneBased) const;

    /**
     * @brief Returns the configured rank suspension scope ("All" or "Rank") for a breakpoint line.
     * @param lineOneBased 1-based source line number.
     * @return Suspension scope identifier string.
     */
    QString breakpointSuspendScope(int lineOneBased) const;

    /**
     * @brief Returns the 1-based line number currently hovered in the gutter (-1 when none).
     * @return 1-based hovered gutter line number, or -1.
     */
    int hoveredGutterLine() const { return m_hoveredGutterLine; }

    /**
     * @brief Updates the 1-based hovered gutter line and triggers ghost breakpoint preview repaint.
     * @param lineOneBased 1-based line number under cursor, or -1 to clear hover state.
     */
    void setHoveredGutterLine(int lineOneBased);

    /**
     * @brief Returns the composite gutter badge state for a 1-based source line.
     * @param lineOneBased 1-based source line number.
     * @return One of "None", "HoverPreview", "Breakpoint", "ConditionalBreakpoint", "ExecutionPc", or "BreakpointAndExecutionPc".
     */
    QString gutterMarkerStateForLine(int lineOneBased) const;

    /**
     * @brief Returns the current cursor line number (1-based).
     * @return 1-based line number.
     */
    int getCurrentLineNumber() const;

    /**
     * @brief Toggles single-line '//' comments on the active line or selection.
     */
    void toggleLineComment();

    /**
     * @brief Reloads color palette and styling from ConfigManager.
     */
    void reloadStyle();

    /**
     * @brief Paints the gutter line numbers, hover highlights, and breakpoint markers.
     * @param event The paint event.
     */
    void lineNumberAreaPaintEvent(QPaintEvent *event);

    /**
     * @brief Handles mouse clicks in the gutter to toggle breakpoints or open the anchored popover.
     * @param event Mouse press event.
     */
    void lineNumberAreaMousePressEvent(QMouseEvent *event);

    /**
     * @brief Tracks cursor movement over the gutter to illuminate hovered line and ghost preview dot.
     * @param event Mouse move event inside LineNumberArea.
     */
    void lineNumberAreaMouseMoveEvent(QMouseEvent *event);

    /**
     * @brief Clears gutter hover state when the mouse cursor exits LineNumberArea.
     * @param event Leave event from LineNumberArea.
     */
    void lineNumberAreaLeaveEvent(QEvent *event);

    /**
     * @brief Determines whether the standard editor context menu may open at the given widget coordinate.
     * @param pos Position in SourceCodeView coordinates.
     * @return False if pos falls within the gutter width or LineNumberArea geometry; true otherwise.
     */
    bool shouldOpenEditorContextMenuAt(const QPoint& pos) const;

    /**
     * @brief Commits or rolls back tentative breakpoint state after the anchored breakpoint popover closes.
     * @param lineOneBased 1-based source line number.
     * @param wasPreviouslySet True if a breakpoint already existed on lineOneBased prior to right-click.
     * @param accepted True if the popover closed with QDialog::Accepted; false if canceled or dismissed.
     * @param enabledChecked True if the breakpoint remains enabled (not unchecked and not removed).
     * @param condition Optional C/C++ boolean condition expression.
     * @param suspendScope Rank suspension scope ("All", "Rank", or "None").
     */
    void handleBreakpointPopoverResult(int lineOneBased, bool wasPreviouslySet, bool accepted,
                                       bool enabledChecked, const QString& condition,
                                       const QString& suspendScope);

signals:
    /** @brief Emitted to request running the target application. */
    void runTargetRequested();

    /** @brief Emitted to request continuing execution. */
    void continueRequested();

    /** @brief Emitted to request single instruction stepping. */
    void stepInstRequested();

    /** @brief Emitted to request toggling a breakpoint at a location string. */
    void toggleBreakpointRequested(const QString& location);

    /** @brief Emitted when a breakpoint is toggled at a file and line. */
    void breakpointToggled(const QString& file, int line, bool isSet, const QString& condition = QString());

    /** @brief Emitted when hovering over a variable identifier. */
    void hoverVariableRequested(const QString& varName, const QPoint& globalPos);

    /** @brief Emitted to hide any active hover tooltip. */
    void hideHoverTooltip();

    /** @brief Emitted to pin a variable into the watch view. */
    void pinVariableRequested(const QString& varName);

    /** @brief Emitted when a 1-based source line is selected or clicked by the user. */
    void sourceLineSelected(int lineOneBased);

protected:
    void resizeEvent(QResizeEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);
    void handleHoverTimeout();
    void highlightCurrentLine();

private:
    LineNumberArea* m_lineNumberArea = nullptr;
    CppSyntaxHighlighter* m_highlighter = nullptr;
    QMap<int, QString> m_breakpoints;
    QMap<int, QString> m_breakpointSuspendScopes;
    QString m_currentFilePath;
    QString m_lastHoveredWord;
    QTimer* m_hoverTimer = nullptr;
    QPoint m_lastMousePos;
    QPoint m_lastGlobalMousePos;
    int m_executionLine = -1;
    int m_hoveredGutterLine = -1;
};

/**
 * @class LineNumberArea
 * @brief Gutter widget displaying line numbers, hover previews, and breakpoint markers adjacent to the editor.
 */
class LineNumberArea : public QWidget {
public:
    /**
     * @brief Constructs a new LineNumberArea with live mouse hover tracking enabled.
     * @param editor Pointer to the associated SourceCodeView.
     */
    explicit LineNumberArea(SourceCodeView *editor) : QWidget(editor), sourceCodeView(editor) {
        setMouseTracking(true);
    }

    /**
     * @brief Returns the dynamic two-lane width size hint for the gutter.
     * @return QSize with width >= 48px.
     */
    QSize sizeHint() const override { return QSize(sourceCodeView->lineNumberAreaWidth(), 0); }

protected:
    /** @brief Delegates gutter painting to SourceCodeView. */
    void paintEvent(QPaintEvent *event) override { sourceCodeView->lineNumberAreaPaintEvent(event); }

    /** @brief Delegates left/right gutter clicks to SourceCodeView. */
    void mousePressEvent(QMouseEvent *event) override { sourceCodeView->lineNumberAreaMousePressEvent(event); }

    /** @brief Consumes mouse button release inside the gutter so right-click release never triggers fallback menus. */
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event) {
            event->accept();
        }
    }

    /** @brief Delegates cursor hover tracking over the gutter to SourceCodeView. */
    void mouseMoveEvent(QMouseEvent *event) override { sourceCodeView->lineNumberAreaMouseMoveEvent(event); }

    /** @brief Clears hovered gutter state when cursor leaves LineNumberArea. */
    void leaveEvent(QEvent *event) override { sourceCodeView->lineNumberAreaLeaveEvent(event); }

    /** @brief Consumes QContextMenuEvent inside the gutter so right-clicks never bubble up to SourceCodeView. */
    void contextMenuEvent(QContextMenuEvent *event) override {
        if (event) {
            event->accept();
        }
    }

private:
    SourceCodeView *sourceCodeView = nullptr;
};

}  // namespace gridlock::ui


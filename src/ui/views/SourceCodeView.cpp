/**
 * @file SourceCodeView.cpp
 * @brief Source code editor implementation with syntax highlighting and gutter controls.
 *
 * Implements the SourceCodeView text editor, CppSyntaxHighlighter for regex-based
 * C++ lexical coloring, and LineNumberArea for gutter rendering and breakpoint interactions.
 */

#include "SourceCodeView.hpp"
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QToolBar>
#include <QAction>
#include <QDebug>
#include <QSet>
#include <QMouseEvent>
#include <QWheelEvent>
#include "../../core/managers/ConfigManager.hpp"
#include "../dialogs/ConditionalBreakpointDialog.hpp"

namespace gridlock::ui {

/**
 * @brief Constructs a new CppSyntaxHighlighter and initializes syntax rules.
 *
 * Configures regex patterns and text formats for C++ types, control flow keywords,
 * preprocessor directives, MPI API symbols, string literals, and comments.
 *
 * @param parent Pointer to the underlying QTextDocument.
 */
CppSyntaxHighlighter::CppSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    HighlightingRule rule;

    // Types: soft blue (#6496ff)
    QTextCharFormat typeFormat;
    typeFormat.setForeground(QColor(100, 150, 255));
    typeFormat.setFontWeight(QFont::Bold);
    const QString typePatterns[] = {
        QStringLiteral("\\bchar\\b"), QStringLiteral("\\bclass\\b"),
        QStringLiteral("\\bconst\\b"), QStringLiteral("\\bdouble\\b"),
        QStringLiteral("\\benum\\b"), QStringLiteral("\\bexplicit\\b"),
        QStringLiteral("\\bfriend\\b"), QStringLiteral("\\binline\\b"),
        QStringLiteral("\\bint\\b"), QStringLiteral("\\blong\\b"),
        QStringLiteral("\\bnamespace\\b"), QStringLiteral("\\boperator\\b"),
        QStringLiteral("\\bprivate\\b"), QStringLiteral("\\bprotected\\b"),
        QStringLiteral("\\bpublic\\b"), QStringLiteral("\\bshort\\b"),
        QStringLiteral("\\bsignals\\b"), QStringLiteral("\\bsigned\\b"),
        QStringLiteral("\\bslots\\b"), QStringLiteral("\\bstatic\\b"),
        QStringLiteral("\\bstruct\\b"), QStringLiteral("\\btemplate\\b"),
        QStringLiteral("\\btypedef\\b"), QStringLiteral("\\btypename\\b"),
        QStringLiteral("\\bunion\\b"), QStringLiteral("\\bunsigned\\b"),
        QStringLiteral("\\bvirtual\\b"), QStringLiteral("\\bvoid\\b"),
        QStringLiteral("\\bvolatile\\b"), QStringLiteral("\\bbool\\b")
    };
    for (const QString &pattern : typePatterns) {
        rule.pattern = QRegularExpression(pattern);
        rule.format = typeFormat;
        highlightingRules.append(rule);
    }

    // Control flow keywords: neon pink/orange
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(255, 80, 150));
    keywordFormat.setFontWeight(QFont::Bold);
    const QString keywordPatterns[] = {
        QStringLiteral("\\bfor\\b"), QStringLiteral("\\breturn\\b"),
        QStringLiteral("\\bif\\b"), QStringLiteral("\\belse\\b"),
        QStringLiteral("\\bwhile\\b"), QStringLiteral("\\bdo\\b"),
        QStringLiteral("\\bswitch\\b"), QStringLiteral("\\bcase\\b"),
        QStringLiteral("\\bbreak\\b"), QStringLiteral("\\bcontinue\\b"),
        QStringLiteral("\\bnew\\b"), QStringLiteral("\\bdelete\\b")
    };
    for (const QString &pattern : keywordPatterns) {
        rule.pattern = QRegularExpression(pattern);
        rule.format = keywordFormat;
        highlightingRules.append(rule);
    }

    // Preprocessor directives: orange (#ff7850)
    QTextCharFormat preprocessorFormat;
    preprocessorFormat.setForeground(QColor(255, 120, 80));
    rule.pattern = QRegularExpression(QStringLiteral("#include[^\n]*"));
    rule.format = preprocessorFormat;
    highlightingRules.append(rule);

    // MPI primitives: purple (#b464ff)
    QTextCharFormat mpiFormat;
    mpiFormat.setForeground(QColor(180, 100, 255));
    rule.pattern = QRegularExpression(QStringLiteral("\\bMPI_[A-Za-z0-9_]+\\b"));
    rule.format = mpiFormat;
    highlightingRules.append(rule);

    // String literals: vibrant green (#64ff64)
    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(100, 255, 100));
    rule.pattern = QRegularExpression(QStringLiteral("\".*?\""));
    rule.format = stringFormat;
    highlightingRules.append(rule);

    // Single-line comments: dim gray (#787878)
    QTextCharFormat singleLineCommentFormat;
    singleLineCommentFormat.setForeground(QColor(120, 120, 120));
    rule.pattern = QRegularExpression(QStringLiteral("//[^\n]*"));
    rule.format = singleLineCommentFormat;
    highlightingRules.append(rule);

    // Multi-line block comments
    multiLineCommentFormat.setForeground(QColor(120, 120, 120));
    commentStartExpression = QRegularExpression(QStringLiteral("/\\*"));
    commentEndExpression = QRegularExpression(QStringLiteral("\\*/"));
}

/**
 * @brief Scans a text block applying syntax highlighting rules and stateful block comments.
 * @param text Content of the paragraph / block.
 */
void CppSyntaxHighlighter::highlightBlock(const QString &text) {
    // Apply single-token regex rules
    for (const HighlightingRule &rule : std::as_const(highlightingRules)) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
    // Track multi-line comment state across blocks
    setCurrentBlockState(0);
    int startIndex = 0;
    if (previousBlockState() != 1) startIndex = text.indexOf(commentStartExpression);
    while (startIndex >= 0) {
        QRegularExpressionMatch match = commentEndExpression.match(text, startIndex);
        int endIndex = match.capturedStart();
        int commentLength = 0;
        if (endIndex == -1) {
            setCurrentBlockState(1);
            commentLength = text.length() - startIndex;
        } else {
            commentLength = endIndex - startIndex + match.capturedLength();
        }
        setFormat(startIndex, commentLength, multiLineCommentFormat);
        startIndex = text.indexOf(commentStartExpression, startIndex + commentLength);
    }
}

/**
 * @class LineNumberArea
 * @brief Gutter widget displaying line numbers and breakpoint markers adjacent to the editor.
 */
class LineNumberArea : public QWidget {
public:
    /**
     * @brief Constructs a new LineNumberArea.
     * @param editor Pointer to the associated SourceCodeView.
     */
    LineNumberArea(SourceCodeView *editor) : QWidget(editor), sourceCodeView(editor) {}

    /**
     * @brief Returns the dynamic two-lane width size hint for the gutter.
     * @return QSize with width >= 48px.
     */
    QSize sizeHint() const override { return QSize(sourceCodeView->lineNumberAreaWidth(), 0); }

protected:
    void paintEvent(QPaintEvent *event) override { sourceCodeView->lineNumberAreaPaintEvent(event); }
    void mousePressEvent(QMouseEvent *event) override { sourceCodeView->lineNumberAreaMousePressEvent(event); }

private:
    SourceCodeView *sourceCodeView;
};

/**
 * @brief Constructs a new SourceCodeView editor widget.
 *
 * Configures monospace font, line number gutter, syntax highlighting, hover inspection timer,
 * and mouse tracking.
 *
 * @param parent Optional parent widget.
 */
SourceCodeView::SourceCodeView(QWidget *parent) : QPlainTextEdit(parent) {
    // Configure borderless editor frame and line number area gutter.
    setFrameShape(QFrame::NoFrame);
    m_lineNumberArea = new LineNumberArea(this);
    connect(document(), &QTextDocument::blockCountChanged, this, &SourceCodeView::updateLineNumberAreaWidth);
    connect(this, &SourceCodeView::updateRequest, this, &SourceCodeView::updateLineNumberArea);
    updateLineNumberAreaWidth(0);

    setReadOnly(true);
    setTextInteractionFlags(Qt::TextSelectableByKeyboard | Qt::TextSelectableByMouse);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    
    reloadStyle(); // Load initial font size and uniform #16181d dark-slate surface

    // Install C++ syntax highlighter
    m_highlighter = new CppSyntaxHighlighter(document());

    // Enable mouse tracking for interactive variable hover inspector
    viewport()->setMouseTracking(true);
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    connect(m_hoverTimer, &QTimer::timeout, this, &SourceCodeView::handleHoverTimeout);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, qOverload<>(&SourceCodeView::highlightCurrentLine));
}

/**
 * @brief Computes the two-lane gutter width reserving 20px for vector badges plus right-aligned line digits.
 * @return Total gutter width in pixels (>= 48px).
 */
int SourceCodeView::lineNumberAreaWidth() const {
    int digits = 3;
    int maxBlocks = qMax(1, blockCount());
    while (maxBlocks >= 1000) {
        maxBlocks /= 10;
        ++digits;
    }
    const int digitsWidth = fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return qMax(48, gutterIconLaneWidth() + digitsWidth + 18);
}

/**
 * @brief Sets active breakpoints from a set of 1-based line numbers.
 * @param bps Set of line numbers.
 */
void SourceCodeView::setBreakpoints(const QSet<int>& bps) {
    m_breakpoints.clear();
    for (int bp : bps) {
        m_breakpoints[bp] = QString();
    }
    highlightCurrentLine();
    m_lineNumberArea->update();
}

/**
 * @brief Returns the set of active 1-based breakpoint line numbers.
 * @return QSet<int> of 1-based breakpoint lines.
 */
QSet<int> SourceCodeView::breakpoints() const {
    QSet<int> result;
    for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it) {
        result.insert(it.key());
    }
    return result;
}

/**
 * @brief Checks whether a breakpoint is set on the given 1-based line number.
 * @param lineOneBased 1-based source line number.
 * @return True if an active breakpoint is registered on lineOneBased.
 */
bool SourceCodeView::hasBreakpoint(int lineOneBased) const {
    return m_breakpoints.contains(lineOneBased);
}

/**
 * @brief Reloads font size, font family, word wrap, and uniform #16181d dark-slate surface from user configuration.
 */
void SourceCodeView::reloadStyle() {
    QSettings s("gridlock", "debugger");
    const int codeFontSize = s.value("appearance/code_font_size", 11).toInt();
    const QString codeFontFamily =
        s.value("appearance/code_font_family", QStringLiteral("JetBrains Mono")).toString();
    const bool wordWrap = s.value("editing/word_wrap", false).toBool();

    setLineWrapMode(wordWrap ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);

    // Update widget QFont directly so font().pointSize() and fontMetrics() reflect live changes.
    QFont codeFont = font();
    if (!codeFontFamily.trimmed().isEmpty()) {
        codeFont.setFamily(codeFontFamily.trimmed());
    }
    codeFont.setStyleHint(QFont::Monospace);
    codeFont.setPointSize(codeFontSize);
    setFont(codeFont);

    // Enforce solid #16181d dark-slate background across both QPlainTextEdit and its viewport
    // to eliminate translucent alpha-compositing bands on partial repaints.
    this->setStyleSheet(QStringLiteral(
        "QPlainTextEdit, QPlainTextEdit QWidget { "
        "   background-color: #16181d; "
        "   color: #d7dae0; "
        "   border: none; "
        "   font-family: '%2', 'JetBrains Mono', 'Fira Code', 'Monospace', monospace; "
        "   font-size: %1pt; "
        "}"
    ).arg(codeFontSize).arg(codeFontFamily.isEmpty() ? QStringLiteral("JetBrains Mono") : codeFontFamily));

    if (viewport()) {
        QPalette pal = viewport()->palette();
        pal.setColor(QPalette::Base, QColor("#16181d"));
        pal.setColor(QPalette::Window, QColor("#16181d"));
        pal.setColor(QPalette::Text, QColor("#d7dae0"));
        viewport()->setPalette(pal);
        viewport()->setAutoFillBackground(true);
    }
    updateLineNumberAreaWidth(0);
}

/**
 * @brief Toggles single-line '//' comments on the active line in the document.
 */
void SourceCodeView::toggleLineComment() {
    QTextCursor tc = textCursor();
    QTextBlock block = tc.block();
    if (!block.isValid()) return;

    const QString lineText = block.text();
    int firstNonSpace = 0;
    while (firstNonSpace < lineText.size() && lineText.at(firstNonSpace).isSpace()) {
        ++firstNonSpace;
    }

    tc.beginEditBlock();
    QTextCursor editCursor(block);
    editCursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, firstNonSpace);
    if (lineText.mid(firstNonSpace).startsWith(QStringLiteral("// "))) {
        editCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 3);
        editCursor.removeSelectedText();
    } else if (lineText.mid(firstNonSpace).startsWith(QStringLiteral("//"))) {
        editCursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
        editCursor.removeSelectedText();
    } else {
        editCursor.insertText(QStringLiteral("// "));
    }
    tc.endEditBlock();
}

/**
 * @brief Retrieves the 1-based line number containing the text cursor.
 * @return 1-based line number.
 */
int SourceCodeView::getCurrentLineNumber() const {
    return textCursor().blockNumber() + 1;
}

/**
 * @brief Toggles a breakpoint on the specified 1-based line.
 * @param lineNum 1-based line number.
 */
void SourceCodeView::toggleBreakpoint(int lineNum) {
    bool isSet = false;
    if (m_breakpoints.contains(lineNum)) {
        m_breakpoints.remove(lineNum);
        isSet = false;
    } else {
        m_breakpoints.insert(lineNum, QString());
        isSet = true;
    }
    highlightCurrentLine();
    m_lineNumberArea->update();
    QString emitPath = m_currentFilePath;
    emit breakpointToggled(emitPath, lineNum, isSet, QString());
}

/**
 * @brief Registers or updates a conditional/rank-filtered breakpoint on a 1-based source line.
 * @param lineOneBased 1-based source line number.
 * @param condition Conditional expression string (e.g., "rank == 0").
 */
void SourceCodeView::setConditionalBreakpoint(int lineOneBased, const QString& condition) {
    if (lineOneBased <= 0) {
        return;
    }
    m_breakpoints.insert(lineOneBased, condition);
    highlightCurrentLine();
    if (m_lineNumberArea) {
        m_lineNumberArea->update();
    }
    emit breakpointToggled(m_currentFilePath, lineOneBased, true, condition);
}

/**
 * @brief Resolves the composite gutter marker badge state for a 1-based source line.
 * @param lineOneBased 1-based source line number.
 * @return "None", "Breakpoint", "ConditionalBreakpoint", "ExecutionPc", or "BreakpointAndExecutionPc".
 */
QString SourceCodeView::gutterMarkerStateForLine(int lineOneBased) const {
    const bool hasBp = m_breakpoints.contains(lineOneBased);
    const bool isExec = (lineOneBased > 0 && lineOneBased == m_executionLine);
    if (hasBp && isExec) {
        return QStringLiteral("BreakpointAndExecutionPc");
    }
    if (hasBp) {
        const QString cond = m_breakpoints.value(lineOneBased).trimmed();
        return cond.isEmpty() ? QStringLiteral("Breakpoint")
                              : QStringLiteral("ConditionalBreakpoint");
    }
    if (isExec) {
        return QStringLiteral("ExecutionPc");
    }
    return QStringLiteral("None");
}

/**
 * @brief Highlights the current cursor line, breakpoint tint bands, and active debugger execution line.
 */
void SourceCodeView::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection selection;
    QColor highlightColor("#1e222a");
    selection.format.setBackground(highlightColor);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    selections.append(selection);

    // Layer subtle translucent crimson/amber background tint across breakpoint lines.
    for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it) {
        const int bpLine = it.key();
        if (bpLine <= 0 || bpLine == m_executionLine) {
            continue;
        }
        QTextBlock bpBlock = document()->findBlockByLineNumber(bpLine - 1);
        if (bpBlock.isValid()) {
            QTextEdit::ExtraSelection bpSel;
            const bool isCond = !it.value().trimmed().isEmpty();
            bpSel.format.setBackground(isCond ? QColor(229, 192, 123, 22)
                                              : QColor(224, 108, 117, 22));
            bpSel.format.setProperty(QTextFormat::FullWidthSelection, true);
            bpSel.cursor = QTextCursor(bpBlock);
            bpSel.cursor.clearSelection();
            selections.append(bpSel);
        }
    }

    // If a 1-based debugger execution line is active, layer a distinct emerald-slate highlight band.
    if (m_executionLine > 0) {
        QTextBlock execBlock = document()->findBlockByLineNumber(m_executionLine - 1);
        if (execBlock.isValid()) {
            QTextEdit::ExtraSelection execSel;
            execSel.format.setBackground(QColor("#20342c"));
            execSel.format.setProperty(QTextFormat::FullWidthSelection, true);
            execSel.cursor = QTextCursor(execBlock);
            execSel.cursor.clearSelection();
            selections.append(execSel);
        }
    }
    setExtraSelections(selections);
}

/**
 * @brief Paints the two-lane gutter with antialiased QPainterPath vector badges and right-aligned line numbers.
 *
 * Left Icon Lane (x = 0..20): Renders centered 11x11px vector badges for Breakpoint (crimson coral circle),
 * ConditionalBreakpoint (amber diamond with dark center dot), ExecutionPc (emerald chevron), and
 * BreakpointAndExecutionPc (crimson outer ring nesting an emerald chevron).
 * Right Line-Number Lane (x = 22..width-8): Renders right-aligned monospace line digits with zero icon collision.
 *
 * @param event The QPaintEvent for the gutter rect.
 */
void SourceCodeView::lineNumberAreaPaintEvent(QPaintEvent *event) {
    QPainter painter(m_lineNumberArea);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    const int gutterW = m_lineNumberArea->width();
    const int iconLaneW = gutterIconLaneWidth();

    painter.fillRect(event->rect(), QColor("#16181d"));
    painter.setPen(QColor("#252932"));
    painter.drawLine(gutterW - 1, event->rect().top(), gutterW - 1, event->rect().bottom());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    // Render visible blocks in view across two non-overlapping horizontal lanes.
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const int lineOneBased = blockNumber + 1;
            const int rowH = qMax(14, bottom - top);
            const qreal cx = iconLaneW * 0.5;
            const qreal cy = top + rowH * 0.5;
            const QString state = gutterMarkerStateForLine(lineOneBased);

            // 1. Left Icon Lane (x = 0..20): Antialiased QPainterPath vector badges.
            if (state == QStringLiteral("Breakpoint")) {
                // Subtle outer halo + 10px crimson-coral disc (#e06c75) with #ff949d rim highlight.
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(224, 108, 117, 64));
                painter.drawEllipse(QPointF(cx, cy), 6.1, 6.1);
                painter.setPen(QPen(QColor("#ff949d"), 1.1));
                painter.setBrush(QColor("#e06c75"));
                painter.drawEllipse(QPointF(cx, cy), 4.7, 4.7);
            } else if (state == QStringLiteral("ConditionalBreakpoint")) {
                // Rotated 11px amber-gold diamond (#e5c07b) with 2.8px dark center cutout dot.
                QPainterPath diamond;
                diamond.moveTo(cx, cy - 5.4);
                diamond.lineTo(cx + 5.4, cy);
                diamond.lineTo(cx, cy + 5.4);
                diamond.lineTo(cx - 5.4, cy);
                diamond.closeSubpath();
                painter.setPen(QPen(QColor("#f0d090"), 1.0));
                painter.setBrush(QColor("#e5c07b"));
                painter.drawPath(diamond);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor("#16181d"));
                painter.drawEllipse(QPointF(cx, cy), 1.5, 1.5);
            } else if (state == QStringLiteral("ExecutionPc")) {
                // Sleek emerald-teal execution chevron (#4ec9b0) + 3px right gutter accent bar.
                QPainterPath chev;
                chev.moveTo(cx - 3.6, cy - 4.5);
                chev.lineTo(cx + 4.6, cy);
                chev.lineTo(cx - 3.6, cy + 4.5);
                chev.lineTo(cx - 1.7, cy);
                chev.closeSubpath();
                painter.setPen(QPen(QColor("#98c379"), 1.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.setBrush(QColor("#4ec9b0"));
                painter.drawPath(chev);
                painter.fillRect(QRect(gutterW - 3, top, 2, rowH), QColor("#4ec9b0"));
            } else if (state == QStringLiteral("BreakpointAndExecutionPc")) {
                // Combined state: crimson outer breakpoint ring (#e06c75) with nested emerald PC chevron (#4ec9b0).
                painter.setPen(QPen(QColor("#e06c75"), 1.8));
                painter.setBrush(QColor("#24181c"));
                painter.drawEllipse(QPointF(cx, cy), 5.3, 5.3);
                QPainterPath innerChev;
                innerChev.moveTo(cx - 2.2, cy - 3.0);
                innerChev.lineTo(cx + 3.2, cy);
                innerChev.lineTo(cx - 2.2, cy + 3.0);
                innerChev.lineTo(cx - 1.0, cy);
                innerChev.closeSubpath();
                painter.setPen(QPen(QColor("#98c379"), 0.8));
                painter.setBrush(QColor("#4ec9b0"));
                painter.drawPath(innerChev);
                painter.fillRect(QRect(gutterW - 3, top, 2, rowH), QColor("#4ec9b0"));
            }

            // 2. Right Line-Number Lane (x = 22..gutterW-8): Right-aligned digits with zero overlap.
            const QString number = QString::number(lineOneBased);
            if (lineOneBased == m_executionLine) {
                painter.setPen(QColor("#4ec9b0"));
            } else if (m_breakpoints.contains(lineOneBased)) {
                painter.setPen(QColor("#e06c75"));
            } else {
                painter.setPen(QColor("#6b7280"));
            }
            const int numLeft = iconLaneW + 2;
            const int numWidth = qMax(12, gutterW - numLeft - 8);
            painter.drawText(numLeft, top, numWidth, rowH, Qt::AlignRight | Qt::AlignVCenter, number);
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

/**
 * @brief Handles gutter mouse clicks for toggling unconditional or conditional breakpoints.
 * @param event The mouse press event.
 */
void SourceCodeView::lineNumberAreaMousePressEvent(QMouseEvent *event) {
    int y = event->pos().y();
    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    
    while (block.isValid()) {
        if (y >= top && y <= bottom) {
            int lineNum = blockNumber + 1;
            QString emitPath = m_currentFilePath;

            if (event->button() == Qt::RightButton) {
                // Right click opens conditional breakpoint dialog
                QString currentCondition = m_breakpoints.value(lineNum, QString());
                ConditionalBreakpointDialog dlg(currentCondition, this);
                if (dlg.exec() == QDialog::Accepted) {
                    QString newCondition = dlg.condition();
                    if (!newCondition.isEmpty() || m_breakpoints.contains(lineNum)) {
                        m_breakpoints.insert(lineNum, newCondition);
                        emit breakpointToggled(emitPath, lineNum, true, newCondition);
                    }
                }
            } else {
                // Left click toggles standard breakpoint
                bool isSet = false;
                if (m_breakpoints.contains(lineNum)) {
                    m_breakpoints.remove(lineNum);
                    isSet = false;
                } else {
                    m_breakpoints.insert(lineNum, QString());
                    isSet = true;
                }
                emit breakpointToggled(emitPath, lineNum, isSet, QString());
            }
            m_lineNumberArea->update();
            break;
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

/**
 * @brief Highlights a 1-based line as the active debugger execution line (or clears if <= 0).
 * @param lineOneBased 1-based source line number, or <= 0 to clear execution highlight.
 */
void SourceCodeView::highlightExecutionLine(int lineOneBased) {
    if (lineOneBased <= 0) {
        m_executionLine = -1;
        highlightCurrentLine();
        if (m_lineNumberArea) {
            m_lineNumberArea->update();
        }
        return;
    }
    m_executionLine = lineOneBased;
    QTextBlock block = document()->findBlockByLineNumber(lineOneBased - 1);
    if (block.isValid()) {
        QTextCursor cursor(block);
        setTextCursor(cursor);
        ensureCursorVisible();
        centerCursor();
    }
    highlightCurrentLine();
    if (m_lineNumberArea) {
        m_lineNumberArea->update();
    }
}

/**
 * @brief Highlights a specific 1-based execution line and scrolls into view.
 * @param lineNumber 1-based line number.
 */
void SourceCodeView::highlightCurrentLine(int lineNumber) {
    highlightExecutionLine(lineNumber);
}

/**
 * @brief Selects a 1-based source line, highlights it as active, and emits sourceLineSelected.
 * @param lineOneBased 1-based source line number.
 */
void SourceCodeView::selectSourceLine(int lineOneBased) {
    if (lineOneBased <= 0) {
        return;
    }
    highlightExecutionLine(lineOneBased);
    emit sourceLineSelected(lineOneBased);
}

/**
 * @brief Adjusts viewport margins to accommodate the gutter width with zero right margin.
 */
void SourceCodeView::updateLineNumberAreaWidth(int) {
    // Ensure zero right-side viewport margin reservation in SourceCodeView.
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
}

/**
 * @brief Handles scroll and geometry updates for the line number area.
 * @param rect Updated dirty region.
 * @param dy Vertical scroll delta.
 */
void SourceCodeView::updateLineNumberArea(const QRect &rect, int dy) {
    if (dy) m_lineNumberArea->scroll(0, dy);
    else m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect())) m_lineNumberArea->resize(lineNumberAreaWidth(), rect.height());
}

/**
 * @brief Synchronizes gutter height upon editor resize.
 * @param e Resize event.
 */
void SourceCodeView::resizeEvent(QResizeEvent *e) {
    QPlainTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

/**
 * @brief Populates the editor with source code and optionally highlights an active line.
 * @param code Source code string.
 * @param activeLine Optional 1-based active line.
 */
void SourceCodeView::setSourceCode(const QString& code, int activeLine) {
    if (!code.isEmpty() && toPlainText() != code) {
        QPlainTextEdit::setPlainText(code);
    }
    
    QList<QTextEdit::ExtraSelection> extraSelections;
    if (activeLine > 0) {
        QTextEdit::ExtraSelection selection;
        QColor activeColor(gridlock::core::ConfigManager::instance().getSourceActiveLine());
        selection.format.setBackground(activeColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        
        QTextBlock block = document()->findBlockByLineNumber(activeLine - 1);
        if (block.isValid()) {
            QTextCursor cursor(block);
            selection.cursor = cursor;
            extraSelections.append(selection);
        }
    }
    setExtraSelections(extraSelections);
}

/**
 * @brief Restarts the hover evaluation debounce timer on mouse movement.
 * @param event Mouse move event.
 */
void SourceCodeView::mouseMoveEvent(QMouseEvent *event) {
    QPlainTextEdit::mouseMoveEvent(event);
    m_lastMousePos = event->pos();
    m_lastGlobalMousePos = event->globalPosition().toPoint();
    m_hoverTimer->start(400);
}

/**
 * @brief Cancels active hover timers and hides tooltips when cursor exits the editor.
 * @param event Leave event.
 */
void SourceCodeView::leaveEvent(QEvent *event) {
    m_hoverTimer->stop();
    m_lastHoveredWord.clear();
    emit hideHoverTooltip();
    QPlainTextEdit::leaveEvent(event);
}

/**
 * @brief Slot triggered after mouse rests on a word, dispatching variable hover requests.
 */
void SourceCodeView::handleHoverTimeout() {
    QTextCursor cursor = cursorForPosition(m_lastMousePos);
    cursor.select(QTextCursor::WordUnderCursor);
    QString word = cursor.selectedText();

    static const QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
    if (cVarRegex.match(word).hasMatch()) {
        if (word != m_lastHoveredWord) {
            m_lastHoveredWord = word;
            emit hoverVariableRequested(word, m_lastGlobalMousePos);
        }
    } else {
        if (!m_lastHoveredWord.isEmpty()) {
            emit hideHoverTooltip();
        }
        m_lastHoveredWord.clear();
    }
}

/**
 * @brief Handles mouse clicks in the editor body, supporting Ctrl+Click variable pinning and line selection sync.
 * @param event Mouse press event.
 */
void SourceCodeView::mousePressEvent(QMouseEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        QTextCursor cursor = cursorForPosition(event->pos());
        cursor.select(QTextCursor::WordUnderCursor);
        QString word = cursor.selectedText();

        static const QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
        if (cVarRegex.match(word).hasMatch()) {
            emit pinVariableRequested(word);
        }
    }
    m_lastHoveredWord.clear();
    emit hideHoverTooltip();
    QPlainTextEdit::mousePressEvent(event);
    if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ControlModifier)) {
        const int clickedLine = textCursor().blockNumber() + 1;
        if (clickedLine > 0) {
            emit sourceLineSelected(clickedLine);
        }
    }
}

/**
 * @brief Handles double click events to pin variables to the watch list.
 * @param event Mouse double-click event.
 */
void SourceCodeView::mouseDoubleClickEvent(QMouseEvent *event) {
    QTextCursor cursor = cursorForPosition(event->pos());
    cursor.select(QTextCursor::WordUnderCursor);
    QString word = cursor.selectedText();

    static const QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
    if (cVarRegex.match(word).hasMatch()) {
        emit pinVariableRequested(word);
    }
    QPlainTextEdit::mouseDoubleClickEvent(event);
}

/**
 * @brief Handles Ctrl+MouseWheel zooming of editor font size.
 * @param event Wheel event.
 */
void SourceCodeView::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        QFont currentFont = font();
        int delta = event->angleDelta().y();
        int newSize = currentFont.pointSize();
        
        if (delta > 0) {
            newSize++;
        } else if (delta < 0) {
            newSize--;
        }
        
        if (newSize < 6) newSize = 6;
        if (newSize > 72) newSize = 72;
        
        currentFont.setPointSize(newSize);
        setFont(currentFont);
        
        updateLineNumberAreaWidth(0);
        event->accept();
    } else {
        QPlainTextEdit::wheelEvent(event);
    }
}

/**
 * @brief Renders the source code editor viewport with a uniform #16181d dark-slate canvas.
 * @param event Paint event covering the dirty viewport region.
 */
void SourceCodeView::paintEvent(QPaintEvent *event) {
    QPlainTextEdit::paintEvent(event);
}

}  // namespace gridlock::ui



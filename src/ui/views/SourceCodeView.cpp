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
#include <QFile>
#include <QKeyEvent>
#include <QSet>
#include <QTextStream>
#include <QApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/utils/ClangdLspClient.hpp"
#include "../dialogs/ConditionalBreakpointDialog.hpp"

namespace gridlock::ui {

namespace {
bool isAppDarkThemeActive() {
    if (qApp && qApp->property("gridlock_dark_mode").isValid()) {
        return qApp->property("gridlock_dark_mode").toBool();
    }
    return true;
}

QStringList activeAppSwatchColors() {
    if (qApp && qApp->property("gridlock_swatch_colors").isValid()) {
        const QStringList sw = qApp->property("gridlock_swatch_colors").toStringList();
        if (sw.size() >= 5) return sw;
    }
    return isAppDarkThemeActive()
               ? QStringList{QStringLiteral("#16181d"), QStringLiteral("#21252b"),
                             QStringLiteral("#2c313a"), QStringLiteral("#00a2ff"),
                             QStringLiteral("#d7dae0")}
               : QStringList{QStringLiteral("#f5f6f8"), QStringLiteral("#e6e9ef"),
                             QStringLiteral("#ccd0da"), QStringLiteral("#1e66f5"),
                             QStringLiteral("#1f2328")};
}
}  // namespace

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
    typeFormat.setForeground(QColor(55, 115, 225));
    typeFormat.setFontWeight(QFont::Bold);
    highlightingRules.append({QRegularExpression(QStringLiteral(
        "\\b(char|class|const|double|enum|explicit|friend|inline|int|long|namespace|operator|private|protected|"
        "public|short|signals|signed|slots|static|struct|template|typedef|typename|union|unsigned|virtual|void|volatile|bool)\\b")), typeFormat});

    // Control flow keywords: crimson/magenta
    QTextCharFormat keywordFormat;
    keywordFormat.setForeground(QColor(215, 58, 125));
    keywordFormat.setFontWeight(QFont::Bold);
    highlightingRules.append({QRegularExpression(QStringLiteral(
        "\\b(for|return|if|else|while|do|switch|case|break|continue|new|delete)\\b")), keywordFormat});

    // Preprocessor directives: orange (#d65d0e)
    QTextCharFormat preprocessorFormat; preprocessorFormat.setForeground(QColor(214, 93, 14));
    highlightingRules.append({QRegularExpression(QStringLiteral("#include[^\n]*")), preprocessorFormat});

    // MPI primitives: purple (#8959a8)
    QTextCharFormat mpiFormat; mpiFormat.setForeground(QColor(137, 89, 168));
    highlightingRules.append({QRegularExpression(QStringLiteral("\\bMPI_[A-Za-z0-9_]+\\b")), mpiFormat});

    // String literals: balanced emerald green (#2e8b57) legible on both light and dark canvases
    QTextCharFormat stringFormat; stringFormat.setForeground(QColor(60, 165, 85));
    highlightingRules.append({QRegularExpression(QStringLiteral("\".*?\"")), stringFormat});

    // Single-line comments: slate gray (#787c85)
    QTextCharFormat singleLineCommentFormat; singleLineCommentFormat.setForeground(QColor(120, 124, 133));
    highlightingRules.append({QRegularExpression(QStringLiteral("//[^\n]*")), singleLineCommentFormat});

    // Multi-line block comments
    multiLineCommentFormat.setForeground(QColor(120, 124, 133));
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

    setReadOnly(false);
    setTextInteractionFlags(Qt::TextEditorInteraction);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    reloadStyle(); // Load initial font size and active theme canvas surface

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
    int digits = 3, maxBlocks = qMax(1, blockCount());
    while (maxBlocks >= 1000) { maxBlocks /= 10; ++digits; }
    const int digitsWidth = fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return qMax(48, gutterIconLaneWidth() + digitsWidth + 18);
}

/** @brief Sets active breakpoints from a set of 1-based line numbers. */
void SourceCodeView::setBreakpoints(const QSet<int>& bps) {
    m_breakpoints.clear();
    m_breakpointSuspendScopes.clear();
    for (int bp : bps) {
        m_breakpoints[bp] = QString();
        m_breakpointSuspendScopes[bp] = QStringLiteral("All");
    }
    highlightCurrentLine();
    m_lineNumberArea->update();
}

/** @brief Returns the set of active 1-based breakpoint line numbers. */
QSet<int> SourceCodeView::breakpoints() const {
    QSet<int> result;
    for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it) result.insert(it.key());
    return result;
}

/** @brief Checks whether a breakpoint is set on the given 1-based line number. */
bool SourceCodeView::hasBreakpoint(int lineOneBased) const {
    return m_breakpoints.contains(lineOneBased);
}

/** @brief Returns the active canvas background color resolved from the active workstation theme. */
QColor SourceCodeView::activeCanvasBackgroundColor() const {
    const QStringList sw = activeAppSwatchColors();
    return QColor(sw.value(0, isAppDarkThemeActive() ? QStringLiteral("#16181d") : QStringLiteral("#f5f6f8")));
}

/**
 * @brief Reloads font size, font family, word wrap, and active theme surface from user configuration.
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
    if (!codeFontFamily.trimmed().isEmpty()) codeFont.setFamily(codeFontFamily.trimmed());
    codeFont.setStyleHint(QFont::Monospace);
    codeFont.setPointSize(codeFontSize);
    setFont(codeFont);

    // Resolve active canvas and text tokens from ThemeManager so Light and Dark themes render natively.
    const bool isDark = isAppDarkThemeActive();
    const QStringList sw = activeAppSwatchColors();
    const QString canvasHex = sw.value(0, isDark ? QStringLiteral("#16181d") : QStringLiteral("#f5f6f8"));
    const QString textHex = sw.value(4, isDark ? QStringLiteral("#d7dae0") : QStringLiteral("#1f2328"));

    this->setStyleSheet(QStringLiteral(
        "QPlainTextEdit, QPlainTextEdit QWidget { "
        "   background-color: %3; color: %4; border: none; "
        "   font-family: '%2', 'JetBrains Mono', 'Fira Code', 'Monospace', monospace; font-size: %1pt; }"
    ).arg(codeFontSize)
     .arg(codeFontFamily.isEmpty() ? QStringLiteral("JetBrains Mono") : codeFontFamily)
     .arg(canvasHex, textHex));

    if (viewport()) {
        QPalette pal = viewport()->palette();
        pal.setColor(QPalette::Base, QColor(canvasHex));
        pal.setColor(QPalette::Window, QColor(canvasHex));
        pal.setColor(QPalette::Text, QColor(textHex));
        viewport()->setPalette(pal);
        viewport()->setAutoFillBackground(true);
    }
    highlightCurrentLine();
    updateLineNumberAreaWidth(0);
    if (m_lineNumberArea) m_lineNumberArea->update();
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
    while (firstNonSpace < lineText.size() && lineText.at(firstNonSpace).isSpace()) ++firstNonSpace;

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

/** @brief Retrieves the 1-based line number containing the text cursor. */
int SourceCodeView::getCurrentLineNumber() const {
    return textCursor().blockNumber() + 1;
}

/** @brief Toggles a breakpoint on the specified 1-based line. */
void SourceCodeView::toggleBreakpoint(int lineNum) {
    const bool isSet = !m_breakpoints.contains(lineNum);
    if (isSet) {
        m_breakpoints.insert(lineNum, QString());
        m_breakpointSuspendScopes.insert(lineNum, QStringLiteral("All"));
    } else {
        m_breakpoints.remove(lineNum);
        m_breakpointSuspendScopes.remove(lineNum);
    }
    highlightCurrentLine();
    if (m_lineNumberArea) m_lineNumberArea->update();
    emit breakpointToggled(m_currentFilePath, lineNum, isSet, QString());
}

/** @brief Registers or updates a conditional/rank-filtered breakpoint on a 1-based source line. */
void SourceCodeView::setConditionalBreakpoint(int lineOneBased, const QString& condition) {
    if (lineOneBased <= 0) return;
    m_breakpoints.insert(lineOneBased, condition);
    if (!m_breakpointSuspendScopes.contains(lineOneBased)) m_breakpointSuspendScopes.insert(lineOneBased, QStringLiteral("All"));
    highlightCurrentLine();
    if (m_lineNumberArea) m_lineNumberArea->update();
    emit breakpointToggled(m_currentFilePath, lineOneBased, true, condition);
}

/** @brief Returns the active C/C++ boolean condition attached to a 1-based breakpoint line. */
QString SourceCodeView::breakpointCondition(int lineOneBased) const {
    return m_breakpoints.value(lineOneBased).trimmed();
}

/** @brief Returns the configured rank suspension scope ("All" or "Rank") for a breakpoint line. */
QString SourceCodeView::breakpointSuspendScope(int lineOneBased) const {
    return m_breakpointSuspendScopes.value(lineOneBased, QStringLiteral("All"));
}

/** @brief Updates the 1-based hovered gutter line and schedules a gutter repaint. */
void SourceCodeView::setHoveredGutterLine(int lineOneBased) {
    const int normalized = (lineOneBased > 0) ? lineOneBased : -1;
    if (m_hoveredGutterLine == normalized) return;
    m_hoveredGutterLine = normalized;
    if (m_lineNumberArea) {
        m_lineNumberArea->setCursor(normalized > 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        m_lineNumberArea->update();
    }
}

/** @brief Resolves the composite gutter marker badge state for a 1-based source line. */
QString SourceCodeView::gutterMarkerStateForLine(int lineOneBased) const {
    const bool hasBp = m_breakpoints.contains(lineOneBased);
    const bool isExec = (lineOneBased > 0 && lineOneBased == m_executionLine);
    if (hasBp && isExec) return QStringLiteral("BreakpointAndExecutionPc");
    if (hasBp) {
        return m_breakpoints.value(lineOneBased).trimmed().isEmpty()
                   ? QStringLiteral("Breakpoint")
                   : QStringLiteral("ConditionalBreakpoint");
    }
    if (isExec) return QStringLiteral("ExecutionPc");
    if (lineOneBased > 0 && lineOneBased == m_hoveredGutterLine) return QStringLiteral("HoverPreview");
    return QStringLiteral("None");
}

/**
 * @brief Highlights the current cursor line, breakpoint tint bands, and active debugger execution line.
 */
void SourceCodeView::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> selections;
    QTextEdit::ExtraSelection selection;
    const bool isDark = isAppDarkThemeActive();
    const QColor highlightColor = isDark ? QColor("#1e222a") : activeCanvasBackgroundColor().darker(104);
    selection.format.setBackground(highlightColor);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();
    selections.append(selection);

    // Layer subtle translucent crimson/amber background tint across breakpoint lines.
    for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it) {
        const int bpLine = it.key();
        if (bpLine <= 0 || bpLine == m_executionLine) continue;
        QTextBlock bpBlock = document()->findBlockByLineNumber(bpLine - 1);
        if (bpBlock.isValid()) {
            QTextEdit::ExtraSelection bpSel;
            const bool isCond = !it.value().trimmed().isEmpty();
            bpSel.format.setBackground(isCond ? QColor(229, 192, 123, 22) : QColor(224, 108, 117, 22));
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
            execSel.format.setBackground(isDark ? QColor("#20342c") : QColor(46, 160, 67, 45));
            execSel.format.setProperty(QTextFormat::FullWidthSelection, true);
            execSel.cursor = QTextCursor(execBlock);
            execSel.cursor.clearSelection();
            selections.append(execSel);
        }
    }
    setExtraSelections(selections);
}

/**
 * @brief Paints the two-lane gutter with hover previews, vector badges, and right-aligned line numbers.
 *
 * Left Icon Lane (x = 0..20): Renders centered vector badges for HoverPreview (translucent crimson ghost
 * circle), Breakpoint (crimson coral circle), ConditionalBreakpoint (amber diamond with dark center dot),
 * ExecutionPc (emerald chevron), and BreakpointAndExecutionPc (crimson outer ring nesting an emerald chevron).
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
    const QFont baseGutterFont = painter.font();

    const bool isDark = isAppDarkThemeActive();
    const QStringList sw = activeAppSwatchColors();
    const QColor gutterBg(sw.value(0, isDark ? QStringLiteral("#16181d") : QStringLiteral("#f5f6f8")));
    const QColor gutterBorder(sw.value(2, isDark ? QStringLiteral("#252932") : QStringLiteral("#d0d4dc")));
    painter.fillRect(event->rect(), gutterBg);
    painter.setPen(gutterBorder);
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
            const bool isHovered = (lineOneBased == m_hoveredGutterLine);
            const QString state = gutterMarkerStateForLine(lineOneBased);

            // Reset painter brush/pen explicitly at the top of each row iteration.
            painter.setBrush(Qt::NoBrush);
            painter.setPen(Qt::NoPen);

            // Illuminate hovered gutter row background with subtle accent wash rgba(82, 139, 255, 0.14).
            if (isHovered) {
                painter.fillRect(QRect(0, top, gutterW - 1, rowH), QColor(82, 139, 255, 36));
            }

            // 1. Left Icon Lane (x = 0..20): Antialiased QPainterPath vector badges & ghost hover preview.
            if (state == QStringLiteral("HoverPreview")) {
                // Translucent ghost breakpoint preview circle rgba(224, 108, 117, 0.40) inside x = 4..16.
                painter.setPen(QPen(QColor(255, 148, 157, 120), 1.0));
                painter.setBrush(QColor(224, 108, 117, 102));
                painter.drawEllipse(QPointF(cx, cy), 4.6, 4.6);
            } else if (state == QStringLiteral("Breakpoint")) {
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
                diamond.moveTo(cx, cy - 5.4); diamond.lineTo(cx + 5.4, cy);
                diamond.lineTo(cx, cy + 5.4); diamond.lineTo(cx - 5.4, cy); diamond.closeSubpath();
                painter.setPen(QPen(QColor("#f0d090"), 1.0));
                painter.setBrush(QColor("#e5c07b"));
                painter.drawPath(diamond);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor("#16181d"));
                painter.drawEllipse(QPointF(cx, cy), 1.5, 1.5);
            } else if (state == QStringLiteral("ExecutionPc")) {
                // Sleek emerald-teal execution chevron (#4ec9b0) + 3px right gutter accent bar.
                QPainterPath chev;
                chev.moveTo(cx - 3.6, cy - 4.5); chev.lineTo(cx + 4.6, cy);
                chev.lineTo(cx - 3.6, cy + 4.5); chev.lineTo(cx - 1.7, cy); chev.closeSubpath();
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
                innerChev.moveTo(cx - 2.2, cy - 3.0); innerChev.lineTo(cx + 3.2, cy);
                innerChev.lineTo(cx - 2.2, cy + 3.0); innerChev.lineTo(cx - 1.0, cy); innerChev.closeSubpath();
                painter.setPen(QPen(QColor("#98c379"), 0.8));
                painter.setBrush(QColor("#4ec9b0"));
                painter.drawPath(innerChev);
                painter.fillRect(QRect(gutterW - 3, top, 2, rowH), QColor("#4ec9b0"));
            }

            // 2. Right Line-Number Lane (x = 22..gutterW-8): Right-aligned digits with hover emphasis.
            QFont rowFont = baseGutterFont;
            rowFont.setBold(isHovered || lineOneBased == m_executionLine);
            painter.setFont(rowFont);

            const QString number = QString::number(lineOneBased);
            const QColor numColor = (lineOneBased == m_executionLine) ? QColor("#4ec9b0")
                                  : isHovered ? QColor("#61afef")
                                  : m_breakpoints.contains(lineOneBased) ? QColor("#e06c75")
                                  : QColor("#6b7280");
            painter.setPen(numColor);
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
 * @brief Determines whether the standard editor context menu may open at pos (suppressing gutter regions).
 * @param pos Coordinate in SourceCodeView widget space.
 * @return False when pos falls inside the gutter width or LineNumberArea geometry; true otherwise.
 */
bool SourceCodeView::shouldOpenEditorContextMenuAt(const QPoint& pos) const {
    if (pos.x() < lineNumberAreaWidth()) return false;
    if (m_lineNumberArea && m_lineNumberArea->geometry().contains(pos)) return false;
    return true;
}

/**
 * @brief Suppresses the standard Undo/Redo/Cut/Copy editor menu when right-clicking over the gutter.
 * @param event Context menu event descriptor.
 */
void SourceCodeView::contextMenuEvent(QContextMenuEvent *event) {
    if (!event) return;
    QPoint editorPos = event->pos();
    if (event->reason() == QContextMenuEvent::Mouse && !event->globalPos().isNull()) {
        const QPoint mapped = mapFromGlobal(event->globalPos());
        if (rect().contains(mapped)) editorPos = mapped;
    }
    if (!shouldOpenEditorContextMenuAt(editorPos)) { event->accept(); return; }
    QPlainTextEdit::contextMenuEvent(event);
}

/**
 * @brief Commits or rolls back tentative breakpoint state after the anchored breakpoint popover closes.
 * @param lineOneBased 1-based source line number.
 * @param wasPreviouslySet True if lineOneBased already held a breakpoint before right-clicking.
 * @param accepted True when the popover was accepted; false when canceled or dismissed.
 * @param enabledChecked True when the breakpoint remains enabled (not unchecked or removed).
 * @param condition Optional C/C++ boolean predicate string.
 * @param suspendScope Rank suspension scope ("All", "Rank", or "None").
 */
void SourceCodeView::handleBreakpointPopoverResult(int lineOneBased, bool wasPreviouslySet,
                                                   bool accepted, bool enabledChecked,
                                                   const QString& condition,
                                                   const QString& suspendScope) {
    if (lineOneBased <= 0) return;
    // Remove breakpoint when newly canceled (!accepted && !wasPreviouslySet) or explicitly disabled/removed.
    if ((!accepted && !wasPreviouslySet) || (accepted && !enabledChecked)) {
        if (m_breakpoints.contains(lineOneBased)) {
            m_breakpoints.remove(lineOneBased);
            m_breakpointSuspendScopes.remove(lineOneBased);
            highlightCurrentLine();
            if (m_lineNumberArea) m_lineNumberArea->update();
            emit breakpointToggled(m_currentFilePath, lineOneBased, false, QString());
        }
        return;
    }
    if (!accepted) return;

    // Persist updated predicate expression and rank suspension scope on the active breakpoint.
    const QString cleanCondition = condition.trimmed();
    const QString cleanScope = suspendScope.trimmed().isEmpty() ? QStringLiteral("All") : suspendScope.trimmed();
    m_breakpoints.insert(lineOneBased, cleanCondition);
    m_breakpointSuspendScopes.insert(lineOneBased, cleanScope);
    highlightCurrentLine();
    if (m_lineNumberArea) m_lineNumberArea->update();
    emit breakpointToggled(m_currentFilePath, lineOneBased, true, cleanCondition);
}

/**
 * @brief Handles gutter mouse clicks: left-click toggles breakpoint, right-click opens anchored popover.
 * @param event The mouse press event inside LineNumberArea.
 */
void SourceCodeView::lineNumberAreaMousePressEvent(QMouseEvent *event) {
    if (!event) return;
    // Explicitly consume left and right button presses at entry so they never propagate to parent views.
    if (event->button() != Qt::LeftButton && event->button() != Qt::RightButton) return;
    event->accept();

    const int y = event->pos().y();
    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    while (block.isValid()) {
        if (y >= top && y <= bottom) {
            const int lineNum = blockNumber + 1;
            if (event->button() == Qt::RightButton) {
                event->accept();
                const bool wasPreviouslySet = hasBreakpoint(lineNum);
                // Tentatively display the breakpoint marker above the callout caret while configuring.
                if (!wasPreviouslySet) toggleBreakpoint(lineNum);

                // Compute global bottom-center anchor point of the gutter breakpoint marker.
                const QPoint iconGlobalPos =
                    m_lineNumberArea->mapToGlobal(QPoint(gutterIconLaneWidth() / 2, top + (qMax(14, bottom - top) / 2) + 5));

                // Configure and anchor the callout popover directly beneath the breakpoint circle.
                ConditionalBreakpointDialog dlg(m_breakpoints.value(lineNum), this);
                dlg.setTargetLineContext(lineNum, block.text().trimmed());
                dlg.setSourceDocumentContext(m_currentFilePath, toPlainText(), lineNum);
                dlg.setSuspendScope(m_breakpointSuspendScopes.value(lineNum, QStringLiteral("All")));
                dlg.positionNearAnchor(iconGlobalPos);
                const bool accepted = (dlg.exec() == QDialog::Accepted);
                handleBreakpointPopoverResult(lineNum, wasPreviouslySet, accepted,
                                              dlg.isBreakpointEnabled() && !dlg.isRemoveRequested(),
                                              dlg.getCondition(), dlg.suspendScope());
            } else {
                event->accept();
                toggleBreakpoint(lineNum);
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
 * @brief Resolves the 1-based line under the mouse cursor inside LineNumberArea for live hover preview.
 * @param event Mouse move event inside LineNumberArea.
 */
void SourceCodeView::lineNumberAreaMouseMoveEvent(QMouseEvent *event) {
    const int y = event->pos().y();
    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber(), hoveredLine = -1;
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    while (block.isValid() && top <= m_lineNumberArea->height()) {
        if (block.isVisible() && y >= top && y < bottom) { hoveredLine = blockNumber + 1; break; }
        block = block.next(); top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
    setHoveredGutterLine(hoveredLine);
}

/**
 * @brief Resets hovered gutter line state when cursor leaves LineNumberArea.
 * @param event Leave event from LineNumberArea.
 */
void SourceCodeView::lineNumberAreaLeaveEvent(QEvent *event) {
    Q_UNUSED(event);
    setHoveredGutterLine(-1);
}

/**
 * @brief Highlights a 1-based line as the active debugger execution line (or clears if <= 0).
 * @param lineOneBased 1-based source line number, or <= 0 to clear execution highlight.
 */
void SourceCodeView::highlightExecutionLine(int lineOneBased) {
    if (lineOneBased <= 0) {
        m_executionLine = -1;
        highlightCurrentLine();
        if (m_lineNumberArea) m_lineNumberArea->update();
        return;
    }
    m_executionLine = lineOneBased;
    QTextBlock block = document()->findBlockByLineNumber(lineOneBased - 1);
    if (block.isValid()) {
        setTextCursor(QTextCursor(block));
        ensureCursorVisible();
        centerCursor();
    }
    highlightCurrentLine();
    if (m_lineNumberArea) m_lineNumberArea->update();
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
    if (lineOneBased <= 0) return;
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
    m_lineNumberArea->setGeometry(QRect(contentsRect().left(), contentsRect().top(), lineNumberAreaWidth(), contentsRect().height()));
}

/**
 * @brief Populates the editor with source code and optionally highlights an active line.
 * @param code Source code string.
 * @param activeLine Optional 1-based active line.
 */
void SourceCodeView::setSourceCode(const QString& code, int activeLine) {
    if (!code.isEmpty() && toPlainText() != code) QPlainTextEdit::setPlainText(code);
    QList<QTextEdit::ExtraSelection> extraSelections;
    if (activeLine > 0) {
        QTextBlock block = document()->findBlockByLineNumber(activeLine - 1);
        if (block.isValid()) {
            QTextEdit::ExtraSelection sel;
            sel.format.setBackground(QColor(gridlock::core::ConfigManager::instance().getSourceActiveLine()));
            sel.format.setProperty(QTextFormat::FullWidthSelection, true);
            sel.cursor = QTextCursor(block);
            extraSelections.append(sel);
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
    const QString word = cursor.selectedText();
    static const QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
    if (cVarRegex.match(word).hasMatch() && word != m_lastHoveredWord) {
        m_lastHoveredWord = word;
        emit hoverVariableRequested(word, m_lastGlobalMousePos);
    } else if (!cVarRegex.match(word).hasMatch()) {
        if (!m_lastHoveredWord.isEmpty()) emit hideHoverTooltip();
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
        const QString word = cursor.selectedText();
        static const QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
        if (cVarRegex.match(word).hasMatch()) emit pinVariableRequested(word);
    }
    m_lastHoveredWord.clear();
    emit hideHoverTooltip();
    QPlainTextEdit::mousePressEvent(event);
    if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ControlModifier)) {
        const int clickedLine = textCursor().blockNumber() + 1;
        if (clickedLine > 0) emit sourceLineSelected(clickedLine);
    }
}

/**
 * @brief Handles double click events to pin variables to the watch list.
 * @param event Mouse double-click event.
 */
void SourceCodeView::mouseDoubleClickEvent(QMouseEvent *event) {
    QTextCursor cursor = cursorForPosition(event->pos());
    cursor.select(QTextCursor::WordUnderCursor);
    const QString word = cursor.selectedText();
    static const QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
    if (cVarRegex.match(word).hasMatch()) emit pinVariableRequested(word);
    QPlainTextEdit::mouseDoubleClickEvent(event);
}

/**
 * @brief Handles Ctrl+MouseWheel zooming of editor font size.
 * @param event Wheel event.
 */
void SourceCodeView::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        QFont currentFont = font();
        const int delta = event->angleDelta().y();
        const int newSize = qBound(6, currentFont.pointSize() + ((delta > 0) ? 1 : ((delta < 0) ? -1 : 0)), 72);
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

/**
 * @brief Returns member and variable completion strings for linePrefix at lineOneBased.
 * @param lineOneBased 1-based line number.
 * @param linePrefix Expression prefix before cursor (e.g., "data[i].").
 * @return List of completion labels.
 */
QStringList SourceCodeView::editorCompletionsAtLine(int lineOneBased, const QString& linePrefix) const {
    const auto items = core::ClangdLspClient::completeDetailedAtLine(
        m_currentFilePath, toPlainText(), lineOneBased, linePrefix);
    QStringList labels;
    labels.reserve(items.size());
    for (const auto& it : items) labels.append(it.label);
    return labels;
}

/**
 * @brief Triggers the floating LSP IntelliSense popup below cursorRect() at lineOneBased.
 * @param linePrefix Expression prefix before cursor.
 * @param lineOneBased 1-based line number.
 */
void SourceCodeView::triggerCompletionPopupAtCursor(const QString& linePrefix, int lineOneBased) {
    const auto items = core::ClangdLspClient::completeDetailedAtLine(
        m_currentFilePath, toPlainText(), lineOneBased, linePrefix);
    if (items.isEmpty()) {
        if (m_completionPopup) m_completionPopup->hide();
        return;
    }
    if (!m_completionPopup) {
        m_completionPopup = new LspCompletionPopup(this);
        connect(m_completionPopup, &LspCompletionPopup::itemChosen, this, [this](const QString& insertText) {
            QTextCursor cur = textCursor();
            const QString line = cur.block().text().left(cur.positionInBlock());
            const int sepPos = qMax(line.lastIndexOf(QLatin1Char('.')),
                                    qMax(line.lastIndexOf(QStringLiteral("->")) + 1,
                                         line.lastIndexOf(QStringLiteral("::")) + 1));
            if (sepPos >= 0 && line.length() > sepPos + 1) {
                cur.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, line.length() - (sepPos + 1));
            }
            cur.insertText(insertText);
            setTextCursor(cur);
        });
    }
    m_completionPopup->setItems(items);
    m_completionPopup->positionBelow(mapToGlobal(cursorRect().bottomLeft() + QPoint(0, 4)), 320);
    m_completionPopup->show();
}

/**
 * @brief Saves the current editor document to disk.
 * @param filePath Target file path, or empty to overwrite currentFile().
 * @return True if successfully written; false otherwise.
 */
bool SourceCodeView::saveToFile(const QString& filePath) {
    const QString target = filePath.isEmpty() ? m_currentFilePath : filePath;
    if (target.isEmpty()) return false;
    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream(&file) << toPlainText();
    file.close();
    document()->setModified(false);
    return true;
}

/**
 * @brief Handles key press events for live LSP autocomplete navigation and invocation.
 * @param event Key event.
 */
void SourceCodeView::keyPressEvent(QKeyEvent *event) {
    if (!event) return;
    if (m_completionPopup && m_completionPopup->isVisible()) {
        if (event->key() == Qt::Key_Down) { m_completionPopup->selectNext(); event->accept(); return; }
        if (event->key() == Qt::Key_Up) { m_completionPopup->selectPrevious(); event->accept(); return; }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Tab) {
            if (m_completionPopup->selectCurrent()) { event->accept(); return; }
        }
        if (event->key() == Qt::Key_Escape) { m_completionPopup->hide(); event->accept(); return; }
    }
    if ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_Space) {
        QTextCursor cur = textCursor();
        triggerCompletionPopupAtCursor(cur.block().text().left(cur.positionInBlock()), cur.blockNumber() + 1);
        event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
    const QString txt = event->text();
    if (txt == QStringLiteral(".") || txt == QStringLiteral(">") || txt == QStringLiteral(":") ||
        (m_completionPopup && m_completionPopup->isVisible() && !txt.isEmpty() &&
         (txt.at(0).isLetterOrNumber() || txt.at(0) == QLatin1Char('_')))) {
        QTextCursor cur = textCursor();
        triggerCompletionPopupAtCursor(cur.block().text().left(cur.positionInBlock()), cur.blockNumber() + 1);
    }
}

}  // namespace gridlock::ui

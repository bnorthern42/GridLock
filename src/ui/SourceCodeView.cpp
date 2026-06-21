#include "SourceCodeView.hpp"
#include <QVBoxLayout>
#include <QPainter>
#include <QTextBlock>
#include <QToolBar>
#include <QAction>
#include <QDebug>
#include <QSet>
#include <QMouseEvent>
#include "../core/ConfigManager.hpp"

namespace gridlock::ui {

CppSyntaxHighlighter::CppSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent)
{
    HighlightingRule rule;

    // Types: soft blue
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

    // Keywords: neon pink/orange
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

    // Preprocessor: neon pink/orange
    QTextCharFormat preprocessorFormat;
    preprocessorFormat.setForeground(QColor(255, 120, 80));
    rule.pattern = QRegularExpression(QStringLiteral("#include[^\n]*"));
    rule.format = preprocessorFormat;
    highlightingRules.append(rule);

    // MPI primitives: soft blue or purple
    QTextCharFormat mpiFormat;
    mpiFormat.setForeground(QColor(180, 100, 255));
    rule.pattern = QRegularExpression(QStringLiteral("\\bMPI_[A-Za-z0-9_]+\\b"));
    rule.format = mpiFormat;
    highlightingRules.append(rule);

    // Strings: green
    QTextCharFormat stringFormat;
    stringFormat.setForeground(QColor(100, 255, 100));
    rule.pattern = QRegularExpression(QStringLiteral("\".*?\""));
    rule.format = stringFormat;
    highlightingRules.append(rule);

    // Comments: dim gray
    QTextCharFormat singleLineCommentFormat;
    singleLineCommentFormat.setForeground(QColor(120, 120, 120));
    rule.pattern = QRegularExpression(QStringLiteral("//[^\n]*"));
    rule.format = singleLineCommentFormat;
    highlightingRules.append(rule);

    multiLineCommentFormat.setForeground(QColor(120, 120, 120));
    commentStartExpression = QRegularExpression(QStringLiteral("/\\*"));
    commentEndExpression = QRegularExpression(QStringLiteral("\\*/"));
}

void CppSyntaxHighlighter::highlightBlock(const QString &text) {
    for (const HighlightingRule &rule : std::as_const(highlightingRules)) {
        QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
        while (matchIterator.hasNext()) {
            QRegularExpressionMatch match = matchIterator.next();
            setFormat(match.capturedStart(), match.capturedLength(), rule.format);
        }
    }
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

class LineNumberArea : public QWidget {
public:
    LineNumberArea(SourceCodeView *editor) : QWidget(editor), sourceCodeView(editor) {}
    QSize sizeHint() const override { return QSize(35, 0); }
protected:
    void paintEvent(QPaintEvent *event) override { sourceCodeView->lineNumberAreaPaintEvent(event); }
    void mousePressEvent(QMouseEvent *event) override { sourceCodeView->lineNumberAreaMousePressEvent(event); }
private:
    SourceCodeView *sourceCodeView;
};

SourceCodeView::SourceCodeView(QWidget *parent) : QPlainTextEdit(parent) {
    m_lineNumberArea = new LineNumberArea(this);
    connect(document(), &QTextDocument::blockCountChanged, this, &SourceCodeView::updateLineNumberAreaWidth);
    connect(this, &SourceCodeView::updateRequest, this, &SourceCodeView::updateLineNumberArea);
    updateLineNumberAreaWidth(0);

    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(11);
    setFont(font);

    QPalette p = this->palette();
    p.setColor(QPalette::Base, QColor(gridlock::core::ConfigManager::instance().getSourceBackground()));
    p.setColor(QPalette::Text, QColor(gridlock::core::ConfigManager::instance().getSourceText()));
    this->setPalette(p);

    m_highlighter = new CppSyntaxHighlighter(document());

    viewport()->setMouseTracking(true);
    m_hoverTimer = new QTimer(this);
    m_hoverTimer->setSingleShot(true);
    connect(m_hoverTimer, &QTimer::timeout, this, &SourceCodeView::handleHoverTimeout);
}

void SourceCodeView::setBreakpoints(const QSet<int>& bps) {
    breakpoints = bps;
    m_lineNumberArea->update();
}

void SourceCodeView::lineNumberAreaPaintEvent(QPaintEvent *event) {
    QPainter painter(m_lineNumberArea);
    painter.fillRect(event->rect(), QColor(40, 40, 40));
    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    
    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(blockNumber + 1);
            painter.setPen(QColor(120, 120, 120));
            painter.drawText(0, top, m_lineNumberArea->width() - 5, fontMetrics().height(), Qt::AlignRight, number);
            if (breakpoints.contains(blockNumber + 1)) {
                painter.setBrush(Qt::red);
                painter.drawEllipse(2, top + 2, 8, 8);
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void SourceCodeView::lineNumberAreaMousePressEvent(QMouseEvent *event) {
    int y = event->pos().y();
    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    
    while (block.isValid()) {
        if (y >= top && y <= bottom) {
            int lineNum = blockNumber + 1;
            bool ctrlClicked = (event->modifiers() & Qt::ControlModifier);
            if (breakpoints.contains(lineNum) && !ctrlClicked) breakpoints.remove(lineNum);
            else breakpoints.insert(lineNum);
            m_lineNumberArea->update();
            QString emitPath = m_currentFilePath.isEmpty() ? "tests/mpi_mm.c" : m_currentFilePath;
            emit breakpointToggled(emitPath, lineNum, ctrlClicked);
            break;
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void SourceCodeView::highlightCurrentLine(int lineNumber) {
    if (lineNumber <= 0) return;
    QList<QTextEdit::ExtraSelection> extraSelections;
    QTextEdit::ExtraSelection selection;
    QColor activeColor(gridlock::core::ConfigManager::instance().getSourceActiveLine());
    activeColor.setAlpha(100);
    selection.format.setBackground(activeColor);
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    
    QTextBlock block = document()->findBlockByLineNumber(lineNumber - 1);
    if (block.isValid()) {
        QTextCursor cursor(block);
        selection.cursor = cursor;
        extraSelections.append(selection);
        setExtraSelections(extraSelections);
        
        setTextCursor(cursor);
        ensureCursorVisible();
        centerCursor();
    }
}

void SourceCodeView::updateLineNumberAreaWidth(int) { setViewportMargins(35, 0, 0, 0); }
void SourceCodeView::updateLineNumberArea(const QRect &rect, int dy) {
    if (dy) m_lineNumberArea->scroll(0, dy);
    else m_lineNumberArea->update(0, rect.y(), m_lineNumberArea->width(), rect.height());
    if (rect.contains(viewport()->rect())) m_lineNumberArea->resize(35, rect.height());
}
void SourceCodeView::resizeEvent(QResizeEvent *e) {
    QPlainTextEdit::resizeEvent(e);
    QRect cr = contentsRect();
    m_lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), 35, cr.height()));
}

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

void SourceCodeView::mouseMoveEvent(QMouseEvent *event) {
    QPlainTextEdit::mouseMoveEvent(event);
    m_lastMousePos = event->pos();
    m_lastGlobalMousePos = event->globalPosition().toPoint();
    m_hoverTimer->start(400);
}

void SourceCodeView::leaveEvent(QEvent *event) {
    m_hoverTimer->stop();
    QPlainTextEdit::leaveEvent(event);
}

void SourceCodeView::handleHoverTimeout() {
    QTextCursor cursor = cursorForPosition(m_lastMousePos);
    cursor.select(QTextCursor::WordUnderCursor);
    QString word = cursor.selectedText();

    QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
    if (cVarRegex.match(word).hasMatch()) {
        emit hoverVariableRequested(word, m_lastGlobalMousePos);
    }

    int line = cursor.blockNumber();
    int character = cursor.positionInBlock();
    QString emitPath = m_currentFilePath.isEmpty() ? "tests/mpi_mm.c" : m_currentFilePath;
    emit semanticHoverRequested(emitPath, line, character, m_lastGlobalMousePos);
}

void SourceCodeView::mousePressEvent(QMouseEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        QTextCursor cursor = cursorForPosition(event->pos());
        cursor.select(QTextCursor::WordUnderCursor);
        QString word = cursor.selectedText();

        QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
        if (cVarRegex.match(word).hasMatch()) {
            emit pinVariableRequested(word);
        }
    }
    QPlainTextEdit::mousePressEvent(event);
}

void SourceCodeView::mouseDoubleClickEvent(QMouseEvent *event) {
    QTextCursor cursor = cursorForPosition(event->pos());
    cursor.select(QTextCursor::WordUnderCursor);
    QString word = cursor.selectedText();

    QRegularExpression cVarRegex("^[a-zA-Z_][a-zA-Z0-9_]*$");
    if (cVarRegex.match(word).hasMatch()) {
        emit pinVariableRequested(word);
    }
    QPlainTextEdit::mouseDoubleClickEvent(event);
}

} // namespace gridlock::ui

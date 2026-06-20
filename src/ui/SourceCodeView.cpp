#include "SourceCodeView.hpp"
#include <QVBoxLayout>
#include <QPainter>
#include <QTextBlock>
#include <QToolBar>
#include <QAction>
#include <QDebug>
#include <QSet>
#include <QMouseEvent>

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

class CodeEditor;

class LineNumberArea : public QWidget {
public:
    LineNumberArea(CodeEditor *editor);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
private:
    CodeEditor *codeEditor;
};

class CodeEditor : public QPlainTextEdit {
public:
    CodeEditor(QWidget* parent, SourceCodeView* view) : QPlainTextEdit(parent), view(view) {
        lineNumberArea = new LineNumberArea(this);
        connect(document(), &QTextDocument::blockCountChanged, this, &CodeEditor::updateLineNumberAreaWidth);
        connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLineNumberArea);
        updateLineNumberAreaWidth(0);
    }

    void lineNumberAreaPaintEvent(QPaintEvent *event) {
        QPainter painter(lineNumberArea);
        painter.fillRect(event->rect(), QColor(40, 40, 40));
        QTextBlock block = document()->begin();
        int blockNumber = 0;
        int top = (int) blockBoundingGeometry(block).translated(contentOffset()).top();
        int bottom = top + (int) blockBoundingRect(block).height();
        
        while (block.isValid() && top <= event->rect().bottom()) {
            if (block.isVisible() && bottom >= event->rect().top()) {
                QString number = QString::number(blockNumber + 1);
                painter.setPen(QColor(120, 120, 120));
                painter.drawText(0, top, lineNumberArea->width() - 5, fontMetrics().height(), Qt::AlignRight, number);
                if (breakpoints.contains(blockNumber + 1)) {
                    painter.setBrush(Qt::red);
                    painter.drawEllipse(2, top + 2, 8, 8);
                }
            }
            block = block.next();
            top = bottom;
            bottom = top + (int) blockBoundingRect(block).height();
            ++blockNumber;
        }
    }

    void lineNumberAreaMousePressEvent(QMouseEvent *event) {
        int y = event->pos().y();
        QTextBlock block = document()->begin();
        int top = (int) blockBoundingGeometry(block).translated(contentOffset()).top();
        int bottom = top + (int) blockBoundingRect(block).height();
        int blockNumber = 1;
        while (block.isValid()) {
            if (y >= top && y <= bottom) {
                if (breakpoints.contains(blockNumber)) breakpoints.remove(blockNumber);
                else breakpoints.insert(blockNumber);
                lineNumberArea->update();
                emit view->breakpointToggled("tests/matrix_multiply.cpp", blockNumber);
                break;
            }
            block = block.next();
            top = bottom;
            bottom = top + (int) blockBoundingRect(block).height();
            ++blockNumber;
        }
    }

    void updateLineNumberAreaWidth(int) { setViewportMargins(35, 0, 0, 0); }
    void updateLineNumberArea(const QRect &rect, int dy) {
        if (dy) lineNumberArea->scroll(0, dy);
        else lineNumberArea->update(0, rect.y(), lineNumberArea->width(), rect.height());
        if (rect.contains(viewport()->rect())) lineNumberArea->resize(35, rect.height());
    }
    void resizeEvent(QResizeEvent *e) override {
        QPlainTextEdit::resizeEvent(e);
        QRect cr = contentsRect();
        lineNumberArea->setGeometry(QRect(cr.left(), cr.top(), 35, cr.height()));
    }

    LineNumberArea* lineNumberArea;
    SourceCodeView* view;
    QSet<int> breakpoints;
};

LineNumberArea::LineNumberArea(CodeEditor *editor) : QWidget(editor), codeEditor(editor) {}

QSize LineNumberArea::sizeHint() const {
    return QSize(35, 0);
}

void LineNumberArea::paintEvent(QPaintEvent *event) {
    codeEditor->lineNumberAreaPaintEvent(event);
}

void LineNumberArea::mousePressEvent(QMouseEvent *event) {
    codeEditor->lineNumberAreaMousePressEvent(event);
}

SourceCodeView::SourceCodeView(QWidget *parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    QToolBar* toolbar = new QToolBar(this);
    toolbar->addAction("▶ Run Target", this, &SourceCodeView::runTargetRequested);
    toolbar->addAction("⏩ Continue", this, &SourceCodeView::continueRequested);
    toolbar->addAction("↷ Step Inst", this, &SourceCodeView::stepInstRequested);
    layout->addWidget(toolbar);

    m_textEdit = new CodeEditor(this, this);
    m_textEdit->setReadOnly(true);
    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(11);
    m_textEdit->setFont(font);

    QPalette p = m_textEdit->palette();
    p.setColor(QPalette::Base, QColor(30, 30, 30));
    p.setColor(QPalette::Text, QColor(220, 220, 220));
    m_textEdit->setPalette(p);

    layout->addWidget(m_textEdit);

    m_highlighter = new CppSyntaxHighlighter(m_textEdit->document());
    setLayout(layout);
}

void SourceCodeView::setSourceCode(const QString& code, int activeLine) {
    if (m_textEdit->toPlainText() != code) {
        m_textEdit->setPlainText(code);
    }
    
    QList<QTextEdit::ExtraSelection> extraSelections;
    if (activeLine > 0) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(QColor(80, 80, 0));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        
        QTextCursor cursor(m_textEdit->document());
        cursor.movePosition(QTextCursor::Start);
        cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, activeLine - 1);
        selection.cursor = cursor;
        extraSelections.append(selection);
    }
    m_textEdit->setExtraSelections(extraSelections);
}

void SourceCodeView::setPlainText(const QString& text) {
    if (m_textEdit) {
        m_textEdit->setPlainText(text);
    }
}

QString SourceCodeView::getPlainText() const {
    return m_textEdit ? m_textEdit->toPlainText() : QString();
}

} // namespace gridlock::ui

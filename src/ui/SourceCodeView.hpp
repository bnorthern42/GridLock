#pragma once
#include <QWidget>
#include <QString>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QTimer>
#include <QPoint>

namespace gridlock::ui {

class CppSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit CppSyntaxHighlighter(QTextDocument *parent = nullptr);
protected:
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

class SourceCodeView : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit SourceCodeView(QWidget *parent = nullptr);
    ~SourceCodeView() override = default;

    void setSourceCode(const QString& code, int activeLine = -1);
    void setCurrentFile(const QString& filePath) { m_currentFilePath = filePath; }
    void highlightCurrentLine(int lineNumber);
    QString getPlainText() const { return toPlainText(); }
    QMargins getViewportMargins() const { return viewportMargins(); }
    void setBreakpoints(const QSet<int>& bps);

    void lineNumberAreaPaintEvent(QPaintEvent *event);
    void lineNumberAreaMousePressEvent(QMouseEvent *event);

signals:
    void runTargetRequested();
    void continueRequested();
    void stepInstRequested();
    void toggleBreakpointRequested(const QString& location);
    void breakpointToggled(const QString& file, int line, bool ctrlClicked = false);
    void hoverVariableRequested(const QString& varName, const QPoint& globalPos);
    void semanticHoverRequested(const QString& file, int line, int character, const QPoint& globalPos);
    void pinVariableRequested(const QString& varName);

protected:
    void resizeEvent(QResizeEvent *e) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect &rect, int dy);
    void handleHoverTimeout();

private:
    LineNumberArea* m_lineNumberArea;
    CppSyntaxHighlighter* m_highlighter;
    QSet<int> breakpoints;
    QString m_currentFilePath;
    QTimer* m_hoverTimer;
    QPoint m_lastMousePos;
    QPoint m_lastGlobalMousePos;
};

} // namespace gridlock::ui

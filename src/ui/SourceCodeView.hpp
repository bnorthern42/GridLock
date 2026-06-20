#pragma once
#include <QWidget>
#include <QString>
#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QTextCharFormat>

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

class SourceCodeView : public QWidget {
    Q_OBJECT
public:
    explicit SourceCodeView(QWidget *parent = nullptr);
    ~SourceCodeView() override = default;

    void setSourceCode(const QString& code, int activeLine = -1);

signals:
    void runTargetRequested();
    void stepInstRequested();
    void toggleBreakpointRequested(const QString& location);
    void breakpointToggled(const QString& file, int line);

private:
    QPlainTextEdit* m_textEdit;
    CppSyntaxHighlighter* m_highlighter;
};

} // namespace gridlock::ui

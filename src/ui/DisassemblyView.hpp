#pragma once
#include <QPlainTextEdit>
#include <QString>
#include <QSyntaxHighlighter>
#include <QRegularExpression>

namespace gridlock::ui {

class AsmSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit AsmSyntaxHighlighter(QTextDocument *parent = nullptr);
protected:
    void highlightBlock(const QString &text) override;
private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QVector<HighlightingRule> highlightingRules;
};

class DisassemblyView : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit DisassemblyView(QWidget *parent = nullptr);
    ~DisassemblyView() override = default;

public slots:
    void updateDisassembly(const QString& asmCode);

private:
    AsmSyntaxHighlighter* m_highlighter;
};

} // namespace gridlock::ui

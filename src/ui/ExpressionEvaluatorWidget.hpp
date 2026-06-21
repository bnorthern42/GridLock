#pragma once
#include <QDockWidget>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>

namespace gridlock::ui {

class ExpressionEvaluatorWidget : public QDockWidget {
    Q_OBJECT
public:
    explicit ExpressionEvaluatorWidget(QWidget* parent = nullptr);

public slots:
    void appendResult(int rankId, const QString& expr, const QString& result);

signals:
    void evaluateRequested(const QString& expression);

private:
    QLineEdit* m_inputField;
    QPushButton* m_evaluateBtn;
    QPlainTextEdit* m_historyArea;
};

} // namespace gridlock::ui

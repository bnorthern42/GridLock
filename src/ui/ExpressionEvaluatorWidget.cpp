#include "ExpressionEvaluatorWidget.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

namespace gridlock::ui {

ExpressionEvaluatorWidget::ExpressionEvaluatorWidget(QWidget* parent) : QDockWidget("Expression Evaluator", parent) {
    QWidget* content = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    auto* inputLayout = new QHBoxLayout();
    m_inputField = new QLineEdit(this);
    m_inputField->setPlaceholderText("Enter C++ expression...");
    m_evaluateBtn = new QPushButton("Evaluate", this);
    
    inputLayout->addWidget(m_inputField);
    inputLayout->addWidget(m_evaluateBtn);

    m_historyArea = new QPlainTextEdit(this);
    m_historyArea->setReadOnly(true);

    mainLayout->addLayout(inputLayout);
    mainLayout->addWidget(m_historyArea);

    setWidget(content);

    auto triggerEval = [this]() {
        QString expr = m_inputField->text().trimmed();
        if (!expr.isEmpty()) {
            emit evaluateRequested(expr);
            m_inputField->selectAll();
        }
    };

    connect(m_evaluateBtn, &QPushButton::clicked, this, triggerEval);
    connect(m_inputField, &QLineEdit::returnPressed, this, triggerEval);
}

void ExpressionEvaluatorWidget::appendResult(int rankId, const QString& expr, const QString& result) {
    m_historyArea->appendPlainText(QString("[Rank %1] %2 = %3").arg(rankId).arg(expr).arg(result));
}

} // namespace gridlock::ui

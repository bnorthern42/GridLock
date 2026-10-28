/**
 * @file ExpressionEvaluatorWidget.cpp
 * @brief Expression evaluator console dock implementation.
 *
 * Implements the ExpressionEvaluatorWidget dock widget containing expression input,
 * execution dispatch triggers, and a multi-rank result log console.
 */

#include "ExpressionEvaluatorWidget.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

namespace gridlock::ui {

/**
 * @brief Constructs a new ExpressionEvaluatorWidget.
 *
 * Sets up the user input field, "Evaluate" button, and read-only history text area.
 * Configures Enter key and button click handlers to trigger expression evaluations.
 *
 * @param parent Optional parent widget.
 */
ExpressionEvaluatorWidget::ExpressionEvaluatorWidget(QWidget* parent) 
    : QDockWidget("Expression Evaluator", parent) {
    // Container widget for dock contents
    QWidget* content = new QWidget(this);
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    // Horizontal top row for expression input and evaluate trigger
    auto* inputLayout = new QHBoxLayout();
    m_inputField = new QLineEdit(this);
    m_inputField->setPlaceholderText("Enter C++ expression...");
    m_evaluateBtn = new QPushButton("Evaluate", this);
    
    inputLayout->addWidget(m_inputField);
    inputLayout->addWidget(m_evaluateBtn);

    // Read-only multi-line history log
    m_historyArea = new QPlainTextEdit(this);
    m_historyArea->setReadOnly(true);

    mainLayout->addLayout(inputLayout);
    mainLayout->addWidget(m_historyArea);

    setWidget(content);

    // Lambda to dispatch trimmed expression to connected debugger backend
    auto triggerEval = [this]() {
        QString expr = m_inputField->text().trimmed();
        if (!expr.isEmpty()) {
            emit evaluateRequested(expr);
            m_inputField->selectAll();
        }
    };

    // Connect evaluate button and Enter key to the trigger
    connect(m_evaluateBtn, &QPushButton::clicked, this, triggerEval);
    connect(m_inputField, &QLineEdit::returnPressed, this, triggerEval);
}

/**
 * @brief Appends evaluated expression results to the history console.
 * @param rankId Rank index whose context evaluated the expression.
 * @param expr Evaluated expression string.
 * @param result Evaluated outcome or error message.
 */
void ExpressionEvaluatorWidget::appendResult(int rankId, const QString& expr, const QString& result) {
    m_historyArea->appendPlainText(QString("[Rank %1] %2 = %3").arg(rankId).arg(expr).arg(result));
}

}  // namespace gridlock::ui


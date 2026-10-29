/**
 * @file ExpressionEvaluatorWidget.cpp
 * @brief Expression evaluator console dock implementation.
 *
 * Implements the ExpressionEvaluatorWidget dock widget containing expression input,
 * execution dispatch triggers, and a multi-rank result log console.
 */

#include "ExpressionEvaluatorWidget.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace gridlock::ui {

/**
 * @brief Constructs a new ExpressionEvaluatorWidget.
 *
 * Sets up a 26px #21252b header control strip with expression input field and
 * "Evaluate" trigger button above a flush borderless history log console.
 *
 * @param parent Optional parent widget.
 */
ExpressionEvaluatorWidget::ExpressionEvaluatorWidget(QWidget* parent)
    : QDockWidget("Expression Evaluator", parent) {
    setContentsMargins(0, 0, 0, 0);

    // Container widget for dock contents with zero outer margins.
    QWidget* content = new QWidget(this);
    content->setContentsMargins(0, 0, 0, 0);
    auto* mainLayout = new QVBoxLayout(content);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Unified 26px #21252b top control strip for expression input and evaluation trigger.
    QWidget* headerBar = new QWidget(content);
    headerBar->setObjectName(QStringLiteral("evaluatorHeaderBar"));
    headerBar->setFixedHeight(26);
    headerBar->setStyleSheet(QStringLiteral(
        "#evaluatorHeaderBar { background: #21252b; border-bottom: 1px solid #2c313a; }"
        "#evaluatorHeaderBar QLabel { color: #abb2bf; font-size: 11px; font-weight: 600; }"));

    auto* inputLayout = new QHBoxLayout(headerBar);
    inputLayout->setContentsMargins(6, 2, 6, 2);
    inputLayout->setSpacing(6);

    auto* promptLabel = new QLabel(QStringLiteral("Expr:"), headerBar);
    m_inputField = new QLineEdit(headerBar);
    m_inputField->setObjectName(QStringLiteral("evaluatorInputEdit"));
    m_inputField->setPlaceholderText(QStringLiteral("Enter C/C++ expression (e.g., rank * sizeof(double))..."));
    m_inputField->setFixedHeight(22);

    m_evaluateBtn = new QPushButton(QStringLiteral("Evaluate"), headerBar);
    m_evaluateBtn->setObjectName(QStringLiteral("evaluatorSubmitBtn"));
    m_evaluateBtn->setFixedHeight(22);

    // Compact "+ Watch" action button to promote tested expressions directly to DifferentialGrid.
    m_watchBtn = new QPushButton(QStringLiteral("+ Watch"), headerBar);
    m_watchBtn->setObjectName(QStringLiteral("evaluatorPromoteWatchBtn"));
    m_watchBtn->setToolTip(QStringLiteral("Promote current expression to Watch Expressions dock"));
    m_watchBtn->setFixedHeight(22);

    inputLayout->addWidget(promptLabel);
    inputLayout->addWidget(m_inputField, 1);
    inputLayout->addWidget(m_evaluateBtn);
    inputLayout->addWidget(m_watchBtn);

    // Flush read-only multi-line history log on #16181d dark-slate canvas.
    m_historyArea = new QPlainTextEdit(content);
    m_historyArea->setObjectName(QStringLiteral("evaluatorHistoryArea"));
    m_historyArea->setFrameShape(QFrame::NoFrame);
    m_historyArea->setReadOnly(true);
    m_historyArea->setStyleSheet(QStringLiteral(
        "QPlainTextEdit#evaluatorHistoryArea { background: #16181d; color: #d7dae0; border: none; "
        "font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 4px 6px; }"));

    mainLayout->addWidget(headerBar);
    mainLayout->addWidget(m_historyArea, 1);

    setWidget(content);

    // Lambda to dispatch trimmed expression to connected debugger backend.
    auto triggerEval = [this]() {
        const QString expr = m_inputField->text().trimmed();
        if (!expr.isEmpty()) {
            emit evaluateRequested(expr);
            m_inputField->selectAll();
        }
    };

    // Connect evaluate button, Enter key, and "+ Watch" promotion button.
    connect(m_evaluateBtn, &QPushButton::clicked, this, triggerEval);
    connect(m_inputField, &QLineEdit::returnPressed, this, triggerEval);
    connect(m_watchBtn, &QPushButton::clicked, this, &ExpressionEvaluatorWidget::promoteCurrentToWatch);
}

/**
 * @brief Sets the current expression input field text.
 * @param expr Expression string to populate into the evaluator field.
 */
void ExpressionEvaluatorWidget::setExpression(const QString& expr) {
    if (m_inputField) {
        m_inputField->setText(expr);
    }
}

/**
 * @brief Promotes the currently entered expression to the Watch Expressions dock.
 * @return True if a non-empty expression was promoted, false otherwise.
 */
bool ExpressionEvaluatorWidget::promoteCurrentToWatch() {
    if (!m_inputField) {
        return false;
    }
    const QString expr = m_inputField->text().trimmed();
    if (expr.isEmpty()) {
        return false;
    }
    emit watchExpressionRequested(expr);
    return true;
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


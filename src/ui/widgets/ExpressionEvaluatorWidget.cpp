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
 * Sets up a 26px #21252b top target/action header strip, a flush borderless
 * #16181d history log console, and a docked 26px #21252b bottom 'eval>' prompt strip.
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

    // 1. Unified 26px #21252b top header strip: rank target selector, status badge, + Watch, and Clear.
    m_headerBar = new QWidget(content);
    m_headerBar->setObjectName(QStringLiteral("evaluatorHeaderBar"));
    m_headerBar->setFixedHeight(26);
    m_headerBar->setStyleSheet(QStringLiteral(
        "#evaluatorHeaderBar { background: #21252b; border-bottom: 1px solid #2c313a; }"
        "#evaluatorHeaderBar QLabel { color: #abb2bf; font-size: 10px; font-weight: 700; }"));

    auto* headerLayout = new QHBoxLayout(m_headerBar);
    headerLayout->setContentsMargins(6, 2, 6, 2);
    headerLayout->setSpacing(6);

    auto* targetLabel = new QLabel(QStringLiteral("TARGET:"), m_headerBar);
    m_rankCombo = new QComboBox(m_headerBar);
    m_rankCombo->setObjectName(QStringLiteral("evaluatorRankCombo"));
    m_rankCombo->setFixedHeight(22);
    m_rankCombo->addItem(QStringLiteral("All Ranks"), -1);
    m_rankCombo->addItem(QStringLiteral("Rank 0"), 0);
    m_rankCombo->addItem(QStringLiteral("Rank 1"), 1);

    auto* statusBadge = new QLabel(QStringLiteral("DWARF / C++ EVALUATOR"), m_headerBar);
    statusBadge->setStyleSheet(QStringLiteral("color: #7f848e; font-size: 10px; font-weight: 600;"));

    // Compact "+ Watch" action button to promote tested expressions directly to DifferentialGrid.
    m_watchBtn = new QPushButton(QStringLiteral("+ Watch"), m_headerBar);
    m_watchBtn->setObjectName(QStringLiteral("evaluatorPromoteWatchBtn"));
    m_watchBtn->setToolTip(QStringLiteral("Promote current expression to Watch Expressions dock"));
    m_watchBtn->setFixedHeight(22);

    m_clearBtn = new QPushButton(QStringLiteral("Clear"), m_headerBar);
    m_clearBtn->setObjectName(QStringLiteral("evaluatorClearBtn"));
    m_clearBtn->setFixedHeight(22);

    headerLayout->addWidget(targetLabel);
    headerLayout->addWidget(m_rankCombo);
    headerLayout->addWidget(statusBadge);
    headerLayout->addStretch(1);
    headerLayout->addWidget(m_watchBtn);
    headerLayout->addWidget(m_clearBtn);

    // 2. Flush read-only multi-line history log on #16181d dark-slate canvas.
    m_historyArea = new QPlainTextEdit(content);
    m_historyArea->setObjectName(QStringLiteral("evaluatorHistoryArea"));
    m_historyArea->setFrameShape(QFrame::NoFrame);
    m_historyArea->setReadOnly(true);
    m_historyArea->setStyleSheet(QStringLiteral(
        "QPlainTextEdit#evaluatorHistoryArea { background: #16181d; color: #d7dae0; border: none; "
        "font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 4px 6px; }"));

    // 3. Docked 26px #21252b bottom expression prompt bar with 'eval>' prefix and Evaluate button.
    m_promptBar = new QWidget(content);
    m_promptBar->setObjectName(QStringLiteral("evaluatorPromptBar"));
    m_promptBar->setFixedHeight(26);
    m_promptBar->setStyleSheet(QStringLiteral(
        "#evaluatorPromptBar { background: #21252b; border-top: 1px solid #2c313a; }"));

    auto* inputLayout = new QHBoxLayout(m_promptBar);
    inputLayout->setContentsMargins(6, 2, 6, 2);
    inputLayout->setSpacing(6);

    auto* promptLabel = new QLabel(QStringLiteral("eval>"), m_promptBar);
    promptLabel->setStyleSheet(QStringLiteral(
        "color: #528bff; font-family: 'JetBrains Mono', monospace; font-size: 11px; font-weight: 700;"));

    m_inputField = new QLineEdit(m_promptBar);
    m_inputField->setObjectName(QStringLiteral("evaluatorInputEdit"));
    m_inputField->setPlaceholderText(QStringLiteral("Enter C/C++ expression (e.g., data[0].coords.x, rank * sizeof(double))..."));
    m_inputField->setFixedHeight(22);

    m_evaluateBtn = new QPushButton(QStringLiteral("Evaluate"), m_promptBar);
    m_evaluateBtn->setObjectName(QStringLiteral("evaluatorSubmitBtn"));
    m_evaluateBtn->setFixedHeight(22);

    inputLayout->addWidget(promptLabel);
    inputLayout->addWidget(m_inputField, 1);
    inputLayout->addWidget(m_evaluateBtn);

    mainLayout->addWidget(m_headerBar);
    mainLayout->addWidget(m_historyArea, 1);
    mainLayout->addWidget(m_promptBar);

    setWidget(content);

    // Lambda to dispatch trimmed expression to connected debugger backend.
    auto triggerEval = [this]() {
        const QString expr = m_inputField->text().trimmed();
        if (!expr.isEmpty()) {
            emit evaluateRequested(expr);
            m_inputField->selectAll();
        }
    };

    // Connect evaluate button, Enter key, "+ Watch" promotion button, and Clear button.
    connect(m_evaluateBtn, &QPushButton::clicked, this, triggerEval);
    connect(m_inputField, &QLineEdit::returnPressed, this, triggerEval);
    connect(m_watchBtn, &QPushButton::clicked, this, &ExpressionEvaluatorWidget::promoteCurrentToWatch);
    connect(m_clearBtn, &QPushButton::clicked, m_historyArea, &QPlainTextEdit::clear);
}

/**
 * @brief Returns the pixel height of the top target/action header strip.
 * @return Header bar height in pixels.
 */
int ExpressionEvaluatorWidget::headerBarHeight() const {
    return m_headerBar ? m_headerBar->height() : 0;
}

/**
 * @brief Returns the pixel height of the bottom eval> prompt strip.
 * @return Prompt bar height in pixels.
 */
int ExpressionEvaluatorWidget::promptBarHeight() const {
    return m_promptBar ? m_promptBar->height() : 0;
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


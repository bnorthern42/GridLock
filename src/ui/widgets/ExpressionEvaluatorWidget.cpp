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

    m_batchFlushTimer = new QTimer(this);
    m_batchFlushTimer->setSingleShot(true);
    m_batchFlushTimer->setInterval(1200);
    connect(m_batchFlushTimer, &QTimer::timeout, this, &ExpressionEvaluatorWidget::flushPendingMultiRankBatch);
}

int ExpressionEvaluatorWidget::headerBarHeight() const {
    return m_headerBar ? m_headerBar->height() : 0;
}

int ExpressionEvaluatorWidget::promptBarHeight() const {
    return m_promptBar ? m_promptBar->height() : 0;
}

void ExpressionEvaluatorWidget::setExpression(const QString& expr) {
    if (m_inputField) m_inputField->setText(expr);
}

bool ExpressionEvaluatorWidget::promoteCurrentToWatch() {
    if (!m_inputField) return false;
    const QString expr = m_inputField->text().trimmed();
    if (expr.isEmpty()) return false;
    emit watchExpressionRequested(expr);
    return true;
}

void ExpressionEvaluatorWidget::setAvailableRankCount(int rankCount) {
    if (!m_rankCombo) return;
    const int prevData = m_rankCombo->currentData().toInt();
    const int n = qMax(1, rankCount);
    QSignalBlocker sb(m_rankCombo);
    m_rankCombo->clear();
    m_rankCombo->addItem(QStringLiteral("All Ranks"), -1);
    for (int r = 0; r < n; ++r) {
        m_rankCombo->addItem(QStringLiteral("Rank %1").arg(r), r);
    }
    const int idx = m_rankCombo->findData(prevData);
    m_rankCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

int ExpressionEvaluatorWidget::selectedTargetRank() const {
    return m_rankCombo ? m_rankCombo->currentData().toInt() : -1;
}

void ExpressionEvaluatorWidget::setSelectedTargetRank(int rankId) {
    if (!m_rankCombo) return;
    if (rankId >= 0 && m_rankCombo->findData(rankId) < 0) setAvailableRankCount(rankId + 1);
    const int idx = m_rankCombo->findData(rankId < 0 ? -1 : rankId);
    if (idx >= 0) m_rankCombo->setCurrentIndex(idx);
}

void ExpressionEvaluatorWidget::beginMultiRankEvaluation(const QString& expr, const QVector<int>& ranks) {
    if (!m_pendingBatchResults.isEmpty()) flushPendingMultiRankBatch();
    m_pendingBatchExpr = expr.trimmed();
    m_pendingBatchRanks.clear();
    m_pendingBatchResults.clear();
    for (int r : ranks) m_pendingBatchRanks.insert(r);
    if (!m_pendingBatchRanks.isEmpty() && m_batchFlushTimer) m_batchFlushTimer->start();
}

void ExpressionEvaluatorWidget::flushPendingMultiRankBatch() {
    if (m_batchFlushTimer) m_batchFlushTimer->stop();
    if (m_pendingBatchResults.isEmpty()) {
        m_pendingBatchExpr.clear();
        m_pendingBatchRanks.clear();
        return;
    }
    QSet<QString> uniqueVals;
    for (auto it = m_pendingBatchResults.constBegin(); it != m_pendingBatchResults.constEnd(); ++it) {
        uniqueVals.insert(it.value().trimmed());
    }
    const bool divergent = (uniqueVals.size() > 1);
    QStringList htmlParts;
    for (auto it = m_pendingBatchResults.constBegin(); it != m_pendingBatchResults.constEnd(); ++it) {
        const QString valEsc = it.value().toHtmlEscaped();
        const QString color = divergent ? QStringLiteral("#f59e0b") : QStringLiteral("#4ec9b0");
        htmlParts.append(QStringLiteral("<span style='color:#94a3b8;'>[Rank %1]:</span> <span style='color:%2;font-weight:%3;'>%4</span>")
                             .arg(it.key())
                             .arg(color, divergent ? QStringLiteral("700") : QStringLiteral("400"), valEsc));
    }
    const QString exprEsc = m_pendingBatchExpr.toHtmlEscaped();
    const QString divBadge = divergent
                                 ? QStringLiteral(" <span style='color:#f44747;font-weight:700;'>[DIVERGENT]</span>")
                                 : QString();
    m_historyArea->appendHtml(QStringLiteral("<span style='color:#61afef;'>%1</span> =&gt; %2%3")
                                  .arg(exprEsc, htmlParts.join(QStringLiteral("  |  ")), divBadge));
    m_pendingBatchExpr.clear();
    m_pendingBatchRanks.clear();
    m_pendingBatchResults.clear();
}

void ExpressionEvaluatorWidget::appendResult(int rankId, const QString& expr, const QString& result) {
    if (!m_pendingBatchRanks.isEmpty() && expr.trimmed() == m_pendingBatchExpr && m_pendingBatchRanks.contains(rankId)) {
        m_pendingBatchResults.insert(rankId, result);
        m_pendingBatchRanks.remove(rankId);
        if (m_pendingBatchRanks.isEmpty()) flushPendingMultiRankBatch();
        return;
    }
    m_historyArea->appendPlainText(QString("[Rank %1] %2 = %3").arg(rankId).arg(expr, result));
}

}  // namespace gridlock::ui


#pragma once

/**
 * @file ExpressionEvaluatorWidget.hpp
 * @brief Dynamic debugger expression evaluation dock widget.
 *
 * Implements ExpressionEvaluatorWidget, allowing the developer to query,
 * calculate, or test arbitrary C/C++ expressions or memory expressions
 * against active cluster ranks.
 */

#include <QComboBox>
#include <QDockWidget>
#include <QLineEdit>
#include <QMap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QVector>

namespace gridlock::ui {

/**
 * @class ExpressionEvaluatorWidget
 * @brief Dockable console for evaluating and inspecting debug expressions.
 */
class ExpressionEvaluatorWidget : public QDockWidget {
    Q_OBJECT

public:
    explicit ExpressionEvaluatorWidget(QWidget* parent = nullptr);
    ~ExpressionEvaluatorWidget() override = default;

    /** @brief Sets the current expression input field text. */
    void setExpression(const QString& expr);

    /** @brief Promotes the currently entered expression to the Watch Expressions dock. */
    bool promoteCurrentToWatch();

    /** @brief Returns the pixel height of the top target/action header strip. */
    int headerBarHeight() const;

    /** @brief Returns the pixel height of the bottom eval> prompt strip. */
    int promptBarHeight() const;

    /** @brief Updates `#evaluatorRankCombo` entries to `All Ranks` + `Rank 0..rankCount-1`. */
    void setAvailableRankCount(int rankCount);

    /** @brief Returns `-1` when `All Ranks` is selected, or the 0-based selected rank ID. */
    int selectedTargetRank() const;

    /** @brief Selects `-1` (`All Ranks`) or a specific 0-based rank in `#evaluatorRankCombo`. */
    void setSelectedTargetRank(int rankId);

    /** @brief Starts an All-Ranks batch expecting `appendResult()` replies for @p ranks. */
    void beginMultiRankEvaluation(const QString& expr, const QVector<int>& ranks);

public slots:
    /** @brief Appends evaluated expression results for @p rankId (or aggregates into an active All-Ranks batch). */
    void appendResult(int rankId, const QString& expr, const QString& result);

signals:
    /** @brief Emitted when the user submits an expression for evaluation. */
    void evaluateRequested(const QString& expression);

    /** @brief Emitted when the user promotes an expression to the Watch Expressions dock. */
    void watchExpressionRequested(const QString& expression);

private:
    void flushPendingMultiRankBatch();

    QWidget* m_headerBar = nullptr;
    QWidget* m_promptBar = nullptr;
    QComboBox* m_rankCombo = nullptr;
    QLineEdit* m_inputField = nullptr;
    QPushButton* m_evaluateBtn = nullptr;
    QPushButton* m_watchBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QPlainTextEdit* m_historyArea = nullptr;

    QString m_pendingBatchExpr;
    QSet<int> m_pendingBatchRanks;
    QMap<int, QString> m_pendingBatchResults;
    QTimer* m_batchFlushTimer = nullptr;
};

}  // namespace gridlock::ui


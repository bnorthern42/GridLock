#pragma once

/**
 * @file ExpressionEvaluatorWidget.hpp
 * @brief Dynamic debugger expression evaluation dock widget.
 *
 * Implements ExpressionEvaluatorWidget, allowing the developer to query,
 * calculate, or test arbitrary C/C++ expressions or memory expressions
 * against active cluster ranks.
 */

#include <QDockWidget>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QString>

namespace gridlock::ui {

/**
 * @class ExpressionEvaluatorWidget
 * @brief Dockable console for evaluating and inspecting debug expressions.
 */
class ExpressionEvaluatorWidget : public QDockWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new ExpressionEvaluatorWidget.
     * @param parent Optional parent widget.
     */
    explicit ExpressionEvaluatorWidget(QWidget* parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~ExpressionEvaluatorWidget() override = default;

    /**
     * @brief Sets the current expression input field text.
     * @param expr Expression string to populate into the evaluator field.
     */
    void setExpression(const QString& expr);

    /**
     * @brief Promotes the currently entered expression to the Watch Expressions dock.
     * @return True if a non-empty expression was promoted, false otherwise.
     */
    bool promoteCurrentToWatch();

public slots:
    /**
     * @brief Appends evaluated expression results for a specific rank to the history console.
     * @param rankId Evaluated MPI rank index.
     * @param expr The submitted expression string.
     * @param result Formatted evaluation output or error.
     */
    void appendResult(int rankId, const QString& expr, const QString& result);

signals:
    /**
     * @brief Emitted when the user submits an expression for evaluation.
     * @param expression The expression string.
     */
    void evaluateRequested(const QString& expression);

    /**
     * @brief Emitted when the user promotes an expression to the Watch Expressions dock.
     * @param expression The expression string to watch across ranks.
     */
    void watchExpressionRequested(const QString& expression);

private:
    /// Input field for typing expressions.
    QLineEdit* m_inputField = nullptr;

    /// Submission button.
    QPushButton* m_evaluateBtn = nullptr;

    /// Promote-to-watch button in the header strip.
    QPushButton* m_watchBtn = nullptr;

    /// Monospace scrollable history output console.
    QPlainTextEdit* m_historyArea = nullptr;
};

}  // namespace gridlock::ui


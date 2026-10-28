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

private:
    /// Input field for typing expressions.
    QLineEdit* m_inputField = nullptr;

    /// Submission button.
    QPushButton* m_evaluateBtn = nullptr;

    /// Monospace scrollable history output console.
    QPlainTextEdit* m_historyArea = nullptr;
};

}  // namespace gridlock::ui


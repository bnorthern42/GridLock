/**
 * @file ConditionalBreakpointDialog.hpp
 * @brief Modal dialog for specifying C/C++ breakpoint predicate expressions.
 *
 * Provides a text input interface allowing users to attach execution conditions
 * (such as "i == 42 && rank == 0") to source code breakpoints before broadcasting
 * them to GDB or DAP debugger backends.
 */

#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QString>

namespace gridlock::ui {

/**
 * @class ConditionalBreakpointDialog
 * @brief Modal dialog prompting for a conditional breakpoint expression.
 */
class ConditionalBreakpointDialog : public QDialog {
    Q_OBJECT

public:
    /**
     * @brief Constructs the dialog with an optional initial condition string.
     * @param currentCondition Pre-existing predicate expression if editing, or empty for new.
     * @param parent Optional parent widget.
     */
    explicit ConditionalBreakpointDialog(const QString& currentCondition = "",
                                         QWidget* parent = nullptr);

    /**
     * @brief Retrieves the trimmed predicate condition string entered by user.
     * @return Trimmed expression string.
     */
    QString condition() const;

private:
    QLineEdit* m_conditionEdit{nullptr};  ///< Single-line text input for predicate expression.
};

}  // namespace gridlock::ui

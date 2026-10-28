/**
 * @file ConditionalBreakpointDialog.cpp
 * @brief Implementation of conditional breakpoint predicate entry dialog.
 *
 * Implements widget layout construction, initial expression population,
 * and user input extraction for conditional breakpoint evaluation.
 */

#include "ConditionalBreakpointDialog.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace gridlock::ui {

/**
 * @brief Constructs and presents the conditional breakpoint configuration modal.
 * @param currentCondition Initial expression to populate into the input field.
 * @param parent Optional parent widget.
 */
ConditionalBreakpointDialog::ConditionalBreakpointDialog(const QString& currentCondition,
                                                         QWidget* parent)
    : QDialog(parent) {
    // Set descriptive dialog title and fixed modal dimensions.
    setWindowTitle("Conditional Breakpoint");
    setFixedSize(350, 120);

    // Main vertical layout containing input label, edit field, and button strip.
    auto* layout = new QVBoxLayout(this);

    // Informational label indicating syntax expectations.
    auto* label = new QLabel("Enter condition (e.g. i == 100 && rank == 0):", this);
    layout->addWidget(label);

    // Single-line text input field populated with current expression.
    m_conditionEdit = new QLineEdit(this);
    m_conditionEdit->setText(currentCondition);
    layout->addWidget(m_conditionEdit);

    // Button strip with stretch spacer pushing actions to the right edge.
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* cancelBtn = new QPushButton("Cancel", this);
    auto* okBtn = new QPushButton("Set Condition", this);
    okBtn->setDefault(true);

    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    layout->addLayout(btnLayout);

    // Connect standard button actions to accept and reject dialog slots.
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
}

/**
 * @brief Extracts the entered condition with leading/trailing whitespace removed.
 * @return Cleaned predicate expression string.
 */
QString ConditionalBreakpointDialog::condition() const {
    return m_conditionEdit->text().trimmed();
}

}  // namespace gridlock::ui

/**
 * @file ConditionalBreakpointDialog.cpp
 * @brief Implementation of conditional breakpoint predicate entry dialog.
 *
 * Implements widget layout construction, initial expression population,
 * and user input extraction for conditional breakpoint evaluation.
 */

#include "ConditionalBreakpointDialog.hpp"

#include <QGroupBox>
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
    // Set descriptive dialog title, minimum workstation dimensions, and dark-slate chrome.
    setWindowTitle(QStringLiteral("Conditional Breakpoint"));
    setMinimumSize(420, 230);
    resize(440, 240);
    setStyleSheet(QStringLiteral(
        "QDialog { background: #16181d; color: #d7dae0; }"
        "QGroupBox { background: #1b1e24; border: 1px solid #282c34; border-radius: 3px; "
        "margin-top: 8px; padding: 10px 8px 8px 8px; color: #d7dae0; font-weight: 600; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; "
        "padding: 0px 6px; color: #61afef; }"
        "QLabel { color: #abb2bf; font-size: 11px; }"
        "QLineEdit { background: #181a1f; color: #d7dae0; border: 1px solid #323844; "
        "border-radius: 2px; padding: 1px 6px; min-height: 20px; max-height: 24px; "
        "font-family: 'JetBrains Mono', monospace; font-size: 11px; }"
        "QPushButton { background: #282c34; color: #d7dae0; border: 1px solid #3b4048; "
        "border-radius: 2px; padding: 2px 10px; min-height: 22px; max-height: 24px; font-size: 11px; }"
        "QPushButton:hover { background: #323842; color: #ffffff; border: 1px solid #528bff; }"));

    // Main vertical layout containing the predicate card and bottom action button strip.
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    // Content card grouping the breakpoint predicate expression input and syntax hint.
    auto* card = new QGroupBox(QStringLiteral("Rank-Aware Breakpoint Predicate"), this);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(10, 12, 10, 10);
    cardLayout->setSpacing(6);

    // Informational label indicating syntax expectations.
    auto* label = new QLabel(QStringLiteral("Enter C/C++ boolean condition (e.g. i == 100 && rank == 0):"), card);
    cardLayout->addWidget(label);

    // Single-line text input field populated with current expression and capped at 24px height.
    m_conditionEdit = new QLineEdit(card);
    m_conditionEdit->setFixedHeight(24);
    m_conditionEdit->setText(currentCondition);
    cardLayout->addWidget(m_conditionEdit);
    cardLayout->addStretch(1);
    layout->addWidget(card, 1);

    // Button strip with stretch spacer pushing 24px-high actions to the right edge.
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto* cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    cancelBtn->setFixedHeight(24);
    auto* okBtn = new QPushButton(QStringLiteral("Set Condition"), this);
    okBtn->setFixedHeight(24);
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

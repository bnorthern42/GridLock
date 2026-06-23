#include "ConditionalBreakpointDialog.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace gridlock::ui {

ConditionalBreakpointDialog::ConditionalBreakpointDialog(const QString& currentCondition, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Conditional Breakpoint");
    setFixedSize(350, 120);

    auto* layout = new QVBoxLayout(this);
    
    auto* label = new QLabel("Enter condition (e.g. i == 100 && rank == 0):", this);
    layout->addWidget(label);

    m_conditionEdit = new QLineEdit(this);
    m_conditionEdit->setText(currentCondition);
    layout->addWidget(m_conditionEdit);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    
    auto* cancelBtn = new QPushButton("Cancel", this);
    auto* okBtn = new QPushButton("Set Condition", this);
    okBtn->setDefault(true);
    
    btnLayout->addWidget(cancelBtn);
    btnLayout->addWidget(okBtn);
    layout->addLayout(btnLayout);

    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
}

QString ConditionalBreakpointDialog::condition() const {
    return m_conditionEdit->text().trimmed();
}

} // namespace gridlock::ui

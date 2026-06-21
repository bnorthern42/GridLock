#include "GdbConsoleWidget.hpp"
#include <QWidget>

namespace gridlock::ui {

GdbConsoleWidget::GdbConsoleWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    QHBoxLayout* topLayout = new QHBoxLayout();
    m_filterEdit = new QLineEdit();
    m_filterEdit->setPlaceholderText("Grep/Filter logs...");
    
    m_clearButton = new QPushButton("Clear");
    
    m_rankCombo = new QComboBox();
    m_rankCombo->addItem("All Ranks", -1);
    for (int i = 0; i < 6; ++i) {
        m_rankCombo->addItem(QString("Rank %1").arg(i), i);
    }

    topLayout->addWidget(m_filterEdit);
    topLayout->addWidget(m_clearButton);
    topLayout->addWidget(m_rankCombo);

    m_consoleEdit = new QPlainTextEdit();
    m_consoleEdit->setReadOnly(true);
    m_consoleEdit->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByKeyboard);

    m_commandEdit = new QLineEdit();
    m_commandEdit->setPlaceholderText("Enter GDB command...");

    layout->addLayout(topLayout);
    layout->addWidget(m_consoleEdit);
    layout->addWidget(m_commandEdit);

    connect(m_clearButton, &QPushButton::clicked, this, &GdbConsoleWidget::onClearClicked);
    connect(m_commandEdit, &QLineEdit::returnPressed, this, &GdbConsoleWidget::onCommandReturnPressed);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &GdbConsoleWidget::onFilterTextChanged);
    connect(m_rankCombo, &QComboBox::currentIndexChanged, this, &GdbConsoleWidget::onRankFilterChanged);
}

void GdbConsoleWidget::appendGdbOutput(int rank, const QString& output) {
    m_logs.append({rank, output});
    
    int selectedRank = m_rankCombo->currentData().toInt();
    QString filterText = m_filterEdit->text();

    if ((selectedRank == -1 || selectedRank == rank) &&
        (filterText.isEmpty() || output.contains(filterText, Qt::CaseInsensitive))) {
        m_consoleEdit->appendPlainText(QString("[GDB OUT Rank %1]: %2").arg(rank).arg(output));
    }
}

void GdbConsoleWidget::onClearClicked() {
    m_logs.clear();
    m_consoleEdit->clear();
}

void GdbConsoleWidget::onCommandReturnPressed() {
    QString cmd = m_commandEdit->text();
    if (cmd.isEmpty()) return;
    
    int rank = m_rankCombo->currentData().toInt();
    emit commandEntered(rank, cmd);
    
    m_commandEdit->clear();
}

void GdbConsoleWidget::onFilterTextChanged(const QString& /*text*/) {
    applyFilter();
}

void GdbConsoleWidget::onRankFilterChanged(int /*index*/) {
    applyFilter();
}

void GdbConsoleWidget::applyFilter() {
    m_consoleEdit->clear();
    int selectedRank = m_rankCombo->currentData().toInt();
    QString filterText = m_filterEdit->text();

    for (const auto& log : m_logs) {
        if ((selectedRank == -1 || selectedRank == log.rank) &&
            (filterText.isEmpty() || log.text.contains(filterText, Qt::CaseInsensitive))) {
            m_consoleEdit->appendPlainText(QString("[GDB OUT Rank %1]: %2").arg(log.rank).arg(log.text));
        }
    }
}

} // namespace gridlock::ui

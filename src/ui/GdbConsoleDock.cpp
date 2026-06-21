#include "GdbConsoleDock.hpp"
#include <QWidget>

namespace gridlock::ui {

GdbConsoleDock::GdbConsoleDock(QWidget* parent) : QDockWidget("GDB Console", parent) {
    QWidget* container = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(container);

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

    setWidget(container);

    connect(m_clearButton, &QPushButton::clicked, this, &GdbConsoleDock::onClearClicked);
    connect(m_commandEdit, &QLineEdit::returnPressed, this, &GdbConsoleDock::onCommandReturnPressed);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &GdbConsoleDock::onFilterTextChanged);
    connect(m_rankCombo, &QComboBox::currentIndexChanged, this, &GdbConsoleDock::onRankFilterChanged);
}

void GdbConsoleDock::appendGdbOutput(int rank, const QString& output) {
    m_logs.append({rank, output});
    
    int selectedRank = m_rankCombo->currentData().toInt();
    QString filterText = m_filterEdit->text();

    if ((selectedRank == -1 || selectedRank == rank) &&
        (filterText.isEmpty() || output.contains(filterText, Qt::CaseInsensitive))) {
        m_consoleEdit->appendPlainText(QString("[GDB OUT Rank %1]: %2").arg(rank).arg(output));
    }
}

void GdbConsoleDock::onClearClicked() {
    m_logs.clear();
    m_consoleEdit->clear();
}

void GdbConsoleDock::onCommandReturnPressed() {
    QString cmd = m_commandEdit->text();
    if (cmd.isEmpty()) return;
    
    int rank = m_rankCombo->currentData().toInt();
    emit commandEntered(rank, cmd);
    
    m_commandEdit->clear();
}

void GdbConsoleDock::onFilterTextChanged(const QString& /*text*/) {
    applyFilter();
}

void GdbConsoleDock::onRankFilterChanged(int /*index*/) {
    applyFilter();
}

void GdbConsoleDock::applyFilter() {
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

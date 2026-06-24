#include "GdbConsoleWidget.hpp"
#include <QWidget>

namespace {
QString formatLogEntry(int rank, const QString& text, bool isInput, bool showRaw) {
    if (showRaw) {
        QString prefix = isInput ? "[GDB IN Rank %1]: %2" : "[GDB OUT Rank %1]: %2";
        return prefix.arg(rank).arg(text);
    }
    
    if (isInput) return QString();
    
    if (text.startsWith("~") || text.startsWith("@") || text.startsWith("&")) {
        QString content = text.mid(1);
        if (content.startsWith("\"") && content.endsWith("\"")) {
            content = content.mid(1, content.length() - 2);
        }
        return content.replace("\\n", "\n").replace("\\t", "\t").replace("\\\"", "\"").trimmed();
    }
    
    return QString();
}
} // namespace

namespace gridlock::ui {

GdbConsoleWidget::GdbConsoleWidget(QWidget* parent) : QWidget(parent) {
    QVBoxLayout* layout = new QVBoxLayout(this);

    QHBoxLayout* topLayout = new QHBoxLayout();
    m_filterEdit = new QLineEdit();
    m_filterEdit->setPlaceholderText("Grep/Filter logs...");
    
    m_clearButton = new QPushButton("Clear");
    
    m_rawMiCheckbox = new QCheckBox("Show Raw MI Traffic");
    m_rawMiCheckbox->setChecked(false);
    
    m_rankCombo = new QComboBox();
    m_rankCombo->addItem("All Ranks", -1);

    topLayout->addWidget(m_filterEdit);
    topLayout->addWidget(m_clearButton);
    topLayout->addWidget(m_rawMiCheckbox);
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
    connect(m_rawMiCheckbox, &QCheckBox::toggled, this, &GdbConsoleWidget::onRawMiToggled);
}

void GdbConsoleWidget::resetRanks(int count) {
    m_logs.clear();
    m_consoleEdit->clear();
    m_rankCombo->blockSignals(true);
    m_rankCombo->clear();
    m_rankCombo->addItem("All Ranks", -1);
    for (int i = 0; i < count; ++i) {
        m_rankCombo->addItem(QString("Rank %1").arg(i), i);
    }
    m_rankCombo->blockSignals(false);
}

void GdbConsoleWidget::appendGdbOutput(int rank, const QString& output) {
    m_logs.append({rank, output, false});
    
    int selectedRank = m_rankCombo->currentData().toInt();
    QString filterText = m_filterEdit->text();
    bool showRaw = m_rawMiCheckbox->isChecked();

    if (selectedRank != -1 && selectedRank != rank) return;
    
    QString displayStr = formatLogEntry(rank, output, false, showRaw);
    if (displayStr.isEmpty()) return;

    if (filterText.isEmpty() || displayStr.contains(filterText, Qt::CaseInsensitive)) {
        m_consoleEdit->appendPlainText(displayStr);
    }
}

void GdbConsoleWidget::appendGdbInput(int rank, const QString& input) {
    m_logs.append({rank, input, true});
    
    int selectedRank = m_rankCombo->currentData().toInt();
    QString filterText = m_filterEdit->text();
    bool showRaw = m_rawMiCheckbox->isChecked();

    if (selectedRank != -1 && selectedRank != rank) return;
    
    QString displayStr = formatLogEntry(rank, input, true, showRaw);
    if (displayStr.isEmpty()) return;

    if (filterText.isEmpty() || displayStr.contains(filterText, Qt::CaseInsensitive)) {
        m_consoleEdit->appendPlainText(displayStr);
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

void GdbConsoleWidget::onRawMiToggled(bool /*checked*/) {
    applyFilter();
}

void GdbConsoleWidget::applyFilter() {
    m_consoleEdit->clear();
    int selectedRank = m_rankCombo->currentData().toInt();
    QString filterText = m_filterEdit->text();
    bool showRaw = m_rawMiCheckbox->isChecked();

    for (const auto& log : m_logs) {
        if (selectedRank != -1 && selectedRank != log.rank) continue;
        
        QString displayStr = formatLogEntry(log.rank, log.text, log.isInput, showRaw);
        if (displayStr.isEmpty()) continue;
        
        if (filterText.isEmpty() || displayStr.contains(filterText, Qt::CaseInsensitive)) {
            m_consoleEdit->appendPlainText(displayStr);
        }
    }
}

} // namespace gridlock::ui

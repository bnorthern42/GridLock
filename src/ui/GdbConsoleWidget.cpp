/**
 * @file GdbConsoleWidget.cpp
 * @brief Implementation of the interactive GDB MI console widget.
 *
 * Implements GDB MI protocol stream formatting, log retention, regular
 * expression / rank-based log filtering, and interactive command dispatch.
 */

#include "GdbConsoleWidget.hpp"

#include <QWidget>

namespace gridlock::ui {

/**
 * @brief Formats an MI protocol stream entry for terminal presentation.
 * @param rank Rank index associated with the stream.
 * @param text Raw stream text.
 * @param isInput True for stdin commands, false for stdout/stderr.
 * @param showRaw True if raw MI syntax should be preserved verbatim.
 * @return Formatted string for console display, or empty if suppressed.
 */
QString GdbConsoleWidget::formatLogEntry(int rank, const QString& text,
                                         bool isInput, bool showRaw) {
    if (showRaw) {
        const QString prefix =
            isInput ? "[GDB IN Rank %1]: %2" : "[GDB OUT Rank %1]: %2";
        return prefix.arg(rank).arg(text);
    }

    if (isInput) {
        return QString();
    }

    // Parse MI console stream (~), target stream (@), and log stream (&).
    if (text.startsWith("~") || text.startsWith("@") || text.startsWith("&")) {
        QString content = text.mid(1);
        if (content.startsWith("\"") && content.endsWith("\"")) {
            content = content.mid(1, content.length() - 2);
        }
        return content.replace("\\n", "\n")
            .replace("\\t", "\t")
            .replace("\\\"", "\"")
            .trimmed();
    }

    return QString();
}

/**
 * @brief Constructs the console widget and assembles toolbar, text view, and input bar.
 * @param parent Optional parent widget context.
 */
GdbConsoleWidget::GdbConsoleWidget(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);

    // Top control strip: search filter, clear button, raw checkbox, and rank dropdown.
    auto* topLayout = new QHBoxLayout();
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Grep/Filter logs...");

    m_clearButton = new QPushButton("Clear", this);

    m_rawMiCheckbox = new QCheckBox("Show Raw MI Traffic", this);
    m_rawMiCheckbox->setChecked(false);

    m_rankCombo = new QComboBox(this);
    m_rankCombo->addItem("All Ranks", -1);

    topLayout->addWidget(m_filterEdit);
    topLayout->addWidget(m_clearButton);
    topLayout->addWidget(m_rawMiCheckbox);
    topLayout->addWidget(m_rankCombo);

    // Read-only console viewport supporting text selection and copy.
    m_consoleEdit = new QPlainTextEdit(this);
    m_consoleEdit->setReadOnly(true);
    m_consoleEdit->setTextInteractionFlags(Qt::TextBrowserInteraction |
                                           Qt::TextSelectableByKeyboard);

    // Bottom command input line edit for interactive GDB command dispatch.
    m_commandEdit = new QLineEdit(this);
    m_commandEdit->setPlaceholderText("Enter GDB command...");

    layout->addLayout(topLayout);
    layout->addWidget(m_consoleEdit);
    layout->addWidget(m_commandEdit);

    // Wire user interaction signals to corresponding filter and dispatch handlers.
    connect(m_clearButton, &QPushButton::clicked, this,
            &GdbConsoleWidget::onClearClicked);
    connect(m_commandEdit, &QLineEdit::returnPressed, this,
            &GdbConsoleWidget::onCommandReturnPressed);
    connect(m_filterEdit, &QLineEdit::textChanged, this,
            &GdbConsoleWidget::onFilterTextChanged);
    connect(m_rankCombo, &QComboBox::currentIndexChanged, this,
            &GdbConsoleWidget::onRankFilterChanged);
    connect(m_rawMiCheckbox, &QCheckBox::toggled, this,
            &GdbConsoleWidget::onRawMiToggled);
}

/**
 * @brief Resets the rank selection dropdown items for a new debug session.
 * @param count Number of MPI ranks participating in the session.
 */
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

/**
 * @brief Appends debugger stdout records and evaluates active filters.
 * @param rank Rank index that produced output.
 * @param output Output payload string.
 */
void GdbConsoleWidget::appendGdbOutput(int rank, const QString& output) {
    m_logs.append({rank, output, false});

    const int selectedRank = m_rankCombo->currentData().toInt();
    const QString filterText = m_filterEdit->text();
    const bool showRaw = m_rawMiCheckbox->isChecked();

    if (selectedRank != -1 && selectedRank != rank) {
        return;
    }

    const QString displayStr = formatLogEntry(rank, output, false, showRaw);
    if (displayStr.isEmpty()) {
        return;
    }

    if (filterText.isEmpty() ||
        displayStr.contains(filterText, Qt::CaseInsensitive)) {
        m_consoleEdit->appendPlainText(displayStr);
    }
}

/**
 * @brief Records stdin command input dispatched to a specific rank.
 * @param rank Target rank index.
 * @param input Command string dispatched.
 */
void GdbConsoleWidget::appendGdbInput(int rank, const QString& input) {
    m_logs.append({rank, input, true});

    const int selectedRank = m_rankCombo->currentData().toInt();
    const QString filterText = m_filterEdit->text();
    const bool showRaw = m_rawMiCheckbox->isChecked();

    if (selectedRank != -1 && selectedRank != rank) {
        return;
    }

    const QString displayStr = formatLogEntry(rank, input, true, showRaw);
    if (displayStr.isEmpty()) {
        return;
    }

    if (filterText.isEmpty() ||
        displayStr.contains(filterText, Qt::CaseInsensitive)) {
        m_consoleEdit->appendPlainText(displayStr);
    }
}

/**
 * @brief Clears the log retention cache and viewport text.
 */
void GdbConsoleWidget::onClearClicked() {
    m_logs.clear();
    m_consoleEdit->clear();
}

/**
 * @brief Dispatches the current command input string to the selected rank.
 */
void GdbConsoleWidget::onCommandReturnPressed() {
    const QString cmd = m_commandEdit->text();
    if (cmd.isEmpty()) {
        return;
    }

    const int rank = m_rankCombo->currentData().toInt();
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

/**
 * @brief Replays retained logs against updated filter predicates.
 */
void GdbConsoleWidget::applyFilter() {
    m_consoleEdit->clear();
    const int selectedRank = m_rankCombo->currentData().toInt();
    const QString filterText = m_filterEdit->text();
    const bool showRaw = m_rawMiCheckbox->isChecked();

    for (const auto& log : m_logs) {
        if (selectedRank != -1 && selectedRank != log.rank) {
            continue;
        }

        const QString displayStr =
            formatLogEntry(log.rank, log.text, log.isInput, showRaw);
        if (displayStr.isEmpty()) {
            continue;
        }

        if (filterText.isEmpty() ||
            displayStr.contains(filterText, Qt::CaseInsensitive)) {
            m_consoleEdit->appendPlainText(displayStr);
        }
    }
}

}  // namespace gridlock::ui

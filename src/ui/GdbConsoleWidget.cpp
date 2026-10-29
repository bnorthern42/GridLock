/**
 * @file GdbConsoleWidget.cpp
 * @brief Implementation of the interactive GDB MI console widget.
 *
 * Implements GDB MI protocol stream formatting, log retention, regular
 * expression / rank-based log filtering, and interactive command dispatch.
 */

#include "GdbConsoleWidget.hpp"

#include <QFrame>
#include <QLabel>
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
 * @brief Constructs the console widget and assembles the 26px header bar, flush text view, and 26px gdb> prompt bar.
 * @param parent Optional parent widget context.
 */
GdbConsoleWidget::GdbConsoleWidget(QWidget* parent)
    : QWidget(parent) {
    setContentsMargins(0, 0, 0, 0);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 1. Unified 26px #21252b top header control strip: Target rank selector, grep filter, Raw MI toggle, and Clear.
    m_headerBar = new QWidget(this);
    m_headerBar->setObjectName(QStringLiteral("gdbConsoleHeaderBar"));
    m_headerBar->setFixedHeight(26);
    m_headerBar->setStyleSheet(QStringLiteral(
        "#gdbConsoleHeaderBar { background: #21252b; border-bottom: 1px solid #2c313a; }"
        "#gdbConsoleHeaderBar QLabel { color: #abb2bf; font-size: 10px; font-weight: 700; }"));

    auto* topLayout = new QHBoxLayout(m_headerBar);
    topLayout->setContentsMargins(6, 2, 6, 2);
    topLayout->setSpacing(6);

    auto* targetLabel = new QLabel(QStringLiteral("TARGET:"), m_headerBar);
    m_rankCombo = new QComboBox(m_headerBar);
    m_rankCombo->setObjectName(QStringLiteral("gdbConsoleRankCombo"));
    m_rankCombo->setFixedHeight(22);
    m_rankCombo->addItem(QStringLiteral("All Ranks"), -1);

    m_filterEdit = new QLineEdit(m_headerBar);
    m_filterEdit->setObjectName(QStringLiteral("gdbConsoleFilterEdit"));
    m_filterEdit->setPlaceholderText(QStringLiteral("Filter GDB/MI stream..."));
    m_filterEdit->setFixedHeight(22);

    m_rawMiCheckbox = new QCheckBox(QStringLiteral("Raw MI"), m_headerBar);
    m_rawMiCheckbox->setChecked(false);

    m_clearButton = new QPushButton(QStringLiteral("Clear"), m_headerBar);
    m_clearButton->setObjectName(QStringLiteral("gdbConsoleClearBtn"));
    m_clearButton->setFixedHeight(22);

    topLayout->addWidget(targetLabel);
    topLayout->addWidget(m_rankCombo);
    topLayout->addWidget(m_filterEdit, 1);
    topLayout->addWidget(m_rawMiCheckbox);
    topLayout->addWidget(m_clearButton);

    // 2. Flush #16181d read-only monospace console viewport with QFrame::NoFrame.
    m_consoleEdit = new QPlainTextEdit(this);
    m_consoleEdit->setObjectName(QStringLiteral("gdbConsoleViewport"));
    m_consoleEdit->setFrameShape(QFrame::NoFrame);
    m_consoleEdit->setReadOnly(true);
    m_consoleEdit->setTextInteractionFlags(Qt::TextBrowserInteraction |
                                           Qt::TextSelectableByKeyboard);
    m_consoleEdit->setStyleSheet(QStringLiteral(
        "QPlainTextEdit#gdbConsoleViewport { background: #16181d; color: #d7dae0; border: none; "
        "font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 4px 6px; }"));

    // 3. Docked 26px #21252b bottom command prompt bar with styled 'gdb>' prefix and Execute button.
    m_promptBar = new QWidget(this);
    m_promptBar->setObjectName(QStringLiteral("gdbConsolePromptBar"));
    m_promptBar->setFixedHeight(26);
    m_promptBar->setStyleSheet(QStringLiteral(
        "#gdbConsolePromptBar { background: #21252b; border-top: 1px solid #2c313a; }"));

    auto* promptLayout = new QHBoxLayout(m_promptBar);
    promptLayout->setContentsMargins(6, 2, 6, 2);
    promptLayout->setSpacing(6);

    auto* promptLabel = new QLabel(QStringLiteral("gdb>"), m_promptBar);
    promptLabel->setStyleSheet(QStringLiteral(
        "color: #528bff; font-family: 'JetBrains Mono', monospace; font-size: 11px; font-weight: 700;"));

    m_commandEdit = new QLineEdit(m_promptBar);
    m_commandEdit->setObjectName(QStringLiteral("gdbConsoleCommandEdit"));
    m_commandEdit->setPlaceholderText(QStringLiteral("Enter GDB command (e.g., info registers, bt, x/16gx $rsp)..."));
    m_commandEdit->setFixedHeight(22);

    m_sendButton = new QPushButton(QStringLiteral("Execute"), m_promptBar);
    m_sendButton->setObjectName(QStringLiteral("gdbConsoleExecuteBtn"));
    m_sendButton->setFixedHeight(22);

    promptLayout->addWidget(promptLabel);
    promptLayout->addWidget(m_commandEdit, 1);
    promptLayout->addWidget(m_sendButton);

    layout->addWidget(m_headerBar);
    layout->addWidget(m_consoleEdit, 1);
    layout->addWidget(m_promptBar);

    // Wire user interaction signals to corresponding filter and dispatch handlers.
    connect(m_clearButton, &QPushButton::clicked, this,
            &GdbConsoleWidget::onClearClicked);
    connect(m_commandEdit, &QLineEdit::returnPressed, this,
            &GdbConsoleWidget::onCommandReturnPressed);
    connect(m_sendButton, &QPushButton::clicked, this,
            &GdbConsoleWidget::onCommandReturnPressed);
    connect(m_filterEdit, &QLineEdit::textChanged, this,
            &GdbConsoleWidget::onFilterTextChanged);
    connect(m_rankCombo, &QComboBox::currentIndexChanged, this,
            &GdbConsoleWidget::onRankFilterChanged);
    connect(m_rawMiCheckbox, &QCheckBox::toggled, this,
            &GdbConsoleWidget::onRawMiToggled);
}

/**
 * @brief Returns the pixel height of the top filter/target header strip.
 * @return Header bar height in pixels.
 */
int GdbConsoleWidget::headerBarHeight() const {
    return m_headerBar ? m_headerBar->height() : 0;
}

/**
 * @brief Returns the pixel height of the bottom gdb> command prompt strip.
 * @return Prompt bar height in pixels.
 */
int GdbConsoleWidget::promptBarHeight() const {
    return m_promptBar ? m_promptBar->height() : 0;
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

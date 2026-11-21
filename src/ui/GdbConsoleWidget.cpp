/**
 * @file GdbConsoleWidget.cpp
 * @brief Implementation of the interactive GDB MI console widget.
 *
 * Implements GDB MI protocol stream formatting, log retention, regular
 * expression / rank-based log filtering, and interactive command dispatch.
 */

#include "GdbConsoleWidget.hpp"

#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QSaveFile>
#include <QScrollBar>
#include <QTextCursor>
#include <QTimer>
#include <QWidget>

#include "../core/managers/ConfigManager.hpp"
#include "widgets/InlineFindBar.hpp"

namespace gridlock::ui {

namespace {
bool isThreadLifecycleSpam(const QString& text) {
    return text.startsWith(QLatin1String("=thread-created")) ||
           text.startsWith(QLatin1String("=thread-exited")) ||
           text.startsWith(QLatin1String("~\"[New Thread")) ||
           text.startsWith(QLatin1String("~\"[Thread "));
}
}  // namespace

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

    if (isInput || isThreadLifecycleSpam(text)) {
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
    targetLabel->setObjectName(QStringLiteral("gdbConsoleTargetLabel"));
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

    // 2. Flush #16181d read-only monospace console viewport with QFrame::NoFrame and lock-free batch settings.
    m_consoleEdit = new QPlainTextEdit(this);
    m_consoleEdit->setObjectName(QStringLiteral("gdbConsoleViewport"));
    m_consoleEdit->setFrameShape(QFrame::NoFrame);
    m_consoleEdit->setReadOnly(true);
    m_consoleEdit->setUndoRedoEnabled(false);
    m_consoleEdit->setMaximumBlockCount(5000);
    m_consoleEdit->setWordWrapMode(QTextOption::NoWrap);
    m_consoleEdit->setTextInteractionFlags(Qt::TextBrowserInteraction |
                                           Qt::TextSelectableByKeyboard);
    m_consoleEdit->setStyleSheet(QStringLiteral(
        "QPlainTextEdit#gdbConsoleViewport { background: #16181d; color: #d7dae0; border: none; "
        "font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 4px 6px; }"));
    m_findBar = new InlineFindBar(m_consoleEdit, false);
    m_flushTimer = new QTimer(this);
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(16);
    connect(m_flushTimer, &QTimer::timeout, this, &GdbConsoleWidget::flushConsoleBatch);

    // Inline (reverse-i-search) strip directly above the bottom prompt bar.
    m_reverseSearchLabel = new QLabel(this);
    m_reverseSearchLabel->setObjectName(QStringLiteral("gdbReverseSearchStrip"));
    m_reverseSearchLabel->setFixedHeight(22);
    m_reverseSearchLabel->setStyleSheet(QStringLiteral(
        "QLabel#gdbReverseSearchStrip { background: #1e222a; color: #e5c07b; border-top: 1px solid #2c313a; "
        "font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 2px 8px; }"));
    m_reverseSearchLabel->hide();

    // Floating/anchored Tab completion popup list widget.
    m_completionPopup = new QListWidget(this);
    m_completionPopup->setObjectName(QStringLiteral("GdbCompletionPopup"));
    m_completionPopup->setWindowFlags(Qt::SubWindow);
    m_completionPopup->setFocusPolicy(Qt::NoFocus);
    m_completionPopup->setStyleSheet(QStringLiteral(
        "QListWidget#GdbCompletionPopup { background: #1e222a; color: #d7dae0; border: 1px solid #528bff; "
        "font-family: 'JetBrains Mono', monospace; font-size: 11px; }"));
    m_completionPopup->hide();
    connect(m_completionPopup, &QListWidget::itemActivated, this, [this](QListWidgetItem*) { acceptSelectedCompletion(); });
    connect(m_completionPopup, &QListWidget::itemClicked, this, [this](QListWidgetItem*) { acceptSelectedCompletion(); });

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
    m_commandEdit->installEventFilter(this);

    m_sendButton = new QPushButton(QStringLiteral("Execute"), m_promptBar);
    m_sendButton->setObjectName(QStringLiteral("gdbConsoleExecuteBtn"));
    m_sendButton->setFixedHeight(22);

    promptLayout->addWidget(promptLabel);
    promptLayout->addWidget(m_commandEdit, 1);
    promptLayout->addWidget(m_sendButton);

    layout->addWidget(m_headerBar);
    layout->addWidget(m_consoleEdit, 1);
    layout->addWidget(m_reverseSearchLabel);
    layout->addWidget(m_promptBar);

    loadCommandHistory();

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
void GdbConsoleWidget::openFindBar() {
    if (m_findBar) m_findBar->openAndFocus();
}

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
    m_pendingBatch.clear();
    if (m_flushTimer) m_flushTimer->stop();
    m_consoleEdit->clear();
    ensureRankCount(count);
}

/**
 * @brief Synchronizes the rank selector dropdown count without clearing retained console logs.
 * @param count Active number of MPI ranks in the session.
 */
void GdbConsoleWidget::ensureRankCount(int count) {
    if (!m_rankCombo || count < 0) return;
    if (count > 0) {
        const bool multi = (count > 1);
        m_rankCombo->setVisible(multi);
        if (auto* tl = findChild<QLabel*>(QStringLiteral("gdbConsoleTargetLabel"))) tl->setVisible(multi);
        if (!multi && m_rankCombo->currentIndex() != 0) m_rankCombo->setCurrentIndex(0);
    }
    if (m_rankCombo->count() == count + 1) return;
    const int previousSelection = m_rankCombo->currentData().toInt();
    m_rankCombo->blockSignals(true);
    m_rankCombo->clear();
    m_rankCombo->addItem(QStringLiteral("All Ranks"), -1);
    for (int i = 0; i < count; ++i) {
        m_rankCombo->addItem(QStringLiteral("Rank %1").arg(i), i);
    }
    const int restoredIdx = m_rankCombo->findData(previousSelection);
    m_rankCombo->setCurrentIndex(restoredIdx >= 0 ? restoredIdx : 0);
    m_rankCombo->blockSignals(false);
}

/**
 * @brief Appends debugger stdout records and queues matching lines for 60 FPS batch insertion.
 * @param rank Rank index that produced output.
 * @param output Output payload string.
 */
void GdbConsoleWidget::appendGdbOutput(int rank, const QString& output) {
    const bool showRaw = m_rawMiCheckbox->isChecked();
    // Drop high-frequency OpenMP/pthread lifecycle spam when Raw MI mode is disabled.
    if (!showRaw && isThreadLifecycleSpam(output)) return;

    m_logs.append({rank, output, false, false, false});
    const int selectedRank = m_rankCombo->currentData().toInt();
    if (selectedRank != -1 && selectedRank != rank) return;

    const QString displayStr = formatLogEntry(rank, output, false, showRaw);
    if (displayStr.isEmpty()) return;

    const QString filterText = m_filterEdit->text();
    if (filterText.isEmpty() || displayStr.contains(filterText, Qt::CaseInsensitive)) {
        m_pendingBatch.append(displayStr);
        if (m_flushTimer && !m_flushTimer->isActive()) m_flushTimer->start();
    }
}

/**
 * @brief Records stdin command input dispatched to a specific rank and queues matching lines.
 * @param rank Target rank index.
 * @param input Command string dispatched.
 */
void GdbConsoleWidget::appendGdbInput(int rank, const QString& input) {
    m_logs.append({rank, input, true, false, false});
    const int selectedRank = m_rankCombo->currentData().toInt();
    if (selectedRank != -1 && selectedRank != rank) return;

    const QString displayStr = formatLogEntry(rank, input, true, m_rawMiCheckbox->isChecked());
    if (displayStr.isEmpty()) return;

    const QString filterText = m_filterEdit->text();
    if (filterText.isEmpty() || displayStr.contains(filterText, Qt::CaseInsensitive)) {
        m_pendingBatch.append(displayStr);
        if (m_flushTimer && !m_flushTimer->isActive()) m_flushTimer->start();
    }
}

/**
 * @brief Appends interactive REPL evaluation output or sanitizer diagnostics for a rank.
 * @param rank Target MPI rank index (or -1 for broadcast diagnostics).
 * @param output Formatted debugger console response or error message.
 * @param isError True when @p output represents an error or security rejection.
 */
void GdbConsoleWidget::appendDebuggerConsoleOutput(int rank, const QString& output,
                                                   bool isError) {
    const QString trimmed = output.trimmed();
    if (trimmed.isEmpty()) return;

    const int normRank = rank < 0 ? 0 : rank;
    const QString displayStr = isError
                                   ? QStringLiteral("[Rank %1 ERR] %2").arg(normRank).arg(trimmed)
                                   : QStringLiteral("[Rank %1] %2").arg(normRank).arg(trimmed);
    m_logs.append({normRank, displayStr, false, true, isError});

    const int selectedRank = m_rankCombo->currentData().toInt();
    if (selectedRank != -1 && rank >= 0 && selectedRank != rank) return;

    const QString filterText = m_filterEdit->text();
    if (filterText.isEmpty() || displayStr.contains(filterText, Qt::CaseInsensitive)) {
        m_pendingBatch.append(displayStr);
        if (m_flushTimer && !m_flushTimer->isActive()) m_flushTimer->start();
    }
}

/**
 * @brief Flushes coalesced pending console lines in one QTextCursor operation while preserving scroll.
 */
void GdbConsoleWidget::flushConsoleBatch() {
    if (m_flushTimer) m_flushTimer->stop();
    if (!m_consoleEdit || m_pendingBatch.isEmpty()) return;

    QScrollBar* sb = m_consoleEdit->verticalScrollBar();
    const int prevVal = sb ? sb->value() : 0;
    const bool stickToBottom = sb && (prevVal >= sb->maximum() - 8);
    const QString batch = m_pendingBatch.join(QLatin1Char('\n'));
    m_pendingBatch.clear();

    m_consoleEdit->setUpdatesEnabled(false);
    QTextCursor cur(m_consoleEdit->document());
    cur.movePosition(QTextCursor::End);
    cur.insertText(m_consoleEdit->document()->isEmpty() ? batch : (QLatin1Char('\n') + batch));
    if (sb) sb->setValue(stickToBottom ? sb->maximum() : prevVal);
    m_consoleEdit->setUpdatesEnabled(true);
}

/**
 * @brief Clears the log retention cache and viewport text.
 */
void GdbConsoleWidget::onClearClicked() {
    m_logs.clear();
    m_pendingBatch.clear();
    if (m_flushTimer) m_flushTimer->stop();
    m_consoleEdit->clear();
}

/**
 * @brief Dispatches the current command input string to the selected rank.
 */
void GdbConsoleWidget::onCommandReturnPressed() {
    if (m_completionPopup && m_completionPopup->isVisible()) {
        acceptSelectedCompletion();
        return;
    }
    if (m_reverseSearchActive) {
        exitReverseSearch(false);
    }
    const QString cmd = m_commandEdit->text();
    if (cmd.trimmed().isEmpty()) return;

    recordCommandInHistory(cmd);
    // Emit -1 when "All Ranks" is active, or the specific 0-based MPI rank index.
    const int rank = m_rankCombo->currentData().toInt();
    emit commandEntered(rank, cmd);
    m_commandEdit->clear();
    m_pendingDraft.clear();
}

void GdbConsoleWidget::loadCommandHistory() {
    m_history.clear();
    const QString path = gridlock::core::ConfigManager::resolveGdbHistoryFilePath();
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const QByteArray& rawLine : file.readAll().split('\n')) {
            const QString line = QString::fromUtf8(rawLine).trimmed();
            if (!line.isEmpty() && (m_history.isEmpty() || m_history.last() != line)) {
                m_history.append(line);
            }
        }
    }
    while (m_history.size() > 500) m_history.removeFirst();
    m_historyIndex = m_history.size();
    m_pendingDraft.clear();
}

void GdbConsoleWidget::saveCommandHistory() const {
    const QString path = gridlock::core::ConfigManager::resolveGdbHistoryFilePath();
    if (path.isEmpty()) return;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write((m_history.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8());
        file.commit();
    }
}

void GdbConsoleWidget::recordCommandInHistory(const QString& command) {
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty()) return;
    m_history.removeAll(trimmed);
    m_history.append(trimmed);
    while (m_history.size() > 500) m_history.removeFirst();
    m_historyIndex = m_history.size();
    saveCommandHistory();
}

void GdbConsoleWidget::navigateHistory(int delta) {
    if (m_history.isEmpty() || !m_commandEdit) return;
    if (m_historyIndex == m_history.size() && delta < 0) {
        m_pendingDraft = m_commandEdit->text();
    }
    const int nextIdx = qBound(0, m_historyIndex + delta, static_cast<int>(m_history.size()));
    if (nextIdx == m_historyIndex) return;
    m_historyIndex = nextIdx;
    if (m_historyIndex == m_history.size()) {
        m_commandEdit->setText(m_pendingDraft);
    } else {
        m_commandEdit->setText(m_history.at(m_historyIndex));
    }
    m_commandEdit->setCursorPosition(m_commandEdit->text().size());
}

void GdbConsoleWidget::startOrCycleReverseSearch() {
    if (!m_reverseSearchActive) {
        m_reverseSearchActive = true;
        m_reverseSearchPriorInput = m_commandEdit ? m_commandEdit->text() : QString();
        m_reverseSearchQuery.clear();
        m_reverseSearchMatch = m_reverseSearchPriorInput;
        m_reverseSearchHistoryIdx = m_history.size();
        hideCompletionPopup();
        updateReverseSearchMatch(false);
        if (m_reverseSearchLabel) m_reverseSearchLabel->show();
    } else {
        updateReverseSearchMatch(true);
    }
}

void GdbConsoleWidget::updateReverseSearchMatch(bool cycleOlder) {
    int startIdx = cycleOlder ? (m_reverseSearchHistoryIdx - 1) : (m_history.size() - 1);
    if (startIdx >= m_history.size()) startIdx = m_history.size() - 1;
    int foundIdx = -1;
    if (!m_reverseSearchQuery.isEmpty()) {
        for (int i = startIdx; i >= 0; --i) {
            if (m_history.at(i).contains(m_reverseSearchQuery, Qt::CaseInsensitive)) {
                foundIdx = i;
                break;
            }
        }
    } else if (!m_history.isEmpty() && startIdx >= 0) {
        foundIdx = startIdx;
    }
    if (foundIdx >= 0) {
        m_reverseSearchHistoryIdx = foundIdx;
        m_reverseSearchMatch = m_history.at(foundIdx);
        if (m_commandEdit) {
            m_commandEdit->setText(m_reverseSearchMatch);
            m_commandEdit->setCursorPosition(m_commandEdit->text().size());
        }
    } else if (!cycleOlder && m_reverseSearchQuery.isEmpty()) {
        m_reverseSearchMatch = m_reverseSearchPriorInput;
    }
    if (m_reverseSearchLabel) {
        const QString prefix = (foundIdx < 0 && !m_reverseSearchQuery.isEmpty())
                                   ? QStringLiteral("(failed reverse-i-search)")
                                   : QStringLiteral("(reverse-i-search)");
        m_reverseSearchLabel->setText(
            QStringLiteral("%1`%2': %3").arg(prefix, m_reverseSearchQuery, m_reverseSearchMatch));
    }
}

void GdbConsoleWidget::exitReverseSearch(bool restorePrior) {
    if (!m_reverseSearchActive) return;
    m_reverseSearchActive = false;
    if (m_reverseSearchLabel) m_reverseSearchLabel->hide();
    if (m_commandEdit) {
        m_commandEdit->setText(restorePrior ? m_reverseSearchPriorInput : m_reverseSearchMatch);
        m_commandEdit->setCursorPosition(m_commandEdit->text().size());
    }
}

QStringList GdbConsoleWidget::offlineCompletionsForPrefix(const QString& prefix) {
    static const QStringList kDict = {
        QStringLiteral("backtrace"),        QStringLiteral("break"),
        QStringLiteral("catch"),            QStringLiteral("catch catch"),
        QStringLiteral("catch load"),       QStringLiteral("catch rethrow"),
        QStringLiteral("catch syscall"),    QStringLiteral("catch throw"),
        QStringLiteral("catch unload"),     QStringLiteral("checkpoint"),
        QStringLiteral("continue"),         QStringLiteral("delete"),
        QStringLiteral("disable"),          QStringLiteral("disassemble"),
        QStringLiteral("enable"),           QStringLiteral("finish"),
        QStringLiteral("frame"),            QStringLiteral("handle"),
        QStringLiteral("info"),             QStringLiteral("info args"),
        QStringLiteral("info breakpoints"), QStringLiteral("info frame"),
        QStringLiteral("info functions"),   QStringLiteral("info handle"),
        QStringLiteral("info locals"),      QStringLiteral("info registers"),
        QStringLiteral("info sharedlibrary"), QStringLiteral("info signals"),
        QStringLiteral("info threads"),     QStringLiteral("info variables"),
        QStringLiteral("jump"),             QStringLiteral("list"),
        QStringLiteral("next"),             QStringLiteral("nexti"),
        QStringLiteral("print"),            QStringLiteral("ptype"),
        QStringLiteral("rbreak"),           QStringLiteral("record"),
        QStringLiteral("record full"),      QStringLiteral("record stop"),
        QStringLiteral("restart"),          QStringLiteral("return"),
        QStringLiteral("run"),              QStringLiteral("set"),
        QStringLiteral("set pagination off"), QStringLiteral("signal"),
        QStringLiteral("step"),             QStringLiteral("stepi"),
        QStringLiteral("tbreak"),           QStringLiteral("thread"),
        QStringLiteral("until"),            QStringLiteral("watch"),
        QStringLiteral("x/")};
    const QString clean = prefix.trimmed();
    QStringList out;
    for (const QString& entry : kDict) {
        if (prefix.startsWith(entry + QLatin1Char(' '), Qt::CaseInsensitive)) continue;
        if (entry.startsWith(prefix, Qt::CaseInsensitive) ||
            (!clean.isEmpty() && !prefix.endsWith(QLatin1Char(' ')) && entry.startsWith(clean, Qt::CaseInsensitive) && !entry.contains(QLatin1Char(' ')))) {
            if (!out.contains(entry)) out.append(entry);
        }
    }
    return out;
}

void GdbConsoleWidget::triggerTabCompletion() {
    if (!m_commandEdit) return;
    if (m_completionPopup && m_completionPopup->isVisible() && m_completionPopup->count() > 0) {
        acceptSelectedCompletion();
        return;
    }
    const QString prefix = m_commandEdit->text().left(m_commandEdit->cursorPosition());
    m_awaitingLiveCompletion = true;
    emit completionRequested(prefix);
    if (m_awaitingLiveCompletion) {
        m_awaitingLiveCompletion = false;
        applyCompletionCandidates(prefix, offlineCompletionsForPrefix(prefix));
    }
}

void GdbConsoleWidget::onConsoleCompletionsReady(const QString& prefix, const QStringList& completions) {
    m_awaitingLiveCompletion = false;
    const QStringList candidates = completions.isEmpty() ? offlineCompletionsForPrefix(prefix) : completions;
    applyCompletionCandidates(prefix, candidates);
}

void GdbConsoleWidget::applyCompletionCandidates(const QString& prefix, const QStringList& candidates) {
    if (!m_commandEdit || candidates.isEmpty()) {
        hideCompletionPopup();
        return;
    }
    if (candidates.size() == 1) {
        m_commandEdit->setText(candidates.first());
        m_commandEdit->setCursorPosition(m_commandEdit->text().size());
        hideCompletionPopup();
        return;
    }
    // Compute longest common prefix across all candidates.
    QString lcp = candidates.first();
    for (int i = 1; i < candidates.size(); ++i) {
        const QString& s = candidates.at(i);
        int j = 0;
        while (j < lcp.size() && j < s.size() && lcp.at(j).toLower() == s.at(j).toLower()) ++j;
        lcp.truncate(j);
    }
    if (lcp.size() > prefix.size()) {
        m_commandEdit->setText(lcp);
        m_commandEdit->setCursorPosition(m_commandEdit->text().size());
    }
    if (m_completionPopup) {
        m_completionPopup->clear();
        m_completionPopup->addItems(candidates);
        m_completionPopup->setCurrentRow(0);
        const int popW = qMax(240, qMin(480, width() - 16));
        const int popH = qMin(160, qMax(48, static_cast<int>(candidates.size()) * 22 + 8));
        const int popY = qMax(0, height() - (m_promptBar ? m_promptBar->height() : 26) - popH - 2);
        m_completionPopup->setGeometry(8, popY, popW, popH);
        m_completionPopup->raise();
        m_completionPopup->show();
    }
}

void GdbConsoleWidget::acceptSelectedCompletion() {
    if (!m_completionPopup || !m_completionPopup->isVisible() || !m_commandEdit) return;
    if (const QListWidgetItem* item = m_completionPopup->currentItem()) {
        m_commandEdit->setText(item->text());
        m_commandEdit->setCursorPosition(m_commandEdit->text().size());
    }
    hideCompletionPopup();
}

void GdbConsoleWidget::hideCompletionPopup() {
    if (m_completionPopup) m_completionPopup->hide();
}

bool GdbConsoleWidget::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_commandEdit && event && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        const int key = ke->key();
        const auto mods = ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);

        if (key == Qt::Key_R && mods == Qt::ControlModifier) {
            startOrCycleReverseSearch();
            return true;
        }
        if (m_reverseSearchActive) {
            if (key == Qt::Key_Escape || (key == Qt::Key_G && mods == Qt::ControlModifier)) {
                exitReverseSearch(true);
                return true;
            }
            if (key == Qt::Key_Tab || key == Qt::Key_Right || key == Qt::Key_Left) {
                exitReverseSearch(false);
                return true;
            }
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                onCommandReturnPressed();
                return true;
            }
            if (key == Qt::Key_Backspace) {
                if (!m_reverseSearchQuery.isEmpty()) m_reverseSearchQuery.chop(1);
                updateReverseSearchMatch(false);
                return true;
            }
            const QString txt = ke->text();
            if (!txt.isEmpty() && txt.at(0).isPrint() && !(mods & Qt::ControlModifier)) {
                m_reverseSearchQuery += txt;
                updateReverseSearchMatch(false);
                return true;
            }
        }
        if (key == Qt::Key_Tab && mods == Qt::NoModifier) {
            triggerTabCompletion();
            return true;
        }
        if (m_completionPopup && m_completionPopup->isVisible()) {
            if (key == Qt::Key_Escape) {
                hideCompletionPopup();
                return true;
            }
            if (key == Qt::Key_Down || key == Qt::Key_Up) {
                const int cnt = m_completionPopup->count();
                if (cnt > 0) {
                    const int cur = m_completionPopup->currentRow();
                    const int nxt = (key == Qt::Key_Down) ? ((cur + 1) % cnt) : ((cur - 1 + cnt) % cnt);
                    m_completionPopup->setCurrentRow(nxt);
                }
                return true;
            }
            if (key == Qt::Key_Return || key == Qt::Key_Enter) {
                acceptSelectedCompletion();
                return true;
            }
            if (!ke->text().isEmpty()) hideCompletionPopup();
        }
        if (key == Qt::Key_Up && mods == Qt::NoModifier) {
            navigateHistory(-1);
            return true;
        }
        if (key == Qt::Key_Down && mods == Qt::NoModifier) {
            navigateHistory(1);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

/**
 * @brief Re-applies viewport filtering when the substring search box changes.
 */
void GdbConsoleWidget::onFilterTextChanged(const QString& /*text*/) { applyFilter(); }

/**
 * @brief Re-applies viewport filtering when the target MPI rank selector changes.
 */
void GdbConsoleWidget::onRankFilterChanged(int /*index*/) { applyFilter(); }

/**
 * @brief Re-renders retained log entries when Raw MI visibility is toggled.
 */
void GdbConsoleWidget::onRawMiToggled(bool /*checked*/) { applyFilter(); }

/**
 * @brief Replays retained logs against updated filter predicates in a single document update.
 */
void GdbConsoleWidget::applyFilter() {
    if (m_flushTimer) m_flushTimer->stop();
    m_pendingBatch.clear();
    const int selectedRank = m_rankCombo->currentData().toInt();
    const QString filterText = m_filterEdit->text();
    const bool showRaw = m_rawMiCheckbox->isChecked();

    QStringList matching;
    for (const auto& log : m_logs) {
        if (selectedRank != -1 && selectedRank != log.rank) continue;
        // Preserve formatted REPL/sanitizer lines verbatim regardless of Raw MI checkbox state.
        const QString displayStr =
            log.isRepl ? log.text : formatLogEntry(log.rank, log.text, log.isInput, showRaw);
        if (displayStr.isEmpty()) continue;
        if (filterText.isEmpty() || displayStr.contains(filterText, Qt::CaseInsensitive)) {
            matching.append(displayStr);
        }
    }
    m_consoleEdit->setUpdatesEnabled(false);
    m_consoleEdit->setPlainText(matching.join(QLatin1Char('\n')));
    if (QScrollBar* sb = m_consoleEdit->verticalScrollBar()) sb->setValue(sb->maximum());
    m_consoleEdit->setUpdatesEnabled(true);
}

}  // namespace gridlock::ui

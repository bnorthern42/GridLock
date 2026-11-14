/**
 * @file CallStackDockWidget.cpp
 * @brief Implementation of the multi-frame Call Stack and OS/OpenMP Thread selector dock widget.
 *
 * Renders up to 64 unwound stack frames (`#`, `Function`, `Location`, `Address`) alongside
 * an interactive OS/OpenMP thread selector dropdown, applying `ThemeManager` dark and light
 * palettes cleanly with `>= 12px` typography and unclipped column headers.
 */

#include "CallStackDockWidget.hpp"

#include "../../core/managers/ThemeManager.hpp"

#include <QAbstractItemView>
#include <QBrush>
#include <QColor>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace gridlock::ui {

/**
 * @brief Constructs CallStackDockWidget, builds UI controls, and applies the active theme.
 * @param parent Optional parent widget.
 */
CallStackDockWidget::CallStackDockWidget(QWidget* parent) : QWidget(parent) {
  setupUi();
  reloadStyle();
}

/**
 * @brief Builds the 26px Thread selector header strip and 4-column `#callStackTable` view.
 */
void CallStackDockWidget::setupUi() {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  // Unified 26px header bar matching VariablesDockWidget chrome metrics.
  m_headerBar = new QWidget(this);
  m_headerBar->setObjectName(QStringLiteral("callStackHeaderBar"));
  m_headerBar->setFixedHeight(26);

  auto* headerLayout = new QHBoxLayout(m_headerBar);
  headerLayout->setContentsMargins(6, 1, 6, 1);
  headerLayout->setSpacing(6);

  auto* threadLabel = new QLabel(QStringLiteral("Thread:"), m_headerBar);
  m_threadCombo = new QComboBox(m_headerBar);
  m_threadCombo->setObjectName(QStringLiteral("threadSelectorCombo"));
  m_threadCombo->setFixedHeight(22);
  m_threadCombo->addItem(QStringLiteral("Thread #1 [idle]"), 1);

  headerLayout->addWidget(threadLabel);
  headerLayout->addWidget(m_threadCombo, 1);
  rootLayout->addWidget(m_headerBar);

  // Configure borderless, alternating-row 4-column Call Stack QTableWidget.
  m_table = new QTableWidget(0, 4, this);
  m_table->setObjectName(QStringLiteral("callStackTable"));
  m_table->setHorizontalHeaderLabels({QStringLiteral("#"),
                                      QStringLiteral("Function"),
                                      QStringLiteral("Location"),
                                      QStringLiteral("Address")});
  m_table->setFrameShape(QFrame::NoFrame);
  m_table->setAlternatingRowColors(true);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->verticalHeader()->setVisible(false);
  m_table->verticalHeader()->setDefaultSectionSize(22);

  // Enforce >= 48px minimum column section width for workstation table cohesion compliance.
  QHeaderView* hdr = m_table->horizontalHeader();
  hdr->setVisible(true);
  hdr->setMinimumSectionSize(48);
  hdr->setStretchLastSection(true);
  hdr->setSectionResizeMode(0, QHeaderView::Interactive);
  hdr->setSectionResizeMode(1, QHeaderView::Stretch);
  hdr->setSectionResizeMode(2, QHeaderView::Interactive);
  hdr->setSectionResizeMode(3, QHeaderView::Interactive);
  m_table->setColumnWidth(0, 52);
  m_table->setColumnWidth(1, 110);
  m_table->setColumnWidth(2, 112);
  m_table->setColumnWidth(3, 96);

  rootLayout->addWidget(m_table, 1);

  // Connect OS/OpenMP thread dropdown changes and single/double-click + Enter row activations.
  connect(m_threadCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &CallStackDockWidget::onThreadComboChanged);
  connect(m_table, &QTableWidget::cellClicked,
          this, &CallStackDockWidget::onFrameRowActivated);
  connect(m_table, &QTableWidget::cellActivated,
          this, &CallStackDockWidget::onFrameRowActivated);
}

/**
 * @brief Populates `#threadSelectorCombo` with live OS/OpenMP thread states without spurious signals.
 * @param threads Thread list reported by `IBackendCoordinator::threadsUpdated`.
 */
void CallStackDockWidget::setThreads(const QVector<ThreadEntry>& threads) {
  m_threads = threads;
  const QSignalBlocker blocker(m_threadCombo);
  m_threadCombo->clear();

  // Reset to a single placeholder entry when no inferior threads are currently reported.
  if (threads.isEmpty()) {
    m_activeThreadId = 1;
    m_threadCombo->addItem(QStringLiteral("Thread #1 [idle]"), 1);
    return;
  }

  // Format each OS/OpenMP thread with its ID, name, and execution state (`stopped` / `running`).
  int currentIndex = 0;
  for (int i = 0; i < threads.size(); ++i) {
    const ThreadEntry& th = threads.at(i);
    const QString cleanName = th.name.isEmpty() ? QStringLiteral("main") : th.name;
    const QString cleanState = th.state.isEmpty() ? QStringLiteral("stopped") : th.state;
    const QString label = QStringLiteral("Thread #%1 [%2] (%3)")
                              .arg(th.threadId)
                              .arg(cleanName, cleanState);
    m_threadCombo->addItem(label, th.threadId);
    // Attach top-frame summary tooltip when provided by GDB `-thread-info` or DAP `threads`.
    if (!th.topFrameSummary.isEmpty()) {
      m_threadCombo->setItemData(i, th.topFrameSummary, Qt::ToolTipRole);
    }
    if (th.isCurrent) {
      currentIndex = i;
      m_activeThreadId = th.threadId;
    }
  }
  // Synchronize combo selection with the debugger's active thread ID.
  m_threadCombo->setCurrentIndex(currentIndex);
  m_activeThreadId = m_threadCombo->itemData(currentIndex).toInt();
}

/**
 * @brief Populates `#callStackTable` with unwound frames and marks @p activeLevel as active.
 * @param frames Ordered call-stack frames from innermost (`level == 0`) to entry frame.
 * @param activeLevel Zero-based frame level currently inspected.
 */
void CallStackDockWidget::setStackFrames(const QVector<StackFrameEntry>& frames,
                                         int activeLevel) {
  m_frames = frames;
  m_activeFrameLevel = (activeLevel >= 0) ? activeLevel : 0;

  // Block table item signals while rebuilding rows to avoid spurious frame activations.
  const QSignalBlocker blocker(m_table);
  m_table->setRowCount(frames.size());

  // Populate #, Function, Location (basename:line), and 64-bit hex Address columns.
  for (int row = 0; row < frames.size(); ++row) {
    const StackFrameEntry& f = frames.at(row);
    const QString funcText = f.function.isEmpty() ? QStringLiteral("??") : f.function;
    const QString baseFile = f.file.isEmpty() ? QString() : QFileInfo(f.file).fileName();
    const QString locText = (!baseFile.isEmpty() && f.line > 0)
                                ? QStringLiteral("%1:%2").arg(baseFile).arg(f.line)
                                : (!baseFile.isEmpty() ? baseFile : QStringLiteral("<unknown>:0"));
    const QString addrText = QStringLiteral("0x%1").arg(f.address, 8, 16, QLatin1Char('0'));

    auto* levelItem = new QTableWidgetItem(QStringLiteral("#%1").arg(f.level));
    auto* funcItem = new QTableWidgetItem(funcText);
    auto* locItem = new QTableWidgetItem(locText);
    auto* addrItem = new QTableWidgetItem(addrText);

    // Expose full canonical file path, line number, and hex PC in row hover tooltips.
    const QString fullTip = (!f.file.isEmpty() && f.line > 0)
                                ? QStringLiteral("#%1 %2 at %3:%4 (%5)")
                                      .arg(f.level)
                                      .arg(funcText, f.file)
                                      .arg(f.line)
                                      .arg(addrText)
                                : QStringLiteral("#%1 %2 (%3)").arg(f.level).arg(funcText, addrText);
    for (QTableWidgetItem* it : {levelItem, funcItem, locItem, addrItem}) {
      it->setToolTip(fullTip);
    }

    m_table->setItem(row, 0, levelItem);
    m_table->setItem(row, 1, funcItem);
    m_table->setItem(row, 2, locItem);
    m_table->setItem(row, 3, addrItem);
  }

  refreshActiveRowHighlight();
}

/**
 * @brief Clears cached stack/thread models and empties `#callStackTable` on session termination.
 */
void CallStackDockWidget::clearFrames() {
  m_frames.clear();
  m_threads.clear();
  m_activeThreadId = 1;
  m_activeFrameLevel = 0;

  // Clear all stack-frame table rows cleanly without emitting selection events.
  const QSignalBlocker tableBlocker(m_table);
  m_table->setRowCount(0);

  // Restore idle placeholder item in `#threadSelectorCombo` after session teardown.
  const QSignalBlocker comboBlocker(m_threadCombo);
  m_threadCombo->clear();
  m_threadCombo->addItem(QStringLiteral("Thread #1 [idle]"), 1);
}

/**
 * @brief Updates the `▶` frame indicator, font weight, and accent tint for `m_activeFrameLevel`.
 */
void CallStackDockWidget::refreshActiveRowHighlight() {
  // Resolve foreground and accent colors from ThemeManager so Dark and Light themes stay legible.
  const auto& tm = gridlock::core::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  const QColor normalFg(pal.textHex);
  const QColor activeFg(pal.accentHex);

  const QSignalBlocker blocker(m_table);
  int selectedRow = -1;
  // Decorate the active caller frame with a `▶` glyph and bold typography.
  for (int row = 0; row < m_frames.size() && row < m_table->rowCount(); ++row) {
    const StackFrameEntry& f = m_frames.at(row);
    const bool isActive = (f.level == m_activeFrameLevel);
    if (isActive) selectedRow = row;

    if (QTableWidgetItem* lvl = m_table->item(row, 0)) {
      lvl->setText(isActive ? QStringLiteral("▶ #%1").arg(f.level)
                            : QStringLiteral("#%1").arg(f.level));
    }
    for (int col = 0; col < 4; ++col) {
      if (QTableWidgetItem* it = m_table->item(row, col)) {
        QFont fnt = it->font();
        fnt.setBold(isActive);
        it->setFont(fnt);
        it->setForeground(QBrush(isActive && col == 0 ? activeFg : normalFg));
      }
    }
  }
  if (selectedRow >= 0) m_table->selectRow(selectedRow);
}

/**
 * @brief Emits `threadSelected(threadId)` when the user chooses a thread in `#threadSelectorCombo`.
 * @param index Selected dropdown index.
 */
void CallStackDockWidget::onThreadComboChanged(int index) {
  if (index < 0 || index >= m_threadCombo->count()) return;
  const int threadId = m_threadCombo->itemData(index).toInt();
  if (threadId <= 0) return;
  m_activeThreadId = threadId;
  m_activeFrameLevel = 0;
  emit threadSelected(threadId);
}

/**
 * @brief Marks @p row as the active caller frame and emits `frameActivated(...)`.
 * @param row Zero-based row index inside `#callStackTable`.
 * @param column Zero-based column index (unused).
 */
void CallStackDockWidget::onFrameRowActivated(int row, int column) {
  Q_UNUSED(column);
  if (row < 0 || row >= m_frames.size()) return;
  const StackFrameEntry& entry = m_frames.at(row);
  m_activeFrameLevel = entry.level;
  refreshActiveRowHighlight();
  emit frameActivated(m_activeThreadId, entry.level, entry.file, entry.line, entry.address);
}

/**
 * @brief Applies active `ThemeManager` dark or light palette tokens to header, combobox, and table.
 */
void CallStackDockWidget::reloadStyle() {
  const auto& tm = gridlock::core::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  const QString altRowHex = pal.isDark ? QStringLiteral("#1c2026") : QStringLiteral("#f1f4f8");

  // Style header bar, thread combobox, call-stack table, and section headers with >= 12px fonts.
  setStyleSheet(QStringLiteral(
      "#callStackHeaderBar { background-color: %1; border-bottom: 1px solid %2; } "
      "#callStackHeaderBar QLabel { color: %3; font-size: 12px; font-weight: 600; background: transparent; } "
      "QComboBox#threadSelectorCombo { background-color: %4; color: %3; border: 1px solid %2; "
      "border-radius: 3px; padding: 0px 6px; min-height: 20px; max-height: 22px; font-size: 12px; } "
      "QComboBox#threadSelectorCombo:focus { border: 1px solid %5; } "
      "QTableWidget#callStackTable { background-color: %4; alternate-background-color: %6; "
      "color: %3; gridline-color: %2; border: none; font-family: 'JetBrains Mono', monospace; font-size: 12px; } "
      "QTableWidget#callStackTable::item:selected { background-color: %5; color: #ffffff; } "
      "QHeaderView::section { background-color: %1; color: %3; border: none; "
      "border-right: 1px solid %2; border-bottom: 1px solid %2; padding: 3px 6px; "
      "font-size: 12px; font-weight: 600; }")
      .arg(pal.headerHex, pal.borderHex, pal.textHex, pal.canvasHex, pal.accentHex, altRowHex));

  refreshActiveRowHighlight();
}

}  // namespace gridlock::ui

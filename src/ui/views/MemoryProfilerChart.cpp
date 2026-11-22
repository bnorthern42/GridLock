/**
 * @file MemoryProfilerChart.cpp
 * @brief Interactive MemoryTreemapCanvas layout/rendering, color palettes, and breakdown table population.
 *
 * Implements the proportional stacked ribbon and balanced 2D treemap canvas
 * (`MemoryTreemapCanvas`), scientific/Okabe-Ito color scheme interpolation,
 * single-PID and multi-rank refresh aggregation, numeric table sorting,
 * substring filtering, and live ThemeManager / scheme property synchronization.
 */

#include "MemoryProfilerWidget.hpp"

#include <algorithm>
#include <cmath>

#include <QCoreApplication>
#include <QDynamicPropertyChangeEvent>
#include <QFontMetrics>
#include <QHeaderView>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QToolTip>

#include "../../core/managers/ConfigManager.hpp"
#include "../../core/managers/ThemeManager.hpp"

namespace gridlock::ui {

namespace {

/** @brief QTableWidgetItem subclass sorting numerically by `Qt::UserRole` `double` values. */
class NumericMegabyteItem : public QTableWidgetItem {
 public:
  using QTableWidgetItem::QTableWidgetItem;
  bool operator<(const QTableWidgetItem& other) const override {
    const QVariant lv = data(Qt::UserRole), rv = other.data(Qt::UserRole);
    return (lv.isValid() && rv.isValid()) ? (lv.toDouble() < rv.toDouble()) : QTableWidgetItem::operator<(other);
  }
};

QColor interpolatePalette(const QVector<QColor>& s, double t) {
  if (s.isEmpty()) return QColor("#3b82f6");
  const double c = std::clamp(t, 0.0, 1.0) * static_cast<double>(s.size() - 1);
  const int lo = static_cast<int>(std::floor(c)), hi = std::min(static_cast<int>(s.size()) - 1, lo + 1);
  const double f = c - static_cast<double>(lo);
  return QColor::fromRgbF(s[lo].redF() + (s[hi].redF() - s[lo].redF()) * f,
                          s[lo].greenF() + (s[hi].greenF() - s[lo].greenF()) * f,
                          s[lo].blueF() + (s[hi].blueF() - s[lo].blueF()) * f);
}

QColor colorForEntryImpl(int index, int totalCount, const MemoryProfileEntry& entry, const QString& scheme) {
  const double rankT = (totalCount > 1) ? static_cast<double>(index) / static_cast<double>(totalCount - 1) : 0.0;
  const uint h = qHash(entry.name + QLatin1Char('|') + entry.topSymbolOrRange);
  if (scheme == QLatin1String("Viridis")) return interpolatePalette({QColor("#440154"), QColor("#3b528b"), QColor("#21918c"), QColor("#5ec962"), QColor("#fde725")}, rankT);
  if (scheme == QLatin1String("Plasma")) return interpolatePalette({QColor("#0d0887"), QColor("#9c179e"), QColor("#ed7953"), QColor("#f0f921")}, rankT);
  if (scheme == QLatin1String("Ocean")) return interpolatePalette({QColor("#0f2c59"), QColor("#0891b2"), QColor("#10b981"), QColor("#f59e0b")}, rankT);
  if (scheme == QLatin1String("HighContrast")) {
    static const QVector<QColor> kOkabe = {QColor("#0072b2"), QColor("#e69f00"), QColor("#009e73"), QColor("#cc79a7"), QColor("#56b4e9"), QColor("#d55e00"), QColor("#f0e442"), QColor("#332288")};
    return kOkabe.at(static_cast<int>((h + static_cast<uint>(index)) % static_cast<uint>(kOkabe.size())));
  }
  static const QVector<QColor> kDistinct = {QColor("#0d9488"), QColor("#4f46e5"), QColor("#e11d48"), QColor("#d97706"), QColor("#0284c7"), QColor("#7c3aed"), QColor("#059669"), QColor("#db2777"), QColor("#2563eb"), QColor("#ca8a04")};
  const QColor base = kDistinct.at(static_cast<int>((h + static_cast<uint>(index)) % static_cast<uint>(kDistinct.size())));
  const int shift = ((index % 3) - 1) * 7;
  return shift >= 0 ? base.lighter(100 + shift) : base.darker(100 - shift);
}

}  // namespace

MemoryTreemapCanvas::MemoryTreemapCanvas(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("memoryTreemapCanvas"));
  setMouseTracking(true);
  setMinimumHeight(175);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void MemoryTreemapCanvas::setEntries(const QVector<MemoryProfileEntry>& entries, uint64_t totalBytes) {
  m_entries = entries; m_totalBytes = totalBytes; m_hoveredEntry = -1;
  if (m_selectedEntry >= m_entries.size()) m_selectedEntry = -1;
  rebuildLayout(); update();
}

void MemoryTreemapCanvas::clear() {
  m_entries.clear(); m_barTiles.clear(); m_treemapTiles.clear();
  m_totalBytes = 0; m_hoveredEntry = -1; m_selectedEntry = -1; update();
}

void MemoryTreemapCanvas::setSelectedEntryIndex(int entryIndex) {
  if (m_selectedEntry != entryIndex) { m_selectedEntry = entryIndex; update(); }
}

/** @brief Recomputes top proportional stacked-bar slices and 2D balanced treemap rectangles. */
void MemoryTreemapCanvas::rebuildLayout() {
  m_barTiles.clear();
  m_treemapTiles.clear();
  if (m_entries.isEmpty() || m_totalBytes == 0 || width() < 24 || height() < 48) return;

  const QRectF barBounds(6.0, 6.0, std::max(16.0, width() - 12.0), 22.0);
  double curX = barBounds.left();
  for (int i = 0; i < m_entries.size(); ++i) {
    const double frac = static_cast<double>(m_entries[i].liveBytes) / static_cast<double>(m_totalBytes);
    const double sliceW = (i + 1 == m_entries.size()) ? std::max(0.0, barBounds.right() - curX)
                                                      : std::max(1.0, barBounds.width() * frac);
    if (curX + sliceW > barBounds.right()) {
      m_barTiles.push_back({QRectF(curX, barBounds.top(), std::max(0.0, barBounds.right() - curX), barBounds.height()), i});
      break;
    }
    m_barTiles.push_back({QRectF(curX, barBounds.top(), sliceW, barBounds.height()), i});
    curX += sliceW;
  }

  const QRectF treeBounds(6.0, 34.0, std::max(16.0, width() - 12.0), std::max(24.0, height() - 40.0));
  subdivideTreemap(0, static_cast<int>(m_entries.size()), treeBounds);
}

/** @brief Balanced binary-split treemap layout producing readable low-aspect-ratio cards. */
void MemoryTreemapCanvas::subdivideTreemap(int beginIdx, int endIdx, const QRectF& bounds) {
  if (beginIdx >= endIdx || bounds.width() <= 1.0 || bounds.height() <= 1.0) return;
  if (endIdx - beginIdx == 1) {
    m_treemapTiles.push_back({bounds.adjusted(1.5, 1.5, -1.5, -1.5), beginIdx});
    return;
  }
  uint64_t sum = 0, halfSum = 0;
  for (int i = beginIdx; i < endIdx; ++i) sum += m_entries[i].liveBytes;
  if (sum == 0) { m_treemapTiles.push_back({bounds.adjusted(1.5, 1.5, -1.5, -1.5), beginIdx}); return; }

  int splitIdx = beginIdx + 1;
  for (int i = beginIdx; i < endIdx - 1; ++i) {
    halfSum += m_entries[i].liveBytes;
    splitIdx = i + 1;
    if (halfSum * 2ULL >= sum) break;
  }
  const double ratio = std::clamp(static_cast<double>(halfSum) / static_cast<double>(sum), 0.08, 0.92);
  if (bounds.width() >= bounds.height()) {
    const double leftW = bounds.width() * ratio;
    subdivideTreemap(beginIdx, splitIdx, QRectF(bounds.left(), bounds.top(), leftW, bounds.height()));
    subdivideTreemap(splitIdx, endIdx, QRectF(bounds.left() + leftW, bounds.top(), bounds.width() - leftW, bounds.height()));
  } else {
    const double topH = bounds.height() * ratio;
    subdivideTreemap(beginIdx, splitIdx, QRectF(bounds.left(), bounds.top(), bounds.width(), topH));
    subdivideTreemap(splitIdx, endIdx, QRectF(bounds.left(), bounds.top() + topH, bounds.width(), bounds.height() - topH));
  }
}

int MemoryTreemapCanvas::tileAtPosition(const QPointF& pos) const {
  for (const auto& t : m_treemapTiles) if (t.rect.contains(pos)) return t.entryIndex;
  for (const auto& t : m_barTiles) if (t.rect.contains(pos)) return t.entryIndex;
  return -1;
}

/** @brief Renders the proportional stacked bar and 2D balanced treemap cards. */
void MemoryTreemapCanvas::paintEvent(QPaintEvent* /*event*/) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing, true);
  auto& tm = gridlock::core::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  p.fillRect(rect(), QColor(pal.canvasHex));

  if (m_entries.isEmpty() || m_totalBytes == 0) {
    QColor mutedText(pal.textHex); mutedText.setAlpha(165);
    p.setPen(mutedText);
    QFont emptyFont = font(); emptyFont.setPixelSize(12); p.setFont(emptyFont);
    p.drawText(rect(), Qt::AlignCenter,
               QStringLiteral("No live rank memory sampled yet — pause a rank or click 'Refresh Now' to inspect library & heap usage."));
    return;
  }

  for (const auto& tile : m_barTiles) {
    if (tile.entryIndex < 0 || tile.entryIndex >= m_entries.size()) continue;
    p.setBrush(Qt::NoBrush);
    const auto& e = m_entries[tile.entryIndex];
    p.fillRect(tile.rect, (tile.entryIndex == m_hoveredEntry) ? e.color.lighter(115) : e.color);
    p.setPen(QPen(QColor(pal.canvasHex), 1.0)); p.drawRect(tile.rect);
  }

  QFont titleFont = font(); titleFont.setPixelSize(11); titleFont.setBold(true);
  QFont subFont = font(); subFont.setPixelSize(10);
  const QFontMetrics fmTitle(titleFont), fmSub(subFont);

  for (const auto& tile : m_treemapTiles) {
    if (tile.entryIndex < 0 || tile.entryIndex >= m_entries.size()) continue;
    const auto& e = m_entries[tile.entryIndex];
    const bool hovered = (tile.entryIndex == m_hoveredEntry), selected = (tile.entryIndex == m_selectedEntry);

    p.setBrush(hovered ? e.color.lighter(114) : e.color);
    p.setPen(QPen(selected ? QColor("#ffffff") : QColor(pal.borderHex), selected ? 2.0 : 1.0));
    p.drawRoundedRect(tile.rect, 3.0, 3.0);

    if (tile.rect.width() >= 48.0 && tile.rect.height() >= 22.0) {
      const QRectF textBox = tile.rect.adjusted(5.0, 3.0, -5.0, -3.0);
      const double lum = 0.2126 * e.color.redF() + 0.7152 * e.color.greenF() + 0.0722 * e.color.blueF();
      const QColor fg = (lum > 0.56) ? QColor("#111827") : QColor("#ffffff");
      const QColor subFg = (lum > 0.56) ? QColor(17, 24, 39, 220) : QColor(255, 255, 255, 225);
      p.setPen(fg); p.setFont(titleFont);
      p.drawText(QRectF(textBox.left(), textBox.top(), textBox.width(), fmTitle.height() + 2),
                 Qt::AlignLeft | Qt::AlignVCenter, fmTitle.elidedText(e.name, Qt::ElideMiddle, static_cast<int>(textBox.width())));
      if (textBox.height() >= 32.0) {
        const double mb = static_cast<double>(e.liveBytes) / (1024.0 * 1024.0);
        const double pct = (100.0 * static_cast<double>(e.liveBytes)) / static_cast<double>(m_totalBytes);
        p.setFont(subFont); p.setPen(subFg);
        p.drawText(QRectF(textBox.left(), textBox.top() + fmTitle.height() + 2, textBox.width(), fmSub.height() + 2),
                   Qt::AlignLeft | Qt::AlignVCenter, fmSub.elidedText(QStringLiteral("%1 MB (%2%)").arg(mb, 0, 'f', 2).arg(pct, 0, 'f', 1), Qt::ElideRight, static_cast<int>(textBox.width())));
      }
    }
  }
}

void MemoryTreemapCanvas::resizeEvent(QResizeEvent* event) { QWidget::resizeEvent(event); rebuildLayout(); }

void MemoryTreemapCanvas::mouseMoveEvent(QMouseEvent* event) {
  const int hit = tileAtPosition(event->position());
  if (hit != m_hoveredEntry) { m_hoveredEntry = hit; update(); }
  if (hit >= 0 && hit < m_entries.size() && m_totalBytes > 0) {
    const auto& e = m_entries[hit];
    const double mb = static_cast<double>(e.liveBytes) / (1024.0 * 1024.0);
    const double dirtyMb = static_cast<double>(e.dirtyBytes) / (1024.0 * 1024.0);
    const double pct = (100.0 * static_cast<double>(e.liveBytes)) / static_cast<double>(m_totalBytes);
    const QString tip = QStringLiteral("%1\nCategory: %2\nLive / RSS: %3 MB (%4%)\nPrivate Dirty: %5 MB\nDetail: %6")
                            .arg(e.name, e.category).arg(mb, 0, 'f', 2).arg(pct, 0, 'f', 1).arg(dirtyMb, 0, 'f', 2).arg(e.topSymbolOrRange);
    QToolTip::showText(event->globalPosition().toPoint(), tip, this);
  } else {
    QToolTip::hideText();
  }
  QWidget::mouseMoveEvent(event);
}

void MemoryTreemapCanvas::leaveEvent(QEvent* event) {
  if (m_hoveredEntry != -1) { m_hoveredEntry = -1; update(); }
  QWidget::leaveEvent(event);
}

void MemoryTreemapCanvas::mousePressEvent(QMouseEvent* event) {
  const int hit = tileAtPosition(event->position());
  setSelectedEntryIndex(hit);
  if (hit >= 0 && hit < m_entries.size()) emit entryClicked(m_entries[hit].name, m_entries[hit].startAddress);
  QWidget::mousePressEvent(event);
}

void MemoryTreemapCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
  const int hit = tileAtPosition(event->position());
  if (hit >= 0 && hit < m_entries.size() && m_entries[hit].startAddress != 0ULL) emit addressActivated(m_entries[hit].startAddress);
  QWidget::mouseDoubleClickEvent(event);
}

int MemoryProfilerWidget::treemapRectCount() const { return m_canvas ? m_canvas->treemapRectCount() : 0; }

void MemoryProfilerWidget::clear() {
  m_lastPid = 0; m_entries.clear();
  if (m_canvas) m_canvas->clear();
  if (m_table) m_table->setRowCount(0);
  if (m_searchEdit) m_searchEdit->clear();
  if (m_summaryLabel) m_summaryLabel->setText(QStringLiteral("TOTAL: 0.00 MB"));
}

void MemoryProfilerWidget::refreshForPid(qint64 pid) {
  if (pid <= 0) return;
  m_lastPid = pid;
  populateFromEntries(collectEntriesForPid(pid), QStringLiteral("PID %1").arg(pid));
}

void MemoryProfilerWidget::refreshAllRanks() {
  if (!m_pidGetter) return;
  QMap<QString, MemoryProfileEntry> merged; int sampledRanks = 0;
  for (int r = 0; r < m_rankCount; ++r) {
    const qint64 pid = m_pidGetter(r);
    if (pid <= 0) continue;
    ++sampledRanks;
    for (const auto& e : collectEntriesForPid(pid)) {
      auto& dst = merged[e.category + QLatin1Char('|') + e.name];
      if (dst.name.isEmpty()) dst = e;
      else { dst.liveBytes += e.liveBytes; dst.dirtyBytes += e.dirtyBytes; dst.peakBytes += e.peakBytes; dst.allocCount += e.allocCount; }
    }
  }
  QVector<MemoryProfileEntry> combined;
  for (auto it = merged.constBegin(); it != merged.constEnd(); ++it) combined.push_back(it.value());
  populateFromEntries(combined, QStringLiteral("%1 Ranks").arg(sampledRanks));
}

/** @brief Populates `#memoryTreemapCanvas` and `#memoryBreakdownTable` from collected entries. */
void MemoryProfilerWidget::populateFromEntries(const QVector<MemoryProfileEntry>& rawEntries, const QString& contextLabel) {
  m_entries = rawEntries;
  std::sort(m_entries.begin(), m_entries.end(), [](const MemoryProfileEntry& a, const MemoryProfileEntry& b) { return a.liveBytes > b.liveBytes; });
  const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
  const QString scheme = m_schemeCombo ? m_schemeCombo->currentData().toString() : gridlock::core::ConfigManager::instance().memoryMapColorScheme();
  uint64_t totalBytes = 0;
  for (int i = 0; i < m_entries.size(); ++i) {
    m_entries[i].color = colorForEntry(i, m_entries.size(), m_entries[i], scheme);
    totalBytes += m_entries[i].liveBytes;
  }
  if (m_summaryLabel) {
    m_summaryLabel->setText(QStringLiteral("%1 — TOTAL: %2 (%3 regions)").arg(contextLabel, formatMegabytes(totalBytes)).arg(m_entries.size()));
  }
  if (m_canvas) m_canvas->setEntries(m_entries, totalBytes);
  if (!m_table) return;

  const bool wasSorting = m_table->isSortingEnabled();
  m_table->setSortingEnabled(false); m_table->setRowCount(0);

  for (const auto& e : m_entries) {
    const int row = m_table->rowCount(); m_table->insertRow(row);
    const double mb = static_cast<double>(e.liveBytes) / (1024.0 * 1024.0), dirtyMb = static_cast<double>(e.dirtyBytes) / (1024.0 * 1024.0);
    const double pct = totalBytes > 0 ? (100.0 * static_cast<double>(e.liveBytes)) / static_cast<double>(totalBytes) : 0.0;

    auto* nameItem = new QTableWidgetItem(e.name);
    nameItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(e.startAddress)); nameItem->setForeground(e.color.lighter(isDark ? 130 : 85));
    auto* catItem = new QTableWidgetItem(e.category);
    auto* rssItem = new NumericMegabyteItem(QStringLiteral("%1 MB").arg(mb, 0, 'f', 2)); rssItem->setData(Qt::UserRole, mb); rssItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* dirtyItem = new NumericMegabyteItem(QStringLiteral("%1 MB").arg(dirtyMb, 0, 'f', 2)); dirtyItem->setData(Qt::UserRole, dirtyMb); dirtyItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* pctItem = new NumericMegabyteItem(QStringLiteral("%1%").arg(pct, 0, 'f', 1)); pctItem->setData(Qt::UserRole, pct); pctItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* symItem = new QTableWidgetItem(e.topSymbolOrRange);

    const QList<QTableWidgetItem*> rowItems = {nameItem, catItem, rssItem, dirtyItem, pctItem, symItem};
    for (int c = 0; c < rowItems.size(); ++c) { rowItems[c]->setToolTip(rowItems[c]->text()); m_table->setItem(row, c, rowItems[c]); }
  }
  m_table->setSortingEnabled(wasSorting);
  m_table->sortItems(m_sortColumn, m_sortOrder);
  if (auto* hdr = m_table->horizontalHeader()) hdr->setSortIndicator(m_sortColumn, m_sortOrder);
  applySearchFilter(m_searchEdit ? m_searchEdit->text() : QString());
}

void MemoryProfilerWidget::applySearchFilter(const QString& text) {
  if (!m_table) return;
  const QString q = text.trimmed();
  for (int r = 0; r < m_table->rowCount(); ++r) {
    bool match = q.isEmpty();
    for (int c = 0; c < m_table->columnCount() && !match; ++c)
      if (const auto* it = m_table->item(r, c); it && it->text().contains(q, Qt::CaseInsensitive)) match = true;
    m_table->setRowHidden(r, !match);
  }
}

QColor MemoryProfilerWidget::colorForEntry(int index, int totalCount, const MemoryProfileEntry& entry, const QString& scheme) {
  return colorForEntryImpl(index, totalCount, entry, scheme);
}

void MemoryProfilerWidget::reapplyColors() {
  const QString scheme = m_schemeCombo ? m_schemeCombo->currentData().toString() : gridlock::core::ConfigManager::instance().memoryMapColorScheme();
  const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode(); uint64_t totalBytes = 0;
  for (int i = 0; i < m_entries.size(); ++i) { m_entries[i].color = colorForEntry(i, m_entries.size(), m_entries[i], scheme); totalBytes += m_entries[i].liveBytes; }
  if (m_canvas) m_canvas->setEntries(m_entries, totalBytes);
  if (m_table) for (int r = 0; r < m_table->rowCount(); ++r) if (auto* it = m_table->item(r, 0)) for (const auto& e : m_entries) if (e.name == it->text()) { it->setForeground(e.color.lighter(isDark ? 130 : 85)); break; }
}

bool MemoryProfilerWidget::eventFilter(QObject* watched, QEvent* event) {
  if (watched == QCoreApplication::instance() && event->type() == QEvent::DynamicPropertyChange && static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName() == "gridlock_memmap_scheme_rev") {
    const QString scheme = gridlock::core::ConfigManager::instance().memoryMapColorScheme();
    if (m_schemeCombo) { QSignalBlocker sb(m_schemeCombo); if (const int idx = m_schemeCombo->findData(scheme); idx >= 0) m_schemeCombo->setCurrentIndex(idx); }
    reapplyColors();
  }
  return QWidget::eventFilter(watched, event);
}

void MemoryProfilerWidget::reloadStyle() {
  auto& tm = gridlock::core::ThemeManager::instance(); const auto pal = tm.paletteForTheme(tm.activeThemeId());
  if (auto* bar = findChild<QWidget*>(QStringLiteral("memProfileControlBar"))) bar->setStyleSheet(QStringLiteral("#memProfileControlBar { background-color: %1; border-bottom: 1px solid %2; } #memProfileControlBar QLabel, #memProfileControlBar QCheckBox { color: %3; font-size: 11px; font-weight: 600; } #memProfileControlBar QLineEdit, #memProfileControlBar QComboBox { background-color: %4; color: %3; border: 1px solid %2; border-radius: 2px; padding: 0px 5px; font-size: 11px; } #memProfileControlBar QPushButton { background-color: %1; color: %3; border: 1px solid %2; border-radius: 2px; padding: 1px 8px; font-size: 10px; font-weight: 600; } #memProfileControlBar QPushButton:hover { background-color: %2; }").arg(pal.headerHex, pal.borderHex, pal.textHex, pal.canvasHex));
  if (m_table) m_table->setStyleSheet(QStringLiteral("QTableWidget { background-color: %1; color: %2; gridline-color: %3; border: none; font-family: 'JetBrains Mono', monospace; font-size: 11px; } QHeaderView::section { background-color: %4; color: %2; border: 1px solid %3; padding: 2px 5px; font-size: 11px; font-weight: 600; }").arg(pal.canvasHex, pal.textHex, pal.borderHex, pal.headerHex));
  if (m_canvas) m_canvas->update();
}

}  // namespace gridlock::ui

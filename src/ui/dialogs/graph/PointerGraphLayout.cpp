/**
 * @file PointerGraphLayout.cpp
 * @brief Implementation of Sugiyama-style layered DAG layout, scalar format overrides,
 *        interactive struct/edge scene items, and zoom/minimap QGraphicsView.
 */

#include "PointerGraphLayout.hpp"

#include "../../../core/managers/ThemeManager.hpp"

#include <QEvent>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneHelpEvent>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QQueue>
#include <QRegularExpression>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gridlock::ui {

namespace {
constexpr qreal kCardWidth = 290.0;
constexpr qreal kMaxExpandedCardWidth = 900.0;
constexpr qreal kHorizColWidth = 152.0;
constexpr qreal kHorizBodyHeight = 44.0;
constexpr qreal kHeaderHeight = 30.0;
constexpr qreal kRowHeight = 24.0;
constexpr qreal kHiddenPillHeight = 22.0;
constexpr qreal kFooterPadding = 8.0;

QString stripTrailingQualifiers(QString t) {
  t = t.trimmed();
  static const QRegularExpression rxQual(
      QStringLiteral("(?:\\b(?:const|volatile|restrict|__restrict__|__restrict)\\b|&)+\\s*$"));
  while (true) {
    const QString next = QString(t).remove(rxQual).trimmed();
    if (next == t) break;
    t = next;
  }
  return t;
}

bool canReachIndex(int nodeCount, const QVector<QVector<int>>& adj, int startIdx, int goalIdx) {
  if (startIdx < 0 || goalIdx < 0 || startIdx >= nodeCount || goalIdx >= nodeCount) return false;
  QVector<bool> visited(nodeCount, false);
  QQueue<int> queue;
  queue.enqueue(startIdx);
  visited[startIdx] = true;
  while (!queue.isEmpty()) {
    const int cur = queue.dequeue();
    if (cur == goalIdx) return true;
    for (int next : adj[cur]) {
      if (next >= 0 && next < nodeCount && !visited[next]) {
        visited[next] = true;
        queue.enqueue(next);
      }
    }
  }
  return false;
}
}  // namespace

bool isNullPointerValue(const QString& v) {
  const QString t = v.trimmed().section(QLatin1Char(' '), 0, 0).toLower();
  if (t == QLatin1String("nullptr") || t == QLatin1String("null")) return true;
  if (!t.startsWith(QLatin1String("0x"))) return false;
  bool ok = false;
  return t.mid(2).toULongLong(&ok, 16) == 0ULL && ok;
}

bool isPointerTypeString(const QString& rawType) {
  const QString t = stripTrailingQualifiers(rawType);
  return t.endsWith(QLatin1Char('*')) && !t.contains(QStringLiteral("(*)"));
}

bool isVoidPointerType(const QString& rawType) {
  if (!isPointerTypeString(rawType)) return false;
  QString base = stripTrailingQualifiers(rawType);
  base.chop(1);
  base = stripTrailingQualifiers(base);
  if (base.startsWith(QStringLiteral("const "))) base = base.mid(6).trimmed();
  return base == QLatin1String("void");
}

bool isExpandableOrPointerField(const PointerGraphDialog::StructFieldEntry& f) {
  const QString v = f.value.trimmed();
  if (isNullPointerValue(v)) return false;
  return v.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ||
         f.type.contains(QLatin1Char('*')) || v.startsWith(QLatin1Char('{'));
}

QString dereferencedTypeName(const QString& rawType) {
  QString t = rawType.trimmed();
  while (t.endsWith(QLatin1Char('*')) || t.endsWith(QLatin1Char(' '))) t.chop(1);
  return t.trimmed();
}

QString formatScalarWithOverride(const QString& rawValue,
                                 const QString& rawType,
                                 const QString& formatOverride) {
  const QString fmt = formatOverride.trimmed().toLower();
  if (fmt.isEmpty()) return rawValue;
  const QString trimmed = rawValue.trimmed();
  if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('{')) || trimmed.startsWith(QLatin1Char('"'))) {
    return rawValue;
  }

  const QString tok = trimmed.section(QLatin1Char(' '), 0, 0);
  if (tok.contains(QLatin1Char('.')) && !tok.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
    return rawValue;
  }

  bool ok = false;
  quint64 uval = 0ULL;
  bool isNeg = false;
  qint64 sval = 0LL;

  if (tok.compare(QLatin1String("nullptr"), Qt::CaseInsensitive) == 0 ||
      tok.compare(QLatin1String("null"), Qt::CaseInsensitive) == 0) {
    ok = true;
    uval = 0ULL;
  } else if (tok.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
    uval = tok.mid(2).toULongLong(&ok, 16);
    sval = static_cast<qint64>(uval);
  } else if (tok.startsWith(QStringLiteral("0b"), Qt::CaseInsensitive)) {
    uval = tok.mid(2).toULongLong(&ok, 2);
    sval = static_cast<qint64>(uval);
  } else if (tok.startsWith(QLatin1Char('-'))) {
    sval = tok.toLongLong(&ok, 10);
    isNeg = ok;
    if (rawType.contains(QStringLiteral("8")) || rawType.contains(QStringLiteral("char"))) {
      uval = static_cast<uint8_t>(sval);
    } else if (rawType.contains(QStringLiteral("16")) || rawType.contains(QStringLiteral("short"))) {
      uval = static_cast<uint16_t>(sval);
    } else if (rawType.contains(QStringLiteral("64")) || rawType.contains(QStringLiteral("long"))) {
      uval = static_cast<uint64_t>(sval);
    } else {
      uval = static_cast<uint32_t>(sval);
    }
  } else {
    uval = tok.toULongLong(&ok, 10);
    sval = static_cast<qint64>(uval);
  }
  if (!ok) return rawValue;

  if (fmt == QLatin1String("x")) return QStringLiteral("0x%1").arg(uval, 0, 16);
  if (fmt == QLatin1String("b")) return QStringLiteral("0b%1").arg(uval, 0, 2);
  if (fmt == QLatin1String("d")) {
    const bool isUnsigned = rawType.contains(QStringLiteral("unsigned"), Qt::CaseInsensitive) ||
                            rawType.contains(QStringLiteral("size_t")) ||
                            rawType.contains(QStringLiteral("uint"), Qt::CaseInsensitive) ||
                            rawType.contains(QLatin1Char('*'));
    return (isUnsigned && !isNeg) ? QString::number(uval, 10) : QString::number(sval, 10);
  }
  return rawValue;
}

// ============================================================================
// PointerGraphNodeItem
// ============================================================================

PointerGraphNodeItem::PointerGraphNodeItem(QString expr,
                                           QString type,
                                           QString addr,
                                           PointerNodeCallbacks callbacks)
    : m_expression(std::move(expr)),
      m_typeName(std::move(type)),
      m_address(std::move(addr)),
      m_cb(std::move(callbacks)) {
  setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable |
           QGraphicsItem::ItemSendsGeometryChanges);
  setAcceptHoverEvents(true);
  setCacheMode(QGraphicsItem::DeviceCoordinateCache);
}

void PointerGraphNodeItem::updateData(
    const QString& expr,
    const QString& type,
    const QString& addr,
    const QVector<PointerGraphDialog::StructFieldEntry>& fields) {
  prepareGeometryChange();
  if (!expr.isEmpty()) m_expression = expr;
  if (!type.isEmpty()) m_typeName = type;
  if (!addr.isEmpty()) m_address = addr;
  m_fields = fields;
  if (!m_fields.isEmpty()) m_placeholderState = PlaceholderState::Unexpanded;
  update();
}

void PointerGraphNodeItem::setIsRoot(bool isRoot) {
  if (m_isRoot == isRoot) return;
  prepareGeometryChange();
  m_isRoot = isRoot;
  update();
}

void PointerGraphNodeItem::setDisplayOptions(const PointerGraphDialog::NodeDisplayOptions& opts) {
  prepareGeometryChange();
  m_displayOptions = opts;
  update();
}

void PointerGraphNodeItem::setPlaceholderState(PlaceholderState state) {
  if (m_placeholderState == state) return;
  prepareGeometryChange();
  m_placeholderState = state;
  update();
}

QString PointerGraphNodeItem::placeholderStatusText() const {
  switch (m_placeholderState) {
    case PlaceholderState::Loading:     return QStringLiteral("(loading fields...)");
    case PlaceholderState::NullPointer: return QStringLiteral("(null pointer)");
    case PlaceholderState::NoFields:    return QStringLiteral("(no fields / opaque type)");
    case PlaceholderState::Unexpanded:  return QStringLiteral("(double-click or expand fields...)");
  }
  return QStringLiteral("(double-click or expand fields...)");
}

void PointerGraphNodeItem::setMutatedFields(const QMap<QString, QString>& transitions) {
  prepareGeometryChange();
  m_mutatedTransitions = transitions;
  setToolTip(headerText());
  update();
}

QVector<int> PointerGraphNodeItem::visibleFieldIndices() const {
  QVector<int> vis;
  vis.reserve(m_fields.size());
  for (int i = 0; i < m_fields.size(); ++i) {
    if (!m_displayOptions.hiddenFields.contains(m_fields[i].name)) vis.append(i);
  }
  return vis;
}

int PointerGraphNodeItem::hiddenFieldCount() const {
  return m_fields.size() - visibleFieldIndices().size();
}

QString PointerGraphNodeItem::hiddenBadgeText() const {
  const int hidden = hiddenFieldCount();
  return (hidden > 0) ? QStringLiteral("(+%1 hidden) click to restore").arg(hidden) : QString();
}

QString PointerGraphNodeItem::formattedFieldValue(int fieldIndex) const {
  if (fieldIndex < 0 || fieldIndex >= m_fields.size()) return QString();
  const auto& f = m_fields[fieldIndex];
  return formatScalarWithOverride(f.value, f.type, m_displayOptions.formatOverride);
}

QString PointerGraphNodeItem::headerText() const {
  QString hdr = m_expression;
  if (!m_typeName.isEmpty()) hdr += QStringLiteral(" : %1").arg(m_typeName);
  if (!m_address.isEmpty()) hdr += QStringLiteral(" @ %1").arg(m_address);
  if (!m_displayOptions.formatOverride.isEmpty()) {
    hdr += QStringLiteral(" [/%1]").arg(m_displayOptions.formatOverride);
  }
  return hdr;
}

QString PointerGraphNodeItem::fieldRowText(int fieldIndex) const {
  if (fieldIndex < 0 || fieldIndex >= m_fields.size()) return QString();
  const auto& f = m_fields[fieldIndex];
  const QString dispVal = formattedFieldValue(fieldIndex);
  QString txt = f.type.isEmpty()
                    ? QStringLiteral("%1 : %2").arg(f.name, dispVal)
                    : QStringLiteral("%1 : %2 (%3)").arg(f.name, dispVal, f.type);
  if (m_mutatedTransitions.contains(f.name)) {
    txt += QStringLiteral(" [%1]").arg(m_mutatedTransitions.value(f.name));
  }
  return txt;
}

QString PointerGraphNodeItem::tooltipForPosition(const QPointF& p) const {
  if (!boundingRect().contains(p)) return QString();
  if (p.y() >= 0.0 && p.y() < kHeaderHeight) {
    QStringList lines{headerText()};
    for (auto it = m_mutatedTransitions.cbegin(); it != m_mutatedTransitions.cend(); ++it) {
      lines.append(QStringLiteral("%1.%2: %3").arg(m_expression, it.key(), it.value()));
    }
    return lines.join(QLatin1Char('\n'));
  }
  const int fIdx = hitTestVisibleSlot(p, nullptr);
  if (fIdx >= 0) return fieldRowText(fIdx);
  if (hitTestHiddenPill(p)) {
    QStringList hiddenNames;
    for (const auto& f : m_fields) {
      if (m_displayOptions.hiddenFields.contains(f.name)) hiddenNames.append(f.name);
    }
    return QStringLiteral("Hidden fields: %1 (click to restore)").arg(hiddenNames.join(QStringLiteral(", ")));
  }
  if (m_fields.isEmpty()) return QStringLiteral("%1\n%2").arg(headerText(), placeholderStatusText());
  return headerText();
}

int PointerGraphNodeItem::indexOfField(const QString& fieldName) const {
  for (int i = 0; i < m_fields.size(); ++i) {
    if (m_fields[i].name == fieldName) return i;
  }
  return -1;
}

qreal PointerGraphNodeItem::computeEffectiveWidth() const {
  const QVector<int> vis = visibleFieldIndices();
  if (!m_displayOptions.expandedWidth) {
    if (m_displayOptions.horizontal) {
      const int cols = qMax(1, static_cast<int>(vis.size()));
      return std::max<qreal>(220.0, cols * kHorizColWidth);
    }
    return kCardWidth;
  }

  QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  mono.setPixelSize(11);
  mono.setBold(true);
  const QFontMetrics hdrFm(mono);
  const qreal hdrRightMargin = m_isRoot ? 52.0 : 30.0;
  qreal neededW = hdrFm.horizontalAdvance(headerText()) + 24.0 + hdrRightMargin;

  mono.setBold(false);
  const QFontMetrics rowFm(mono);
  if (m_displayOptions.horizontal) {
    const int cols = qMax(1, static_cast<int>(vis.size()));
    qreal maxColW = kHorizColWidth;
    for (int fIdx : vis) {
      const auto& f = m_fields[fIdx];
      const bool expandable = isExpandableOrPointerField(f);
      QString valTxt = formattedFieldValue(fIdx);
      if (m_mutatedTransitions.contains(f.name)) valTxt += QStringLiteral(" [%1]").arg(m_mutatedTransitions.value(f.name));
      maxColW = std::max<qreal>(maxColW, rowFm.horizontalAdvance(f.name) + (expandable ? 48.0 : 18.0));
      maxColW = std::max<qreal>(maxColW, rowFm.horizontalAdvance(valTxt) + 18.0);
    }
    neededW = std::max(neededW, cols * maxColW);
    return std::clamp(std::ceil(neededW), std::max<qreal>(220.0, cols * kHorizColWidth), kMaxExpandedCardWidth);
  }

  if (m_fields.isEmpty()) {
    neededW = std::max<qreal>(neededW, rowFm.horizontalAdvance(placeholderStatusText()) + 28.0);
  } else {
    for (int fIdx : vis) {
      const auto& f = m_fields[fIdx];
      const bool expandable = isExpandableOrPointerField(f);
      const bool nullPtr = !expandable && (f.type.contains(QLatin1Char('*')) || isNullPointerValue(f.value));
      const qreal rightPad = (expandable || nullPtr) ? 68.0 : 10.0;
      neededW = std::max<qreal>(neededW, rowFm.horizontalAdvance(fieldRowText(fIdx)) + 26.0 + rightPad);
    }
  }
  return std::clamp(std::ceil(neededW), kCardWidth, kMaxExpandedCardWidth);
}

QRectF PointerGraphNodeItem::boundingRect() const {
  const QVector<int> vis = visibleFieldIndices();
  const qreal hiddenBar = (hiddenFieldCount() > 0) ? kHiddenPillHeight : 0.0;
  const qreal w = computeEffectiveWidth();
  if (m_displayOptions.horizontal) {
    return QRectF(0.0, 0.0, w, kHeaderHeight + kHorizBodyHeight + hiddenBar + kFooterPadding);
  }
  const int rows = m_fields.isEmpty() ? 1 : static_cast<int>(vis.size());
  return QRectF(0.0, 0.0, w, kHeaderHeight + rows * kRowHeight + hiddenBar + kFooterPadding);
}

QPointF PointerGraphNodeItem::fieldSourceAnchor(const QString& fieldName) const {
  const QRectF rect = boundingRect();
  const QVector<int> vis = visibleFieldIndices();
  int visPos = -1;
  for (int i = 0; i < vis.size(); ++i) {
    if (m_fields[vis[i]].name == fieldName) {
      visPos = i;
      break;
    }
  }
  if (m_displayOptions.horizontal) {
    if (visPos >= 0 && !vis.isEmpty()) {
      const qreal colW = rect.width() / static_cast<qreal>(vis.size());
      return mapToScene(QPointF((visPos + 1) * colW, kHeaderHeight + kHorizBodyHeight * 0.5));
    }
    return mapToScene(QPointF(rect.width(), kHeaderHeight * 0.5));
  }
  if (visPos >= 0) {
    return mapToScene(QPointF(rect.width(), kHeaderHeight + visPos * kRowHeight + kRowHeight * 0.5));
  }
  if (hiddenFieldCount() > 0 && indexOfField(fieldName) >= 0) {
    return mapToScene(QPointF(rect.width(), kHeaderHeight + vis.size() * kRowHeight + kHiddenPillHeight * 0.5));
  }
  return mapToScene(QPointF(rect.width(), kHeaderHeight * 0.5));
}

QPointF PointerGraphNodeItem::targetAnchor() const {
  return mapToScene(QPointF(0.0, kHeaderHeight * 0.5));
}

int PointerGraphNodeItem::hitTestVisibleSlot(const QPointF& p, bool* hitDerefBadgeOut) const {
  if (hitDerefBadgeOut) *hitDerefBadgeOut = false;
  const QVector<int> vis = visibleFieldIndices();
  if (vis.isEmpty()) return -1;
  const QRectF rect = boundingRect();

  if (m_displayOptions.horizontal) {
    if (p.y() < kHeaderHeight || p.y() >= kHeaderHeight + kHorizBodyHeight ||
        p.x() < 0.0 || p.x() >= rect.width()) {
      return -1;
    }
    const qreal colW = rect.width() / static_cast<qreal>(vis.size());
    const int col = std::clamp(static_cast<int>(p.x() / colW), 0, static_cast<int>(vis.size()) - 1);
    const qreal localX = p.x() - col * colW;
    if (hitDerefBadgeOut && p.y() <= kHeaderHeight + 22.0 && localX >= colW - 38.0) {
      *hitDerefBadgeOut = true;
    }
    return vis[col];
  }

  const qreal bodyH = vis.size() * kRowHeight;
  if (p.y() < kHeaderHeight || p.y() >= kHeaderHeight + bodyH || p.x() < 0.0 || p.x() >= rect.width()) {
    return -1;
  }
  const int row = std::clamp(static_cast<int>((p.y() - kHeaderHeight) / kRowHeight),
                             0, static_cast<int>(vis.size()) - 1);
  if (hitDerefBadgeOut && p.x() >= rect.width() - 64.0) *hitDerefBadgeOut = true;
  return vis[row];
}

bool PointerGraphNodeItem::hitTestHiddenPill(const QPointF& p) const {
  if (hiddenFieldCount() <= 0) return false;
  const QVector<int> vis = visibleFieldIndices();
  const qreal bodyH = m_displayOptions.horizontal
                          ? kHorizBodyHeight
                          : (m_fields.isEmpty() ? kRowHeight : vis.size() * kRowHeight);
  return p.x() >= 4.0 && p.x() <= boundingRect().width() - 4.0 &&
         p.y() >= kHeaderHeight + bodyH && p.y() <= kHeaderHeight + bodyH + kHiddenPillHeight;
}

void PointerGraphNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
  painter->setRenderHint(QPainter::Antialiasing, true);
  const bool isDark = core::ThemeManager::instance().isDarkMode();
  const QRectF rect = boundingRect();
  const qreal cardW = rect.width();

  const QColor bgColor = isDark ? QColor(QStringLiteral("#181c24")) : QColor(QStringLiteral("#ffffff"));
  const QColor borderColor = isSelected() ? QColor(QStringLiteral("#38bdf8"))
                                          : (!m_mutatedTransitions.isEmpty()
                                                 ? QColor(QStringLiteral("#f59e0b"))
                                                 : (isDark ? QColor(QStringLiteral("#334155")) : QColor(QStringLiteral("#cbd5e1"))));
  painter->setPen(QPen(borderColor, (isSelected() || !m_mutatedTransitions.isEmpty()) ? 2.0 : 1.2));
  painter->setBrush(bgColor);
  painter->drawRoundedRect(rect, 7.0, 7.0);

  QPainterPath hdrPath;
  hdrPath.addRoundedRect(QRectF(0.0, 0.0, cardW, kHeaderHeight), 7.0, 7.0);
  painter->fillPath(hdrPath, isDark ? QColor(QStringLiteral("#1e293b")) : QColor(QStringLiteral("#e2e8f0")));
  QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  mono.setPixelSize(11);
  mono.setBold(true);
  painter->setFont(mono);
  painter->setPen(isDark ? QColor(QStringLiteral("#f8fafc")) : QColor(QStringLiteral("#0f172a")));

  const QString hdrText = headerText();
  const qreal hdrRightMargin = m_isRoot ? 52.0 : 30.0;
  painter->drawText(QRectF(10.0, 0.0, cardW - 10.0 - hdrRightMargin, kHeaderHeight),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    painter->fontMetrics().elidedText(hdrText, Qt::ElideRight,
                                                      static_cast<int>(cardW - 12.0 - hdrRightMargin)));

  // Header orientation badge `⇄` + optional root unpin `×`.
  const QRectF orientRect(cardW - (m_isRoot ? 46.0 : 24.0), 6.0, 18.0, 18.0);
  painter->setPen(QPen(isDark ? QColor(QStringLiteral("#475569")) : QColor(QStringLiteral("#94a3b8")), 1.0));
  painter->setBrush(m_displayOptions.horizontal
                        ? (isDark ? QColor(QStringLiteral("#0c4a6e")) : QColor(QStringLiteral("#e0f2fe")))
                        : (isDark ? QColor(QStringLiteral("#0f172a")) : QColor(QStringLiteral("#f8fafc"))));
  painter->drawRoundedRect(orientRect, 4.0, 4.0);
  painter->setPen(isDark ? QColor(QStringLiteral("#7dd3fc")) : QColor(QStringLiteral("#0369a1")));
  painter->drawText(orientRect, Qt::AlignCenter, QStringLiteral("⇄"));

  if (m_isRoot) {
    const QRectF unpinRect(cardW - 24.0, 6.0, 18.0, 18.0);
    painter->setPen(QPen(isDark ? QColor(QStringLiteral("#64748b")) : QColor(QStringLiteral("#94a3b8")), 1.0));
    painter->setBrush(isDark ? QColor(QStringLiteral("#0f172a")) : QColor(QStringLiteral("#f8fafc")));
    painter->drawRoundedRect(unpinRect, 4.0, 4.0);
    painter->setPen(isDark ? QColor(QStringLiteral("#f87171")) : QColor(QStringLiteral("#dc2626")));
    painter->drawText(unpinRect, Qt::AlignCenter, QStringLiteral("×"));
  }

  mono.setBold(false);
  painter->setFont(mono);
  if (m_fields.isEmpty()) {
    const QColor emptyColor = (m_placeholderState == PlaceholderState::Loading)
                                  ? QColor(QStringLiteral("#38bdf8"))
                                  : (isDark ? QColor(QStringLiteral("#94a3b8")) : QColor(QStringLiteral("#64748b")));
    painter->setPen(emptyColor);
    painter->drawText(QRectF(10.0, kHeaderHeight, cardW - 20.0, kRowHeight),
                      Qt::AlignVCenter | Qt::AlignLeft, placeholderStatusText());
    return;
  }

  const QVector<int> vis = visibleFieldIndices();
  qreal bodyBottomY = kHeaderHeight;

  if (m_displayOptions.horizontal) {
    bodyBottomY = kHeaderHeight + kHorizBodyHeight;
    if (!vis.isEmpty()) {
      const qreal colW = cardW / static_cast<qreal>(vis.size());
      for (int c = 0; c < vis.size(); ++c) {
        const qreal colX = c * colW;
        const int fIdx = vis[c];
        const auto& f = m_fields[fIdx];
        const bool isMutated = m_mutatedTransitions.contains(f.name);
        const bool expandable = isExpandableOrPointerField(f);
        if (isMutated) {
          painter->fillRect(QRectF(colX + 1.0, kHeaderHeight + 1.0, colW - 2.0, kHorizBodyHeight - 2.0),
                            isDark ? QColor(QStringLiteral("#451a03")) : QColor(QStringLiteral("#fef3c7")));
        }
        if (c > 0) {
          painter->setPen(QPen(isDark ? QColor(QStringLiteral("#334155")) : QColor(QStringLiteral("#cbd5e1")), 1.0));
          painter->drawLine(QPointF(colX, kHeaderHeight + 2.0), QPointF(colX, bodyBottomY - 2.0));
        }
        painter->setPen(isDark ? QColor(QStringLiteral("#94a3b8")) : QColor(QStringLiteral("#475569")));
        painter->drawText(QRectF(colX + 6.0, kHeaderHeight + 2.0, colW - (expandable ? 40.0 : 10.0), 18.0),
                          Qt::AlignVCenter | Qt::AlignLeft,
                          painter->fontMetrics().elidedText(f.name, Qt::ElideRight, static_cast<int>(colW - (expandable ? 42.0 : 12.0))));
        if (expandable) {
          const QRectF pill(colX + colW - 34.0, kHeaderHeight + 3.0, 28.0, 16.0);
          painter->setPen(QPen(QColor(QStringLiteral("#0284c7")), 1.0));
          painter->setBrush(isDark ? QColor(QStringLiteral("#0c4a6e")) : QColor(QStringLiteral("#e0f2fe")));
          painter->drawRoundedRect(pill, 3.0, 3.0);
          painter->setPen(isDark ? QColor(QStringLiteral("#7dd3fc")) : QColor(QStringLiteral("#0369a1")));
          painter->drawText(pill, Qt::AlignCenter, QStringLiteral("➔"));
        }
        painter->setPen(isMutated
                            ? (isDark ? QColor(QStringLiteral("#fbbf24")) : QColor(QStringLiteral("#b45309")))
                            : (isDark ? QColor(QStringLiteral("#e2e8f0")) : QColor(QStringLiteral("#1e293b"))));
        QString valTxt = formattedFieldValue(fIdx);
        if (isMutated) valTxt += QStringLiteral(" [%1]").arg(m_mutatedTransitions.value(f.name));
        painter->drawText(QRectF(colX + 6.0, kHeaderHeight + 21.0, colW - 12.0, 20.0),
                          Qt::AlignVCenter | Qt::AlignLeft,
                          painter->fontMetrics().elidedText(valTxt, Qt::ElideRight, static_cast<int>(colW - 12.0)));
      }
    }
  } else {
    bodyBottomY = kHeaderHeight + vis.size() * kRowHeight;
    for (int vIdx = 0; vIdx < vis.size(); ++vIdx) {
      const qreal y = kHeaderHeight + vIdx * kRowHeight;
      const int fIdx = vis[vIdx];
      const auto& f = m_fields[fIdx];
      const bool isMutated = m_mutatedTransitions.contains(f.name);
      const bool expandable = isExpandableOrPointerField(f);
      const bool nullPtr = !expandable && (f.type.contains(QLatin1Char('*')) || isNullPointerValue(f.value));
      const qreal rightPad = (expandable || nullPtr) ? 68.0 : 10.0;

      if (isMutated) {
        painter->fillRect(QRectF(2.0, y + 1.0, cardW - 4.0, kRowHeight - 2.0),
                          isDark ? QColor(QStringLiteral("#451a03")) : QColor(QStringLiteral("#fef3c7")));
      }
      painter->setPen(isMutated
                          ? (isDark ? QColor(QStringLiteral("#fbbf24")) : QColor(QStringLiteral("#b45309")))
                          : (isDark ? QColor(QStringLiteral("#e2e8f0")) : QColor(QStringLiteral("#1e293b"))));
      const QString rowTxt = fieldRowText(fIdx);
      painter->drawText(QRectF(10.0, y, cardW - 10.0 - rightPad, kRowHeight),
                        Qt::AlignVCenter | Qt::AlignLeft,
                        painter->fontMetrics().elidedText(rowTxt, Qt::ElideRight,
                                                          static_cast<int>(cardW - 14.0 - rightPad)));

      if (expandable || nullPtr) {
        const QRectF badgeRect(cardW - 62.0, y + 3.0, 54.0, kRowHeight - 6.0);
        painter->setPen(QPen(expandable ? QColor(QStringLiteral("#0284c7")) : QColor(QStringLiteral("#64748b")), 1.0));
        painter->setBrush(expandable ? (isDark ? QColor(QStringLiteral("#0c4a6e")) : QColor(QStringLiteral("#e0f2fe")))
                                     : (isDark ? QColor(QStringLiteral("#1e293b")) : QColor(QStringLiteral("#f1f5f9"))));
        painter->drawRoundedRect(badgeRect, 4.0, 4.0);
        painter->setPen(expandable ? (isDark ? QColor(QStringLiteral("#7dd3fc")) : QColor(QStringLiteral("#0369a1")))
                                   : QColor(QStringLiteral("#94a3b8")));
        painter->drawText(badgeRect, Qt::AlignCenter, expandable ? QStringLiteral("➔ Deref") : QStringLiteral("nullptr"));
      }
    }
  }

  // Render clickable `(+N hidden) click to restore` footer pill when fields are suppressed.
  if (hiddenFieldCount() > 0) {
    const QRectF pillRect(8.0, bodyBottomY + 2.0, cardW - 16.0, kHiddenPillHeight - 4.0);
    painter->setPen(QPen(isDark ? QColor(QStringLiteral("#475569")) : QColor(QStringLiteral("#94a3b8")), 1.0));
    painter->setBrush(isDark ? QColor(QStringLiteral("#1e293b")) : QColor(QStringLiteral("#f1f5f9")));
    painter->drawRoundedRect(pillRect, 4.0, 4.0);
    painter->setPen(isDark ? QColor(QStringLiteral("#7dd3fc")) : QColor(QStringLiteral("#0284c7")));
    painter->drawText(pillRect, Qt::AlignCenter, hiddenBadgeText());
  }
}

bool PointerGraphNodeItem::sceneEvent(QEvent* event) {
  if (event && event->type() == QEvent::GraphicsSceneHelp) {
    auto* helpEv = static_cast<QGraphicsSceneHelpEvent*>(event);
    const QString tip = tooltipForPosition(mapFromScene(helpEv->scenePos()));
    setToolTip(tip);
    if (!tip.isEmpty()) QToolTip::showText(helpEv->screenPos(), tip, helpEv->widget());
    else QToolTip::hideText();
    event->accept();
    return true;
  }
  return QGraphicsObject::sceneEvent(event);
}

void PointerGraphNodeItem::hoverMoveEvent(QGraphicsSceneHoverEvent* event) {
  if (event) {
    const QString tip = tooltipForPosition(event->pos());
    setToolTip(tip);
    if (tip != m_lastHoverTooltip) {
      m_lastHoverTooltip = tip;
      if (!tip.isEmpty() && scene() && !scene()->views().isEmpty() && scene()->views().constFirst()->isVisible()) {
        QToolTip::showText(event->screenPos(), tip, scene()->views().constFirst()->viewport());
      } else {
        QToolTip::hideText();
      }
    }
  }
  QGraphicsObject::hoverMoveEvent(event);
}

void PointerGraphNodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
  m_lastHoverTooltip.clear();
  QToolTip::hideText();
  QGraphicsObject::hoverLeaveEvent(event);
}

QVariant PointerGraphNodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
  if (change == ItemPositionHasChanged && m_cb.onMoved) m_cb.onMoved(this);
  return QGraphicsObject::itemChange(change, value);
}

void PointerGraphNodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
  if (event && event->button() == Qt::LeftButton) {
    if (event->modifiers() & Qt::ShiftModifier) {
      if (m_cb.onToggleExpandedWidth) m_cb.onToggleExpandedWidth(this);
      event->accept();
      return;
    }
    const QPointF p = event->pos();
    const qreal cardW = boundingRect().width();
    if (p.y() >= 0.0 && p.y() <= kHeaderHeight) {
      if (m_isRoot && p.x() >= cardW - 26.0 && m_cb.onUnpinRoot) {
        m_cb.onUnpinRoot(m_expression);
        event->accept();
        return;
      }
      const qreal orientLeft = cardW - (m_isRoot ? 48.0 : 26.0);
      const qreal orientRight = cardW - (m_isRoot ? 26.0 : 4.0);
      if (p.x() >= orientLeft && p.x() <= orientRight && m_cb.onToggleOrientation) {
        m_cb.onToggleOrientation(this);
        event->accept();
        return;
      }
    }
    if (hitTestHiddenPill(p) && m_cb.onRestoreHidden) {
      m_cb.onRestoreHidden(this);
      event->accept();
      return;
    }
    bool hitDeref = false;
    const int fIdx = hitTestVisibleSlot(p, &hitDeref);
    if (fIdx >= 0 && hitDeref && isExpandableOrPointerField(m_fields[fIdx]) && m_cb.onExpandField) {
      m_cb.onExpandField(m_expression, m_fields[fIdx].name);
      event->accept();
      return;
    }
  }
  QGraphicsObject::mousePressEvent(event);
}

bool PointerGraphNodeItem::handleDoubleClickAt(const QPointF& itemPos) {
  const int fIdx = hitTestVisibleSlot(itemPos, nullptr);
  if (fIdx >= 0 && m_cb.onExpandField) {
    if (isExpandableOrPointerField(m_fields[fIdx])) m_cb.onExpandField(m_expression, m_fields[fIdx].name);
    return true;
  }
  if (m_fields.isEmpty() && m_cb.onExpandNode) {
    m_cb.onExpandNode(m_expression);
    return true;
  }
  return false;
}

void PointerGraphNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
  if (event && event->button() == Qt::LeftButton && handleDoubleClickAt(event->pos())) {
    event->accept();
    return;
  }
  QGraphicsObject::mouseDoubleClickEvent(event);
}

void PointerGraphNodeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
  if (!event) return;
  QMenu menu;
  const int fIdx = hitTestVisibleSlot(event->pos(), nullptr);
  QAction* expandNodeAct = m_fields.isEmpty()
      ? menu.addAction(QStringLiteral("Expand / Reload Node Fields"))
      : nullptr;
  QAction* hideFieldAct = (fIdx >= 0)
      ? menu.addAction(QStringLiteral("Hide Field '%1'").arg(m_fields[fIdx].name))
      : nullptr;
  const int hiddenCnt = hiddenFieldCount();
  QAction* showHiddenAct = (hiddenCnt > 0)
      ? menu.addAction(QStringLiteral("Show All Hidden Fields (%1)").arg(hiddenCnt))
      : nullptr;
  if (expandNodeAct || hideFieldAct || showHiddenAct) menu.addSeparator();

  QAction* expandWidthAct = menu.addAction(m_displayOptions.expandedWidth
      ? QStringLiteral("Compact Card Width (Shift+Click)")
      : QStringLiteral("Expand Card Width to Fit Values (Shift+Click)"));
  QAction* orientAct = menu.addAction(QStringLiteral("Toggle Horizontal / Vertical Layout"));
  QMenu* fmtMenu = menu.addMenu(QStringLiteral("Display Format"));
  QAction* fmtAuto = fmtMenu->addAction(QStringLiteral("Auto"));
  QAction* fmtHex = fmtMenu->addAction(QStringLiteral("Hex (/x)"));
  QAction* fmtDec = fmtMenu->addAction(QStringLiteral("Decimal (/d)"));
  QAction* fmtBin = fmtMenu->addAction(QStringLiteral("Binary (/b)"));
  for (auto* a : {fmtAuto, fmtHex, fmtDec, fmtBin}) a->setCheckable(true);
  const QString curFmt = m_displayOptions.formatOverride;
  fmtAuto->setChecked(curFmt.isEmpty());
  fmtHex->setChecked(curFmt == QLatin1String("x"));
  fmtDec->setChecked(curFmt == QLatin1String("d"));
  fmtBin->setChecked(curFmt == QLatin1String("b"));

  menu.addSeparator();
  QAction* unpinAct = m_isRoot ? menu.addAction(QStringLiteral("Unpin Root (%1)").arg(m_expression)) : nullptr;
  QAction* resetPosAct = menu.addAction(QStringLiteral("Reset Card Position"));

  QAction* chosen = menu.exec(event->screenPos());
  if (!chosen) return;
  if (chosen == expandNodeAct && m_cb.onExpandNode) m_cb.onExpandNode(m_expression);
  else if (chosen == hideFieldAct && fIdx >= 0 && m_cb.onHideField) m_cb.onHideField(this, m_fields[fIdx].name);
  else if (chosen == showHiddenAct && m_cb.onRestoreHidden) m_cb.onRestoreHidden(this);
  else if (chosen == expandWidthAct && m_cb.onToggleExpandedWidth) m_cb.onToggleExpandedWidth(this);
  else if (chosen == orientAct && m_cb.onToggleOrientation) m_cb.onToggleOrientation(this);
  else if (chosen == fmtAuto && m_cb.onSetFormat) m_cb.onSetFormat(this, QString());
  else if (chosen == fmtHex && m_cb.onSetFormat) m_cb.onSetFormat(this, QStringLiteral("x"));
  else if (chosen == fmtDec && m_cb.onSetFormat) m_cb.onSetFormat(this, QStringLiteral("d"));
  else if (chosen == fmtBin && m_cb.onSetFormat) m_cb.onSetFormat(this, QStringLiteral("b"));
  else if (chosen == unpinAct && m_cb.onUnpinRoot) m_cb.onUnpinRoot(m_expression);
  else if (chosen == resetPosAct && m_cb.onResetPos) m_cb.onResetPos(this);
  event->accept();
}

PointerLayoutResult computeLayeredDagLayout(const QVector<PointerLayoutNode>& nodes,
                                            const QVector<PointerLayoutEdge>& edges,
                                            bool respectPinned,
                                            const PointerLayoutConfig& config) {
  PointerLayoutResult result;
  const int n = nodes.size();
  if (n == 0) return result;
  result.layers.fill(-1, n);
  result.positions.resize(n);

  QVector<QVector<int>> outAdj(n);
  QVector<QVector<PointerLayoutEdge>> inEdges(n), outEdges(n);
  QVector<int> inDegree(n, 0);
  for (const auto& e : edges) {
    if (e.sourceIndex < 0 || e.sourceIndex >= n || e.targetIndex < 0 || e.targetIndex >= n || e.sourceIndex == e.targetIndex) continue;
    outAdj[e.sourceIndex].append(e.targetIndex);
    outEdges[e.sourceIndex].append(e);
    inEdges[e.targetIndex].append(e);
    ++inDegree[e.targetIndex];
  }

  auto runBfsFromSeeds = [&](const QVector<int>& seeds) {
    QQueue<int> q;
    for (int s : seeds) if (result.layers[s] == -1) { result.layers[s] = 0; q.enqueue(s); }
    while (!q.isEmpty()) {
      const int u = q.dequeue();
      for (int v : outAdj[u]) if (result.layers[v] == -1) { result.layers[v] = result.layers[u] + 1; q.enqueue(v); }
    }
  };

  QVector<int> rootSeeds, zeroInSeeds;
  for (int i = 0; i < n; ++i) if (nodes[i].isRoot) rootSeeds.append(i);
  runBfsFromSeeds(rootSeeds);
  for (int i = 0; i < n; ++i) if (result.layers[i] == -1 && inDegree[i] == 0) zeroInSeeds.append(i);
  runBfsFromSeeds(zeroInSeeds);
  for (int i = 0; i < n; ++i) if (result.layers[i] == -1) runBfsFromSeeds({i});

  int maxLayer = 0;
  for (int layer : result.layers) maxLayer = std::max(maxLayer, layer);

  QVector<QVector<int>> layerNodes(maxLayer + 1);
  QVector<int> orderInLayer(n, 0);
  for (int i = 0; i < n; ++i) {
    orderInLayer[i] = layerNodes[result.layers[i]].size();
    layerNodes[result.layers[i]].append(i);
  }
  auto refreshLayerOrder = [&](int l) {
    for (int idx = 0; idx < layerNodes[l].size(); ++idx) orderInLayer[layerNodes[l][idx]] = idx;
  };

  for (int pass = 0; pass < 2; ++pass) {
    for (int l = 1; l <= maxLayer; ++l) {
      QVector<double> bary(n, 0.0);
      for (int v : layerNodes[l]) {
        double sum = 0.0; int count = 0;
        for (const auto& e : inEdges[v]) if (result.layers[e.sourceIndex] < l) {
          sum += static_cast<double>(orderInLayer[e.sourceIndex]) + std::min(0.45, std::max(0, e.sourceRow) * 0.05);
          ++count;
        }
        bary[v] = (count > 0) ? (sum / count) : static_cast<double>(orderInLayer[v]);
      }
      std::stable_sort(layerNodes[l].begin(), layerNodes[l].end(), [&](int a, int b) {
        return (std::abs(bary[a] - bary[b]) > 1e-6) ? (bary[a] < bary[b]) : (a < b);
      });
      refreshLayerOrder(l);
    }
    for (int l = maxLayer - 1; l >= 0; --l) {
      QVector<double> bary(n, 0.0);
      for (int u : layerNodes[l]) {
        double sum = 0.0; int count = 0;
        for (const auto& e : outEdges[u]) if (result.layers[e.targetIndex] > l) {
          sum += static_cast<double>(orderInLayer[e.targetIndex]) - std::min(0.45, std::max(0, e.sourceRow) * 0.05);
          ++count;
        }
        bary[u] = (count > 0) ? (sum / count) : static_cast<double>(orderInLayer[u]);
      }
      std::stable_sort(layerNodes[l].begin(), layerNodes[l].end(), [&](int a, int b) {
        return (std::abs(bary[a] - bary[b]) > 1e-6) ? (bary[a] < bary[b]) : (a < b);
      });
      refreshLayerOrder(l);
    }
  }

  QVector<qreal> layerX(maxLayer + 1, 0.0), layerHeights(maxLayer + 1, 0.0);
  qreal maxLayerHeight = 0.0;
  for (int l = 0; l <= maxLayer; ++l) {
    if (l > 0) {
      qreal prevMaxW = config.defaultCardWidth;
      for (int u : layerNodes[l - 1]) prevMaxW = std::max(prevMaxW, nodes[u].size.width());
      layerX[l] = layerX[l - 1] + prevMaxW + config.layerHorizontalGap;
    }
    qreal totalH = 0.0;
    for (int idx = 0; idx < layerNodes[l].size(); ++idx) {
      totalH += std::max<qreal>(60.0, nodes[layerNodes[l][idx]].size.height());
      if (idx + 1 < layerNodes[l].size()) totalH += config.verticalSiblingGap;
    }
    layerHeights[l] = totalH;
    maxLayerHeight = std::max(maxLayerHeight, totalH);
  }

  for (int l = 0; l <= maxLayer; ++l) {
    qreal y = std::max<qreal>(0.0, (maxLayerHeight - layerHeights[l]) * 0.5);
    for (int u : layerNodes[l]) {
      result.positions[u] = (respectPinned && nodes[u].isPinned) ? nodes[u].currentPos : QPointF(layerX[l], y);
      y += std::max<qreal>(60.0, nodes[u].size.height()) + config.verticalSiblingGap;
    }
  }
  return result;
}

QPointF computeIncrementalNodePosition(bool hasParent, const QPointF& parentPos, const QSizeF& parentSize,
                                       const QSizeF& newNodeSize, const QVector<QRectF>& occupiedRects,
                                       const PointerLayoutConfig& config) {
  const qreal cardW = std::max(config.defaultCardWidth, newNodeSize.width());
  const qreal cardH = std::max<qreal>(60.0, newNodeSize.height());
  const qreal candX = hasParent ? (parentPos.x() + std::max(config.defaultCardWidth, parentSize.width()) + config.layerHorizontalGap) : 0.0;
  qreal candY = hasParent ? parentPos.y() : 0.0;
  const qreal padX = 12.0, padY = config.verticalSiblingGap * 0.5;
  for (int guard = 0; guard < 256; ++guard) {
    const QRectF candidate(candX - padX, candY - padY, cardW + 2.0 * padX, cardH + 2.0 * padY);
    qreal pushBottom = -1.0; bool collided = false;
    for (const QRectF& occ : occupiedRects) if (candidate.intersects(occ)) { collided = true; pushBottom = std::max(pushBottom, occ.bottom()); }
    if (!collided) break;
    candY = pushBottom + config.verticalSiblingGap;
  }
  return QPointF(candX, candY);
}

AliasCycleAnalysis analyzeCyclesAndAliases(int nodeCount, const QVector<PointerLayoutEdge>& edges,
                                           const QVector<QPointF>& nodePositions) {
  AliasCycleAnalysis out;
  out.edgeIsCycleOrAlias.fill(false, edges.size());
  out.nodeInDegree.fill(0, nodeCount);
  if (nodeCount <= 0 || edges.isEmpty()) return out;

  QVector<QVector<int>> adj(nodeCount);
  for (const auto& e : edges) if (e.sourceIndex >= 0 && e.sourceIndex < nodeCount && e.targetIndex >= 0 && e.targetIndex < nodeCount) {
    adj[e.sourceIndex].append(e.targetIndex); ++out.nodeInDegree[e.targetIndex];
  }
  QSet<int> primaryTargetSeen;
  for (int i = 0; i < edges.size(); ++i) {
    const int src = edges[i].sourceIndex, dst = edges[i].targetIndex;
    if (src < 0 || src >= nodeCount || dst < 0 || dst >= nodeCount) continue;
    const bool isSelfLoop = (src == dst);
    const qreal srcX = (src < nodePositions.size()) ? nodePositions[src].x() : 0.0;
    const qreal dstX = (dst < nodePositions.size()) ? nodePositions[dst].x() : 0.0;
    const bool isBackEdge = !isSelfLoop && (dstX <= srcX) && canReachIndex(nodeCount, adj, dst, src);
    const bool isSecondaryAlias = primaryTargetSeen.contains(dst);
    if (!isSelfLoop && !isBackEdge) primaryTargetSeen.insert(dst);
    const bool flagged = isSelfLoop || isBackEdge || isSecondaryAlias;
    out.edgeIsCycleOrAlias[i] = flagged;
    if (flagged) out.anyCycleOrAlias = true;
  }
  return out;
}

QSet<int> findUnreachableNonRootNodes(int nodeCount, const QSet<int>& rootIndices,
                                      const QVector<PointerLayoutEdge>& edges) {
  QSet<int> unreachable;
  if (nodeCount <= 0) return unreachable;
  QVector<QVector<int>> adj(nodeCount);
  for (const auto& e : edges) if (e.sourceIndex >= 0 && e.sourceIndex < nodeCount && e.targetIndex >= 0 && e.targetIndex < nodeCount) {
    adj[e.sourceIndex].append(e.targetIndex);
  }
  QVector<bool> reachable(nodeCount, false);
  QQueue<int> q;
  for (int r : rootIndices) if (r >= 0 && r < nodeCount && !reachable[r]) { reachable[r] = true; q.enqueue(r); }
  while (!q.isEmpty()) {
    const int cur = q.dequeue();
    for (int nxt : adj[cur]) if (!reachable[nxt]) { reachable[nxt] = true; q.enqueue(nxt); }
  }
  for (int i = 0; i < nodeCount; ++i) if (!reachable[i] && !rootIndices.contains(i)) unreachable.insert(i);
  return unreachable;
}

}  // namespace gridlock::ui

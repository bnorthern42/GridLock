/**
 * @file PointerGraphLayout.cpp
 * @brief Implementation of Sugiyama-style layered DAG layout, scalar format overrides,
 *        interactive struct/edge scene items, and zoom/minimap QGraphicsView.
 */

#include "PointerGraphLayout.hpp"

#include "../../../core/managers/ThemeManager.hpp"

#include <QFontDatabase>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQueue>
#include <QScrollBar>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gridlock::ui {

namespace {
constexpr qreal kCardWidth = 290.0;
constexpr qreal kHorizColWidth = 152.0;
constexpr qreal kHorizBodyHeight = 44.0;
constexpr qreal kHeaderHeight = 30.0;
constexpr qreal kRowHeight = 24.0;
constexpr qreal kHiddenPillHeight = 22.0;
constexpr qreal kFooterPadding = 8.0;
constexpr qreal kMinZoom = 0.25;
constexpr qreal kMaxZoom = 2.50;
constexpr qreal kMinimapW = 160.0;
constexpr qreal kMinimapH = 110.0;
constexpr qreal kMinimapMargin = 12.0;

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
  update();
}

void PointerGraphNodeItem::setIsRoot(bool isRoot) {
  if (m_isRoot == isRoot) return;
  m_isRoot = isRoot;
  update();
}

void PointerGraphNodeItem::setDisplayOptions(const PointerGraphDialog::NodeDisplayOptions& opts) {
  prepareGeometryChange();
  m_displayOptions = opts;
  update();
}

void PointerGraphNodeItem::setMutatedFields(const QMap<QString, QString>& transitions) {
  m_mutatedTransitions = transitions;
  if (m_mutatedTransitions.isEmpty()) {
    setToolTip(m_expression);
  } else {
    QStringList lines;
    for (auto it = m_mutatedTransitions.cbegin(); it != m_mutatedTransitions.cend(); ++it) {
      lines.append(QStringLiteral("%1.%2: %3").arg(m_expression, it.key(), it.value()));
    }
    setToolTip(lines.join(QLatin1Char('\n')));
  }
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

int PointerGraphNodeItem::indexOfField(const QString& fieldName) const {
  for (int i = 0; i < m_fields.size(); ++i) {
    if (m_fields[i].name == fieldName) return i;
  }
  return -1;
}

QRectF PointerGraphNodeItem::boundingRect() const {
  const QVector<int> vis = visibleFieldIndices();
  const qreal hiddenBar = (hiddenFieldCount() > 0) ? kHiddenPillHeight : 0.0;
  if (m_displayOptions.horizontal) {
    const int cols = qMax(1, static_cast<int>(vis.size()));
    const qreal w = std::max<qreal>(220.0, cols * kHorizColWidth);
    return QRectF(0.0, 0.0, w, kHeaderHeight + kHorizBodyHeight + hiddenBar + kFooterPadding);
  }
  const int rows = m_fields.isEmpty() ? 1 : static_cast<int>(vis.size());
  return QRectF(0.0, 0.0, kCardWidth, kHeaderHeight + rows * kRowHeight + hiddenBar + kFooterPadding);
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

  QString hdrText = m_expression;
  if (!m_typeName.isEmpty()) hdrText += QStringLiteral(" : %1").arg(m_typeName);
  if (!m_address.isEmpty()) hdrText += QStringLiteral(" @ %1").arg(m_address);
  if (!m_displayOptions.formatOverride.isEmpty()) {
    hdrText += QStringLiteral(" [/%1]").arg(m_displayOptions.formatOverride);
  }
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
    painter->setPen(isDark ? QColor(QStringLiteral("#94a3b8")) : QColor(QStringLiteral("#64748b")));
    painter->drawText(QRectF(10.0, kHeaderHeight, cardW - 20.0, kRowHeight),
                      Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("(double-click or expand fields...)"));
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
      const QString dispVal = formattedFieldValue(fIdx);
      QString rowTxt = f.type.isEmpty()
                           ? QStringLiteral("%1 : %2").arg(f.name, dispVal)
                           : QStringLiteral("%1 : %2 (%3)").arg(f.name, dispVal, f.type);
      if (isMutated) rowTxt += QStringLiteral(" [%1]").arg(m_mutatedTransitions.value(f.name));
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

QVariant PointerGraphNodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
  if (change == ItemPositionHasChanged && m_cb.onMoved) m_cb.onMoved(this);
  return QGraphicsObject::itemChange(change, value);
}

void PointerGraphNodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
  if (event && event->button() == Qt::LeftButton) {
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

void PointerGraphNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
  if (event) {
    const int fIdx = hitTestVisibleSlot(event->pos(), nullptr);
    if (fIdx >= 0 && m_cb.onExpandField) {
      m_cb.onExpandField(m_expression, m_fields[fIdx].name);
      event->accept();
      return;
    }
  }
  QGraphicsObject::mouseDoubleClickEvent(event);
}

void PointerGraphNodeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
  if (!event) return;
  QMenu menu;
  const int fIdx = hitTestVisibleSlot(event->pos(), nullptr);
  QAction* hideFieldAct = (fIdx >= 0)
      ? menu.addAction(QStringLiteral("Hide Field '%1'").arg(m_fields[fIdx].name))
      : nullptr;
  const int hiddenCnt = hiddenFieldCount();
  QAction* showHiddenAct = (hiddenCnt > 0)
      ? menu.addAction(QStringLiteral("Show All Hidden Fields (%1)").arg(hiddenCnt))
      : nullptr;
  if (hideFieldAct || showHiddenAct) menu.addSeparator();

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
  if (chosen == hideFieldAct && fIdx >= 0 && m_cb.onHideField) m_cb.onHideField(this, m_fields[fIdx].name);
  else if (chosen == showHiddenAct && m_cb.onRestoreHidden) m_cb.onRestoreHidden(this);
  else if (chosen == orientAct && m_cb.onToggleOrientation) m_cb.onToggleOrientation(this);
  else if (chosen == fmtAuto && m_cb.onSetFormat) m_cb.onSetFormat(this, QString());
  else if (chosen == fmtHex && m_cb.onSetFormat) m_cb.onSetFormat(this, QStringLiteral("x"));
  else if (chosen == fmtDec && m_cb.onSetFormat) m_cb.onSetFormat(this, QStringLiteral("d"));
  else if (chosen == fmtBin && m_cb.onSetFormat) m_cb.onSetFormat(this, QStringLiteral("b"));
  else if (chosen == unpinAct && m_cb.onUnpinRoot) m_cb.onUnpinRoot(m_expression);
  else if (chosen == resetPosAct && m_cb.onResetPos) m_cb.onResetPos(this);
  event->accept();
}

// ============================================================================
// PointerGraphEdgeItem
// ============================================================================

PointerGraphEdgeItem::PointerGraphEdgeItem(PointerGraphNodeItem* src, QString fieldName, PointerGraphNodeItem* dst)
    : m_source(src), m_fieldName(std::move(fieldName)), m_target(dst) {
  setZValue(-1.0);
  updateGeometry();
}

void PointerGraphEdgeItem::setTargetNode(PointerGraphNodeItem* dst) { m_target = dst; updateGeometry(); }
void PointerGraphEdgeItem::setCycleOrAlias(bool flag) { m_isCycleOrAlias = flag; updateGeometry(); }

void PointerGraphEdgeItem::updateGeometry() {
  if (!m_source || !m_target) return;
  const QPointF p1 = m_source->fieldSourceAnchor(m_fieldName), p2 = m_target->targetAnchor();
  QPainterPath path(p1);
  if (p2.x() >= p1.x() + 20.0) {
    const qreal dx = (p2.x() - p1.x()) * 0.5;
    path.cubicTo(QPointF(p1.x() + dx, p1.y()), QPointF(p2.x() - dx, p2.y()), p2);
  } else {
    const qreal archY = std::max(m_source->sceneBoundingRect().bottom(), m_target->sceneBoundingRect().bottom()) + 75.0;
    path.cubicTo(QPointF(p1.x() + 65.0, archY), QPointF(p2.x() - 65.0, archY), p2);
  }
  m_midPoint = path.pointAtPercent(0.5);
  setPath(path);
  update();
}

void PointerGraphEdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
  painter->setRenderHint(QPainter::Antialiasing, true);
  const QColor edgeColor = m_isCycleOrAlias ? QColor(QStringLiteral("#f59e0b")) : QColor(QStringLiteral("#38bdf8"));
  painter->setPen(QPen(edgeColor, m_isCycleOrAlias ? 2.2 : 1.7, m_isCycleOrAlias ? Qt::DashLine : Qt::SolidLine));
  painter->setBrush(Qt::NoBrush);
  painter->drawPath(path());

  const QPointF p2 = m_target ? m_target->targetAnchor() : QPointF();
  QPolygonF arrow;
  arrow << p2 << QPointF(p2.x() - 8.0, p2.y() - 4.5) << QPointF(p2.x() - 8.0, p2.y() + 4.5);
  painter->setPen(Qt::NoPen);
  painter->setBrush(edgeColor);
  painter->drawPolygon(arrow);

  if (m_isCycleOrAlias) {
    const QRectF pill(m_midPoint.x() - 62.0, m_midPoint.y() - 9.0, 124.0, 18.0);
    painter->setPen(QPen(QColor(QStringLiteral("#f59e0b")), 1.0));
    painter->setBrush(QColor(QStringLiteral("#451a03")));
    painter->drawRoundedRect(pill, 4.0, 4.0);
    QFont f = painter->font();
    f.setPixelSize(9); f.setBold(true); painter->setFont(f);
    painter->setPen(QColor(QStringLiteral("#fde68a")));
    painter->drawText(pill, Qt::AlignCenter, QStringLiteral("CYCLE / SHARED ALIAS"));
  }
}

PointerGraphView::PointerGraphView(QGraphicsScene* scene, QWidget* parent)
    : QGraphicsView(scene, parent) {
  setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  setDragMode(QGraphicsView::ScrollHandDrag);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorViewCenter);
  setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
  if (scene) connect(scene, &QGraphicsScene::changed, viewport(), QOverload<>::of(&QWidget::update));
}

void PointerGraphView::setZoomFactor(double factor) {
  const double clamped = std::clamp(factor, static_cast<double>(kMinZoom), static_cast<double>(kMaxZoom));
  const double cur = zoomFactor();
  if (cur <= 0.0) return;
  const double ratio = clamped / cur;
  if (std::abs(ratio - 1.0) > 1e-4) { scale(ratio, ratio); viewport()->update(); }
}

QRectF PointerGraphView::minimapViewportRect() const {
  if (!viewport() || viewport()->width() < 220 || viewport()->height() < 150) return QRectF();
  if (!scene() || scene()->items().isEmpty()) return QRectF();
  return QRectF(viewport()->width() - kMinimapW - kMinimapMargin,
                viewport()->height() - kMinimapH - kMinimapMargin, kMinimapW, kMinimapH);
}

QRectF PointerGraphView::computeWorldBounds() const {
  if (!scene()) return QRectF(0.0, 0.0, 1.0, 1.0);
  QRectF world = scene()->itemsBoundingRect();
  const QRectF cam = mapToScene(viewport()->rect()).boundingRect();
  return (world.isEmpty() ? cam : world.united(cam)).adjusted(-40.0, -40.0, 40.0, 40.0);
}

QPointF PointerGraphView::minimapPointToScene(const QPointF& viewPos) const {
  const QRectF mm = minimapViewportRect().adjusted(6.0, 6.0, -6.0, -6.0), world = computeWorldBounds();
  if (mm.isEmpty() || world.width() <= 0.0 || world.height() <= 0.0) return QPointF();
  const qreal s = std::min(mm.width() / world.width(), mm.height() / world.height());
  const qreal ox = mm.left() + (mm.width() - world.width() * s) * 0.5;
  const qreal oy = mm.top() + (mm.height() - world.height() * s) * 0.5;
  return QPointF(world.left() + (viewPos.x() - ox) / s, world.top() + (viewPos.y() - oy) / s);
}

void PointerGraphView::wheelEvent(QWheelEvent* event) {
  if (!event) return;
  const int dy = (event->angleDelta().y() != 0) ? event->angleDelta().y() : event->pixelDelta().y();
  if (dy != 0) setZoomFactor(zoomFactor() * ((dy > 0) ? 1.15 : (1.0 / 1.15)));
  event->accept();
}

void PointerGraphView::paintEvent(QPaintEvent* event) {
  QGraphicsView::paintEvent(event);
  const QRectF mm = minimapViewportRect();
  if (mm.isEmpty()) return;

  QPainter p(viewport());
  p.setRenderHint(QPainter::Antialiasing, true);
  const bool isDark = core::ThemeManager::instance().isDarkMode();
  p.setPen(QPen(isDark ? QColor(QStringLiteral("#334155")) : QColor(QStringLiteral("#94a3b8")), 1.1));
  p.setBrush(isDark ? QColor(15, 23, 42, 220) : QColor(248, 250, 252, 230));
  p.drawRoundedRect(mm, 6.0, 6.0);

  const QRectF inner = mm.adjusted(6.0, 6.0, -6.0, -6.0), world = computeWorldBounds();
  if (world.width() <= 0.0 || world.height() <= 0.0) return;
  const qreal s = std::min(inner.width() / world.width(), inner.height() / world.height());
  const qreal ox = inner.left() + (inner.width() - world.width() * s) * 0.5;
  const qreal oy = inner.top() + (inner.height() - world.height() * s) * 0.5;
  auto mapRect = [&](const QRectF& r) {
    return QRectF(ox + (r.left() - world.left()) * s, oy + (r.top() - world.top()) * s,
                  std::max<qreal>(3.0, r.width() * s), std::max<qreal>(2.5, r.height() * s));
  };

  p.save();
  p.setClipRect(inner);
  for (QGraphicsItem* item : scene()->items()) {
    if (auto* node = dynamic_cast<PointerGraphNodeItem*>(item)) {
      p.setPen(QPen(node->isRoot() ? QColor(QStringLiteral("#f59e0b")) : QColor(QStringLiteral("#38bdf8")), 1.0));
      p.setBrush(isDark ? QColor(QStringLiteral("#1e293b")) : QColor(QStringLiteral("#cbd5e1")));
      p.drawRoundedRect(mapRect(node->sceneBoundingRect()), 1.5, 1.5);
    }
  }
  p.setPen(QPen(QColor(QStringLiteral("#38bdf8")), 1.4));
  p.setBrush(QColor(56, 189, 248, 38));
  p.drawRect(mapRect(mapToScene(viewport()->rect()).boundingRect()).intersected(inner));
  p.restore();
}

void PointerGraphView::mousePressEvent(QMouseEvent* event) {
  if (event && event->button() == Qt::LeftButton && minimapViewportRect().contains(event->position())) {
    m_panningMinimap = true;
    centerOn(minimapPointToScene(event->position()));
    viewport()->update();
    event->accept();
    return;
  }
  QGraphicsView::mousePressEvent(event);
}

void PointerGraphView::mouseMoveEvent(QMouseEvent* event) {
  if (event && m_panningMinimap) {
    centerOn(minimapPointToScene(event->position()));
    viewport()->update();
    event->accept();
    return;
  }
  QGraphicsView::mouseMoveEvent(event);
}

void PointerGraphView::mouseReleaseEvent(QMouseEvent* event) {
  if (event && m_panningMinimap && event->button() == Qt::LeftButton) {
    m_panningMinimap = false;
    event->accept();
    return;
  }
  QGraphicsView::mouseReleaseEvent(event);
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

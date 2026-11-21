/**
 * @file PointerGraphLayout.cpp
 * @brief Implementation of Sugiyama-style layered DAG layout, barycenter crossing minimization,
 *        cycle/shared-alias analysis, orphan pruning, and QGraphicsScene card/edge items.
 */

#include "PointerGraphLayout.hpp"

#include "../../core/managers/ThemeManager.hpp"

#include <QFontDatabase>
#include <QGraphicsSceneContextMenuEvent>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QQueue>
#include <algorithm>
#include <cmath>

namespace gridlock::ui {

namespace {
constexpr qreal kCardWidth = 290.0;
constexpr qreal kHeaderHeight = 30.0;
constexpr qreal kRowHeight = 24.0;
constexpr qreal kFooterPadding = 8.0;

/** @brief Returns true when a directed path exists from @p startIdx to @p goalIdx. */
bool canReachIndex(int nodeCount,
                   const QVector<QVector<int>>& adj,
                   int startIdx,
                   int goalIdx) {
  if (startIdx < 0 || goalIdx < 0 || startIdx >= nodeCount || goalIdx >= nodeCount) {
    return false;
  }
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

// ============================================================================
// PointerGraphNodeItem
// ============================================================================

PointerGraphNodeItem::PointerGraphNodeItem(
    QString expr,
    QString type,
    QString addr,
    std::function<void(PointerGraphNodeItem*)> movedCb,
    std::function<void(const QString&, const QString&)> expandCb,
    std::function<void(const QString&)> unpinCb,
    std::function<void(PointerGraphNodeItem*)> resetPosCb)
    : m_expression(std::move(expr)),
      m_typeName(std::move(type)),
      m_address(std::move(addr)),
      m_onMoved(std::move(movedCb)),
      m_onExpandField(std::move(expandCb)),
      m_onUnpinRoot(std::move(unpinCb)),
      m_onResetPos(std::move(resetPosCb)) {
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

int PointerGraphNodeItem::indexOfField(const QString& fieldName) const {
  for (int i = 0; i < m_fields.size(); ++i) {
    if (m_fields[i].name == fieldName) return i;
  }
  return -1;
}

QPointF PointerGraphNodeItem::fieldSourceAnchor(const QString& fieldName) const {
  const int idx = indexOfField(fieldName);
  const qreal rowY = (idx >= 0) ? (kHeaderHeight + idx * kRowHeight + kRowHeight * 0.5)
                                : (kHeaderHeight * 0.5);
  return mapToScene(QPointF(kCardWidth, rowY));
}

QPointF PointerGraphNodeItem::targetAnchor() const {
  return mapToScene(QPointF(0.0, kHeaderHeight * 0.5));
}

QRectF PointerGraphNodeItem::boundingRect() const {
  const int rows = qMax(1, static_cast<int>(m_fields.size()));
  return QRectF(0.0, 0.0, kCardWidth, kHeaderHeight + rows * kRowHeight + kFooterPadding);
}

void PointerGraphNodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
  painter->setRenderHint(QPainter::Antialiasing, true);
  const bool isDark = core::ThemeManager::instance().isDarkMode();
  const QRectF rect = boundingRect();

  const QColor bgColor = isDark ? QColor(QStringLiteral("#181c24")) : QColor(QStringLiteral("#ffffff"));
  const QColor borderColor = isSelected() ? QColor(QStringLiteral("#38bdf8"))
                                          : (!m_mutatedTransitions.isEmpty()
                                                 ? QColor(QStringLiteral("#f59e0b"))
                                                 : (isDark ? QColor(QStringLiteral("#334155")) : QColor(QStringLiteral("#cbd5e1"))));
  painter->setPen(QPen(borderColor, (isSelected() || !m_mutatedTransitions.isEmpty()) ? 2.0 : 1.2));
  painter->setBrush(bgColor);
  painter->drawRoundedRect(rect, 7.0, 7.0);

  QPainterPath hdrPath;
  hdrPath.addRoundedRect(QRectF(0.0, 0.0, kCardWidth, kHeaderHeight), 7.0, 7.0);
  painter->fillPath(hdrPath, isDark ? QColor(QStringLiteral("#1e293b")) : QColor(QStringLiteral("#e2e8f0")));
  QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  mono.setPixelSize(11);
  mono.setBold(true);
  painter->setFont(mono);
  painter->setPen(isDark ? QColor(QStringLiteral("#f8fafc")) : QColor(QStringLiteral("#0f172a")));

  QString hdrText = m_expression;
  if (!m_typeName.isEmpty()) hdrText += QStringLiteral(" : %1").arg(m_typeName);
  if (!m_address.isEmpty()) hdrText += QStringLiteral(" @ %1").arg(m_address);
  const qreal hdrRightMargin = m_isRoot ? 30.0 : 10.0;
  painter->drawText(QRectF(10.0, 0.0, kCardWidth - 10.0 - hdrRightMargin, kHeaderHeight),
                    Qt::AlignVCenter | Qt::AlignLeft,
                    painter->fontMetrics().elidedText(hdrText, Qt::ElideRight,
                                                      static_cast<int>(kCardWidth - 12.0 - hdrRightMargin)));

  if (m_isRoot) {
    const QRectF unpinRect(kCardWidth - 24.0, 6.0, 18.0, 18.0);
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
    painter->drawText(QRectF(10.0, kHeaderHeight, kCardWidth - 20.0, kRowHeight),
                      Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("(double-click or expand fields...)"));
    return;
  }

  for (int i = 0; i < m_fields.size(); ++i) {
    const qreal y = kHeaderHeight + i * kRowHeight;
    const auto& f = m_fields[i];
    const bool isMutated = m_mutatedTransitions.contains(f.name);
    const bool expandable = isExpandableOrPointerField(f);
    const bool nullPtr = !expandable && (f.type.contains(QLatin1Char('*')) || isNullPointerValue(f.value));
    const qreal rightPad = (expandable || nullPtr) ? 68.0 : 10.0;

    if (isMutated) {
      painter->fillRect(QRectF(2.0, y + 1.0, kCardWidth - 4.0, kRowHeight - 2.0),
                        isDark ? QColor(QStringLiteral("#451a03")) : QColor(QStringLiteral("#fef3c7")));
    }

    painter->setPen(isMutated
                        ? (isDark ? QColor(QStringLiteral("#fbbf24")) : QColor(QStringLiteral("#b45309")))
                        : (isDark ? QColor(QStringLiteral("#e2e8f0")) : QColor(QStringLiteral("#1e293b"))));
    QString rowTxt = f.type.isEmpty()
                         ? QStringLiteral("%1 : %2").arg(f.name, f.value)
                         : QStringLiteral("%1 : %2 (%3)").arg(f.name, f.value, f.type);
    if (isMutated) rowTxt += QStringLiteral(" [%1]").arg(m_mutatedTransitions.value(f.name));
    painter->drawText(QRectF(10.0, y, kCardWidth - 10.0 - rightPad, kRowHeight),
                      Qt::AlignVCenter | Qt::AlignLeft,
                      painter->fontMetrics().elidedText(rowTxt, Qt::ElideRight,
                                                        static_cast<int>(kCardWidth - 14.0 - rightPad)));

    if (expandable || nullPtr) {
      const QRectF badgeRect(kCardWidth - 62.0, y + 3.0, 54.0, kRowHeight - 6.0);
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

QVariant PointerGraphNodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
  if (change == ItemPositionHasChanged && m_onMoved) m_onMoved(this);
  return QGraphicsObject::itemChange(change, value);
}

void PointerGraphNodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
  if (event && event->button() == Qt::LeftButton) {
    const QPointF p = event->pos();
    if (m_isRoot && p.y() >= 0.0 && p.y() <= kHeaderHeight && p.x() >= kCardWidth - 26.0 && m_onUnpinRoot) {
      m_onUnpinRoot(m_expression);
      event->accept();
      return;
    }
    const int row = static_cast<int>((p.y() - kHeaderHeight) / kRowHeight);
    if (row >= 0 && row < m_fields.size() && p.x() >= kCardWidth - 64.0 &&
        isExpandableOrPointerField(m_fields[row]) && m_onExpandField) {
      m_onExpandField(m_expression, m_fields[row].name);
      event->accept();
      return;
    }
  }
  QGraphicsObject::mousePressEvent(event);
}

void PointerGraphNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
  if (event) {
    const int row = static_cast<int>((event->pos().y() - kHeaderHeight) / kRowHeight);
    if (row >= 0 && row < m_fields.size() && m_onExpandField) {
      m_onExpandField(m_expression, m_fields[row].name);
      event->accept();
      return;
    }
  }
  QGraphicsObject::mouseDoubleClickEvent(event);
}

void PointerGraphNodeItem::contextMenuEvent(QGraphicsSceneContextMenuEvent* event) {
  if (!event) return;
  QMenu menu;
  QAction* unpinAct = m_isRoot ? menu.addAction(QStringLiteral("Unpin Root (%1)").arg(m_expression)) : nullptr;
  QAction* resetPosAct = menu.addAction(QStringLiteral("Reset Card Position"));
  QAction* chosen = menu.exec(event->screenPos());
  if (chosen && chosen == unpinAct && m_onUnpinRoot) {
    m_onUnpinRoot(m_expression);
  } else if (chosen && chosen == resetPosAct && m_onResetPos) {
    m_onResetPos(this);
  }
  event->accept();
}

// ============================================================================
// PointerGraphEdgeItem
// ============================================================================

PointerGraphEdgeItem::PointerGraphEdgeItem(PointerGraphNodeItem* src,
                                           QString fieldName,
                                           PointerGraphNodeItem* dst)
    : m_source(src), m_fieldName(std::move(fieldName)), m_target(dst) {
  setZValue(-1.0);
  updateGeometry();
}

void PointerGraphEdgeItem::setTargetNode(PointerGraphNodeItem* dst) {
  m_target = dst;
  updateGeometry();
}

void PointerGraphEdgeItem::setCycleOrAlias(bool flag) {
  m_isCycleOrAlias = flag;
  updateGeometry();
}

void PointerGraphEdgeItem::updateGeometry() {
  if (!m_source || !m_target) return;
  const QPointF p1 = m_source->fieldSourceAnchor(m_fieldName);
  const QPointF p2 = m_target->targetAnchor();
  QPainterPath path(p1);
  if (p2.x() >= p1.x() + 20.0) {
    const qreal dx = (p2.x() - p1.x()) * 0.5;
    path.cubicTo(QPointF(p1.x() + dx, p1.y()), QPointF(p2.x() - dx, p2.y()), p2);
  } else {
    const qreal archY = std::max(m_source->sceneBoundingRect().bottom(),
                                 m_target->sceneBoundingRect().bottom()) + 75.0;
    path.cubicTo(QPointF(p1.x() + 65.0, archY), QPointF(p2.x() - 65.0, archY), p2);
  }
  m_midPoint = path.pointAtPercent(0.5);
  setPath(path);
  update();
}

void PointerGraphEdgeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
  painter->setRenderHint(QPainter::Antialiasing, true);
  const QColor edgeColor = m_isCycleOrAlias ? QColor(QStringLiteral("#f59e0b"))
                                            : QColor(QStringLiteral("#38bdf8"));
  painter->setPen(QPen(edgeColor, m_isCycleOrAlias ? 2.2 : 1.7,
                       m_isCycleOrAlias ? Qt::DashLine : Qt::SolidLine));
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
    f.setPixelSize(9);
    f.setBold(true);
    painter->setFont(f);
    painter->setPen(QColor(QStringLiteral("#fde68a")));
    painter->drawText(pill, Qt::AlignCenter, QStringLiteral("CYCLE / SHARED ALIAS"));
  }
}

// ============================================================================
// Sugiyama-Style Layered DAG Layout, Incremental Placement & Graph Analysis
// ============================================================================

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
  QVector<QVector<PointerLayoutEdge>> inEdges(n);
  QVector<QVector<PointerLayoutEdge>> outEdges(n);
  QVector<int> inDegree(n, 0);

  for (const auto& e : edges) {
    if (e.sourceIndex < 0 || e.sourceIndex >= n || e.targetIndex < 0 || e.targetIndex >= n) continue;
    if (e.sourceIndex == e.targetIndex) continue;
    outAdj[e.sourceIndex].append(e.targetIndex);
    outEdges[e.sourceIndex].append(e);
    inEdges[e.targetIndex].append(e);
    ++inDegree[e.targetIndex];
  }

  auto runBfsFromSeeds = [&](const QVector<int>& seeds) {
    QQueue<int> q;
    for (int s : seeds) {
      if (result.layers[s] == -1) {
        result.layers[s] = 0;
        q.enqueue(s);
      }
    }
    while (!q.isEmpty()) {
      const int u = q.dequeue();
      for (int v : outAdj[u]) {
        if (result.layers[v] == -1) {
          result.layers[v] = result.layers[u] + 1;
          q.enqueue(v);
        }
      }
    }
  };

  QVector<int> rootSeeds;
  for (int i = 0; i < n; ++i) {
    if (nodes[i].isRoot) rootSeeds.append(i);
  }
  runBfsFromSeeds(rootSeeds);

  QVector<int> zeroInSeeds;
  for (int i = 0; i < n; ++i) {
    if (result.layers[i] == -1 && inDegree[i] == 0) zeroInSeeds.append(i);
  }
  runBfsFromSeeds(zeroInSeeds);

  for (int i = 0; i < n; ++i) {
    if (result.layers[i] == -1) runBfsFromSeeds({i});
  }

  int maxLayer = 0;
  for (int layer : result.layers) {
    if (layer > maxLayer) maxLayer = layer;
  }

  QVector<QVector<int>> layerNodes(maxLayer + 1);
  QVector<int> orderInLayer(n, 0);
  for (int i = 0; i < n; ++i) {
    const int l = result.layers[i];
    orderInLayer[i] = layerNodes[l].size();
    layerNodes[l].append(i);
  }

  auto refreshLayerOrder = [&](int l) {
    for (int idx = 0; idx < layerNodes[l].size(); ++idx) {
      orderInLayer[layerNodes[l][idx]] = idx;
    }
  };

  constexpr int kBarycenterPasses = 2;
  for (int pass = 0; pass < kBarycenterPasses; ++pass) {
    for (int l = 1; l <= maxLayer; ++l) {
      QVector<double> bary(n, 0.0);
      for (int v : layerNodes[l]) {
        double sum = 0.0;
        int count = 0;
        for (const auto& e : inEdges[v]) {
          if (result.layers[e.sourceIndex] < l) {
            const double rowBias = std::min(0.45, std::max(0, e.sourceRow) * 0.05);
            sum += static_cast<double>(orderInLayer[e.sourceIndex]) + rowBias;
            ++count;
          }
        }
        bary[v] = (count > 0) ? (sum / static_cast<double>(count))
                              : static_cast<double>(orderInLayer[v]);
      }
      std::stable_sort(layerNodes[l].begin(), layerNodes[l].end(), [&](int a, int b) {
        if (std::abs(bary[a] - bary[b]) > 1e-6) return bary[a] < bary[b];
        return a < b;
      });
      refreshLayerOrder(l);
    }

    for (int l = maxLayer - 1; l >= 0; --l) {
      QVector<double> bary(n, 0.0);
      for (int u : layerNodes[l]) {
        double sum = 0.0;
        int count = 0;
        for (const auto& e : outEdges[u]) {
          if (result.layers[e.targetIndex] > l) {
            const double rowBias = std::min(0.45, std::max(0, e.sourceRow) * 0.05);
            sum += static_cast<double>(orderInLayer[e.targetIndex]) - rowBias;
            ++count;
          }
        }
        bary[u] = (count > 0) ? (sum / static_cast<double>(count))
                              : static_cast<double>(orderInLayer[u]);
      }
      std::stable_sort(layerNodes[l].begin(), layerNodes[l].end(), [&](int a, int b) {
        if (std::abs(bary[a] - bary[b]) > 1e-6) return bary[a] < bary[b];
        return a < b;
      });
      refreshLayerOrder(l);
    }
  }

  QVector<qreal> layerX(maxLayer + 1, 0.0);
  QVector<qreal> layerHeights(maxLayer + 1, 0.0);
  qreal maxLayerHeight = 0.0;
  for (int l = 0; l <= maxLayer; ++l) {
    if (l > 0) {
      qreal prevMaxW = config.defaultCardWidth;
      for (int u : layerNodes[l - 1]) prevMaxW = std::max(prevMaxW, nodes[u].size.width());
      layerX[l] = layerX[l - 1] + prevMaxW + config.layerHorizontalGap;
    }
    qreal totalH = 0.0;
    for (int idx = 0; idx < layerNodes[l].size(); ++idx) {
      totalH += std::max<qreal>(80.0, nodes[layerNodes[l][idx]].size.height());
      if (idx + 1 < layerNodes[l].size()) totalH += config.verticalSiblingGap;
    }
    layerHeights[l] = totalH;
    maxLayerHeight = std::max(maxLayerHeight, totalH);
  }

  for (int l = 0; l <= maxLayer; ++l) {
    qreal y = std::max<qreal>(0.0, (maxLayerHeight - layerHeights[l]) * 0.5);
    for (int u : layerNodes[l]) {
      result.positions[u] = (respectPinned && nodes[u].isPinned)
                                ? nodes[u].currentPos
                                : QPointF(layerX[l], y);
      y += std::max<qreal>(80.0, nodes[u].size.height()) + config.verticalSiblingGap;
    }
  }

  return result;
}

QPointF computeIncrementalNodePosition(bool hasParent,
                                       const QPointF& parentPos,
                                       const QSizeF& parentSize,
                                       const QSizeF& newNodeSize,
                                       const QVector<QRectF>& occupiedRects,
                                       const PointerLayoutConfig& config) {
  const qreal cardW = std::max(config.defaultCardWidth, newNodeSize.width());
  const qreal cardH = std::max<qreal>(80.0, newNodeSize.height());
  const qreal parentW = std::max(config.defaultCardWidth, parentSize.width());

  const qreal candX = hasParent ? (parentPos.x() + parentW + config.layerHorizontalGap) : 0.0;
  qreal candY = hasParent ? parentPos.y() : 0.0;

  const qreal padX = 12.0;
  const qreal padY = config.verticalSiblingGap * 0.5;
  for (int guard = 0; guard < 256; ++guard) {
    const QRectF candidate(candX - padX, candY - padY, cardW + 2.0 * padX, cardH + 2.0 * padY);
    qreal pushBottom = -1.0;
    bool collided = false;
    for (const QRectF& occ : occupiedRects) {
      if (candidate.intersects(occ)) {
        collided = true;
        pushBottom = std::max(pushBottom, occ.bottom());
      }
    }
    if (!collided) break;
    candY = pushBottom + config.verticalSiblingGap;
  }

  return QPointF(candX, candY);
}

AliasCycleAnalysis analyzeCyclesAndAliases(int nodeCount,
                                           const QVector<PointerLayoutEdge>& edges,
                                           const QVector<QPointF>& nodePositions) {
  AliasCycleAnalysis out;
  out.edgeIsCycleOrAlias.fill(false, edges.size());
  out.nodeInDegree.fill(0, nodeCount);
  if (nodeCount <= 0 || edges.isEmpty()) return out;

  QVector<QVector<int>> adj(nodeCount);
  for (const auto& e : edges) {
    if (e.sourceIndex >= 0 && e.sourceIndex < nodeCount &&
        e.targetIndex >= 0 && e.targetIndex < nodeCount) {
      adj[e.sourceIndex].append(e.targetIndex);
      ++out.nodeInDegree[e.targetIndex];
    }
  }

  QSet<int> primaryTargetSeen;
  for (int i = 0; i < edges.size(); ++i) {
    const int src = edges[i].sourceIndex;
    const int dst = edges[i].targetIndex;
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

QSet<int> findUnreachableNonRootNodes(int nodeCount,
                                      const QSet<int>& rootIndices,
                                      const QVector<PointerLayoutEdge>& edges) {
  QSet<int> unreachable;
  if (nodeCount <= 0) return unreachable;

  QVector<QVector<int>> adj(nodeCount);
  for (const auto& e : edges) {
    if (e.sourceIndex >= 0 && e.sourceIndex < nodeCount &&
        e.targetIndex >= 0 && e.targetIndex < nodeCount) {
      adj[e.sourceIndex].append(e.targetIndex);
    }
  }

  QVector<bool> reachable(nodeCount, false);
  QQueue<int> q;
  for (int r : rootIndices) {
    if (r >= 0 && r < nodeCount && !reachable[r]) {
      reachable[r] = true;
      q.enqueue(r);
    }
  }
  while (!q.isEmpty()) {
    const int cur = q.dequeue();
    for (int nxt : adj[cur]) {
      if (!reachable[nxt]) {
        reachable[nxt] = true;
        q.enqueue(nxt);
      }
    }
  }

  for (int i = 0; i < nodeCount; ++i) {
    if (!reachable[i] && !rootIndices.contains(i)) unreachable.insert(i);
  }
  return unreachable;
}

}  // namespace gridlock::ui

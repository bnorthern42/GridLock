/**
 * @file PointerGraphLayout.hpp
 * @brief Sugiyama-style layered DAG layout, cycle/alias analysis, and QGraphicsScene card/edge items.
 *
 * Encapsulates both the pure layered DAG layout math (BFS topological layering,
 * barycenter crossing minimization, incremental child placement, and orphan pruning)
 * and the QGraphicsScene card/edge item classes used by PointerGraphDialog.
 */

#pragma once

#include "PointerGraphDialog.hpp"

#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <functional>

class QGraphicsSceneContextMenuEvent;
class QGraphicsSceneMouseEvent;

namespace gridlock::ui {

/** @brief Returns true when @p v represents a null pointer (`0x0`, `0x00000000`, `nullptr`). */
bool isNullPointerValue(const QString& v);

/** @brief Returns true when a struct field represents a non-null pointer or composite sub-object. */
bool isExpandableOrPointerField(const PointerGraphDialog::StructFieldEntry& f);

/** @brief Strips trailing pointer qualifiers (`*`) for clean child node type labels. */
QString dereferencedTypeName(const QString& rawType);

/**
 * @struct PointerLayoutNode
 * @brief Lightweight layout descriptor for a single card in the pointer graph.
 */
struct PointerLayoutNode {
  QString key;           ///< Canonical key (normalized hex address if present, else expression).
  QSizeF size;           ///< Bounding dimensions of the rendered card (`width x height`).
  bool isRoot = false;   ///< True if the card represents a pinned root expression.
  bool isPinned = false; ///< True if the user manually positioned this card.
  QPointF currentPos;    ///< Existing scene position (preserved when `isPinned == true`).
};

/**
 * @struct PointerLayoutEdge
 * @brief Lightweight directed edge between two indices in the layout node array.
 */
struct PointerLayoutEdge {
  int sourceIndex = -1; ///< Index of the source node card.
  int targetIndex = -1; ///< Index of the destination node card.
  int sourceRow = 0;    ///< Field row index on the source card (preserves field ordering).
};

/**
 * @struct PointerLayoutConfig
 * @brief Spacing parameters controlling layered DAG and incremental node placement.
 */
struct PointerLayoutConfig {
  qreal layerHorizontalGap = 130.0; ///< Horizontal gap between adjacent topological layers.
  qreal verticalSiblingGap = 36.0;  ///< Vertical gap between cards stacked in the same layer.
  qreal defaultCardWidth = 290.0;   ///< Default card width fallback for empty layers.
};

/**
 * @struct PointerLayoutResult
 * @brief Output topological layers and scene coordinates computed for all nodes.
 */
struct PointerLayoutResult {
  QVector<int> layers;        ///< Assigned topological rank layer (`0..maxLayer`) per node.
  QVector<QPointF> positions; ///< Assigned scene `(x, y)` per node (honoring pinned nodes).
};

/**
 * @struct AliasCycleAnalysis
 * @brief Per-edge cycle/shared-alias classification and per-node incoming edge degrees.
 */
struct AliasCycleAnalysis {
  QVector<bool> edgeIsCycleOrAlias; ///< True when edge is a self-loop, back-edge, or shared alias.
  QVector<int> nodeInDegree;        ///< Incoming edge count per node.
  bool anyCycleOrAlias = false;     ///< True if at least one edge participates in a cycle or alias.
};

/**
 * @class PointerGraphNodeItem
 * @brief Draggable 2D struct/class card rendering header metadata, root unpin control,
 *        field slots, mutation diffs, and clickable `[➔ Deref]` pills.
 */
class PointerGraphNodeItem : public QGraphicsObject {
 public:
  PointerGraphNodeItem(QString expr,
                       QString type,
                       QString addr,
                       std::function<void(PointerGraphNodeItem*)> movedCb,
                       std::function<void(const QString&, const QString&)> expandCb,
                       std::function<void(const QString&)> unpinCb,
                       std::function<void(PointerGraphNodeItem*)> resetPosCb);

  void updateData(const QString& expr,
                  const QString& type,
                  const QString& addr,
                  const QVector<PointerGraphDialog::StructFieldEntry>& fields);
  void setIsRoot(bool isRoot);
  void setMutatedFields(const QMap<QString, QString>& transitions);

  const QString& expression() const { return m_expression; }
  const QString& typeName() const { return m_typeName; }
  const QString& address() const { return m_address; }
  bool isRoot() const { return m_isRoot; }
  const QVector<PointerGraphDialog::StructFieldEntry>& fields() const { return m_fields; }

  int indexOfField(const QString& fieldName) const;
  QPointF fieldSourceAnchor(const QString& fieldName) const;
  QPointF targetAnchor() const;
  QRectF boundingRect() const override;
  void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

 protected:
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
  void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
  void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
  void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

 private:
  QString m_expression;
  QString m_typeName;
  QString m_address;
  bool m_isRoot = false;
  QVector<PointerGraphDialog::StructFieldEntry> m_fields;
  QMap<QString, QString> m_mutatedTransitions;
  std::function<void(PointerGraphNodeItem*)> m_onMoved;
  std::function<void(const QString&, const QString&)> m_onExpandField;
  std::function<void(const QString&)> m_onUnpinRoot;
  std::function<void(PointerGraphNodeItem*)> m_onResetPos;
};

/**
 * @class PointerGraphEdgeItem
 * @brief Directed cubic Bezier arrow connecting a source struct field slot to a target node card.
 */
class PointerGraphEdgeItem : public QGraphicsPathItem {
 public:
  PointerGraphEdgeItem(PointerGraphNodeItem* src, QString fieldName, PointerGraphNodeItem* dst);

  PointerGraphNodeItem* sourceNode() const { return m_source; }
  const QString& sourceField() const { return m_fieldName; }
  PointerGraphNodeItem* targetNode() const { return m_target; }
  bool isCycleOrAlias() const { return m_isCycleOrAlias; }

  void setTargetNode(PointerGraphNodeItem* dst);
  void setCycleOrAlias(bool flag);
  void updateGeometry();
  void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

 private:
  PointerGraphNodeItem* m_source = nullptr;
  QString m_fieldName;
  PointerGraphNodeItem* m_target = nullptr;
  QPointF m_midPoint;
  bool m_isCycleOrAlias = false;
};

/** @brief Computes a Sugiyama-style layered DAG layout with barycenter crossing minimization. */
PointerLayoutResult computeLayeredDagLayout(const QVector<PointerLayoutNode>& nodes,
                                            const QVector<PointerLayoutEdge>& edges,
                                            bool respectPinned,
                                            const PointerLayoutConfig& config = {});

/** @brief Computes a non-overlapping incremental scene position for a single newly discovered card. */
QPointF computeIncrementalNodePosition(bool hasParent,
                                       const QPointF& parentPos,
                                       const QSizeF& parentSize,
                                       const QSizeF& newNodeSize,
                                       const QVector<QRectF>& occupiedRects,
                                       const PointerLayoutConfig& config = {});

/** @brief Classifies directed edges for cycles (self-loops/back-edges) and shared aliases (in-degree > 1). */
AliasCycleAnalysis analyzeCyclesAndAliases(int nodeCount,
                                           const QVector<PointerLayoutEdge>& edges,
                                           const QVector<QPointF>& nodePositions);

/** @brief Returns indices of non-root nodes unreachable from any pinned root via active directed edges. */
QSet<int> findUnreachableNonRootNodes(int nodeCount,
                                      const QSet<int>& rootIndices,
                                      const QVector<PointerLayoutEdge>& edges);

}  // namespace gridlock::ui

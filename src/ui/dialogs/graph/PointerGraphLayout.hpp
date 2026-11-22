/**
 * @file PointerGraphLayout.hpp
 * @brief Sugiyama-style layered DAG layout, scalar formatters, zoom/minimap QGraphicsView,
 *        and interactive QGraphicsScene struct/edge items.
 */

#pragma once

#include "PointerGraphDialog.hpp"

#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QMap>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <functional>

class QEvent;
class QGraphicsSceneContextMenuEvent;
class QGraphicsSceneHelpEvent;
class QGraphicsSceneHoverEvent;
class QGraphicsSceneMouseEvent;
class QMouseEvent;
class QPaintEvent;
class QWheelEvent;

namespace gridlock::ui {

/** @brief Returns true when @p v represents a null pointer (`0x0`, `0x00000000`, `nullptr`). */
bool isNullPointerValue(const QString& v);

/** @brief Returns true when @p rawType is a C/C++ pointer type (`T *`, excluding function pointers). */
bool isPointerTypeString(const QString& rawType);

/** @brief Returns true when @p rawType is `void *` / `const void *` (opaque, non-dereferenceable). */
bool isVoidPointerType(const QString& rawType);

/** @brief Returns true when a struct field represents a non-null pointer or composite sub-object. */
bool isExpandableOrPointerField(const PointerGraphDialog::StructFieldEntry& f);

/** @brief Strips trailing pointer qualifiers (`*`) for clean child node type labels. */
QString dereferencedTypeName(const QString& rawType);

/**
 * @brief Formats scalar/integer/pointer @p rawValue using per-node @p formatOverride (`""`, `"x"`, `"d"`, `"b"`).
 */
QString formatScalarWithOverride(const QString& rawValue,
                                 const QString& rawType,
                                 const QString& formatOverride);

struct PointerLayoutNode {
  QString key;
  QSizeF size;
  bool isRoot = false;
  bool isPinned = false;
  QPointF currentPos;
};

struct PointerLayoutEdge {
  int sourceIndex = -1;
  int targetIndex = -1;
  int sourceRow = 0;
};

struct PointerLayoutConfig {
  qreal layerHorizontalGap = 130.0;
  qreal verticalSiblingGap = 36.0;
  qreal defaultCardWidth = 290.0;
};

struct PointerLayoutResult {
  QVector<int> layers;
  QVector<QPointF> positions;
};

struct AliasCycleAnalysis {
  QVector<bool> edgeIsCycleOrAlias;
  QVector<int> nodeInDegree;
  bool anyCycleOrAlias = false;
};

/**
 * @struct PointerNodeCallbacks
 * @brief Callbacks connecting interactive card gestures back to PointerGraphDialog.
 */
struct PointerNodeCallbacks {
  std::function<void(PointerGraphNodeItem*)> onMoved;
  std::function<void(const QString&)> onExpandNode;
  std::function<void(const QString&, const QString&)> onExpandField;
  std::function<void(const QString&)> onUnpinRoot;
  std::function<void(PointerGraphNodeItem*)> onResetPos;
  std::function<void(PointerGraphNodeItem*, const QString&)> onHideField;
  std::function<void(PointerGraphNodeItem*)> onRestoreHidden;
  std::function<void(PointerGraphNodeItem*)> onToggleOrientation;
  std::function<void(PointerGraphNodeItem*)> onToggleExpandedWidth;
  std::function<void(PointerGraphNodeItem*, const QString&)> onSetFormat;
};

/**
 * @class PointerGraphNodeItem
 * @brief Draggable 2D struct/class card supporting vertical/horizontal layouts,
 *        per-row hover tooltips, Shift+Click width expansion, per-field suppression,
 *        and `/x`, `/d`, `/b` format overrides.
 */
class PointerGraphNodeItem : public QGraphicsObject {
 public:
  enum class PlaceholderState {
    Unexpanded,  ///< `(double-click or expand fields...)`
    Loading,     ///< `(loading fields...)`
    NullPointer, ///< `(null pointer)`
    NoFields     ///< `(no fields / opaque type)`
  };

  PointerGraphNodeItem(QString expr,
                       QString type,
                       QString addr,
                       PointerNodeCallbacks callbacks);

  void updateData(const QString& expr,
                  const QString& type,
                  const QString& addr,
                  const QVector<PointerGraphDialog::StructFieldEntry>& fields);
  void setIsRoot(bool isRoot);
  void setMutatedFields(const QMap<QString, QString>& transitions);
  void setDisplayOptions(const PointerGraphDialog::NodeDisplayOptions& opts);
  void setPlaceholderState(PlaceholderState state);

  const QString& expression() const { return m_expression; }
  const QString& typeName() const { return m_typeName; }
  const QString& address() const { return m_address; }
  bool isRoot() const { return m_isRoot; }
  PlaceholderState placeholderState() const { return m_placeholderState; }
  QString placeholderStatusText() const;
  const PointerGraphDialog::NodeDisplayOptions& displayOptions() const { return m_displayOptions; }
  const QVector<PointerGraphDialog::StructFieldEntry>& fields() const { return m_fields; }

  QVector<int> visibleFieldIndices() const;
  int hiddenFieldCount() const;
  QString hiddenBadgeText() const;
  QString formattedFieldValue(int fieldIndex) const;
  QString headerText() const;
  QString fieldRowText(int fieldIndex) const;
  QString tooltipForPosition(const QPointF& itemPos) const;
  bool handleDoubleClickAt(const QPointF& itemPos);

  int indexOfField(const QString& fieldName) const;
  QPointF fieldSourceAnchor(const QString& fieldName) const;
  QPointF targetAnchor() const;
  QRectF boundingRect() const override;
  void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override;

 protected:
  bool sceneEvent(QEvent* event) override;
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
  void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
  void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
  void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
  void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
  void contextMenuEvent(QGraphicsSceneContextMenuEvent* event) override;

 private:
  qreal computeEffectiveWidth() const;
  int hitTestVisibleSlot(const QPointF& itemPos, bool* hitDerefBadgeOut = nullptr) const;
  bool hitTestHiddenPill(const QPointF& itemPos) const;

  QString m_expression;
  QString m_typeName;
  QString m_address;
  bool m_isRoot = false;
  PlaceholderState m_placeholderState = PlaceholderState::Unexpanded;
  PointerGraphDialog::NodeDisplayOptions m_displayOptions;
  QVector<PointerGraphDialog::StructFieldEntry> m_fields;
  QMap<QString, QString> m_mutatedTransitions;
  QString m_lastHoverTooltip;
  PointerNodeCallbacks m_cb;
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

/**
 * @class PointerGraphView
 * @brief Interactive QGraphicsView with clamped cursor-centered wheel zoom (`0.25x..2.5x`)
 *        and a bottom-right corner minimap (`160x110 px`) supporting click/drag panning.
 */
class PointerGraphView : public QGraphicsView {
 public:
  explicit PointerGraphView(QGraphicsScene* scene, QWidget* parent = nullptr);

  double zoomFactor() const { return transform().m11(); }
  void setZoomFactor(double factor);
  QRectF minimapViewportRect() const;

 protected:
  void wheelEvent(QWheelEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  QRectF computeWorldBounds() const;
  QPointF minimapPointToScene(const QPointF& viewPos) const;
  bool m_panningMinimap = false;
};

PointerLayoutResult computeLayeredDagLayout(const QVector<PointerLayoutNode>& nodes,
                                            const QVector<PointerLayoutEdge>& edges,
                                            bool respectPinned,
                                            const PointerLayoutConfig& config = {});

QPointF computeIncrementalNodePosition(bool hasParent,
                                       const QPointF& parentPos,
                                       const QSizeF& parentSize,
                                       const QSizeF& newNodeSize,
                                       const QVector<QRectF>& occupiedRects,
                                       const PointerLayoutConfig& config = {});

AliasCycleAnalysis analyzeCyclesAndAliases(int nodeCount,
                                           const QVector<PointerLayoutEdge>& edges,
                                           const QVector<QPointF>& nodePositions);

QSet<int> findUnreachableNonRootNodes(int nodeCount,
                                      const QSet<int>& rootIndices,
                                      const QVector<PointerLayoutEdge>& edges);

}  // namespace gridlock::ui

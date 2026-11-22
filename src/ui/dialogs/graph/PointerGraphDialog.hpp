/**
 * @file PointerGraphDialog.hpp
 * @brief Interactive 2D pointer and struct graph canvas dialog with layered DAG layout,
 *        zoom/minimap viewport, per-node display controls, and SVG/PNG export.
 */

#pragma once

#include <QCheckBox>
#include <QCompleter>
#include <QDialog>
#include <QGraphicsScene>
#include <QLineEdit>
#include <QMap>
#include <QPair>
#include <QPointF>
#include <QPushButton>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QStringListModel>
#include <QVariantList>
#include <QVector>

class IBackendCoordinator;

namespace gridlock::ui {

class PointerGraphNodeItem;
class PointerGraphEdgeItem;
class PointerGraphView;

/**
 * @class PointerGraphDialog
 * @brief Modeless interactive 2D object and pointer graph visualizer window.
 */
class PointerGraphDialog : public QDialog {
  Q_OBJECT

 public:
  /**
   * @struct StructFieldEntry
   * @brief Single member row displayed inside a graphed struct or class card.
   */
  struct StructFieldEntry {
    QString name;   ///< Member identifier (e.g., "next", "mesh", "dims").
    QString value;  ///< Formatted runtime value or pointer address (e.g., "0x7fff2000", "{...}").
    QString type;   ///< Static C/C++ type string (e.g., "Node*", "MeshCfg", "double").
  };

  /**
   * @struct NodeDisplayOptions
   * @brief Per-node UI customization state preserved across debugger stop refreshes.
   */
  struct NodeDisplayOptions {
    QSet<QString> hiddenFields; ///< Suppressed field names collapsed into the hidden-fields pill.
    bool horizontal = false;    ///< True for horizontal tabular column layout, false for vertical rows.
    bool expandedWidth = false; ///< True to expand card width (capped at 900px) to fit un-elided values.
    QString formatOverride;     ///< Scalar display format: `""` (Auto), `"x"` (Hex), `"d"` (Dec), `"b"` (Bin).
  };

  explicit PointerGraphDialog(::IBackendCoordinator* coordinator = nullptr,
                              QWidget* parent = nullptr);
  ~PointerGraphDialog() override = default;

  void setCoordinator(::IBackendCoordinator* coordinator);
  void setFocusedRank(int rankId) { m_focusedRank = qMax(0, rankId); }

  bool isAutoRefreshEnabled() const;
  void setAutoRefreshEnabled(bool enabled);

  bool isPruneUnreferencedEnabled() const;
  void setPruneUnreferencedEnabled(bool enabled);

  QStringList pinnedRootExpressions() const { return m_rootExpressions; }
  void pinRootExpression(const QString& expression);
  bool unpinRootExpression(const QString& expression, bool pruneOrphans = true);

  void addAndEvaluateRootExpression(const QString& expression,
                                    const QString& typeHint = QString(),
                                    const QString& valueHint = QString());

  void upsertStructNode(const QString& expression,
                        const QString& typeName,
                        const QString& address,
                        const QVector<StructFieldEntry>& fields,
                        const QString& parentExpr = QString(),
                        const QString& parentField = QString());

  bool connectPointerField(const QString& sourceExpr,
                           const QString& fieldName,
                           const QString& targetAddressOrExpr);

  void graphExpression(const QString& expression,
                       const QString& typeHint = QString(),
                       const QString& valueHint = QString());

  bool expandNode(const QString& nodeExprOrAddr);
  void expandNodeField(const QString& nodeExpr, const QString& fieldName);
  void expandAllPointersOneHop();

  void autoLayoutNodes(bool resetManualPositions = false);
  void resetLayoutAndRelayoutDag();
  void clearGraph();
  bool canUndoClear() const { return m_hasUndoSnapshot; }
  bool undoClearGraph();
  bool undoClear() { return undoClearGraph(); }

  int nodeCount() const { return m_nodes.size(); }
  int edgeCount() const { return m_edges.size(); }
  bool hasCycleOrSharedAlias() const { return m_hasCycleOrSharedAlias; }

  bool nodeHasField(const QString& nodeExpr, const QString& fieldName) const;
  /** @brief Returns the active rendered value of @p fieldName on @p nodeExpr (reflecting `/x`, `/d`, `/b` overrides). */
  QString fieldValue(const QString& nodeExpr, const QString& fieldName) const;
  bool isFieldMutated(const QString& nodeExpr, const QString& fieldName) const;
  QString fieldMutationSummary(const QString& nodeExpr, const QString& fieldName) const;
  int mutatedFieldCount() const;
  int stopRefreshCount() const { return m_stopRefreshCount; }

  // --- Layout, Tooltip & Position Inspection ---
  QPointF nodePosition(const QString& nodeExprOrAddr) const;
  bool setNodePosition(const QString& nodeExprOrAddr, const QPointF& pos);
  bool isNodeManuallyPositioned(const QString& nodeExprOrAddr) const;
  int nodeLayer(const QString& nodeExprOrAddr) const;
  QSizeF nodeCardSize(const QString& nodeExprOrAddr) const;
  QString nodeTooltipAt(const QString& nodeExprOrAddr, const QPointF& itemLocalPos) const;
  QString nodeFieldTooltip(const QString& nodeExprOrAddr, const QString& fieldName) const;
  QString nodePlaceholderText(const QString& nodeExprOrAddr) const;
  QString edgeTargetAddressOrExpr(const QString& sourceExprOrAddr, const QString& fieldName) const;
  bool isEdgeCycleOrAlias(const QString& sourceExprOrAddr, const QString& fieldName) const;

  // --- Per-Node Field Suppression, Orientation, Width & Format Overrides ---
  bool setNodeFieldHidden(const QString& nodeExprOrAddr, const QString& fieldName, bool hidden);
  bool restoreAllHiddenFields(const QString& nodeExprOrAddr);
  int hiddenFieldCount(const QString& nodeExprOrAddr) const;
  int visibleFieldCount(const QString& nodeExprOrAddr) const;
  QString hiddenFieldsBadgeText(const QString& nodeExprOrAddr) const;

  bool setNodeHorizontal(const QString& nodeExprOrAddr, bool horizontal);
  bool toggleNodeOrientation(const QString& nodeExprOrAddr);
  bool isNodeHorizontal(const QString& nodeExprOrAddr) const;

  bool setNodeExpandedWidth(const QString& nodeExprOrAddr, bool expanded);
  bool toggleNodeExpandedWidth(const QString& nodeExprOrAddr);
  bool isNodeExpandedWidth(const QString& nodeExprOrAddr) const;

  bool setNodeFormatOverride(const QString& nodeExprOrAddr, const QString& formatOverride);
  QString nodeFormatOverride(const QString& nodeExprOrAddr) const;

  // --- Viewport Zoom, Minimap & High-Resolution SVG/PNG Export ---
  double zoomFactor() const;
  void setZoomFactor(double factor);
  void resetZoom();
  void fitGraphInView();
  QRectF minimapViewportRect() const;

  bool exportGraphToPng(const QString& filePath);
  bool exportGraphToSvg(const QString& filePath);

  // --- Live Symbol & Variable Autocomplete ---
  void setCompletionCandidates(const QStringList& candidates);
  QStringList completionCandidates() const { return m_completionCandidates; }
  QCompleter* expressionCompleter() const { return m_completer; }
  QLineEdit* rootInputEdit() const { return m_rootEdit; }
  void setRootInputText(const QString& text);

 signals:
  void nodeExpansionRequested(const QString& nodeExpression, const QString& queryExpression);

 public slots:
  void refreshPinnedRoots();
  void onTargetStopped(int rankId);
  void onVariableChildrenUpdated(int rank,
                                 const QString& parentExpression,
                                 const QVariantList& children);

 private:
  void setupUi();
  void recomputeCyclesAndRefreshEdges();
  void syncNodeRootFlags();
  void applyDisplayOptionsToNode(PointerGraphNodeItem* node);
  void recordManualNodePosition(PointerGraphNodeItem* node);
  void placeNewNodeIncrementally(PointerGraphNodeItem* node, const PointerGraphNodeItem* parentNode);
  void reconcileEdgesForUpdatedNode(PointerGraphNodeItem* node, const QVector<StructFieldEntry>& fields);
  void removeEdge(PointerGraphEdgeItem* edge);
  void removeNode(PointerGraphNodeItem* node);
  void pruneUnreferencedNodes();
  QString canonicalNodeKey(const PointerGraphNodeItem* node) const;
  PointerGraphNodeItem* findNode(const QString& exprOrAddr) const;
  bool isNodePinnedRoot(const PointerGraphNodeItem* node) const;

  static QString normalizeHexAddress(const QString& rawAddr);
  static QString normalizeFormatCode(const QString& rawFormat);

  struct NodeSnapshot {
    QString expr, typeName, address;
    QVector<StructFieldEntry> fields;
    QPointF pos;
  };
  struct EdgeSnapshot { QString srcExpr, fieldName, dstKey; };
  struct GraphUndoSnapshot {
    QVector<NodeSnapshot> nodes;
    QVector<EdgeSnapshot> edges;
    QStringList roots;
    QString rootInput;
    QMap<QString, QPointF> pinnedPositions;
    QMap<QString, NodeDisplayOptions> displayOptions;
    QMap<QString, QMap<QString, QString>> previousFieldValues, mutatedTransitions;
  };

  ::IBackendCoordinator* m_coordinator = nullptr;
  int m_focusedRank = 0;
  int m_stopRefreshCount = 0;
  bool m_suppressPositionPinning = false;
  bool m_inBulkAutoExpand = false;

  QLineEdit* m_rootEdit = nullptr;
  QCompleter* m_completer = nullptr;
  QStringListModel* m_completerModel = nullptr;
  QStringList m_completionCandidates;
  QPushButton* m_addBtn = nullptr;
  QPushButton* m_refreshBtn = nullptr;
  QCheckBox* m_autoRefreshCheck = nullptr;
  QCheckBox* m_pruneCheck = nullptr;
  QPushButton* m_layoutBtn = nullptr;
  QPushButton* m_expandHopBtn = nullptr;
  QPushButton* m_clearBtn = nullptr;
  QPushButton* m_undoClearBtn = nullptr;
  QPushButton* m_exportSvgBtn = nullptr;
  QPushButton* m_exportPngBtn = nullptr;
  QPushButton* m_resetZoomBtn = nullptr;
  QPushButton* m_fitBtn = nullptr;
  QPushButton* m_closeBtn = nullptr;
  QGraphicsScene* m_scene = nullptr;
  PointerGraphView* m_view = nullptr;

  QStringList m_rootExpressions;
  QMap<QString, QPointF> m_pinnedPositions;
  QMap<QString, int> m_nodeLayers;
  QMap<QString, NodeDisplayOptions> m_displayOptions;
  QMap<QString, QMap<QString, QString>> m_previousFieldValues;
  QMap<QString, QMap<QString, QString>> m_mutatedFieldTransitions;
  QVector<PointerGraphNodeItem*> m_nodes;
  QMap<QString, PointerGraphNodeItem*> m_nodesByExpr;
  QMap<QString, PointerGraphNodeItem*> m_nodesByAddress;
  QVector<PointerGraphEdgeItem*> m_edges;
  QMap<QString, QPair<QString, QString>> m_pendingParentLinks;
  QMap<QString, QPair<QString, QString>> m_pendingTypeValueHints;
  QMap<QString, QString> m_pendingQueryToNodeExpr;
  QSet<QString> m_pendingExpandNodes;
  QSet<QString> m_autoExpandRoots;
  bool m_hasCycleOrSharedAlias = false;
  bool m_hasUndoSnapshot = false;
  GraphUndoSnapshot m_undoSnapshot;
};

}  // namespace gridlock::ui

/**
 * @file PointerGraphDialog.hpp
 * @brief Interactive 2D pointer and struct graph canvas dialog with layered DAG layout.
 *
 * Renders live C/C++ structs, classes, linked lists, trees, and mesh objects on an
 * interactive QGraphicsScene/QGraphicsView canvas with draggable node cards,
 * cubic Bezier pointer edges, address-based aliasing/cycle detection, Sugiyama-style
 * layered DAG auto-layout with manual drag position preservation, incremental multi-root
 * pinning, dynamic alias splitting/merging, and live GDB/DAP child field expansion.
 */

#pragma once

#include <QCheckBox>
#include <QDialog>
#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLineEdit>
#include <QMap>
#include <QPair>
#include <QPointF>
#include <QPushButton>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

class IBackendCoordinator;

namespace gridlock::ui {

class PointerGraphNodeItem;
class PointerGraphEdgeItem;

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
   * @brief Constructs the interactive pointer/struct graph window and binds @p coordinator.
   * @param coordinator Optional active backend coordinator for live child queries.
   * @param parent Optional parent widget.
   */
  explicit PointerGraphDialog(::IBackendCoordinator* coordinator = nullptr,
                              QWidget* parent = nullptr);

  /** @brief Destroys the dialog and releases scene items. */
  ~PointerGraphDialog() override = default;

  /**
   * @brief Updates the active backend coordinator and reconnects `variableChildrenUpdated`.
   * @param coordinator Active debugger backend coordinator.
   */
  void setCoordinator(::IBackendCoordinator* coordinator);

  /**
   * @brief Sets the focused MPI rank index queried during live struct expansion.
   * @param rankId Zero-based MPI rank index.
   */
  void setFocusedRank(int rankId) { m_focusedRank = qMax(0, rankId); }

  /** @brief Returns whether automatic re-evaluation on debugger `*stopped` events is enabled. */
  bool isAutoRefreshEnabled() const;

  /** @brief Enables or disables automatic re-evaluation of pinned roots on debugger `*stopped` events. */
  void setAutoRefreshEnabled(bool enabled);

  /** @brief Returns whether unreferenced non-root orphan cards are pruned after pointer reassignment. */
  bool isPruneUnreferencedEnabled() const;

  /** @brief Enables or disables automatic pruning of unreferenced non-root orphan cards. */
  void setPruneUnreferencedEnabled(bool enabled);

  /** @brief Returns the ordered list of pinned root expressions retained across debugger stop events. */
  QStringList pinnedRootExpressions() const { return m_rootExpressions; }

  /** @brief Pins @p expression as a persistent root node to re-evaluate on every target stop. */
  void pinRootExpression(const QString& expression);

  /**
   * @brief Unpins @p expression from the persistent root list and optionally prunes unreachable orphans.
   * @param expression Pinned root expression to unpin.
   * @param pruneOrphans When true, removes the unpinned node and children unreachable from remaining roots.
   * @return True if @p expression was previously pinned and removed.
   */
  bool unpinRootExpression(const QString& expression, bool pruneOrphans = true);

  /**
   * @brief Incrementally adds and evaluates a root expression without clearing existing graphed roots.
   * @param expression Root variable or pointer expression (e.g., "headA", "mesh").
   * @param typeHint Optional static type hint from VariablesDockWidget.
   * @param valueHint Optional runtime value/address hint from VariablesDockWidget.
   */
  void addAndEvaluateRootExpression(const QString& expression,
                                    const QString& typeHint = QString(),
                                    const QString& valueHint = QString());

  /**
   * @brief Inserts or updates a struct node card on the canvas, deduplicating by non-null hex address.
   *
   * Never overwrites manually dragged card positions; brand-new nodes are placed incrementally
   * relative to their parent card, and updated pointer fields reconcile stale/reassigned edges
   * dynamically (splitting or merging shared aliases).
   */
  void upsertStructNode(const QString& expression,
                        const QString& typeName,
                        const QString& address,
                        const QVector<StructFieldEntry>& fields,
                        const QString& parentExpr = QString(),
                        const QString& parentField = QString());

  /**
   * @brief Connects or re-targets `(sourceExpr, fieldName)` to an existing node matching @p targetAddressOrExpr.
   * @return True if both endpoints resolved and a directed Bezier edge was created or updated.
   */
  bool connectPointerField(const QString& sourceExpr,
                           const QString& fieldName,
                           const QString& targetAddressOrExpr);

  /**
   * @brief Graphs a root C++ pointer or struct expression without clearing existing roots.
   */
  void graphExpression(const QString& expression,
                       const QString& typeHint = QString(),
                       const QString& valueHint = QString());

  /**
   * @brief Dereferences or expands a specific child field on an existing node card.
   */
  void expandNodeField(const QString& nodeExpr, const QString& fieldName);

  /**
   * @brief Expands all unexpanded pointer and composite fields across current nodes by 1 hop.
   */
  void expandAllPointersOneHop();

  /**
   * @brief Arranges struct node cards in a Sugiyama-style layered DAG layout with barycenter ordering.
   * @param resetManualPositions When true, clears manually pinned drag positions before laying out all nodes.
   */
  void autoLayoutNodes(bool resetManualPositions = false);

  /** @brief Resets all manual drag positions and computes a fresh layered DAG layout. */
  void resetLayoutAndRelayoutDag();

  /**
   * @brief Removes all node cards, directed edges, pinned roots, and manual position state from the canvas.
   */
  void clearGraph();

  /** @brief Returns the number of unique struct/object node cards currently on the canvas. */
  int nodeCount() const { return m_nodes.size(); }

  /** @brief Returns the number of directed pointer/member Bezier edges currently on the canvas. */
  int edgeCount() const { return m_edges.size(); }

  /** @brief Returns true if any directed edge forms a pointer cycle or shared multi-parent alias. */
  bool hasCycleOrSharedAlias() const { return m_hasCycleOrSharedAlias; }

  /** @brief Checks whether the node identified by @p nodeExpr contains a field named @p fieldName. */
  bool nodeHasField(const QString& nodeExpr, const QString& fieldName) const;

  /** @brief Returns the current formatted runtime value of @p fieldName on @p nodeExpr, or empty string. */
  QString fieldValue(const QString& nodeExpr, const QString& fieldName) const;

  /** @brief Returns true if @p fieldName on @p nodeExpr mutated compared to its previous stop snapshot. */
  bool isFieldMutated(const QString& nodeExpr, const QString& fieldName) const;

  /** @brief Returns the `"oldValue -> newValue"` mutation transition string for @p fieldName on @p nodeExpr. */
  QString fieldMutationSummary(const QString& nodeExpr, const QString& fieldName) const;

  /** @brief Returns total number of currently highlighted mutated struct fields across all nodes. */
  int mutatedFieldCount() const;

  /** @brief Returns total count of stop-triggered auto-refreshes executed. */
  int stopRefreshCount() const { return m_stopRefreshCount; }

  /** @brief Returns the current scene `(x, y)` position of @p nodeExprOrAddr, or `(0, 0)` if missing. */
  QPointF nodePosition(const QString& nodeExprOrAddr) const;

  /** @brief Manually moves @p nodeExprOrAddr to @p pos, pinning its position against automatic layout updates. */
  bool setNodePosition(const QString& nodeExprOrAddr, const QPointF& pos);

  /** @brief Returns true when @p nodeExprOrAddr has a user-pinned manual position. */
  bool isNodeManuallyPositioned(const QString& nodeExprOrAddr) const;

  /** @brief Returns the assigned topological DAG layer (`0..k`) of @p nodeExprOrAddr, or `-1` if missing. */
  int nodeLayer(const QString& nodeExprOrAddr) const;

  /** @brief Returns the normalized address (or expression) targeted by `(sourceExprOrAddr, fieldName)`. */
  QString edgeTargetAddressOrExpr(const QString& sourceExprOrAddr, const QString& fieldName) const;

  /** @brief Returns true if the directed edge for `(sourceExprOrAddr, fieldName)` is flagged as cycle/shared-alias. */
  bool isEdgeCycleOrAlias(const QString& sourceExprOrAddr, const QString& fieldName) const;

 public slots:
  /** @brief Re-evaluates all pinned root expressions and active nodes without wiping canvas layout. */
  void refreshPinnedRoots();

  /** @brief Invoked when MPI rank @p rankId stops at a breakpoint/step or becomes the focused rank. */
  void onTargetStopped(int rankId);

  /** @brief Receives resolved child fields from `IBackendCoordinator::variableChildrenUpdated`. */
  void onVariableChildrenUpdated(int rank,
                                 const QString& parentExpression,
                                 const QVariantList& children);

 private:
  void setupUi();
  void recomputeCyclesAndRefreshEdges();
  void syncNodeRootFlags();
  void recordManualNodePosition(PointerGraphNodeItem* node);
  void placeNewNodeIncrementally(PointerGraphNodeItem* node, const PointerGraphNodeItem* parentNode);
  void reconcileEdgesForUpdatedNode(PointerGraphNodeItem* node, const QVector<StructFieldEntry>& fields);
  void removeEdge(PointerGraphEdgeItem* edge);
  void removeNode(PointerGraphNodeItem* node);
  void pruneUnreferencedNodes();
  QString canonicalNodeKey(const PointerGraphNodeItem* node) const;
  PointerGraphNodeItem* findNode(const QString& exprOrAddr) const;
  bool isNodePinnedRoot(const PointerGraphNodeItem* node) const;

  /** @brief Normalizes a hex pointer string (`0x00007fff...` -> `0x7fff...`), or empty if null/non-hex. */
  static QString normalizeHexAddress(const QString& rawAddr);

  ::IBackendCoordinator* m_coordinator = nullptr;  ///< Attached debugger backend coordinator.
  int m_focusedRank = 0;                           ///< Active MPI rank index for live queries.
  int m_stopRefreshCount = 0;                      ///< Count of stop-triggered auto-refresh cycles.
  bool m_suppressPositionPinning = false;          ///< Guard preventing programmatic setPos from pinning nodes.

  QLineEdit* m_rootEdit = nullptr;                 ///< Toolbar root expression input (`#pointerGraphRootEdit`).
  QPushButton* m_addBtn = nullptr;                 ///< Toolbar graph button (`#pointerGraphAddBtn`).
  QPushButton* m_refreshBtn = nullptr;             ///< Toolbar manual refresh button (`#pointerGraphRefreshBtn`).
  QCheckBox* m_autoRefreshCheck = nullptr;         ///< Toolbar auto-refresh on stop toggle (`#pointerGraphAutoRefreshCheck`).
  QCheckBox* m_pruneCheck = nullptr;               ///< Toolbar orphan pruning toggle (`#pointerGraphPruneCheck`).
  QPushButton* m_layoutBtn = nullptr;              ///< Toolbar Re-Layout DAG button (`#pointerGraphLayoutBtn`).
  QPushButton* m_expandHopBtn = nullptr;           ///< Toolbar 1-hop expand button (`#pointerGraphExpandHopBtn`).
  QPushButton* m_clearBtn = nullptr;               ///< Toolbar clear button (`#pointerGraphClearBtn`).
  QPushButton* m_closeBtn = nullptr;               ///< Toolbar close button (`#pointerGraphCloseBtn`).
  QGraphicsScene* m_scene = nullptr;               ///< 2D graphics scene owning node cards and Bezier edges.
  QGraphicsView* m_view = nullptr;                 ///< Interactive pan/zoom canvas (`#pointerGraphView`).

  QStringList m_rootExpressions;                                   ///< Pinned root expressions retained across stops.
  QMap<QString, QPointF> m_pinnedPositions;                        ///< Canonical node key -> user-dragged `(x, y)`.
  QMap<QString, int> m_nodeLayers;                                 ///< Canonical node key -> assigned DAG layer.
  QMap<QString, QMap<QString, QString>> m_previousFieldValues;     ///< Maps nodeExpr -> (fieldName -> previousValue).
  QMap<QString, QMap<QString, QString>> m_mutatedFieldTransitions; ///< Maps nodeExpr -> (fieldName -> "old -> new").
  QVector<PointerGraphNodeItem*> m_nodes;                          ///< Ordered list of unique node cards.
  QMap<QString, PointerGraphNodeItem*> m_nodesByExpr;              ///< Lookup from expression path to node card.
  QMap<QString, PointerGraphNodeItem*> m_nodesByAddress;           ///< Lookup from normalized `0x...` address to node card.
  QVector<PointerGraphEdgeItem*> m_edges;                          ///< Active directed pointer/struct Bezier edges.
  QMap<QString, QPair<QString, QString>> m_pendingParentLinks;     ///< Maps childExpr -> (parentExpr, parentField).
  QMap<QString, QPair<QString, QString>> m_pendingTypeValueHints;  ///< Maps expr -> (typeHint, valueHint).
  QSet<QString> m_autoExpandRoots;                                 ///< Roots queued for automatic 1-hop child expansion.
  bool m_hasCycleOrSharedAlias = false;                            ///< True when a cycle or multi-parent alias exists.
};

}  // namespace gridlock::ui

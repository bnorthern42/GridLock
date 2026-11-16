/**
 * @file PointerGraphDialog.hpp
 * @brief DDD-inspired interactive 2D pointer and struct graph canvas dialog.
 *
 * Renders live C/C++ structs, classes, linked lists, trees, and mesh objects on an
 * interactive QGraphicsScene/QGraphicsView canvas with draggable node cards,
 * cubic Bezier pointer edges, address-based aliasing/cycle detection, layered DAG
 * auto-layout, and live GDB/DAP child field expansion.
 */

#pragma once

#include <QDialog>
#include <QGraphicsObject>
#include <QGraphicsPathItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLineEdit>
#include <QMap>
#include <QPair>
#include <QPushButton>
#include <QSet>
#include <QString>
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

  /**
   * @brief Inserts or updates a struct node card on the canvas, deduplicating by non-null hex address.
   * @param expression Full C++ expression path (e.g., "Iconfig", "n1").
   * @param typeName C/C++ type label (e.g., "InputConfig", "Node").
   * @param address Hex memory address (e.g., "0x7fff1000"), or empty if value-typed.
   * @param fields Ordered list of member fields to render inside the card.
   * @param parentExpr Optional parent node expression that expanded this node.
   * @param parentField Optional field name on @p parentExpr that links to this node.
   */
  void upsertStructNode(const QString& expression,
                        const QString& typeName,
                        const QString& address,
                        const QVector<StructFieldEntry>& fields,
                        const QString& parentExpr = QString(),
                        const QString& parentField = QString());

  /**
   * @brief Connects a pointer/struct field on @p sourceExpr to an existing node matching @p targetAddressOrExpr.
   * @param sourceExpr Source node expression (e.g., "n2").
   * @param fieldName Source field name (e.g., "next").
   * @param targetAddressOrExpr Target hex address ("0x7fff1000") or node expression.
   * @return True if both endpoints resolved and a directed Bezier edge was created or updated.
   */
  bool connectPointerField(const QString& sourceExpr,
                           const QString& fieldName,
                           const QString& targetAddressOrExpr);

  /**
   * @brief Graphs a root C++ pointer or struct expression and requests its live fields from the backend.
   * @param expression Root variable or pointer expression (e.g., "Iconfig", "mesh").
   * @param typeHint Optional static type hint from VariablesDockWidget.
   * @param valueHint Optional runtime value/address hint from VariablesDockWidget.
   */
  void graphExpression(const QString& expression,
                       const QString& typeHint = QString(),
                       const QString& valueHint = QString());

  /**
   * @brief Dereferences or expands a specific child field on an existing node card.
   * @param nodeExpr Parent node expression on the canvas (e.g., "Iconfig").
   * @param fieldName Member field name to dereference/expand (e.g., "mesh", "next").
   */
  void expandNodeField(const QString& nodeExpr, const QString& fieldName);

  /**
   * @brief Expands all unexpanded pointer and composite fields across current nodes by 1 hop.
   */
  void expandAllPointersOneHop();

  /**
   * @brief Arranges all active struct node cards left-to-right in a layered topological DAG layout.
   */
  void autoLayoutNodes();

  /**
   * @brief Removes all node cards, directed edges, and pending expansion state from the canvas.
   */
  void clearGraph();

  /** @brief Returns the number of unique struct/object node cards currently on the canvas. */
  int nodeCount() const { return m_nodes.size(); }

  /** @brief Returns the number of directed pointer/member Bezier edges currently on the canvas. */
  int edgeCount() const { return m_edges.size(); }

  /** @brief Returns true if any directed edge forms a pointer cycle or shared multi-parent alias. */
  bool hasCycleOrSharedAlias() const { return m_hasCycleOrSharedAlias; }

  /**
   * @brief Checks whether the node identified by @p nodeExpr contains a field named @p fieldName.
   * @param nodeExpr Node expression or address key.
   * @param fieldName Member field identifier to look up.
   * @return True if the node exists and lists @p fieldName.
   */
  bool nodeHasField(const QString& nodeExpr, const QString& fieldName) const;

 public slots:
  /**
   * @brief Receives resolved child fields from `IBackendCoordinator::variableChildrenUpdated`.
   * @param rank MPI rank index.
   * @param parentExpression Parent C++ expression whose children were listed.
   * @param children List of QVariantMap descriptors (`name`, `value`, `type`).
   */
  void onVariableChildrenUpdated(int rank,
                                 const QString& parentExpression,
                                 const QVariantList& children);

 private:
  /** @brief Builds the toolbar, 2D graphics view, and status legend. */
  void setupUi();

  /** @brief Recomputes cycle/shared-alias flags across all directed edges and updates geometry. */
  void recomputeCyclesAndRefreshEdges();

  /** @brief Returns true if @p startNode can reach @p goalNode along existing directed edges. */
  bool canReachNode(const PointerGraphNodeItem* startNode,
                    const PointerGraphNodeItem* goalNode) const;

  /** @brief Normalizes a hex pointer string (`0x00007fff...` -> `0x7fff...`), or empty if null/non-hex. */
  static QString normalizeHexAddress(const QString& rawAddr);

  ::IBackendCoordinator* m_coordinator = nullptr;  ///< Attached debugger backend coordinator.
  int m_focusedRank = 0;                           ///< Active MPI rank index for live queries.

  QLineEdit* m_rootEdit = nullptr;                 ///< Toolbar root expression input (`#pointerGraphRootEdit`).
  QPushButton* m_addBtn = nullptr;                 ///< Toolbar graph button (`#pointerGraphAddBtn`).
  QPushButton* m_layoutBtn = nullptr;              ///< Toolbar DAG layout button (`#pointerGraphLayoutBtn`).
  QPushButton* m_expandHopBtn = nullptr;           ///< Toolbar 1-hop expand button (`#pointerGraphExpandHopBtn`).
  QPushButton* m_clearBtn = nullptr;               ///< Toolbar clear button (`#pointerGraphClearBtn`).
  QPushButton* m_closeBtn = nullptr;               ///< Toolbar close button (`#pointerGraphCloseBtn`).
  QGraphicsScene* m_scene = nullptr;               ///< 2D graphics scene owning node cards and Bezier edges.
  QGraphicsView* m_view = nullptr;                 ///< Interactive pan/zoom canvas (`#pointerGraphView`).

  QVector<PointerGraphNodeItem*> m_nodes;                        ///< Ordered list of unique node cards.
  QMap<QString, PointerGraphNodeItem*> m_nodesByExpr;            ///< Lookup from expression path to node card.
  QMap<QString, PointerGraphNodeItem*> m_nodesByAddress;         ///< Lookup from normalized `0x...` address to node card.
  QVector<PointerGraphEdgeItem*> m_edges;                        ///< Active directed pointer/struct Bezier edges.
  QMap<QString, QPair<QString, QString>> m_pendingParentLinks;   ///< Maps childExpr -> (parentExpr, parentField).
  QMap<QString, QPair<QString, QString>> m_pendingTypeValueHints;///< Maps expr -> (typeHint, valueHint).
  QSet<QString> m_autoExpandRoots;                               ///< Roots queued for automatic 1-hop child expansion.
  bool m_hasCycleOrSharedAlias = false;                          ///< True when a cycle or multi-parent alias exists.
};

}  // namespace gridlock::ui

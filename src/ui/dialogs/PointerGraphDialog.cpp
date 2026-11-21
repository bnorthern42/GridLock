/**
 * @file PointerGraphDialog.cpp
 * @brief Implementation of the interactive 2D pointer and struct graph visualizer dialog.
 *
 * Delegates scene card/edge rendering, zoom/minimap viewport, and Sugiyama-style layered
 * DAG layout to PointerGraphLayout.{hpp,cpp} while managing coordinator subscriptions,
 * multi-root pinning, manual position & display option persistence, dynamic alias
 * splitting/merging, and high-resolution SVG/PNG graph export.
 */

#include "PointerGraphDialog.hpp"
#include "PointerGraphLayout.hpp"

#include "../../core/hpc/DapCoordinator.hpp"
#include "../../core/hpc/GdbRankCoordinator.hpp"
#include "../../core/hpc/IBackendCoordinator.hpp"
#include "../../core/managers/ThemeManager.hpp"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgGenerator>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace gridlock::ui {

namespace {
struct ScopedPinSuppress {
  bool& flag;
  bool prev;
  explicit ScopedPinSuppress(bool& f) : flag(f), prev(f) { flag = true; }
  ~ScopedPinSuppress() { flag = prev; }
};
}  // namespace

PointerGraphDialog::PointerGraphDialog(::IBackendCoordinator* coordinator, QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint) {
  setObjectName(QStringLiteral("PointerGraphDialog"));
  setWindowTitle(QStringLiteral("Pointer & Struct Graph Visualizer"));
  resize(1160, 740);
  setupUi();
  setCoordinator(coordinator);
}

void PointerGraphDialog::setupUi() {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(8, 8, 8, 8);
  rootLayout->setSpacing(6);

  // Row 1: Incremental Root Expression input, Refresh, Auto-Refresh, Prune Unreferenced, and Close.
  auto* row1 = new QHBoxLayout();
  row1->setSpacing(6);
  m_rootEdit = new QLineEdit(this);
  m_rootEdit->setObjectName(QStringLiteral("pointerGraphRootEdit"));
  m_rootEdit->setPlaceholderText(QStringLiteral("Add root pointer or struct expression (e.g., headA, mesh, cfg)..."));
  m_addBtn = new QPushButton(QStringLiteral("Graph Expression"), this);
  m_addBtn->setObjectName(QStringLiteral("pointerGraphAddBtn"));
  m_refreshBtn = new QPushButton(QStringLiteral("Refresh Now"), this);
  m_refreshBtn->setObjectName(QStringLiteral("pointerGraphRefreshBtn"));
  m_autoRefreshCheck = new QCheckBox(QStringLiteral("Auto-Refresh on Stop"), this);
  m_autoRefreshCheck->setObjectName(QStringLiteral("pointerGraphAutoRefreshCheck"));
  m_autoRefreshCheck->setChecked(true);
  m_pruneCheck = new QCheckBox(QStringLiteral("Prune Unreferenced Nodes"), this);
  m_pruneCheck->setObjectName(QStringLiteral("pointerGraphPruneCheck"));
  m_pruneCheck->setChecked(true);
  m_closeBtn = new QPushButton(QStringLiteral("✕ CLOSE"), this);
  m_closeBtn->setObjectName(QStringLiteral("pointerGraphCloseBtn"));

  row1->addWidget(m_rootEdit, 1);
  row1->addWidget(m_addBtn);
  row1->addWidget(m_refreshBtn);
  row1->addWidget(m_autoRefreshCheck);
  row1->addWidget(m_pruneCheck);
  row1->addWidget(m_closeBtn);
  rootLayout->addLayout(row1);

  // Row 2: Re-Layout DAG, Expand 1-Hop, Clear Graph, SVG/PNG Export, and Zoom/Fit controls.
  auto* row2 = new QHBoxLayout();
  row2->setSpacing(6);
  m_layoutBtn = new QPushButton(QStringLiteral("Re-Layout DAG"), this);
  m_layoutBtn->setObjectName(QStringLiteral("pointerGraphLayoutBtn"));
  m_layoutBtn->setToolTip(QStringLiteral("Reset manual card positions and compute a layered Sugiyama DAG layout"));
  m_expandHopBtn = new QPushButton(QStringLiteral("Expand All Pointers (1 Hop)"), this);
  m_expandHopBtn->setObjectName(QStringLiteral("pointerGraphExpandHopBtn"));
  m_clearBtn = new QPushButton(QStringLiteral("Clear Graph"), this);
  m_clearBtn->setObjectName(QStringLiteral("pointerGraphClearBtn"));

  m_exportSvgBtn = new QPushButton(QStringLiteral("Export SVG..."), this);
  m_exportSvgBtn->setObjectName(QStringLiteral("pointerGraphExportSvgBtn"));
  m_exportPngBtn = new QPushButton(QStringLiteral("Export PNG..."), this);
  m_exportPngBtn->setObjectName(QStringLiteral("pointerGraphExportPngBtn"));

  auto* zoomOutBtn = new QPushButton(QStringLiteral("Zoom -"), this);
  m_resetZoomBtn = new QPushButton(QStringLiteral("100%"), this);
  m_resetZoomBtn->setObjectName(QStringLiteral("pointerGraphResetZoomBtn"));
  auto* zoomInBtn = new QPushButton(QStringLiteral("Zoom +"), this);
  m_fitBtn = new QPushButton(QStringLiteral("Fit Graph"), this);
  m_fitBtn->setObjectName(QStringLiteral("pointerGraphFitBtn"));

  row2->addWidget(m_layoutBtn);
  row2->addWidget(m_expandHopBtn);
  row2->addWidget(m_clearBtn);
  row2->addStretch(1);
  row2->addWidget(m_exportSvgBtn);
  row2->addWidget(m_exportPngBtn);
  row2->addWidget(zoomOutBtn);
  row2->addWidget(m_resetZoomBtn);
  row2->addWidget(zoomInBtn);
  row2->addWidget(m_fitBtn);
  rootLayout->addLayout(row2);

  // 2D Interactive Canvas with cursor-centered wheel zoom & corner minimap.
  m_scene = new QGraphicsScene(this);
  m_view = new PointerGraphView(m_scene, this);
  m_view->setObjectName(QStringLiteral("pointerGraphView"));
  rootLayout->addWidget(m_view, 1);

  connect(m_addBtn, &QPushButton::clicked, this, [this]() {
    if (m_rootEdit && !m_rootEdit->text().trimmed().isEmpty()) {
      addAndEvaluateRootExpression(m_rootEdit->text().trimmed());
    }
  });
  connect(m_rootEdit, &QLineEdit::returnPressed, m_addBtn, &QPushButton::click);
  connect(m_refreshBtn, &QPushButton::clicked, this, &PointerGraphDialog::refreshPinnedRoots);
  connect(m_pruneCheck, &QCheckBox::toggled, this, [this](bool checked) {
    if (checked) {
      pruneUnreferencedNodes();
      recomputeCyclesAndRefreshEdges();
    }
  });
  connect(m_layoutBtn, &QPushButton::clicked, this, &PointerGraphDialog::resetLayoutAndRelayoutDag);
  connect(m_expandHopBtn, &QPushButton::clicked, this, &PointerGraphDialog::expandAllPointersOneHop);
  connect(m_clearBtn, &QPushButton::clicked, this, &PointerGraphDialog::clearGraph);
  connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);

  connect(m_exportSvgBtn, &QPushButton::clicked, this, [this]() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Pointer Graph SVG"),
        QStringLiteral("pointer_graph.svg"), QStringLiteral("SVG Vector (*.svg)"));
    if (!path.isEmpty()) exportGraphToSvg(path);
  });
  connect(m_exportPngBtn, &QPushButton::clicked, this, [this]() {
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export Pointer Graph PNG"),
        QStringLiteral("pointer_graph.png"), QStringLiteral("PNG Image (*.png)"));
    if (!path.isEmpty()) exportGraphToPng(path);
  });

  connect(zoomInBtn, &QPushButton::clicked, this, [this]() { setZoomFactor(zoomFactor() * 1.15); });
  connect(zoomOutBtn, &QPushButton::clicked, this, [this]() { setZoomFactor(zoomFactor() / 1.15); });
  connect(m_resetZoomBtn, &QPushButton::clicked, this, &PointerGraphDialog::resetZoom);
  connect(m_fitBtn, &QPushButton::clicked, this, &PointerGraphDialog::fitGraphInView);
}

void PointerGraphDialog::setCoordinator(::IBackendCoordinator* coordinator) {
  if (m_coordinator == coordinator) return;
  if (m_coordinator) disconnect(m_coordinator, nullptr, this, nullptr);
  m_coordinator = coordinator;
  if (m_coordinator) {
    connect(m_coordinator, &IBackendCoordinator::variableChildrenUpdated,
            this, &PointerGraphDialog::onVariableChildrenUpdated, Qt::UniqueConnection);
    if (auto* gdb = dynamic_cast<gridlock::GdbRankCoordinator*>(m_coordinator)) {
      connect(gdb, &gridlock::GdbRankCoordinator::rankStateChanged, this,
              [this](int rankId, const gridlock::RankState& state) {
                if (rankId == m_focusedRank && state.currentState == QLatin1String("stopped")) {
                  onTargetStopped(rankId);
                }
              });
    } else if (auto* dap = dynamic_cast<DapCoordinator*>(m_coordinator)) {
      connect(dap, &DapCoordinator::executionStopped, this,
              [this](int rankId, const QString&) {
                if (rankId == m_focusedRank) onTargetStopped(rankId);
              });
    }
  }
}

bool PointerGraphDialog::isAutoRefreshEnabled() const {
  return m_autoRefreshCheck ? m_autoRefreshCheck->isChecked() : true;
}

void PointerGraphDialog::setAutoRefreshEnabled(bool enabled) {
  if (m_autoRefreshCheck) m_autoRefreshCheck->setChecked(enabled);
}

bool PointerGraphDialog::isPruneUnreferencedEnabled() const {
  return m_pruneCheck ? m_pruneCheck->isChecked() : true;
}

void PointerGraphDialog::setPruneUnreferencedEnabled(bool enabled) {
  if (m_pruneCheck) m_pruneCheck->setChecked(enabled);
}

QString PointerGraphDialog::canonicalNodeKey(const PointerGraphNodeItem* node) const {
  if (!node) return QString();
  return !node->address().isEmpty() ? node->address() : node->expression();
}

PointerGraphNodeItem* PointerGraphDialog::findNode(const QString& exprOrAddr) const {
  const QString clean = exprOrAddr.trimmed();
  if (clean.isEmpty()) return nullptr;
  if (auto* byExpr = m_nodesByExpr.value(clean, nullptr)) return byExpr;
  const QString normAddr = normalizeHexAddress(clean);
  return !normAddr.isEmpty() ? m_nodesByAddress.value(normAddr, nullptr) : nullptr;
}

bool PointerGraphDialog::isNodePinnedRoot(const PointerGraphNodeItem* node) const {
  if (!node) return false;
  if (m_rootExpressions.contains(node->expression())) return true;
  for (const QString& expr : m_rootExpressions) {
    if (m_nodesByExpr.value(expr, nullptr) == node) return true;
  }
  return false;
}

void PointerGraphDialog::syncNodeRootFlags() {
  for (auto* node : m_nodes) node->setIsRoot(isNodePinnedRoot(node));
}

void PointerGraphDialog::applyDisplayOptionsToNode(PointerGraphNodeItem* node) {
  if (!node) return;
  node->setDisplayOptions(m_displayOptions.value(canonicalNodeKey(node)));
  for (auto* e : m_edges) e->updateGeometry();
  if (m_view && m_view->viewport()) m_view->viewport()->update();
}

void PointerGraphDialog::pinRootExpression(const QString& expression) {
  const QString clean = expression.trimmed();
  if (!clean.isEmpty() && !m_rootExpressions.contains(clean)) {
    m_rootExpressions.append(clean);
    syncNodeRootFlags();
  }
}

bool PointerGraphDialog::unpinRootExpression(const QString& expression, bool pruneOrphans) {
  const QString clean = expression.trimmed();
  if (m_rootExpressions.removeAll(clean) <= 0) return false;
  syncNodeRootFlags();
  if (pruneOrphans) pruneUnreferencedNodes();
  recomputeCyclesAndRefreshEdges();
  return true;
}

void PointerGraphDialog::addAndEvaluateRootExpression(const QString& expression,
                                                      const QString& typeHint,
                                                      const QString& valueHint) {
  graphExpression(expression, typeHint, valueHint);
}

void PointerGraphDialog::refreshPinnedRoots() {
  QStringList targets = m_rootExpressions;
  for (auto* node : m_nodes) {
    if (!targets.contains(node->expression())) targets.append(node->expression());
  }
  for (const QString& expr : targets) {
    m_autoExpandRoots.insert(expr);
    if (m_coordinator) m_coordinator->requestVariableChildren(m_focusedRank, expr);
  }
}

void PointerGraphDialog::onTargetStopped(int rankId) {
  m_focusedRank = qMax(0, rankId);
  if (!isAutoRefreshEnabled()) return;
  ++m_stopRefreshCount;
  refreshPinnedRoots();
}

QString PointerGraphDialog::normalizeHexAddress(const QString& rawAddr) {
  static const QRegularExpression rxHex(QStringLiteral("(0x[0-9a-fA-F]+)"));
  const auto m = rxHex.match(rawAddr.trimmed());
  if (!m.hasMatch()) return QString();
  bool ok = false;
  const quint64 val = m.captured(1).mid(2).toULongLong(&ok, 16);
  return (ok && val != 0ULL) ? QStringLiteral("0x%1").arg(val, 0, 16) : QString();
}

QString PointerGraphDialog::normalizeFormatCode(const QString& rawFormat) {
  QString f = rawFormat.trimmed().toLower();
  if (f.startsWith(QLatin1Char('/'))) f.remove(0, 1);
  if (f == QLatin1String("auto") || f.isEmpty()) return QString();
  if (f == QLatin1String("x") || f == QLatin1String("hex")) return QStringLiteral("x");
  if (f == QLatin1String("d") || f == QLatin1String("dec")) return QStringLiteral("d");
  if (f == QLatin1String("b") || f == QLatin1String("bin")) return QStringLiteral("b");
  return QString();
}

void PointerGraphDialog::recordManualNodePosition(PointerGraphNodeItem* node) {
  if (node && !m_suppressPositionPinning) {
    m_pinnedPositions.insert(canonicalNodeKey(node), node->pos());
  }
  for (auto* e : m_edges) e->updateGeometry();
  if (m_view && m_view->viewport()) m_view->viewport()->update();
}

void PointerGraphDialog::placeNewNodeIncrementally(PointerGraphNodeItem* node,
                                                   const PointerGraphNodeItem* parentNode) {
  if (!node) return;
  QVector<QRectF> occupied;
  occupied.reserve(m_nodes.size());
  for (const auto* other : m_nodes) {
    if (other != node) occupied.append(other->sceneBoundingRect());
  }
  const QPointF pos = computeIncrementalNodePosition(
      parentNode != nullptr,
      parentNode ? parentNode->pos() : QPointF(),
      parentNode ? parentNode->boundingRect().size() : QSizeF(),
      node->boundingRect().size(),
      occupied);
  ScopedPinSuppress guard(m_suppressPositionPinning);
  node->setPos(pos);
}

void PointerGraphDialog::removeEdge(PointerGraphEdgeItem* edge) {
  if (!edge) return;
  m_edges.removeAll(edge);
  if (m_scene) m_scene->removeItem(edge);
  delete edge;
}

void PointerGraphDialog::removeNode(PointerGraphNodeItem* node) {
  if (!node) return;
  const QVector<PointerGraphEdgeItem*> edgesCopy = m_edges;
  for (auto* e : edgesCopy) {
    if (e->sourceNode() == node || e->targetNode() == node) removeEdge(e);
  }
  const QString key = canonicalNodeKey(node);
  m_pinnedPositions.remove(key);
  m_pinnedPositions.remove(node->expression());
  m_nodeLayers.remove(key);
  m_nodeLayers.remove(node->expression());
  m_displayOptions.remove(key);
  m_displayOptions.remove(node->expression());
  m_previousFieldValues.remove(node->expression());
  m_mutatedFieldTransitions.remove(node->expression());

  for (auto it = m_nodesByExpr.begin(); it != m_nodesByExpr.end();) {
    if (it.value() == node) it = m_nodesByExpr.erase(it);
    else ++it;
  }
  for (auto it = m_nodesByAddress.begin(); it != m_nodesByAddress.end();) {
    if (it.value() == node) it = m_nodesByAddress.erase(it);
    else ++it;
  }
  m_nodes.removeAll(node);
  if (m_scene) m_scene->removeItem(node);
  delete node;
}

void PointerGraphDialog::pruneUnreferencedNodes() {
  if (m_nodes.isEmpty()) return;
  QMap<const PointerGraphNodeItem*, int> indexOf;
  QSet<int> rootIndices;
  for (int i = 0; i < m_nodes.size(); ++i) {
    indexOf.insert(m_nodes[i], i);
    if (isNodePinnedRoot(m_nodes[i])) rootIndices.insert(i);
  }
  QVector<PointerLayoutEdge> layoutEdges;
  layoutEdges.reserve(m_edges.size());
  for (const auto* e : m_edges) {
    const int s = indexOf.value(e->sourceNode(), -1);
    const int t = indexOf.value(e->targetNode(), -1);
    if (s >= 0 && t >= 0) layoutEdges.append({s, t, 0});
  }
  const QSet<int> unreachable = findUnreachableNonRootNodes(m_nodes.size(), rootIndices, layoutEdges);
  if (unreachable.isEmpty()) return;

  QVector<PointerGraphNodeItem*> toDelete;
  for (int idx : unreachable) toDelete.append(m_nodes[idx]);
  for (auto* orphan : toDelete) removeNode(orphan);
}

void PointerGraphDialog::reconcileEdgesForUpdatedNode(PointerGraphNodeItem* node,
                                                      const QVector<StructFieldEntry>& fields) {
  if (!node || fields.isEmpty()) return;
  QMap<QString, StructFieldEntry> fieldMap;
  for (const auto& f : fields) fieldMap.insert(f.name, f);

  QVector<PointerGraphEdgeItem*> outgoing;
  for (auto* e : m_edges) {
    if (e->sourceNode() == node) outgoing.append(e);
  }

  bool topologyChanged = false;
  for (auto* edge : outgoing) {
    const QString fName = edge->sourceField();
    const auto it = fieldMap.constFind(fName);
    if (it == fieldMap.constEnd() || isNullPointerValue(it->value)) {
      removeEdge(edge);
      topologyChanged = true;
      continue;
    }

    const QString newAddr = normalizeHexAddress(it->value);
    PointerGraphNodeItem* oldTarget = edge->targetNode();
    if (!newAddr.isEmpty() && oldTarget && !oldTarget->address().isEmpty() &&
        oldTarget->address() != newAddr) {
      removeEdge(edge);
      topologyChanged = true;
      if (m_nodesByAddress.contains(newAddr)) {
        connectPointerField(node->expression(), fName, newAddr);
      } else {
        const QString childExpr = QStringLiteral("%1.%2").arg(node->expression(), fName);
        upsertStructNode(childExpr, dereferencedTypeName(it->type), newAddr, {}, node->expression(), fName);
        if (m_coordinator) {
          m_pendingParentLinks.insert(childExpr, {node->expression(), fName});
          m_pendingTypeValueHints.insert(childExpr, {it->type, it->value});
          m_coordinator->requestVariableChildren(m_focusedRank, childExpr);
        }
      }
    }
  }

  if (topologyChanged && isPruneUnreferencedEnabled()) pruneUnreferencedNodes();
}

void PointerGraphDialog::upsertStructNode(const QString& expression,
                                          const QString& typeName,
                                          const QString& address,
                                          const QVector<StructFieldEntry>& fields,
                                          const QString& parentExpr,
                                          const QString& parentField) {
  const QString cleanExpr = expression.trimmed();
  if (cleanExpr.isEmpty()) return;
  const QString normAddr = normalizeHexAddress(address);

  PointerGraphNodeItem* node = !normAddr.isEmpty() ? m_nodesByAddress.value(normAddr, nullptr) : nullptr;
  if (!node) node = m_nodesByExpr.value(cleanExpr, nullptr);
  const bool isNewNode = (node == nullptr);
  if (isNewNode && parentExpr.trimmed().isEmpty()) pinRootExpression(cleanExpr);
  const QString oldKey = isNewNode ? QString() : canonicalNodeKey(node);
  const QString oldExpr = isNewNode ? QString() : node->expression();
  const bool wasPlaceholder = !isNewNode && node->fields().isEmpty();

  if (isNewNode) {
    PointerNodeCallbacks cb;
    cb.onMoved = [this](PointerGraphNodeItem* moved) { recordManualNodePosition(moved); };
    cb.onExpandField = [this](const QString& nExpr, const QString& fName) { expandNodeField(nExpr, fName); };
    cb.onUnpinRoot = [this](const QString& rExpr) { unpinRootExpression(rExpr, true); };
    cb.onResetPos = [this](PointerGraphNodeItem* target) {
      m_pinnedPositions.remove(canonicalNodeKey(target));
      autoLayoutNodes(false);
    };
    cb.onHideField = [this](PointerGraphNodeItem* target, const QString& fName) {
      setNodeFieldHidden(canonicalNodeKey(target), fName, true);
    };
    cb.onRestoreHidden = [this](PointerGraphNodeItem* target) {
      restoreAllHiddenFields(canonicalNodeKey(target));
    };
    cb.onToggleOrientation = [this](PointerGraphNodeItem* target) {
      toggleNodeOrientation(canonicalNodeKey(target));
    };
    cb.onSetFormat = [this](PointerGraphNodeItem* target, const QString& fmt) {
      setNodeFormatOverride(canonicalNodeKey(target), fmt);
    };
    node = new PointerGraphNodeItem(cleanExpr, typeName.trimmed(), normAddr, std::move(cb));
    m_scene->addItem(node);
    m_nodes.append(node);
  }

  const QString nodeKey = wasPlaceholder ? cleanExpr : node->expression();
  if (!fields.isEmpty()) {
    auto& prevMap = m_previousFieldValues[nodeKey];
    auto& mutMap = m_mutatedFieldTransitions[nodeKey];
    for (const auto& f : fields) {
      if (prevMap.contains(f.name)) {
        const QString oldVal = prevMap.value(f.name);
        if (oldVal != f.value) mutMap.insert(f.name, QStringLiteral("%1 -> %2").arg(oldVal, f.value));
        else mutMap.remove(f.name);
      }
      prevMap.insert(f.name, f.value);
    }
    if (nodeKey != cleanExpr) {
      m_previousFieldValues[cleanExpr] = prevMap;
      m_mutatedFieldTransitions[cleanExpr] = mutMap;
    }
  }

  const QString updatedExpr = (isNewNode || wasPlaceholder || oldExpr == cleanExpr) ? cleanExpr : oldExpr;
  node->updateData(updatedExpr, typeName.trimmed(), normAddr, fields);
  node->setMutatedFields(m_mutatedFieldTransitions.value(nodeKey));

  const QString newKey = canonicalNodeKey(node);
  if (!oldKey.isEmpty() && oldKey != newKey) {
    if (m_pinnedPositions.contains(oldKey) && !m_pinnedPositions.contains(newKey)) {
      m_pinnedPositions.insert(newKey, m_pinnedPositions.take(oldKey));
    }
    if (m_nodeLayers.contains(oldKey) && !m_nodeLayers.contains(newKey)) {
      m_nodeLayers.insert(newKey, m_nodeLayers.take(oldKey));
    }
    if (m_displayOptions.contains(oldKey) && !m_displayOptions.contains(newKey)) {
      m_displayOptions.insert(newKey, m_displayOptions.take(oldKey));
    }
  }
  if (wasPlaceholder && !oldExpr.isEmpty() && oldExpr != updatedExpr && !m_rootExpressions.contains(oldExpr)) {
    m_nodesByExpr.remove(oldExpr);
  }
  m_nodesByExpr.insert(cleanExpr, node);
  if (updatedExpr != cleanExpr) m_nodesByExpr.insert(updatedExpr, node);
  if (!normAddr.isEmpty()) m_nodesByAddress.insert(normAddr, node);
  syncNodeRootFlags();
  node->setDisplayOptions(m_displayOptions.value(newKey));

  if (isNewNode) placeNewNodeIncrementally(node, findNode(parentExpr));
  if (!parentExpr.trimmed().isEmpty() && !parentField.trimmed().isEmpty()) {
    connectPointerField(parentExpr.trimmed(), parentField.trimmed(),
                        !normAddr.isEmpty() ? normAddr : cleanExpr);
  }
  if (!fields.isEmpty()) reconcileEdgesForUpdatedNode(node, fields);
  recomputeCyclesAndRefreshEdges();
}

bool PointerGraphDialog::connectPointerField(const QString& sourceExpr,
                                             const QString& fieldName,
                                             const QString& targetAddressOrExpr) {
  PointerGraphNodeItem* src = findNode(sourceExpr);
  PointerGraphNodeItem* dst = findNode(targetAddressOrExpr);
  const QString cleanField = fieldName.trimmed();
  if (!src || !dst || cleanField.isEmpty()) return false;

  for (auto* edge : m_edges) {
    if (edge->sourceNode() == src && edge->sourceField() == cleanField) {
      if (edge->targetNode() != dst) {
        edge->setTargetNode(dst);
        if (isPruneUnreferencedEnabled()) pruneUnreferencedNodes();
      }
      recomputeCyclesAndRefreshEdges();
      return true;
    }
  }
  auto* edge = new PointerGraphEdgeItem(src, cleanField, dst);
  m_scene->addItem(edge);
  m_edges.append(edge);
  recomputeCyclesAndRefreshEdges();
  return true;
}

void PointerGraphDialog::recomputeCyclesAndRefreshEdges() {
  QMap<const PointerGraphNodeItem*, int> indexOf;
  QVector<PointerLayoutNode> layoutNodes;
  QVector<QPointF> positions;
  layoutNodes.reserve(m_nodes.size());
  positions.reserve(m_nodes.size());
  for (int i = 0; i < m_nodes.size(); ++i) {
    auto* n = m_nodes[i];
    indexOf.insert(n, i);
    const QString key = canonicalNodeKey(n);
    layoutNodes.append({key, n->boundingRect().size(), isNodePinnedRoot(n),
                        m_pinnedPositions.contains(key), n->pos()});
    positions.append(n->pos());
  }

  QVector<PointerLayoutEdge> layoutEdges;
  layoutEdges.reserve(m_edges.size());
  for (const auto* e : m_edges) {
    const int s = indexOf.value(e->sourceNode(), -1);
    const int t = indexOf.value(e->targetNode(), -1);
    const int row = (s >= 0) ? std::max(0, e->sourceNode()->indexOfField(e->sourceField())) : 0;
    layoutEdges.append({s, t, row});
  }

  const PointerLayoutResult topo = computeLayeredDagLayout(layoutNodes, layoutEdges, true);
  m_nodeLayers.clear();
  for (int i = 0; i < m_nodes.size() && i < topo.layers.size(); ++i) {
    m_nodeLayers.insert(canonicalNodeKey(m_nodes[i]), topo.layers[i]);
  }

  const AliasCycleAnalysis analysis = analyzeCyclesAndAliases(m_nodes.size(), layoutEdges, positions);
  m_hasCycleOrSharedAlias = analysis.anyCycleOrAlias;
  for (int i = 0; i < m_edges.size(); ++i) {
    m_edges[i]->setCycleOrAlias(analysis.edgeIsCycleOrAlias[i]);
  }
  if (m_view && m_view->viewport()) m_view->viewport()->update();
}

void PointerGraphDialog::graphExpression(const QString& expression,
                                         const QString& typeHint,
                                         const QString& valueHint) {
  const QString clean = expression.trimmed();
  if (clean.isEmpty()) return;
  pinRootExpression(clean);
  if (m_rootEdit) m_rootEdit->setText(clean);
  m_pendingTypeValueHints.insert(clean, {typeHint.trimmed(), valueHint.trimmed()});
  m_autoExpandRoots.insert(clean);
  upsertStructNode(clean, typeHint, normalizeHexAddress(valueHint), {});
  if (m_coordinator) m_coordinator->requestVariableChildren(m_focusedRank, clean);
}

void PointerGraphDialog::expandNodeField(const QString& nodeExpr, const QString& fieldName) {
  PointerGraphNodeItem* src = findNode(nodeExpr);
  if (!src) return;
  const int idx = src->indexOfField(fieldName.trimmed());
  const StructFieldEntry field = (idx >= 0) ? src->fields().at(idx)
                                            : StructFieldEntry{fieldName.trimmed(), QString(), QString()};
  if (isNullPointerValue(field.value)) return;

  const QString normAddr = normalizeHexAddress(field.value);
  if (!normAddr.isEmpty() && m_nodesByAddress.contains(normAddr)) {
    connectPointerField(src->expression(), fieldName.trimmed(), normAddr);
    return;
  }

  const QString childExpr = QStringLiteral("%1.%2").arg(src->expression(), fieldName.trimmed());
  m_pendingParentLinks.insert(childExpr, {src->expression(), fieldName.trimmed()});
  m_pendingTypeValueHints.insert(childExpr, {field.type, field.value});
  if (m_coordinator) m_coordinator->requestVariableChildren(m_focusedRank, childExpr);
}

void PointerGraphDialog::expandAllPointersOneHop() {
  const auto snapshot = m_nodes;
  for (auto* node : snapshot) {
    for (const auto& f : node->fields()) {
      if (isExpandableOrPointerField(f)) expandNodeField(node->expression(), f.name);
    }
  }
}

void PointerGraphDialog::onVariableChildrenUpdated(int,
                                                   const QString& parentExpression,
                                                   const QVariantList& children) {
  const QString cleanExpr = parentExpression.trimmed();
  if (cleanExpr.isEmpty()) return;
  if (!m_nodesByExpr.contains(cleanExpr) && !m_pendingParentLinks.contains(cleanExpr) &&
      !m_pendingTypeValueHints.contains(cleanExpr)) {
    return;
  }

  QVector<StructFieldEntry> parsedFields;
  parsedFields.reserve(children.size());
  for (const QVariant& v : children) {
    const QVariantMap m = v.toMap();
    const QString n = m.value(QStringLiteral("name")).toString().trimmed();
    if (!n.isEmpty()) {
      parsedFields.append({n, m.value(QStringLiteral("value")).toString().trimmed(),
                           m.value(QStringLiteral("type")).toString().trimmed()});
    }
  }
  const auto hints = m_pendingTypeValueHints.value(cleanExpr);
  const auto parentLink = m_pendingParentLinks.take(cleanExpr);
  upsertStructNode(cleanExpr, hints.first, hints.second, parsedFields,
                   parentLink.first, parentLink.second);
  if (m_autoExpandRoots.remove(cleanExpr)) {
    for (const auto& f : parsedFields) {
      if (isExpandableOrPointerField(f)) expandNodeField(cleanExpr, f.name);
    }
  }
}

void PointerGraphDialog::autoLayoutNodes(bool resetManualPositions) {
  if (resetManualPositions) m_pinnedPositions.clear();
  if (m_nodes.isEmpty()) return;

  QMap<const PointerGraphNodeItem*, int> indexOf;
  QVector<PointerLayoutNode> layoutNodes;
  layoutNodes.reserve(m_nodes.size());
  for (int i = 0; i < m_nodes.size(); ++i) {
    auto* n = m_nodes[i];
    indexOf.insert(n, i);
    const QString key = canonicalNodeKey(n);
    const bool pinned = !resetManualPositions && m_pinnedPositions.contains(key);
    layoutNodes.append({key, n->boundingRect().size(), isNodePinnedRoot(n), pinned,
                        pinned ? m_pinnedPositions.value(key) : n->pos()});
  }

  QVector<PointerLayoutEdge> layoutEdges;
  layoutEdges.reserve(m_edges.size());
  for (const auto* e : m_edges) {
    const int s = indexOf.value(e->sourceNode(), -1);
    const int t = indexOf.value(e->targetNode(), -1);
    const int row = (s >= 0) ? std::max(0, e->sourceNode()->indexOfField(e->sourceField())) : 0;
    if (s >= 0 && t >= 0) layoutEdges.append({s, t, row});
  }

  const PointerLayoutResult layout = computeLayeredDagLayout(
      layoutNodes, layoutEdges, /*respectPinned=*/!resetManualPositions);

  {
    ScopedPinSuppress guard(m_suppressPositionPinning);
    for (int i = 0; i < m_nodes.size() && i < layout.positions.size(); ++i) {
      if (!resetManualPositions && layoutNodes[i].isPinned) continue;
      m_nodes[i]->setPos(layout.positions[i]);
    }
  }
  recomputeCyclesAndRefreshEdges();
}

void PointerGraphDialog::resetLayoutAndRelayoutDag() { autoLayoutNodes(true); }

void PointerGraphDialog::clearGraph() {
  if (m_scene) m_scene->clear();
  if (m_rootEdit) m_rootEdit->clear();
  m_rootExpressions.clear();
  m_pinnedPositions.clear();
  m_nodeLayers.clear();
  m_displayOptions.clear();
  m_previousFieldValues.clear();
  m_mutatedFieldTransitions.clear();
  m_nodes.clear();
  m_nodesByExpr.clear();
  m_nodesByAddress.clear();
  m_edges.clear();
  m_pendingParentLinks.clear();
  m_pendingTypeValueHints.clear();
  m_autoExpandRoots.clear();
  m_hasCycleOrSharedAlias = false;
  if (m_view && m_view->viewport()) m_view->viewport()->update();
}

bool PointerGraphDialog::nodeHasField(const QString& nodeExpr, const QString& fieldName) const {
  const PointerGraphNodeItem* node = findNode(nodeExpr);
  return node && node->indexOfField(fieldName.trimmed()) >= 0;
}

QString PointerGraphDialog::fieldValue(const QString& nodeExpr, const QString& fieldName) const {
  const PointerGraphNodeItem* node = findNode(nodeExpr);
  if (!node) return QString();
  const int idx = node->indexOfField(fieldName.trimmed());
  return (idx >= 0) ? node->formattedFieldValue(idx) : QString();
}

bool PointerGraphDialog::isFieldMutated(const QString& nodeExpr, const QString& fieldName) const {
  return !fieldMutationSummary(nodeExpr, fieldName).isEmpty();
}

QString PointerGraphDialog::fieldMutationSummary(const QString& nodeExpr,
                                                 const QString& fieldName) const {
  const QString clean = nodeExpr.trimmed();
  if (m_mutatedFieldTransitions.contains(clean)) {
    return m_mutatedFieldTransitions.value(clean).value(fieldName.trimmed());
  }
  if (const PointerGraphNodeItem* node = findNode(clean)) {
    return m_mutatedFieldTransitions.value(node->expression()).value(fieldName.trimmed());
  }
  return QString();
}

int PointerGraphDialog::mutatedFieldCount() const {
  int count = 0;
  for (auto* node : m_nodes) count += m_mutatedFieldTransitions.value(node->expression()).size();
  return count;
}

QPointF PointerGraphDialog::nodePosition(const QString& nodeExprOrAddr) const {
  const PointerGraphNodeItem* node = findNode(nodeExprOrAddr);
  return node ? node->pos() : QPointF();
}

bool PointerGraphDialog::setNodePosition(const QString& nodeExprOrAddr, const QPointF& pos) {
  PointerGraphNodeItem* node = findNode(nodeExprOrAddr);
  if (!node) return false;
  node->setPos(pos);
  m_pinnedPositions.insert(canonicalNodeKey(node), node->pos());
  recomputeCyclesAndRefreshEdges();
  return true;
}

bool PointerGraphDialog::isNodeManuallyPositioned(const QString& nodeExprOrAddr) const {
  const PointerGraphNodeItem* node = findNode(nodeExprOrAddr);
  return node && m_pinnedPositions.contains(canonicalNodeKey(node));
}

int PointerGraphDialog::nodeLayer(const QString& nodeExprOrAddr) const {
  const PointerGraphNodeItem* node = findNode(nodeExprOrAddr);
  return node ? m_nodeLayers.value(canonicalNodeKey(node), -1) : -1;
}

QSizeF PointerGraphDialog::nodeCardSize(const QString& nodeExprOrAddr) const {
  const PointerGraphNodeItem* node = findNode(nodeExprOrAddr);
  return node ? node->boundingRect().size() : QSizeF();
}

QString PointerGraphDialog::edgeTargetAddressOrExpr(const QString& sourceExprOrAddr,
                                                    const QString& fieldName) const {
  const PointerGraphNodeItem* src = findNode(sourceExprOrAddr);
  const QString cleanField = fieldName.trimmed();
  if (!src || cleanField.isEmpty()) return QString();
  for (const auto* e : m_edges) {
    if (e->sourceNode() == src && e->sourceField() == cleanField && e->targetNode()) {
      return canonicalNodeKey(e->targetNode());
    }
  }
  return QString();
}

bool PointerGraphDialog::isEdgeCycleOrAlias(const QString& sourceExprOrAddr,
                                            const QString& fieldName) const {
  const PointerGraphNodeItem* src = findNode(sourceExprOrAddr);
  const QString cleanField = fieldName.trimmed();
  if (!src || cleanField.isEmpty()) return false;
  for (const auto* e : m_edges) {
    if (e->sourceNode() == src && e->sourceField() == cleanField) return e->isCycleOrAlias();
  }
  return false;
}

bool PointerGraphDialog::setNodeFieldHidden(const QString& nodeExprOrAddr,
                                            const QString& fieldName,
                                            bool hidden) {
  PointerGraphNodeItem* node = findNode(nodeExprOrAddr);
  const QString cleanField = fieldName.trimmed();
  if (!node || cleanField.isEmpty() || node->indexOfField(cleanField) < 0) return false;
  auto& opts = m_displayOptions[canonicalNodeKey(node)];
  if (hidden) opts.hiddenFields.insert(cleanField);
  else opts.hiddenFields.remove(cleanField);
  applyDisplayOptionsToNode(node);
  return true;
}

bool PointerGraphDialog::restoreAllHiddenFields(const QString& nodeExprOrAddr) {
  PointerGraphNodeItem* node = findNode(nodeExprOrAddr);
  if (!node) return false;
  m_displayOptions[canonicalNodeKey(node)].hiddenFields.clear();
  applyDisplayOptionsToNode(node);
  return true;
}

int PointerGraphDialog::hiddenFieldCount(const QString& nodeExprOrAddr) const {
  const PointerGraphNodeItem* n = findNode(nodeExprOrAddr); return n ? n->hiddenFieldCount() : 0;
}
int PointerGraphDialog::visibleFieldCount(const QString& nodeExprOrAddr) const {
  const PointerGraphNodeItem* n = findNode(nodeExprOrAddr); return n ? n->visibleFieldIndices().size() : 0;
}
QString PointerGraphDialog::hiddenFieldsBadgeText(const QString& nodeExprOrAddr) const {
  const PointerGraphNodeItem* n = findNode(nodeExprOrAddr); return n ? n->hiddenBadgeText() : QString();
}

bool PointerGraphDialog::setNodeHorizontal(const QString& nodeExprOrAddr, bool horizontal) {
  PointerGraphNodeItem* node = findNode(nodeExprOrAddr);
  if (!node) return false;
  m_displayOptions[canonicalNodeKey(node)].horizontal = horizontal;
  applyDisplayOptionsToNode(node);
  return true;
}

bool PointerGraphDialog::toggleNodeOrientation(const QString& nodeExprOrAddr) {
  PointerGraphNodeItem* node = findNode(nodeExprOrAddr);
  if (!node) return false;
  auto& opts = m_displayOptions[canonicalNodeKey(node)];
  opts.horizontal = !opts.horizontal;
  applyDisplayOptionsToNode(node);
  return true;
}

bool PointerGraphDialog::isNodeHorizontal(const QString& nodeExprOrAddr) const {
  const PointerGraphNodeItem* n = findNode(nodeExprOrAddr); return n && n->displayOptions().horizontal;
}

bool PointerGraphDialog::setNodeFormatOverride(const QString& nodeExprOrAddr, const QString& formatOverride) {
  PointerGraphNodeItem* node = findNode(nodeExprOrAddr);
  if (!node) return false;
  m_displayOptions[canonicalNodeKey(node)].formatOverride = normalizeFormatCode(formatOverride);
  applyDisplayOptionsToNode(node);
  return true;
}

QString PointerGraphDialog::nodeFormatOverride(const QString& nodeExprOrAddr) const {
  const PointerGraphNodeItem* n = findNode(nodeExprOrAddr); return n ? n->displayOptions().formatOverride : QString();
}
double PointerGraphDialog::zoomFactor() const { return m_view ? m_view->zoomFactor() : 1.0; }
void PointerGraphDialog::setZoomFactor(double factor) { if (m_view) m_view->setZoomFactor(factor); }
void PointerGraphDialog::resetZoom() { setZoomFactor(1.0); }
QRectF PointerGraphDialog::minimapViewportRect() const { return m_view ? m_view->minimapViewportRect() : QRectF(); }

void PointerGraphDialog::fitGraphInView() {
  if (!m_view || !m_scene || m_scene->items().isEmpty()) return;
  m_view->fitInView(m_scene->itemsBoundingRect().adjusted(-40.0, -40.0, 40.0, 40.0), Qt::KeepAspectRatio);
  m_view->setZoomFactor(m_view->zoomFactor());
}

bool PointerGraphDialog::exportGraphToPng(const QString& filePath) {
  if (!m_scene || filePath.trimmed().isEmpty()) return false;
  QRectF bounds = m_scene->itemsBoundingRect();
  if (bounds.isEmpty()) bounds = QRectF(0.0, 0.0, 640.0, 400.0);
  else bounds.adjust(-32.0, -32.0, 32.0, 32.0);

  const int w = std::clamp(static_cast<int>(std::ceil(bounds.width() * 2.0)), 64, 16384);
  const int h = std::clamp(static_cast<int>(std::ceil(bounds.height() * 2.0)), 64, 16384);
  QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
  const bool isDark = core::ThemeManager::instance().isDarkMode();
  img.fill(isDark ? QColor(QStringLiteral("#0f172a")) : QColor(QStringLiteral("#f8fafc")));

  QPainter painter(&img);
  if (!painter.isActive()) return false;
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                         QPainter::SmoothPixmapTransform, true);
  m_scene->render(&painter, QRectF(0.0, 0.0, w, h), bounds);
  painter.end();
  return img.save(filePath, "PNG");
}

bool PointerGraphDialog::exportGraphToSvg(const QString& filePath) {
  if (!m_scene || filePath.trimmed().isEmpty()) return false;
  QRectF bounds = m_scene->itemsBoundingRect();
  if (bounds.isEmpty()) bounds = QRectF(0.0, 0.0, 640.0, 400.0);
  else bounds.adjust(-32.0, -32.0, 32.0, 32.0);

  const QSize svgSize(std::max(64, static_cast<int>(std::ceil(bounds.width()))),
                      std::max(64, static_cast<int>(std::ceil(bounds.height()))));
  QSvgGenerator generator;
  generator.setFileName(filePath);
  generator.setSize(svgSize);
  generator.setViewBox(QRectF(0.0, 0.0, svgSize.width(), svgSize.height()));
  generator.setTitle(QStringLiteral("GridLock Pointer & Struct Graph"));

  QPainter painter(&generator);
  if (!painter.isActive()) return false;
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing, true);
  const bool isDark = core::ThemeManager::instance().isDarkMode();
  painter.fillRect(QRectF(0.0, 0.0, svgSize.width(), svgSize.height()),
                   isDark ? QColor(QStringLiteral("#0f172a")) : QColor(QStringLiteral("#f8fafc")));
  m_scene->render(&painter, QRectF(0.0, 0.0, svgSize.width(), svgSize.height()), bounds);
  painter.end();
  return QFileInfo::exists(filePath) && QFileInfo(filePath).size() > 0;
}

}  // namespace gridlock::ui

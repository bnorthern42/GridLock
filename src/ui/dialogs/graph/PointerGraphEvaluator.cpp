/**
 * @file PointerGraphEvaluator.cpp
 * @brief Coordinator binding, GDB/DAP struct/pointer evaluation, field parsing, and alias/cycle topology reconciliation for PointerGraphDialog.
 */

#include "PointerGraphDialog.hpp"
#include "PointerGraphLayout.hpp"

#include "../../../core/hpc/dap/DapCoordinator.hpp"
#include "../../../core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../../../core/hpc/IBackendCoordinator.hpp"

#include <QRegularExpression>
#include <algorithm>
#include <utility>

namespace gridlock::ui {

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

}  // namespace gridlock::ui

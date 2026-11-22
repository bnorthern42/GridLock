/**
 * @file PointerGraphDialog.cpp
 * @brief UI layout, viewport zoom/minimap, display options, undo history, and SVG/PNG export for PointerGraphDialog.
 *
 * GDB/DAP struct evaluation, field parsing, and pointer/alias reconciliation reside
 * in PointerGraphEvaluator.cpp; card/edge scene items and DAG layout reside in PointerGraphLayout.cpp.
 */

#include "PointerGraphDialog.hpp"
#include "PointerGraphLayout.hpp"

#include "../../../core/managers/ThemeManager.hpp"

#include <QAbstractItemView>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QPainter>
#include <QSvgGenerator>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <utility>

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

  // Row 1: Incremental Root Expression input with live autocomplete, Refresh, Auto-Refresh, Prune, and Close.
  auto* row1 = new QHBoxLayout(); row1->setSpacing(6);
  m_rootEdit = new QLineEdit(this); m_rootEdit->setObjectName(QStringLiteral("pointerGraphRootEdit"));
  m_rootEdit->setPlaceholderText(QStringLiteral("Add root pointer or struct expression (e.g., headA, data[0], cfg)..."));
  m_completerModel = new QStringListModel(this);
  m_completer = new QCompleter(m_completerModel, this);
  m_completer->setCompletionMode(QCompleter::PopupCompletion);
  m_completer->setCaseSensitivity(Qt::CaseInsensitive);
  m_completer->setFilterMode(Qt::MatchContains);
  if (auto* pop = m_completer->popup()) {
    pop->setObjectName(QStringLiteral("pointerGraphCompleterPopup"));
    pop->setStyleSheet(QStringLiteral("QAbstractItemView { background-color: #1e222a; color: #e5e9f0; selection-background-color: #3b82f6; selection-color: #ffffff; border: 1px solid #3b4252; padding: 2px; font-size: 12px; }"));
  }
  m_rootEdit->setCompleter(m_completer);

  m_addBtn = new QPushButton(QStringLiteral("Graph Expression"), this); m_addBtn->setObjectName(QStringLiteral("pointerGraphAddBtn"));
  m_refreshBtn = new QPushButton(QStringLiteral("Refresh Now"), this); m_refreshBtn->setObjectName(QStringLiteral("pointerGraphRefreshBtn"));
  m_autoRefreshCheck = new QCheckBox(QStringLiteral("Auto-Refresh on Stop"), this); m_autoRefreshCheck->setObjectName(QStringLiteral("pointerGraphAutoRefreshCheck")); m_autoRefreshCheck->setChecked(true);
  m_pruneCheck = new QCheckBox(QStringLiteral("Prune Unreferenced Nodes"), this); m_pruneCheck->setObjectName(QStringLiteral("pointerGraphPruneCheck")); m_pruneCheck->setChecked(true);
  m_closeBtn = new QPushButton(QStringLiteral("✕ CLOSE"), this); m_closeBtn->setObjectName(QStringLiteral("pointerGraphCloseBtn"));
  row1->addWidget(m_rootEdit, 1); row1->addWidget(m_addBtn); row1->addWidget(m_refreshBtn);
  row1->addWidget(m_autoRefreshCheck); row1->addWidget(m_pruneCheck); row1->addWidget(m_closeBtn);
  rootLayout->addLayout(row1);

  // Row 2: Re-Layout DAG, Expand 1-Hop, Clear Graph, SVG/PNG Export, and Zoom/Fit controls.
  auto* row2 = new QHBoxLayout(); row2->setSpacing(6);
  m_layoutBtn = new QPushButton(QStringLiteral("Re-Layout DAG"), this); m_layoutBtn->setObjectName(QStringLiteral("pointerGraphLayoutBtn"));
  m_layoutBtn->setToolTip(QStringLiteral("Reset manual card positions and compute a layered Sugiyama DAG layout"));
  m_expandHopBtn = new QPushButton(QStringLiteral("Expand All Pointers (1 Hop)"), this); m_expandHopBtn->setObjectName(QStringLiteral("pointerGraphExpandHopBtn"));
  m_clearBtn = new QPushButton(QStringLiteral("Clear Graph"), this); m_clearBtn->setObjectName(QStringLiteral("pointerGraphClearBtn"));
  m_undoClearBtn = new QPushButton(QStringLiteral("Undo Clear"), this); m_undoClearBtn->setObjectName(QStringLiteral("pointerGraphUndoClearBtn"));
  m_undoClearBtn->setEnabled(false);
  m_exportSvgBtn = new QPushButton(QStringLiteral("Export SVG..."), this); m_exportSvgBtn->setObjectName(QStringLiteral("pointerGraphExportSvgBtn"));
  m_exportPngBtn = new QPushButton(QStringLiteral("Export PNG..."), this); m_exportPngBtn->setObjectName(QStringLiteral("pointerGraphExportPngBtn"));
  auto* zoomOutBtn = new QPushButton(QStringLiteral("Zoom -"), this);
  m_resetZoomBtn = new QPushButton(QStringLiteral("100%"), this); m_resetZoomBtn->setObjectName(QStringLiteral("pointerGraphResetZoomBtn"));
  auto* zoomInBtn = new QPushButton(QStringLiteral("Zoom +"), this);
  m_fitBtn = new QPushButton(QStringLiteral("Fit Graph"), this); m_fitBtn->setObjectName(QStringLiteral("pointerGraphFitBtn"));

  row2->addWidget(m_layoutBtn); row2->addWidget(m_expandHopBtn); row2->addWidget(m_clearBtn); row2->addWidget(m_undoClearBtn); row2->addStretch(1);
  row2->addWidget(m_exportSvgBtn); row2->addWidget(m_exportPngBtn);
  row2->addWidget(zoomOutBtn); row2->addWidget(m_resetZoomBtn); row2->addWidget(zoomInBtn); row2->addWidget(m_fitBtn);
  rootLayout->addLayout(row2);

  m_scene = new QGraphicsScene(this);
  m_view = new PointerGraphView(m_scene, this); m_view->setObjectName(QStringLiteral("pointerGraphView"));
  rootLayout->addWidget(m_view, 1);

  connect(m_addBtn, &QPushButton::clicked, this, [this]() {
    if (m_rootEdit && !m_rootEdit->text().trimmed().isEmpty()) addAndEvaluateRootExpression(m_rootEdit->text().trimmed());
  });
  connect(m_rootEdit, &QLineEdit::returnPressed, m_addBtn, &QPushButton::click);
  connect(m_refreshBtn, &QPushButton::clicked, this, &PointerGraphDialog::refreshPinnedRoots);
  connect(m_pruneCheck, &QCheckBox::toggled, this, [this](bool checked) {
    if (checked) { pruneUnreferencedNodes(); recomputeCyclesAndRefreshEdges(); }
  });
  connect(m_layoutBtn, &QPushButton::clicked, this, &PointerGraphDialog::resetLayoutAndRelayoutDag);
  connect(m_expandHopBtn, &QPushButton::clicked, this, &PointerGraphDialog::expandAllPointersOneHop);
  connect(m_clearBtn, &QPushButton::clicked, this, &PointerGraphDialog::clearGraph);
  connect(m_undoClearBtn, &QPushButton::clicked, this, &PointerGraphDialog::undoClearGraph);
  connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);

  connect(m_exportSvgBtn, &QPushButton::clicked, this, [this]() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Pointer Graph SVG"), QStringLiteral("pointer_graph.svg"), QStringLiteral("SVG Vector (*.svg)"));
    if (!path.isEmpty()) exportGraphToSvg(path);
  });
  connect(m_exportPngBtn, &QPushButton::clicked, this, [this]() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Pointer Graph PNG"), QStringLiteral("pointer_graph.png"), QStringLiteral("PNG Image (*.png)"));
    if (!path.isEmpty()) exportGraphToPng(path);
  });
  connect(zoomInBtn, &QPushButton::clicked, this, [this]() { setZoomFactor(zoomFactor() * 1.15); });
  connect(zoomOutBtn, &QPushButton::clicked, this, [this]() { setZoomFactor(zoomFactor() / 1.15); });
  connect(m_resetZoomBtn, &QPushButton::clicked, this, &PointerGraphDialog::resetZoom);
  connect(m_fitBtn, &QPushButton::clicked, this, &PointerGraphDialog::fitGraphInView);
}

void PointerGraphDialog::setCompletionCandidates(const QStringList& candidates) {
  QStringList deduped;
  QSet<QString> seen;
  auto push = [&](const QString& raw) {
    const QString s = raw.trimmed();
    if (!s.isEmpty() && !seen.contains(s)) { seen.insert(s); deduped.append(s); }
  };
  for (const QString& c : candidates) push(c);
  for (const QString& r : m_rootExpressions) push(r);
  for (const auto* n : m_nodes) if (n) push(n->expression());
  m_completionCandidates = deduped;
  if (m_completerModel) m_completerModel->setStringList(m_completionCandidates);
}

void PointerGraphDialog::setRootInputText(const QString& text) {
  if (!m_rootEdit) return;
  m_rootEdit->setText(text);
  m_rootEdit->setFocus();
  if (m_completer && !text.trimmed().isEmpty()) {
    m_completer->setCompletionPrefix(text.trimmed());
    m_completer->complete();
  }
}

bool PointerGraphDialog::isAutoRefreshEnabled() const { return m_autoRefreshCheck ? m_autoRefreshCheck->isChecked() : true; }
void PointerGraphDialog::setAutoRefreshEnabled(bool enabled) { if (m_autoRefreshCheck) m_autoRefreshCheck->setChecked(enabled); }
bool PointerGraphDialog::isPruneUnreferencedEnabled() const { return m_pruneCheck ? m_pruneCheck->isChecked() : true; }
void PointerGraphDialog::setPruneUnreferencedEnabled(bool enabled) { if (m_pruneCheck) m_pruneCheck->setChecked(enabled); }

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
  if (!m_nodes.isEmpty() || !m_rootExpressions.isEmpty()) {
    GraphUndoSnapshot snap;
    snap.roots = m_rootExpressions; snap.rootInput = m_rootEdit ? m_rootEdit->text() : QString();
    snap.pinnedPositions = m_pinnedPositions; snap.displayOptions = m_displayOptions;
    snap.previousFieldValues = m_previousFieldValues; snap.mutatedTransitions = m_mutatedFieldTransitions;
    for (const auto* n : m_nodes) if (n) snap.nodes.append({n->expression(), n->typeName(), n->address(), n->fields(), n->pos()});
    for (const auto* e : m_edges) if (e && e->sourceNode() && e->targetNode()) snap.edges.append({e->sourceNode()->expression(), e->sourceField(), canonicalNodeKey(e->targetNode())});
    m_undoSnapshot = std::move(snap); m_hasUndoSnapshot = true;
    if (m_undoClearBtn) m_undoClearBtn->setEnabled(true);
  }
  if (m_scene) m_scene->clear();
  if (m_rootEdit) m_rootEdit->clear();
  m_rootExpressions.clear(); m_pinnedPositions.clear(); m_nodeLayers.clear(); m_displayOptions.clear();
  m_previousFieldValues.clear(); m_mutatedFieldTransitions.clear(); m_nodes.clear();
  m_nodesByExpr.clear(); m_nodesByAddress.clear(); m_edges.clear();
  m_pendingParentLinks.clear(); m_pendingTypeValueHints.clear(); m_autoExpandRoots.clear();
  m_hasCycleOrSharedAlias = false;
  if (m_view && m_view->viewport()) m_view->viewport()->update();
}

bool PointerGraphDialog::undoClearGraph() {
  if (!m_hasUndoSnapshot) return false;
  const GraphUndoSnapshot snap = std::move(m_undoSnapshot);
  m_hasUndoSnapshot = false;
  if (m_undoClearBtn) m_undoClearBtn->setEnabled(false);
  m_rootExpressions = snap.roots; m_displayOptions = snap.displayOptions;
  for (const auto& ns : snap.nodes) {
    upsertStructNode(ns.expr, ns.typeName, ns.address, ns.fields);
    if (auto* node = findNode(ns.expr)) { ScopedPinSuppress g(m_suppressPositionPinning); node->setPos(ns.pos); }
  }
  m_pinnedPositions = snap.pinnedPositions;
  m_previousFieldValues = snap.previousFieldValues; m_mutatedFieldTransitions = snap.mutatedTransitions;
  for (auto* n : m_nodes) n->setMutatedFields(m_mutatedFieldTransitions.value(n->expression()));
  for (const auto& es : snap.edges) connectPointerField(es.srcExpr, es.fieldName, es.dstKey);
  if (m_rootEdit) m_rootEdit->setText(snap.rootInput);
  syncNodeRootFlags(); recomputeCyclesAndRefreshEdges();
  return true;
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

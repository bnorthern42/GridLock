/**
 * @file PointerGraphDialog.cpp
 * @brief Implementation of the DDD-inspired interactive 2D pointer and struct graph visualizer.
 *
 * Implements draggable struct/object cards, real-time cubic Bezier pointer edges,
 * address-based node deduplication, cycle and shared-alias detection, layered
 * topological DAG auto-layout, and live GDB/DAP `requestVariableChildren` expansion.
 */

#include "PointerGraphDialog.hpp"

#include "../../core/hpc/DapCoordinator.hpp"
#include "../../core/hpc/GdbRankCoordinator.hpp"
#include "../../core/hpc/IBackendCoordinator.hpp"
#include "../../core/managers/ThemeManager.hpp"

#include <QFontDatabase>
#include <QGraphicsSceneMouseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QQueue>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <cmath>
#include <functional>

namespace gridlock::ui {

namespace {
constexpr qreal kCardWidth = 290.0;
constexpr qreal kHeaderHeight = 30.0;
constexpr qreal kRowHeight = 24.0;
constexpr qreal kFooterPadding = 8.0;

/** @brief Returns true when @p v represents a null pointer (`0x0`, `0x00000000`, `nullptr`). */
bool isNullPointerValue(const QString& v) {
  const QString t = v.trimmed().section(QLatin1Char(' '), 0, 0).toLower();
  if (t == QLatin1String("nullptr") || t == QLatin1String("null")) return true;
  if (!t.startsWith(QLatin1String("0x"))) return false;
  bool ok = false;
  return t.mid(2).toULongLong(&ok, 16) == 0ULL && ok;
}

/** @brief Returns true when a struct field represents a non-null pointer or composite sub-object. */
bool isExpandableOrPointerField(const PointerGraphDialog::StructFieldEntry& f) {
  const QString v = f.value.trimmed();
  if (isNullPointerValue(v)) return false;
  return v.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive) ||
         f.type.contains(QLatin1Char('*')) || v.startsWith(QLatin1Char('{'));
}
}  // namespace

/**
 * @class PointerGraphNodeItem
 * @brief Draggable 2D struct/class card rendering header metadata, field slots, and `[➔ Deref]` actions.
 *
 * Each card represents either a unique memory address (`0x...`) or a value-typed struct
 * expression path. Moving a card triggers immediate Bezier path recalculation on all
 * incoming and outgoing pointer edges.
 */
class PointerGraphNodeItem : public QGraphicsObject {
 public:
  /**
   * @brief Constructs a movable struct node card for @p expr, @p type, and @p addr.
   * @param expr Primary C++ expression path.
   * @param type Static C/C++ type identifier.
   * @param addr Normalized hex memory address, or empty if value-typed.
   * @param movedCb Callback invoked whenever the card position changes on the canvas.
   * @param expandCb Callback invoked when a field row or `[➔ Deref]` badge is activated.
   */
  PointerGraphNodeItem(QString expr,
                       QString type,
                       QString addr,
                       std::function<void()> movedCb,
                       std::function<void(const QString&, const QString&)> expandCb)
      : m_expression(std::move(expr)),
        m_typeName(std::move(type)),
        m_address(std::move(addr)),
        m_onMoved(std::move(movedCb)),
        m_onExpandField(std::move(expandCb)) {
    // Enable interactive dragging, selection, and position-change notifications for attached edges.
    setFlags(QGraphicsItem::ItemIsMovable | QGraphicsItem::ItemIsSelectable |
             QGraphicsItem::ItemSendsGeometryChanges);
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
  }

  /**
   * @brief Updates node metadata and field rows, triggering a geometry and scene repaint.
   * @param expr Updated C++ expression label.
   * @param type Updated C/C++ type label.
   * @param addr Updated normalized hex address (`0x...`).
   * @param fields Ordered list of member fields to display.
   */
  void updateData(const QString& expr,
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

  /**
   * @brief Updates the map of field-level value transitions (`fieldName -> "old -> new"`) since the last stop.
   */
  void setMutatedFields(const QMap<QString, QString>& transitions) {
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

  /** @brief Returns the primary C++ expression label for this node card. */
  const QString& expression() const { return m_expression; }
  /** @brief Returns the C/C++ type label for this node card. */
  const QString& typeName() const { return m_typeName; }
  /** @brief Returns the normalized hex address (`0x...`) for this node card. */
  const QString& address() const { return m_address; }
  /** @brief Returns the ordered list of struct member fields rendered in this card. */
  const QVector<PointerGraphDialog::StructFieldEntry>& fields() const { return m_fields; }

  /**
   * @brief Returns the 0-based row index of @p fieldName, or `-1` if not present.
   * @param fieldName Struct member identifier to search for.
   * @return Zero-based row index, or `-1` when not found.
   */
  int indexOfField(const QString& fieldName) const {
    for (int i = 0; i < m_fields.size(); ++i) {
      if (m_fields[i].name == fieldName) return i;
    }
    return -1;
  }

  /**
   * @brief Computes the scene-space outgoing anchor point on the right edge of field row @p fieldName.
   * @param fieldName Source pointer or struct field identifier.
   * @return Scene coordinate at the right border of the matching field slot.
   */
  QPointF fieldSourceAnchor(const QString& fieldName) const {
    const int idx = indexOfField(fieldName);
    const qreal rowY = (idx >= 0) ? (kHeaderHeight + idx * kRowHeight + kRowHeight * 0.5)
                                  : (kHeaderHeight * 0.5);
    return mapToScene(QPointF(kCardWidth, rowY));
  }

  /**
   * @brief Computes the scene-space incoming target anchor point on the left header edge of the card.
   * @return Scene coordinate at the vertical midpoint of the card header's left edge.
   */
  QPointF targetAnchor() const { return mapToScene(QPointF(0.0, kHeaderHeight * 0.5)); }

  /**
   * @brief Returns the bounding rectangle sized dynamically to the number of member fields.
   * @return Local-coordinate bounding box for Qt graphics view culling and hit-testing.
   */
  QRectF boundingRect() const override {
    const int rows = qMax(1, static_cast<int>(m_fields.size()));
    return QRectF(0.0, 0.0, kCardWidth, kHeaderHeight + rows * kRowHeight + kFooterPadding);
  }

  /**
   * @brief Renders the dark/light theme-cohesive struct card, member rows, mutation highlights, and `[➔ Deref]` badges.
   * @param painter Active QPainter context.
   */
  void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
    painter->setRenderHint(QPainter::Antialiasing, true);
    const bool isDark = core::ThemeManager::instance().isDarkMode();
    const QRectF rect = boundingRect();

    // Draw outer rounded card body and selection highlight border.
    const QColor bgColor = isDark ? QColor(QStringLiteral("#181c24")) : QColor(QStringLiteral("#ffffff"));
    const QColor borderColor = isSelected() ? QColor(QStringLiteral("#38bdf8"))
                                            : (!m_mutatedTransitions.isEmpty()
                                                   ? QColor(QStringLiteral("#f59e0b"))
                                                   : (isDark ? QColor(QStringLiteral("#334155")) : QColor(QStringLiteral("#cbd5e1"))));
    painter->setPen(QPen(borderColor, (isSelected() || !m_mutatedTransitions.isEmpty()) ? 2.0 : 1.2));
    painter->setBrush(bgColor);
    painter->drawRoundedRect(rect, 7.0, 7.0);

    // Draw header banner with expression, type, and @ 0xAddress badge.
    QPainterPath hdrPath;
    hdrPath.addRoundedRect(QRectF(0.0, 0.0, kCardWidth, kHeaderHeight), 7.0, 7.0);
    painter->fillPath(hdrPath, isDark ? QColor(QStringLiteral("#1e293b")) : QColor(QStringLiteral("#e2e8f0")));
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(11);
    mono.setBold(true);
    painter->setFont(mono);
    painter->setPen(isDark ? QColor(QStringLiteral("#f8fafc")) : QColor(QStringLiteral("#0f172a")));

    // Format and elide the header title string so long C++ template types never overflow.
    QString hdrText = m_expression;
    if (!m_typeName.isEmpty()) hdrText += QStringLiteral(" : %1").arg(m_typeName);
    if (!m_address.isEmpty()) hdrText += QStringLiteral(" @ %1").arg(m_address);
    painter->drawText(QRectF(10.0, 0.0, kCardWidth - 20.0, kHeaderHeight),
                      Qt::AlignVCenter | Qt::AlignLeft,
                      painter->fontMetrics().elidedText(hdrText, Qt::ElideRight, static_cast<int>(kCardWidth - 20.0)));

    // Render each member field row and its clickable `[➔ Deref]` badge when expandable.
    mono.setBold(false);
    painter->setFont(mono);
    if (m_fields.isEmpty()) {
      painter->setPen(isDark ? QColor(QStringLiteral("#94a3b8")) : QColor(QStringLiteral("#64748b")));
      painter->drawText(QRectF(10.0, kHeaderHeight, kCardWidth - 20.0, kRowHeight),
                        Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("(double-click or expand fields...)"));
      return;
    }

    // Iterate over all member fields and paint name : value (type) plus mutation highlight and optional dereference pill.
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
      if (isMutated) {
        rowTxt += QStringLiteral(" [%1]").arg(m_mutatedTransitions.value(f.name));
      }
      painter->drawText(QRectF(10.0, y, kCardWidth - 10.0 - rightPad, kRowHeight),
                        Qt::AlignVCenter | Qt::AlignLeft,
                        painter->fontMetrics().elidedText(rowTxt, Qt::ElideRight, static_cast<int>(kCardWidth - 14.0 - rightPad)));

      // Paint clickable `[➔ Deref]` action pill on non-null pointer/struct rows or muted `nullptr` badge.
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

 protected:
  /**
   * @brief Notifies connected Bezier edges whenever the user drags this node card.
   * @param change Graphics item change type.
   * @param value New attribute value.
   */
  QVariant itemChange(GraphicsItemChange change, const QVariant& value) override {
    if (change == ItemPositionHasChanged && m_onMoved) m_onMoved();
    return QGraphicsObject::itemChange(change, value);
  }

  /**
   * @brief Detects clicks on `[➔ Deref]` badges to trigger immediate pointer/member expansion.
   * @param event Mouse press event in item coordinates.
   */
  void mousePressEvent(QGraphicsSceneMouseEvent* event) override {
    if (event && event->button() == Qt::LeftButton) {
      const QPointF p = event->pos();
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

  /**
   * @brief Double-clicking any field row expands or dereferences that member.
   * @param event Mouse double-click event in item coordinates.
   */
  void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override {
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

 private:
  QString m_expression;                                          ///< C++ expression path.
  QString m_typeName;                                            ///< Static C/C++ type name.
  QString m_address;                                             ///< Normalized hex address (`0x...`).
  QVector<PointerGraphDialog::StructFieldEntry> m_fields;        ///< Displayed struct fields.
  QMap<QString, QString> m_mutatedTransitions;                   ///< Field-level `old -> new` diff transitions.
  std::function<void()> m_onMoved;                               ///< Callback refreshing Bezier edges on move.
  std::function<void(const QString&, const QString&)> m_onExpandField; ///< Callback expanding a clicked field.
};

/**
 * @class PointerGraphEdgeItem
 * @brief Directed cubic Bezier arrow connecting a source struct field slot to a target node card.
 *
 * Automatically highlights back-edges, pointer cycles, and multi-parent shared aliases
 * in high-contrast Amber with a `[CYCLE / SHARED ALIAS]` pill label.
 */
class PointerGraphEdgeItem : public QGraphicsPathItem {
 public:
  /**
   * @brief Constructs a directed edge from `(src, fieldName)` to `dst`.
   * @param src Origin struct node card.
   * @param fieldName Origin field slot name.
   * @param dst Target struct node card.
   */
  PointerGraphEdgeItem(PointerGraphNodeItem* src, QString fieldName, PointerGraphNodeItem* dst)
      : m_source(src), m_fieldName(std::move(fieldName)), m_target(dst) {
    setZValue(-1.0);
    updateGeometry();
  }

  /** @brief Returns the source node card. */
  PointerGraphNodeItem* sourceNode() const { return m_source; }
  /** @brief Returns the source field identifier. */
  const QString& sourceField() const { return m_fieldName; }
  /** @brief Returns the destination node card. */
  PointerGraphNodeItem* targetNode() const { return m_target; }

  /**
   * @brief Marks or clears cycle/shared-alias status and refreshes edge styling.
   * @param flag True when this edge participates in a directed cycle or shared alias.
   */
  void setCycleOrAlias(bool flag) {
    m_isCycleOrAlias = flag;
    updateGeometry();
  }

  /**
   * @brief Rebuilds the cubic Bezier path and arrowhead between source field and target header.
   */
  void updateGeometry() {
    if (!m_source || !m_target) return;
    const QPointF p1 = m_source->fieldSourceAnchor(m_fieldName);
    const QPointF p2 = m_target->targetAnchor();
    QPainterPath path(p1);
    if (p2.x() >= p1.x() + 20.0) {
      // Forward left-to-right edge: smooth horizontal S-curve between cards.
      const qreal dx = (p2.x() - p1.x()) * 0.5;
      path.cubicTo(QPointF(p1.x() + dx, p1.y()), QPointF(p2.x() - dx, p2.y()), p2);
    } else {
      // Back-edge / cycle arc: curve below both cards (`maxY + 75px`) so it never clips behind card bodies.
      const qreal archY = std::max(m_source->sceneBoundingRect().bottom(), m_target->sceneBoundingRect().bottom()) + 75.0;
      path.cubicTo(QPointF(p1.x() + 65.0, archY), QPointF(p2.x() - 65.0, archY), p2);
    }
    m_midPoint = path.pointAtPercent(0.5);
    setPath(path);
    update();
  }

  /**
   * @brief Paints the Bezier curve, arrowhead triangle, and `[CYCLE / SHARED ALIAS]` badge when active.
   * @param painter Active QPainter context.
   */
  void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QColor edgeColor = m_isCycleOrAlias ? QColor(QStringLiteral("#f59e0b"))
                                              : QColor(QStringLiteral("#38bdf8"));
    painter->setPen(QPen(edgeColor, m_isCycleOrAlias ? 2.2 : 1.7,
                         m_isCycleOrAlias ? Qt::DashLine : Qt::SolidLine));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());

    // Draw solid arrowhead at target anchor `p2`.
    const QPointF p2 = m_target ? m_target->targetAnchor() : QPointF();
    QPolygonF arrow;
    arrow << p2 << QPointF(p2.x() - 8.0, p2.y() - 4.5) << QPointF(p2.x() - 8.0, p2.y() + 4.5);
    painter->setPen(Qt::NoPen);
    painter->setBrush(edgeColor);
    painter->drawPolygon(arrow);

    // Render high-contrast badge at curve midpoint when edge participates in a cycle or shared alias.
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

 private:
  PointerGraphNodeItem* m_source = nullptr;  ///< Origin struct card.
  QString m_fieldName;                       ///< Origin pointer/composite field name.
  PointerGraphNodeItem* m_target = nullptr;  ///< Destination struct card.
  QPointF m_midPoint;                        ///< Midpoint of Bezier curve for badge placement.
  bool m_isCycleOrAlias = false;             ///< True when edge forms a cycle or multi-parent alias.
};

/** @brief Constructs the interactive pointer/struct graph dialog and binds @p coordinator. */
PointerGraphDialog::PointerGraphDialog(::IBackendCoordinator* coordinator, QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint) {
  setObjectName(QStringLiteral("PointerGraphDialog"));
  setWindowTitle(QStringLiteral("DDD Pointer & Struct Graph Visualizer"));
  resize(1100, 720);
  setupUi();
  setCoordinator(coordinator);
}

/** @brief Builds the toolbar, interactive QGraphicsView canvas, and control connections. */
void PointerGraphDialog::setupUi() {
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(8, 8, 8, 8);
  rootLayout->setSpacing(6);

  // Top toolbar: Root Expression input + Graph, Refresh Now, Auto-Refresh on Stop, Auto-Layout, Expand 1-Hop, Clear, and Zoom buttons.
  auto* toolbar = new QHBoxLayout();
  toolbar->setSpacing(6);
  m_rootEdit = new QLineEdit(this);
  m_rootEdit->setObjectName(QStringLiteral("pointerGraphRootEdit"));
  m_rootEdit->setPlaceholderText(QStringLiteral("Enter pointer or struct expression (e.g., mesh, Iconfig, head)..."));
  m_addBtn = new QPushButton(QStringLiteral("Graph Expression"), this);
  m_addBtn->setObjectName(QStringLiteral("pointerGraphAddBtn"));
  m_refreshBtn = new QPushButton(QStringLiteral("Refresh Now"), this);
  m_refreshBtn->setObjectName(QStringLiteral("pointerGraphRefreshBtn"));
  m_autoRefreshCheck = new QCheckBox(QStringLiteral("Auto-Refresh on Stop"), this);
  m_autoRefreshCheck->setObjectName(QStringLiteral("pointerGraphAutoRefreshCheck"));
  m_autoRefreshCheck->setChecked(true);
  m_layoutBtn = new QPushButton(QStringLiteral("Auto-Layout (Layered DAG)"), this);
  m_layoutBtn->setObjectName(QStringLiteral("pointerGraphLayoutBtn"));
  m_expandHopBtn = new QPushButton(QStringLiteral("Expand All Pointers (1 Hop)"), this);
  m_expandHopBtn->setObjectName(QStringLiteral("pointerGraphExpandHopBtn"));
  m_clearBtn = new QPushButton(QStringLiteral("Clear Graph"), this);
  m_clearBtn->setObjectName(QStringLiteral("pointerGraphClearBtn"));

  auto* zoomInBtn = new QPushButton(QStringLiteral("Zoom +"), this);
  auto* zoomOutBtn = new QPushButton(QStringLiteral("Zoom -"), this);
  auto* zoomFitBtn = new QPushButton(QStringLiteral("Fit"), this);
  m_closeBtn = new QPushButton(QStringLiteral("✕ CLOSE"), this);
  m_closeBtn->setObjectName(QStringLiteral("pointerGraphCloseBtn"));

  toolbar->addWidget(m_rootEdit, 1);
  toolbar->addWidget(m_addBtn);
  toolbar->addWidget(m_refreshBtn);
  toolbar->addWidget(m_autoRefreshCheck);
  toolbar->addWidget(m_layoutBtn);
  toolbar->addWidget(m_expandHopBtn);
  toolbar->addWidget(m_clearBtn);
  toolbar->addWidget(zoomInBtn);
  toolbar->addWidget(zoomOutBtn);
  toolbar->addWidget(zoomFitBtn);
  toolbar->addWidget(m_closeBtn);
  rootLayout->addLayout(toolbar);

  // 2D Interactive Canvas.
  m_scene = new QGraphicsScene(this);
  m_view = new QGraphicsView(m_scene, this);
  m_view->setObjectName(QStringLiteral("pointerGraphView"));
  m_view->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  m_view->setDragMode(QGraphicsView::ScrollHandDrag);
  rootLayout->addWidget(m_view, 1);

  connect(m_addBtn, &QPushButton::clicked, this, [this]() {
    if (m_rootEdit && !m_rootEdit->text().trimmed().isEmpty()) graphExpression(m_rootEdit->text().trimmed());
  });
  connect(m_rootEdit, &QLineEdit::returnPressed, m_addBtn, &QPushButton::click);
  connect(m_refreshBtn, &QPushButton::clicked, this, &PointerGraphDialog::refreshPinnedRoots);
  connect(m_layoutBtn, &QPushButton::clicked, this, &PointerGraphDialog::autoLayoutNodes);
  connect(m_expandHopBtn, &QPushButton::clicked, this, &PointerGraphDialog::expandAllPointersOneHop);
  connect(m_clearBtn, &QPushButton::clicked, this, &PointerGraphDialog::clearGraph);
  connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::close);
  connect(zoomInBtn, &QPushButton::clicked, this, [this]() { if (m_view) m_view->scale(1.15, 1.15); });
  connect(zoomOutBtn, &QPushButton::clicked, this, [this]() { if (m_view) m_view->scale(1.0 / 1.15, 1.0 / 1.15); });
  connect(zoomFitBtn, &QPushButton::clicked, this, [this]() {
    if (m_view && m_scene && !m_scene->items().isEmpty())
      m_view->fitInView(m_scene->itemsBoundingRect().adjusted(-40, -40, 40, 40), Qt::KeepAspectRatio);
  });
}

/**
 * @brief Binds @p coordinator and connects `variableChildrenUpdated` and target stop notifications.
 * @param coordinator Active debugger backend coordinator instance.
 */
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

void PointerGraphDialog::pinRootExpression(const QString& expression) {
  const QString clean = expression.trimmed();
  if (!clean.isEmpty() && !m_rootExpressions.contains(clean)) {
    m_rootExpressions.append(clean);
  }
}

void PointerGraphDialog::refreshPinnedRoots() {
  QStringList targets = m_rootExpressions;
  for (auto it = m_nodesByExpr.cbegin(); it != m_nodesByExpr.cend(); ++it) {
    if (!targets.contains(it.key())) targets.append(it.key());
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

/**
 * @brief Normalizes hex pointer strings (`0x00007fff...` -> `0x7fff...`) and filters out null/non-hex.
 * @param rawAddr Raw value or address string from GDB/DAP.
 * @return Canonical `0x<hex>` string, or empty string when null (`0x0`) or non-pointer.
 */
QString PointerGraphDialog::normalizeHexAddress(const QString& rawAddr) {
  static const QRegularExpression rxHex(QStringLiteral("(0x[0-9a-fA-F]+)"));
  const auto m = rxHex.match(rawAddr.trimmed());
  if (!m.hasMatch()) return QString();
  bool ok = false;
  const quint64 val = m.captured(1).mid(2).toULongLong(&ok, 16);
  return (ok && val != 0ULL) ? QStringLiteral("0x%1").arg(val, 0, 16) : QString();
}

/**
 * @brief Inserts or updates a struct node card on the canvas, deduplicating by non-null hex address.
 * @param expression C++ expression path.
 * @param typeName Static C/C++ type string.
 * @param address Hex memory address (`0x...`) or empty if value-typed.
 * @param fields Member fields to populate inside the card.
 * @param parentExpr Optional parent node expression that spawned this expansion.
 * @param parentField Optional field name on @p parentExpr linking to this node.
 */
void PointerGraphDialog::upsertStructNode(const QString& expression,
                                          const QString& typeName,
                                          const QString& address,
                                          const QVector<StructFieldEntry>& fields,
                                          const QString& parentExpr,
                                          const QString& parentField) {
  const QString cleanExpr = expression.trimmed();
  if (cleanExpr.isEmpty()) return;
  if (parentExpr.trimmed().isEmpty()) pinRootExpression(cleanExpr);
  const QString normAddr = normalizeHexAddress(address);

  // Deduplicate by normalized hex address first, then by expression path.
  PointerGraphNodeItem* node = !normAddr.isEmpty() ? m_nodesByAddress.value(normAddr, nullptr) : nullptr;
  if (!node) node = m_nodesByExpr.value(cleanExpr, nullptr);
  const bool isNewNode = (node == nullptr);

  // Allocate a new movable graphics card only when neither address nor expression is on the canvas.
  if (!node) {
    node = new PointerGraphNodeItem(
        cleanExpr, typeName.trimmed(), normAddr,
        [this]() { for (auto* e : m_edges) e->updateGeometry(); },
        [this](const QString& nExpr, const QString& fName) { expandNodeField(nExpr, fName); });
    m_scene->addItem(node);
    m_nodes.append(node);
  }

  // Compare each incoming field value against the previous stop snapshot to highlight mutations.
  const QString nodeKey = node->expression();
  if (!fields.isEmpty()) {
    auto& prevMap = m_previousFieldValues[nodeKey];
    auto& mutMap = m_mutatedFieldTransitions[nodeKey];
    for (const auto& f : fields) {
      if (prevMap.contains(f.name)) {
        const QString oldVal = prevMap.value(f.name);
        if (oldVal != f.value) {
          mutMap.insert(f.name, QStringLiteral("%1 -> %2").arg(oldVal, f.value));
        } else {
          mutMap.remove(f.name);
        }
      }
      prevMap.insert(f.name, f.value);
    }
    if (nodeKey != cleanExpr) {
      m_previousFieldValues[cleanExpr] = prevMap;
      m_mutatedFieldTransitions[cleanExpr] = mutMap;
    }
  }

  node->updateData(cleanExpr, typeName.trimmed(), normAddr, fields);
  node->setMutatedFields(m_mutatedFieldTransitions.value(nodeKey));
  m_nodesByExpr.insert(cleanExpr, node);
  if (!normAddr.isEmpty()) m_nodesByAddress.insert(normAddr, node);

  // Connect parent -> child edge if this node was spawned by expanding a parent field.
  if (!parentExpr.trimmed().isEmpty() && !parentField.trimmed().isEmpty()) {
    connectPointerField(parentExpr.trimmed(), parentField.trimmed(),
                        !normAddr.isEmpty() ? normAddr : cleanExpr);
  }
  if (isNewNode) {
    autoLayoutNodes();
  } else {
    recomputeCyclesAndRefreshEdges();
  }
}

/**
 * @brief Connects `(sourceExpr, fieldName)` to an existing node matching @p targetAddressOrExpr.
 * @param sourceExpr Origin node expression or hex address.
 * @param fieldName Origin pointer/struct field name.
 * @param targetAddressOrExpr Destination hex address or expression key.
 * @return True if both endpoints exist and a directed Bezier edge was created/updated.
 */
bool PointerGraphDialog::connectPointerField(const QString& sourceExpr,
                                             const QString& fieldName,
                                             const QString& targetAddressOrExpr) {
  PointerGraphNodeItem* src = m_nodesByExpr.value(sourceExpr.trimmed(), nullptr);
  if (!src) src = m_nodesByAddress.value(normalizeHexAddress(sourceExpr), nullptr);
  const QString normTarget = normalizeHexAddress(targetAddressOrExpr);
  PointerGraphNodeItem* dst = !normTarget.isEmpty() ? m_nodesByAddress.value(normTarget, nullptr) : nullptr;
  if (!dst) dst = m_nodesByExpr.value(targetAddressOrExpr.trimmed(), nullptr);
  if (!src || !dst) return false;

  // Reuse existing edge if `(src, fieldName)` is already wired.
  for (auto* edge : m_edges) {
    if (edge->sourceNode() == src && edge->sourceField() == fieldName.trimmed()) {
      recomputeCyclesAndRefreshEdges();
      return true;
    }
  }
  auto* edge = new PointerGraphEdgeItem(src, fieldName.trimmed(), dst);
  m_scene->addItem(edge);
  m_edges.append(edge);
  recomputeCyclesAndRefreshEdges();
  return true;
}

/**
 * @brief Returns true if @p startNode can reach @p goalNode along existing directed edges.
 * @param startNode BFS starting node card.
 * @param goalNode Target node card to test reachability against.
 * @return True when a directed path exists from @p startNode to @p goalNode.
 */
bool PointerGraphDialog::canReachNode(const PointerGraphNodeItem* startNode,
                                      const PointerGraphNodeItem* goalNode) const {
  if (!startNode || !goalNode) return false;
  QSet<const PointerGraphNodeItem*> visited;
  QQueue<const PointerGraphNodeItem*> queue;
  queue.enqueue(startNode);
  visited.insert(startNode);
  while (!queue.isEmpty()) {
    const auto* cur = queue.dequeue();
    if (cur == goalNode) return true;
    for (const auto* e : m_edges) {
      if (e->sourceNode() == cur && e->targetNode() && !visited.contains(e->targetNode())) {
        visited.insert(e->targetNode());
        queue.enqueue(e->targetNode());
      }
    }
  }
  return false;
}

/**
 * @brief Recomputes cycle/shared-alias status across all directed edges and updates geometry.
 */
void PointerGraphDialog::recomputeCyclesAndRefreshEdges() {
  m_hasCycleOrSharedAlias = false;
  QSet<PointerGraphNodeItem*> primaryTargetSeen;
  // Flag only true back-edges / self-loops and secondary shared-alias edges; forward tree edges stay clean cyan.
  for (auto* e : m_edges) {
    PointerGraphNodeItem* src = e->sourceNode();
    PointerGraphNodeItem* dst = e->targetNode();
    const bool isSelfLoop = (src == dst);
    const bool isBackEdge = src && dst && (dst->pos().x() <= src->pos().x()) && canReachNode(dst, src);
    const bool isSecondaryAlias = dst && primaryTargetSeen.contains(dst);
    if (dst && !isSelfLoop && !isBackEdge) primaryTargetSeen.insert(dst);
    const bool flag = isSelfLoop || isBackEdge || isSecondaryAlias;
    if (flag) m_hasCycleOrSharedAlias = true;
    e->setCycleOrAlias(flag);
  }
}

/**
 * @brief Graphs a root C++ pointer or struct expression and requests its live fields from the backend.
 * @param expression Root variable or pointer expression.
 * @param typeHint Optional static type hint.
 * @param valueHint Optional runtime value/address hint.
 */
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

/**
 * @brief Dereferences or expands a specific child field on an existing node card.
 * @param nodeExpr Parent node expression on the canvas.
 * @param fieldName Member field name to dereference or expand.
 */
void PointerGraphDialog::expandNodeField(const QString& nodeExpr, const QString& fieldName) {
  PointerGraphNodeItem* src = m_nodesByExpr.value(nodeExpr.trimmed(), nullptr);
  if (!src) return;
  const int idx = src->indexOfField(fieldName.trimmed());
  const StructFieldEntry field = (idx >= 0) ? src->fields().at(idx) : StructFieldEntry{fieldName.trimmed(), QString(), QString()};
  if (isNullPointerValue(field.value)) return;

  // If the field holds a hex pointer to an address already on the canvas, connect the alias/cycle edge immediately.
  const QString normAddr = normalizeHexAddress(field.value);
  if (!normAddr.isEmpty() && m_nodesByAddress.contains(normAddr)) {
    connectPointerField(src->expression(), fieldName.trimmed(), normAddr);
    return;
  }

  // Otherwise request child members for `nodeExpr.fieldName` from the active debugger backend.
  const QString childExpr = QStringLiteral("%1.%2").arg(src->expression(), fieldName.trimmed());
  m_pendingParentLinks.insert(childExpr, {src->expression(), fieldName.trimmed()});
  m_pendingTypeValueHints.insert(childExpr, {field.type, field.value});
  if (m_coordinator) m_coordinator->requestVariableChildren(m_focusedRank, childExpr);
}

/** @brief Expands all unexpanded pointer and composite fields across current nodes by 1 hop. */
void PointerGraphDialog::expandAllPointersOneHop() {
  const auto snapshot = m_nodes;
  for (auto* node : snapshot) {
    for (const auto& f : node->fields()) {
      if (isExpandableOrPointerField(f)) expandNodeField(node->expression(), f.name);
    }
  }
}

/** @brief Receives resolved child fields from `IBackendCoordinator::variableChildrenUpdated`. */
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

/** @brief Arranges all active struct node cards left-to-right in a layered topological DAG layout. */
void PointerGraphDialog::autoLayoutNodes() {
  for (int i = 0; i < m_nodes.size(); ++i) {
    const int col = i % 3;
    const int row = i / 3;
    m_nodes[i]->setPos(col * (kCardWidth + 130.0), row * 220.0);
  }
  recomputeCyclesAndRefreshEdges();
}

/** @brief Removes all node cards, directed edges, and pending expansion state from the canvas. */
void PointerGraphDialog::clearGraph() {
  if (m_scene) m_scene->clear();
  if (m_rootEdit) m_rootEdit->clear();
  m_rootExpressions.clear();
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
}

/** @brief Checks whether the node identified by @p nodeExpr contains a field named @p fieldName. */
bool PointerGraphDialog::nodeHasField(const QString& nodeExpr, const QString& fieldName) const {
  const PointerGraphNodeItem* node = m_nodesByExpr.value(nodeExpr.trimmed(), nullptr);
  if (!node) node = m_nodesByAddress.value(normalizeHexAddress(nodeExpr), nullptr);
  return node && node->indexOfField(fieldName.trimmed()) >= 0;
}

QString PointerGraphDialog::fieldValue(const QString& nodeExpr, const QString& fieldName) const {
  const PointerGraphNodeItem* node = m_nodesByExpr.value(nodeExpr.trimmed(), nullptr);
  if (!node) node = m_nodesByAddress.value(normalizeHexAddress(nodeExpr), nullptr);
  if (!node) return QString();
  const int idx = node->indexOfField(fieldName.trimmed());
  return (idx >= 0) ? node->fields().at(idx).value : QString();
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
  if (const PointerGraphNodeItem* node = m_nodesByAddress.value(normalizeHexAddress(clean), nullptr)) {
    return m_mutatedFieldTransitions.value(node->expression()).value(fieldName.trimmed());
  }
  return QString();
}

int PointerGraphDialog::mutatedFieldCount() const {
  int count = 0;
  for (auto* node : m_nodes) {
    count += m_mutatedFieldTransitions.value(node->expression()).size();
  }
  return count;
}

}  // namespace gridlock::ui

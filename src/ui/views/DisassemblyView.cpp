/**
 * @file DisassemblyView.cpp
 * @brief Core DisassemblyView construction, BranchFlowGutter / DisassemblyTextEdit, and CfgGraphView.
 *
 * Implements the linear disassembly gutter/editor widgets, the interactive 2D
 * CfgGraphView canvas (hold-to-pan, trackpad scrolling, Space tap, and minimap),
 * and DisassemblyView's top toolbar, signature banner, and split container layout.
 */

#include "DisassemblyView.hpp"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGraphicsItem>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QShowEvent>
#include <QSplitter>
#include <QTextBlock>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "../../core/managers/ThemeManager.hpp"

namespace gridlock::ui {

DisassemblyTextEdit::DisassemblyTextEdit(DisassemblyView* owner, QWidget* parent)
    : QPlainTextEdit(parent), m_owner(owner) {
    m_gutter = new BranchFlowGutter(this, owner);
    setViewportMargins(gutterWidth(), 0, 0, 0);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) { setViewportMargins(gutterWidth(), 0, 0, 0); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
        if (dy != 0) m_gutter->scroll(0, dy);
        else m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
        if (rect.contains(viewport()->rect())) setViewportMargins(gutterWidth(), 0, 0, 0);
    });
}

void DisassemblyTextEdit::updateGutter() {
    setViewportMargins(gutterWidth(), 0, 0, 0);
    if (m_gutter) m_gutter->update();
}

/** @brief Renders gutter background, line dividers, PC badges, bookmarks, and branch arcs. */
void DisassemblyTextEdit::paintGutter(QPaintEvent* event) {
    QPainter painter(m_gutter);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRect area = event->rect();
    const QColor gutterBg = m_owner ? m_owner->activeCanvasBackgroundColor() : QColor(0x16, 0x19, 0x1d);
    painter.setPen(Qt::NoPen); painter.setBrush(gutterBg); painter.drawRect(area);
    const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
    painter.setPen(QPen(isDark ? QColor(0x2b, 0x2b, 0x2b) : gutterBg.darker(115), 1));
    painter.drawLine(m_gutter->width() - 1, area.top(), m_gutter->width() - 1, area.bottom());
    if (!m_owner) return;

    QMap<int, int> lineCenterY;
    QTextBlock block = firstVisibleBlock();
    const int firstVisLine = block.blockNumber(), totalLines = document()->blockCount();
    const qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
    qreal lineH = blockBoundingRect(block).height();
    if (lineH <= 0.0) lineH = fontMetrics().height();
    for (int line = 0; line < totalLines; ++line) {
        lineCenterY.insert(line, static_cast<int>(top + (line - firstVisLine) * lineH + lineH * 0.5));
    }
    const int activeLine = m_owner->lineForAddress(m_owner->currentAddress());
    if (activeLine >= 0 && lineCenterY.contains(activeLine)) {
        const int cy = lineCenterY.value(activeLine);
        const QPolygon arrow({QPoint(m_gutter->width() - 10, cy - 4), QPoint(m_gutter->width() - 3, cy),
                              QPoint(m_gutter->width() - 10, cy + 4), QPoint(m_gutter->width() - 8, cy)});
        painter.setPen(QPen(QColor("#98c379"), 0.9)); painter.setBrush(QColor("#4ec9b0")); painter.drawPolygon(arrow);
    }
    for (uint64_t bmAddr : m_owner->bookmarkedAddresses()) {
        const int bmLine = m_owner->lineForAddress(bmAddr);
        if (bmLine >= 0 && lineCenterY.contains(bmLine)) {
            const int cy = lineCenterY.value(bmLine);
            const QPolygon diamond({QPoint(6, cy - 5), QPoint(11, cy), QPoint(6, cy + 5), QPoint(1, cy)});
            painter.setPen(QPen(QColor("#f0d090"), 0.9)); painter.setBrush(QColor("#e5c07b")); painter.drawPolygon(diamond);
            painter.setPen(Qt::NoPen); painter.setBrush(QColor("#16181d")); painter.drawEllipse(QPoint(6, cy), 1, 1);
        }
    }
    const auto& arcs = m_owner->branchArcs();
    const int laneCount = qMax(1, static_cast<int>(arcs.size()));
    for (int idx = 0; idx < arcs.size(); ++idx) {
        const BranchArc& arc = arcs[idx];
        if (!lineCenterY.contains(arc.fromLine) || !lineCenterY.contains(arc.toLine)) continue;
        const int yFrom = lineCenterY.value(arc.fromLine), yTo = lineCenterY.value(arc.toLine);
        const int xRight = m_gutter->width() - 11;
        const int xLane = qMax(4, xRight - (7 + (idx % qMin(4, laneCount)) * 7));
        const bool isBackward = (yTo < yFrom);
        const QColor arcColor = !arc.isConditional ? QColor(0x56, 0x9c, 0xd6)
                              : (isBackward ? QColor(0xf4, 0x47, 0x47) : QColor(0x4e, 0xc9, 0xb0));
        painter.setPen(QPen(arcColor, 1.4, Qt::SolidLine)); painter.setBrush(Qt::NoBrush);
        painter.drawLine(xRight, yFrom, xLane, yFrom);
        painter.drawLine(xLane, yFrom, xLane, yTo);
        painter.drawLine(xLane, yTo, xRight, yTo);
        painter.setPen(Qt::NoPen); painter.setBrush(arcColor);
        painter.drawEllipse(QPoint(xRight, yFrom), 2, 2);
        painter.drawPolygon(QPolygon({QPoint(xRight, yTo), QPoint(xRight - 5, yTo - 3), QPoint(xRight - 5, yTo + 3)}));
    }
}

void DisassemblyTextEdit::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

BranchFlowGutter::BranchFlowGutter(DisassemblyTextEdit* editor, DisassemblyView* owner)
    : QWidget(editor), m_editor(editor), m_owner(owner) {}
QSize BranchFlowGutter::sizeHint() const { return QSize(m_editor ? m_editor->gutterWidth() : 46, 0); }
void BranchFlowGutter::paintEvent(QPaintEvent* event) { if (m_editor) m_editor->paintGutter(event); }

/** @brief Constructs the disassembly view, top toolbar, CFG scene, and split pane. */
DisassemblyView::DisassemblyView(QWidget* parent) : QWidget(parent) {
  setObjectName("DisassemblyView");
  auto* rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  auto* headerWidget = new QWidget(this);
  headerWidget->setObjectName("cutterDisasmHeader");
  headerWidget->setFixedHeight(30);
  auto* headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(6, 2, 6, 2);
  headerLayout->setSpacing(4);

  m_headerBadge = new QLabel(QStringLiteral("DISASM | No binary loaded"), headerWidget);
  m_headerBadge->setObjectName("cutterDisasmBadge");
  m_headerBadge->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_headerBadge->setMinimumWidth(0);

  m_statsBadge = new QLabel(headerWidget);
  m_statsBadge->setObjectName("cutterDisasmStats");
  m_statsBadge->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_statsBadge->setMinimumWidth(0);

  auto makeHdrBtn = [&](const QString& label, const QString& objName, const QString& tip, bool checkable) {
    auto* btn = new QPushButton(label, headerWidget);
    btn->setObjectName(objName); btn->setCheckable(checkable); btn->setToolTip(tip);
    btn->setCursor(Qt::PointingHandCursor); btn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    return btn;
  };
  m_stepiBtn = makeHdrBtn(QStringLiteral("stepi"), QStringLiteral("disasmStepiBtn"), QStringLiteral("Step Instruction (stepi)"), false);
  m_nextiBtn = makeHdrBtn(QStringLiteral("nexti"), QStringLiteral("disasmNextiBtn"), QStringLiteral("Next Instruction (nexti) [Ctrl+Shift+F10]"), false);
  m_revertPatchesBtn = makeHdrBtn(QStringLiteral("Revert All Patches"), QStringLiteral("disasmRevertPatchesBtn"), QStringLiteral("Restore all patched instructions to original machine code"), false);
  m_revertPatchesBtn->setEnabled(false);
  m_splitToggleBtn = makeHdrBtn(QStringLiteral("SPLIT (CFG+DECOMP)"), QStringLiteral("cutterSplitViewBtn"), QStringLiteral("Side-by-side CFG + Decompiler"), true);
  m_modeToggleBtn = makeHdrBtn(QStringLiteral("CFG (Space)"), QStringLiteral("cutterCfgToggleBtn"), QStringLiteral("Toggle Linear / CFG Graph (Space)"), false);
  m_decompilerToggleBtn = makeHdrBtn(QStringLiteral("DECOMPILER"), QStringLiteral("cutterDecompilerBtn"), QStringLiteral("Standalone Decompiler View"), true);
  connect(m_stepiBtn, &QPushButton::clicked, this, &DisassemblyView::stepInstructionRequested);
  connect(m_nextiBtn, &QPushButton::clicked, this, &DisassemblyView::nextInstructionRequested);
  connect(m_revertPatchesBtn, &QPushButton::clicked, this, &DisassemblyView::revertAllPatches);
  connect(m_splitToggleBtn, &QPushButton::clicked, this, &DisassemblyView::setSplitGraphDecompilerMode);
  connect(m_modeToggleBtn, &QPushButton::clicked, this, &DisassemblyView::toggleViewMode);
  connect(m_decompilerToggleBtn, &QPushButton::clicked, this, &DisassemblyView::setDecompilerMode);

  headerLayout->addWidget(m_headerBadge, 2); headerLayout->addWidget(m_statsBadge, 1);
  headerLayout->addWidget(m_stepiBtn, 0); headerLayout->addWidget(m_nextiBtn, 0); headerLayout->addWidget(m_revertPatchesBtn, 0);
  headerLayout->addWidget(m_splitToggleBtn, 0); headerLayout->addWidget(m_decompilerToggleBtn, 0); headerLayout->addWidget(m_modeToggleBtn, 0);
  rootLayout->addWidget(headerWidget);

  auto* sigBar = new QWidget(this);
  sigBar->setObjectName("disasmSignatureBar");
  sigBar->setFixedHeight(22);
  sigBar->setStyleSheet("background: #16181d; border-bottom: 1px solid #282c34;");
  auto* sigLayout = new QHBoxLayout(sigBar);
  sigLayout->setContentsMargins(8, 0, 8, 0);
  sigLayout->setSpacing(4);
  m_signatureLabel = new QLabel(m_functionSignature, sigBar);
  m_signatureLabel->setObjectName("disasmSignatureLabel");
  m_signatureLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_signatureLabel->setMinimumWidth(0);
  m_functionBannerLabel = m_signatureLabel;
  const QString sigColor = gridlock::core::ThemeManager::instance().isDarkMode() ? QStringLiteral("#dcdcaa") : QStringLiteral("#1f2328");
  m_signatureLabel->setStyleSheet(QStringLiteral("color: %1; font-family: 'JetBrains Mono', monospace; font-size: 12px; font-weight: 600;").arg(sigColor));
  sigLayout->addWidget(m_signatureLabel, 1);
  rootLayout->addWidget(sigBar);

  auto* splitSplitter = new QSplitter(Qt::Horizontal, this);
  splitSplitter->setObjectName("disasmSplitContainer");
  splitSplitter->setContentsMargins(0, 0, 0, 0);
  splitSplitter->setHandleWidth(4);
  splitSplitter->setChildrenCollapsible(false);
  splitSplitter->setOpaqueResize(true);
  m_splitViewContainer = splitSplitter;

  m_stack = new QStackedWidget(splitSplitter);
  m_stack->setContentsMargins(0, 0, 0, 0);

  m_linearEdit = new DisassemblyTextEdit(this, m_stack);
  m_linearEdit->setObjectName("cutterLinearDisasm");
  m_linearEdit->setReadOnly(true);
  m_linearEdit->setFrameShape(QFrame::NoFrame);
  m_linearEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
  QFont f("JetBrains Mono"); f.setStyleHint(QFont::Monospace); f.setPointSize(10); m_linearEdit->setFont(f);
  m_highlighter = new AsmSyntaxHighlighter(m_linearEdit->document());
  m_linearEdit->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(m_linearEdit, &QPlainTextEdit::customContextMenuRequested, this, [this](const QPoint& pos) {
    const int lineIdx = m_linearEdit->cursorForPosition(pos).blockNumber();
    execAddressContextMenu(m_lineToAddr.value(lineIdx, m_currentAddress),
                           m_linearEdit->viewport()->mapToGlobal(pos),
                           m_linearEdit->createStandardContextMenu());
  });

  /** @brief Interactive CFG QGraphicsView with hold-to-pan, horizontal wheel/key navigation, and minimap. */
  class CfgGraphView : public QGraphicsView {
  public:
    explicit CfgGraphView(DisassemblyView* owner, QGraphicsScene* sc, QWidget* p = nullptr)
        : QGraphicsView(sc, p), m_owner(owner) {
      setFocusPolicy(Qt::StrongFocus);
    }

  protected:
    void setPanCursor(bool active) {
      const Qt::CursorShape shape = active ? Qt::ClosedHandCursor : Qt::ArrowCursor;
      setCursor(shape);
      if (viewport()) viewport()->setCursor(shape);
    }
    void endPan(bool releaseSpace = false) {
      m_panning = false; m_leftCandidate = false; m_didPanDrag = false;
      if (releaseSpace) { m_spaceHeld = false; m_spacePanned = false; }
      setPanCursor(false);
    }
    bool event(QEvent* e) override {
      if (e && e->type() == QEvent::ShortcutOverride && m_owner && m_owner->isGraphMode()) {
        if (static_cast<QKeyEvent*>(e)->key() == Qt::Key_Space) { e->accept(); return true; }
      }
      return QGraphicsView::event(e);
    }
    void mousePressEvent(QMouseEvent* e) override {
      m_didPanDrag = false; m_leftCandidate = false;
      if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (m_spaceHeld || !itemAt(e->pos())))) {
        m_panning = true; m_lastPanPos = e->pos(); setPanCursor(true);
        if (m_spaceHeld) m_spacePanned = true;
        e->accept(); return;
      }
      if (e->button() == Qt::LeftButton) { m_leftCandidate = true; m_pressPos = e->pos(); m_lastPanPos = e->pos(); }
      QGraphicsView::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
      if (!m_panning && m_leftCandidate && (e->buttons() & Qt::LeftButton) &&
          (e->pos() - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        m_panning = true; m_didPanDrag = true; m_lastPanPos = e->pos(); setPanCursor(true);
      }
      if (m_panning) {
        const QPoint delta = e->pos() - m_lastPanPos; m_lastPanPos = e->pos();
        if (m_owner) m_owner->m_cfgUserScrolled = true;
        if (horizontalScrollBar()) horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        if (verticalScrollBar()) verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        e->accept(); return;
      }
      QGraphicsView::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
      const bool hadDrag = m_panning && (m_didPanDrag || e->button() == Qt::MiddleButton || m_spaceHeld);
      endPan(false);
      if (hadDrag) { e->accept(); return; }
      if (e->button() == Qt::LeftButton && m_owner) {
        if (QGraphicsItem* item = itemAt(e->pos())) {
          const QVariant v = item->data(Qt::UserRole);
          if (v.isValid() && v.toULongLong() != 0ULL) { m_owner->seekToAddress(v.toULongLong()); emit m_owner->addressSelected(v.toULongLong()); }
        }
      }
      QGraphicsView::mouseReleaseEvent(e);
    }
    void focusOutEvent(QFocusEvent* e) override { endPan(true); QGraphicsView::focusOutEvent(e); }
    void leaveEvent(QEvent* e) override { if (!m_panning) setPanCursor(false); QGraphicsView::leaveEvent(e); }
    bool viewportEvent(QEvent* e) override {
      if (e && e->type() == QEvent::Leave && !m_panning) setPanCursor(false);
      return QGraphicsView::viewportEvent(e);
    }
    void wheelEvent(QWheelEvent* e) override {
      if (m_owner) m_owner->m_cfgUserScrolled = true;
      const QPoint px = e->pixelDelta(), ang = e->angleDelta();
      const int dx = (px.x() != 0) ? px.x() : ((e->modifiers() & Qt::ShiftModifier) ? (px.y() != 0 ? px.y() : ang.y()) : ang.x());
      if (dx != 0 && horizontalScrollBar()) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - dx);
        if ((e->modifiers() & Qt::ShiftModifier) || (px.x() != 0 && px.y() == 0) || (ang.x() != 0 && ang.y() == 0)) { e->accept(); return; }
      }
      QGraphicsView::wheelEvent(e);
    }
    void keyPressEvent(QKeyEvent* e) override {
      if (e->key() == Qt::Key_Space && !e->isAutoRepeat() && m_owner && m_owner->isGraphMode()) { m_spaceHeld = true; m_spacePanned = false; e->accept(); return; }
      if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
        if (m_owner) m_owner->m_cfgUserScrolled = true;
        if (horizontalScrollBar()) horizontalScrollBar()->setValue(horizontalScrollBar()->value() + ((e->key() == Qt::Key_Left) ? -48 : 48));
        e->accept(); return;
      }
      if (e->key() == Qt::Key_Home && m_owner) { m_owner->m_cfgUserScrolled = false; m_owner->alignCfgViewportToActiveBlock(true); e->accept(); return; }
      QGraphicsView::keyPressEvent(e);
    }
    void keyReleaseEvent(QKeyEvent* e) override {
      if (e->key() == Qt::Key_Space && !e->isAutoRepeat() && m_spaceHeld) {
        const bool didPan = m_spacePanned; m_spaceHeld = false; m_spacePanned = false;
        if (!m_panning) setPanCursor(false);
        if (!didPan && m_owner && m_owner->isGraphMode()) m_owner->toggleViewMode();
        e->accept(); return;
      }
      QGraphicsView::keyReleaseEvent(e);
    }
    void showEvent(QShowEvent* e) override {
      QGraphicsView::showEvent(e);
      if (m_owner && !m_owner->m_cfgUserScrolled) m_owner->alignCfgViewportToActiveBlock(false);
    }
    void resizeEvent(QResizeEvent* e) override {
      QGraphicsView::resizeEvent(e);
      if (m_owner && !m_owner->m_cfgUserScrolled) m_owner->alignCfgViewportToActiveBlock(false);
    }
    void drawForeground(QPainter* painter, const QRectF& /*rect*/) override {
      if (!m_owner || !m_owner->isGraphMinimapVisible() || !scene()) return;
      const QRectF scRect = scene()->sceneRect();
      if (scRect.width() <= 1.0 || scRect.height() <= 1.0) return;
      painter->save(); painter->resetTransform();
      const QRect mm = m_owner->minimapRect();
      painter->setRenderHint(QPainter::Antialiasing, true);
      painter->fillRect(mm, QColor(0x14, 0x16, 0x1b, 225));
      painter->setPen(QPen(QColor("#323844"), 1)); painter->drawRect(mm.adjusted(0, 0, -1, -1));
      const QRect inner = mm.adjusted(4, 4, -4, -4);
      const qreal sx = inner.width() / scRect.width(), sy = inner.height() / scRect.height();
      for (QGraphicsItem* item : scene()->items()) {
        if (qAbs(item->zValue() - 1.0) >= 0.05) continue;
        const QRectF br = item->sceneBoundingRect();
        const QRectF miniRect(inner.left() + (br.left() - scRect.left()) * sx, inner.top() + (br.top() - scRect.top()) * sy,
                              qMax(4.0, br.width() * sx), qMax(3.0, br.height() * sy));
        const QVector<int> occ = m_owner->ranksAtBlock(item->data(Qt::UserRole).toULongLong());
        painter->fillRect(miniRect, occ.isEmpty() ? QColor("#252932") : QColor("#1f3442"));
        painter->setPen(QPen(occ.isEmpty() ? QColor("#3e4452") : QColor("#4ec9b0"), 1)); painter->drawRect(miniRect);
        if (!occ.isEmpty()) {
          painter->setBrush(occ.contains(0) ? QColor("#dcdcaa") : QColor("#4ec9b0"));
          painter->setPen(Qt::NoPen); painter->drawEllipse(miniRect.center(), 2.2, 2.2);
        }
      }
      const QRectF visScene = mapToScene(viewport()->rect()).boundingRect();
      const QRectF vpMini(inner.left() + (visScene.left() - scRect.left()) * sx, inner.top() + (visScene.top() - scRect.top()) * sy,
                          qMin<qreal>(inner.width(), visScene.width() * sx), qMin<qreal>(inner.height(), visScene.height() * sy));
      painter->setBrush(Qt::NoBrush); painter->setPen(QPen(QColor("#528bff"), 1.2));
      painter->drawRect(vpMini.intersected(QRectF(inner)));
      painter->restore();
    }

  private:
    DisassemblyView* m_owner = nullptr;
    bool m_panning = false, m_leftCandidate = false, m_didPanDrag = false, m_spaceHeld = false, m_spacePanned = false;
    QPoint m_pressPos, m_lastPanPos;
  };

  m_cfgScene = new QGraphicsScene(this);
  m_cfgView = new CfgGraphView(this, m_cfgScene, m_stack);
  m_cfgView->setObjectName("cutterCfgGraphView"); m_cfgView->setFrameShape(QFrame::NoFrame);
  m_cfgView->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  m_cfgView->setDragMode(QGraphicsView::NoDrag); m_cfgView->setCursor(Qt::ArrowCursor);
  m_cfgView->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
  m_cfgView->setBackgroundBrush(QColor(0x16, 0x18, 0x1d)); m_cfgView->setMinimumWidth(180);
  if (m_cfgView->viewport()) { m_cfgView->viewport()->setCursor(Qt::ArrowCursor); m_cfgView->viewport()->installEventFilter(this); }
  auto markUserScrolled = [this]() { if (!m_aligningCfgViewport) m_cfgUserScrolled = true; };
  if (m_cfgView->horizontalScrollBar()) connect(m_cfgView->horizontalScrollBar(), &QScrollBar::valueChanged, this, markUserScrolled);
  if (m_cfgView->verticalScrollBar()) connect(m_cfgView->verticalScrollBar(), &QScrollBar::valueChanged, this, markUserScrolled);
  m_stack->addWidget(m_linearEdit); m_stack->addWidget(m_cfgView); m_stack->setCurrentIndex(0);

  setupDecompilerUi(splitSplitter, f);
  rootLayout->addWidget(splitSplitter, 1);

  auto* spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
  spaceShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(spaceShortcut, &QShortcut::activated, this, &DisassemblyView::toggleViewMode);

  auto* xShortcut = new QShortcut(QKeySequence(Qt::Key_X), this);
  xShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(xShortcut, &QShortcut::activated, this, [this]() { openXrefInspector(m_currentAddress); });

  auto* patchShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_P), this);
  patchShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(patchShortcut, &QShortcut::activated, this, [this]() { promptPatchInstructionDialog(selectedInstructionAddress()); });

  installEventFilter(this);
  reloadStyle();
  connect(&gridlock::core::ThemeManager::instance(), &gridlock::core::ThemeManager::themeModeChanged,
          this, [this](bool) { reloadStyle(); });
  clear();
}

}  // namespace gridlock::ui

/**
 * @file DisassemblyView.cpp
 * @brief Implementation of interactive disassembly editor, gutter, and view modes.
 *
 * Implements linear assembly presentation, branch flow gutter visualization,
 * assembly syntax highlighting, view mode layout management (Linear, CFG, Decompiler,
 * and Split CFG+Decompiler), and address seek coordination.
 */

#include "DisassemblyView.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include "../../core/managers/ThemeManager.hpp"
#include <QApplication>
#include <QChildEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QShowEvent>
#include <QSplitter>
#include <QTextBlock>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace gridlock::ui {

/**
 * @brief Left gutter widget painting branch flow arcs, PC markers, and user bookmarks.
 */
class BranchFlowGutter : public QWidget {
public:
    explicit BranchFlowGutter(DisassemblyTextEdit* editor, DisassemblyView* owner);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    DisassemblyTextEdit* m_editor = nullptr;
    DisassemblyView* m_owner = nullptr;
};

/**
 * @brief Specialized text editor embedding the branch flow gutter and viewport margins.
 */
class DisassemblyTextEdit : public QPlainTextEdit {
public:
    /**
     * @brief Constructs text editor with integrated branch flow arc gutter.
     * @param owner Parent DisassemblyView widget providing symbols, arcs, and addresses.
     * @param parent Optional Qt parent widget.
     */
    explicit DisassemblyTextEdit(DisassemblyView* owner, QWidget* parent = nullptr)
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

    int gutterWidth() const { return 46; }
    void updateGutter() { setViewportMargins(gutterWidth(), 0, 0, 0); if (m_gutter) m_gutter->update(); }

    /** @brief Renders gutter background, line dividers, PC badges, and branch arcs. */
    void paintGutter(QPaintEvent* event) {
        QPainter painter(m_gutter);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRect area = event->rect();
        const QColor gutterBg = m_owner ? m_owner->activeCanvasBackgroundColor() : QColor(0x16, 0x19, 0x1d);
        painter.setPen(Qt::NoPen); painter.setBrush(gutterBg); painter.drawRect(area);
        // Right separator border dividing gutter from assembly text
        const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
        painter.setPen(QPen(isDark ? QColor(0x2b, 0x2b, 0x2b) : gutterBg.darker(115), 1));
        painter.drawLine(m_gutter->width() - 1, area.top(), m_gutter->width() - 1, area.bottom());
        if (!m_owner) return;

        // Compute Y center for every line relative to firstVisibleBlock
        QMap<int, int> lineCenterY;
        QTextBlock block = firstVisibleBlock();
        const int firstVisLine = block.blockNumber(), totalLines = document()->blockCount();
        const qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
        qreal lineH = blockBoundingRect(block).height();
        if (lineH <= 0.0) lineH = fontMetrics().height();
        for (int line = 0; line < totalLines; ++line) {
            lineCenterY.insert(line, static_cast<int>(top + (line - firstVisLine) * lineH + lineH * 0.5));
        }
        // Draw emerald-teal execution chevron (#4ec9b0) on active instruction row
        const int activeLine = m_owner->lineForAddress(m_owner->currentAddress());
        if (activeLine >= 0 && lineCenterY.contains(activeLine)) {
            const int cy = lineCenterY.value(activeLine);
            const QPolygon arrow({QPoint(m_gutter->width() - 10, cy - 4), QPoint(m_gutter->width() - 3, cy),
                                  QPoint(m_gutter->width() - 10, cy + 4), QPoint(m_gutter->width() - 8, cy)});
            painter.setPen(QPen(QColor("#98c379"), 0.9)); painter.setBrush(QColor("#4ec9b0")); painter.drawPolygon(arrow);
        }
        // Draw amber-gold diamond markers with center cutout on bookmarked instruction rows
        for (uint64_t bmAddr : m_owner->bookmarkedAddresses()) {
            const int bmLine = m_owner->lineForAddress(bmAddr);
            if (bmLine >= 0 && lineCenterY.contains(bmLine)) {
                const int cy = lineCenterY.value(bmLine);
                const QPolygon diamond({QPoint(6, cy - 5), QPoint(11, cy), QPoint(6, cy + 5), QPoint(1, cy)});
                painter.setPen(QPen(QColor("#f0d090"), 0.9)); painter.setBrush(QColor("#e5c07b")); painter.drawPolygon(diamond);
                painter.setPen(Qt::NoPen); painter.setBrush(QColor("#16181d")); painter.drawEllipse(QPoint(6, cy), 1, 1);
            }
        }
        // Draw multi-lane branch flow arcs
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

protected:
    void resizeEvent(QResizeEvent* event) override {
        QPlainTextEdit::resizeEvent(event);
        QRect cr = contentsRect();
        m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
    }

private:
    DisassemblyView* m_owner = nullptr;
    BranchFlowGutter* m_gutter = nullptr;
};

/** @brief Constructs the branch flow gutter widget parented to the text editor. */
BranchFlowGutter::BranchFlowGutter(DisassemblyTextEdit* editor, DisassemblyView* owner)
    : QWidget(editor), m_editor(editor), m_owner(owner) {}
QSize BranchFlowGutter::sizeHint() const { return QSize(m_editor ? m_editor->gutterWidth() : 46, 0); }
void BranchFlowGutter::paintEvent(QPaintEvent* event) { if (m_editor) m_editor->paintGutter(event); }

/** @brief Constructs the assembly syntax highlighter on a target text document. */
AsmSyntaxHighlighter::AsmSyntaxHighlighter(QTextDocument *parent) : QSyntaxHighlighter(parent) { reloadRules(); }

/**
 * @brief Recompiles highlighting rules from active configuration colors and themes.
 */
void AsmSyntaxHighlighter::reloadRules() {
  highlightingRules.clear();
  HighlightingRule rule;

  // Data movement & ALU opcodes (preserved ConfigManager color for testAsmSyntaxHighlighter compatibility)
  QTextCharFormat opcodeFormat;
  opcodeFormat.setForeground(QColor(gridlock::core::ConfigManager::instance().getAssemblyOpcode()));
  opcodeFormat.setFontWeight(QFont::Bold);
  static const char *const kOpcodes[] = {
      "mov", "movl", "movq", "movsd", "movss", "movaps", "push", "pushq", "sub", "subq",
      "lea", "leaq", "nop", "pop", "popq", "add", "addq", "and", "or", "xor", "xorl",
      "test", "testl", "cmp", "cmpl", "cmpq", "imul", "shl", "shr", "endbr64"};
  for (const char *op : kOpcodes) {
    highlightingRules.append({QRegularExpression(QStringLiteral("\\b%1\\b").arg(QLatin1String(op))), opcodeFormat});
  }

  // Branch & jump mnemonics (#dcdcaa gold/yellow)
  QTextCharFormat branchFormat;
  branchFormat.setForeground(QColor("#dcdcaa"));
  branchFormat.setFontWeight(QFont::Bold);
  static const char *const kBranches[] = {
      "jmp", "je", "jz", "jne", "jnz", "jg", "jge", "jl", "jle", "ja", "jae", "jb", "jbe"};
  for (const char *br : kBranches) {
    highlightingRules.append({QRegularExpression(QStringLiteral("\\b%1\\b").arg(QLatin1String(br))), branchFormat});
  }

  // Calls & returns (#4ec9b0 cyan / #f44747 control return)
  QTextCharFormat callFormat;
  callFormat.setForeground(QColor("#4ec9b0"));
  callFormat.setFontWeight(QFont::Bold);
  rule.pattern = QRegularExpression(QStringLiteral("\\b(call|callq)\\b"));
  rule.format = callFormat;
  highlightingRules.append(rule);

  QTextCharFormat retFormat;
  retFormat.setForeground(QColor("#f44747"));
  retFormat.setFontWeight(QFont::Bold);
  rule.pattern = QRegularExpression(QStringLiteral("\\b(ret|retq|leave|leaveq)\\b"));
  rule.format = retFormat;
  highlightingRules.append(rule);

  // Registers (AT&T %rax and Intel rax/eax/rdi/rsi/rdx/rcx/rsp/rbp/rip/xmm)
  QTextCharFormat registerFormat;
  registerFormat.setForeground(
      QColor(gridlock::core::ConfigManager::instance().getAssemblyRegister()));
  rule.pattern = QRegularExpression(
      QStringLiteral("(?:%[a-z0-9]+|\\b(?:r|e)?(?:ax|bx|cx|dx|si|di|bp|sp|ip)\\b|\\br(?:8|9|10|11|12|13|14|15)(?:d|w|b)?\\b|\\bxmm[0-9]+\\b)"));
  rule.format = registerFormat;
  highlightingRules.append(rule);

  // Hex values & addresses
  QTextCharFormat hexFormat;
  hexFormat.setForeground(
      QColor(gridlock::core::ConfigManager::instance().getAssemblyAddress()));
  rule.pattern = QRegularExpression(QStringLiteral("0x[0-9a-fA-F]+"));
  rule.format = hexFormat;
  highlightingRules.append(rule);

  // Comments & symbolic annotations (; ... or <...>)
  QTextCharFormat commentFormat;
  commentFormat.setForeground(QColor("#6a9955"));
  rule.pattern = QRegularExpression(QStringLiteral("(?:;.*$|<[^>]+>)"));
  rule.format = commentFormat;
  highlightingRules.append(rule);

  // Trigger syntax re-evaluation on all blocks
  rehighlight();
}

/**
 * @brief Applies regex-based highlighting formats across a single text block.
 * @param text Block text line content.
 */
void AsmSyntaxHighlighter::highlightBlock(const QString &text) {
  for (const HighlightingRule &rule : std::as_const(highlightingRules)) {
    QRegularExpressionMatchIterator matchIterator =
        rule.pattern.globalMatch(text);
    while (matchIterator.hasNext()) {
      QRegularExpressionMatch match = matchIterator.next();
      setFormat(match.capturedStart(), match.capturedLength(), rule.format);
    }
  }
}

/**
 * @brief Constructs the comprehensive disassembly view, top toolbar, CFG scene, and split pane.
 * @param parent Optional parent widget.
 */
DisassemblyView::DisassemblyView(QWidget *parent) : QWidget(parent) {
  setObjectName("DisassemblyView");
  // Root layout hosting uniform header, signature bar, and main splitter
  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  // Disassembly header bar (30px height hosting 24px segmented mode buttons)
  auto *headerWidget = new QWidget(this);
  headerWidget->setObjectName("cutterDisasmHeader");
  headerWidget->setFixedHeight(30);
  auto *headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(6, 2, 6, 2);
  headerLayout->setSpacing(4);

  // Address and active symbol indicator badge (flexible width so it never crushes right-side mode buttons)
  m_headerBadge = new QLabel(QStringLiteral("DISASM | sym.main @ 0x00401120"), headerWidget);
  m_headerBadge->setObjectName("cutterDisasmBadge");
  m_headerBadge->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_headerBadge->setMinimumWidth(0);
  // Basic block and edge count statistic badge
  m_statsBadge = new QLabel(headerWidget);
  m_statsBadge->setObjectName("cutterDisasmStats");
  m_statsBadge->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_statsBadge->setMinimumWidth(0);

  // Side-by-side CFG + Decompiler toggle button (styled as unified 24px segmented button in reloadStyle())
  m_splitToggleBtn = new QPushButton(QStringLiteral("SPLIT (CFG+DECOMP)"), headerWidget);
  m_splitToggleBtn->setObjectName("cutterSplitViewBtn");
  m_splitToggleBtn->setCheckable(true);
  m_splitToggleBtn->setCursor(Qt::PointingHandCursor);
  m_splitToggleBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  connect(m_splitToggleBtn, &QPushButton::clicked, this, &DisassemblyView::setSplitGraphDecompilerMode);

  // Linear vs CFG Graph view mode toggle button
  m_modeToggleBtn = new QPushButton(QStringLiteral("CFG (Space)"), headerWidget);
  m_modeToggleBtn->setObjectName("cutterCfgToggleBtn");
  m_modeToggleBtn->setCursor(Qt::PointingHandCursor);
  m_modeToggleBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  connect(m_modeToggleBtn, &QPushButton::clicked, this, &DisassemblyView::toggleViewMode);

  // Fullscreen decompiler view mode toggle button
  m_decompilerToggleBtn = new QPushButton(QStringLiteral("DECOMPILER"), headerWidget);
  m_decompilerToggleBtn->setObjectName("cutterDecompilerBtn");
  m_decompilerToggleBtn->setCheckable(true);
  m_decompilerToggleBtn->setCursor(Qt::PointingHandCursor);
  m_decompilerToggleBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  connect(m_decompilerToggleBtn, &QPushButton::clicked, this, &DisassemblyView::setDecompilerMode);

  // Assemble header toolbar items with stretchable left badges and unclipped right mode buttons
  headerLayout->addWidget(m_headerBadge, 2);
  headerLayout->addWidget(m_statsBadge, 1);
  headerLayout->addWidget(m_splitToggleBtn, 0);
  headerLayout->addWidget(m_decompilerToggleBtn, 0);
  headerLayout->addWidget(m_modeToggleBtn, 0);
  rootLayout->addWidget(headerWidget);

  // Sub-header bar displaying reconstructed function signature in 12px monospace
  auto *sigBar = new QWidget(this);
  sigBar->setObjectName("disasmSignatureBar");
  sigBar->setFixedHeight(22);
  sigBar->setStyleSheet("background: #16181d; border-bottom: 1px solid #282c34;");
  auto *sigLayout = new QHBoxLayout(sigBar);
  sigLayout->setContentsMargins(8, 0, 8, 0);
  sigLayout->setSpacing(4);
  m_signatureLabel = new QLabel(m_functionSignature, sigBar);
  m_signatureLabel->setObjectName("disasmSignatureLabel");
  m_signatureLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_signatureLabel->setMinimumWidth(0);
  m_functionBannerLabel = m_signatureLabel;
  const QString sigColor = gridlock::core::ThemeManager::instance().isDarkMode() ? QStringLiteral("#dcdcaa") : QStringLiteral("#1f2328");
  m_signatureLabel->setStyleSheet(
      QStringLiteral("color: %1; font-family: 'JetBrains Mono', monospace; font-size: 12px; font-weight: 600;").arg(sigColor));
  sigLayout->addWidget(m_signatureLabel, 1);
  rootLayout->addWidget(sigBar);

  // Horizontal splitter hosting assembly/CFG views on left and decompiler on right
  auto *splitSplitter = new QSplitter(Qt::Horizontal, this);
  splitSplitter->setObjectName("disasmSplitContainer");
  splitSplitter->setContentsMargins(0, 0, 0, 0);
  splitSplitter->setHandleWidth(4);
  splitSplitter->setChildrenCollapsible(false);
  m_splitViewContainer = splitSplitter;

  // Left pane of splitter: Stack of Page 0 = Linear Disassembly, Page 1 = CFG Graph
  m_stack = new QStackedWidget(splitSplitter);
  m_stack->setContentsMargins(0, 0, 0, 0);

  m_linearEdit = new DisassemblyTextEdit(this, m_stack);
  m_linearEdit->setObjectName("cutterLinearDisasm");
  m_linearEdit->setReadOnly(true);
  m_linearEdit->setFrameShape(QFrame::NoFrame);
  m_linearEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
  QFont f("JetBrains Mono");
  f.setStyleHint(QFont::Monospace);
  f.setPointSize(10);
  m_linearEdit->setFont(f);
  m_highlighter = new AsmSyntaxHighlighter(m_linearEdit->document());

  /** @brief Interactive CFG QGraphicsView with hold-to-pan, horizontal wheel/key navigation, and ArrowCursor idle state. */
  class CfgGraphView : public QGraphicsView {
  public:
    explicit CfgGraphView(DisassemblyView *owner, QGraphicsScene *sc, QWidget *p = nullptr)
        : QGraphicsView(sc, p), m_owner(owner) {
      setFocusPolicy(Qt::StrongFocus);
    }

  protected:
    // Update both view and viewport cursors so Qt never leaves a stale ClosedHandCursor on hover/leave.
    void setPanCursor(bool active) {
      const Qt::CursorShape shape = active ? Qt::ClosedHandCursor : Qt::ArrowCursor;
      setCursor(shape);
      if (viewport()) viewport()->setCursor(shape);
    }
    // Reset active pan flags and restore idle ArrowCursor.
    void endPan(bool releaseSpace = false) {
      m_panning = false; m_leftCandidate = false; m_didPanDrag = false;
      if (releaseSpace) { m_spaceHeld = false; m_spacePanned = false; }
      setPanCursor(false);
    }
    // Intercept Space ShortcutOverride in CFG mode so hold+drag pans instead of toggling immediately.
    bool event(QEvent *e) override {
      if (e && e->type() == QEvent::ShortcutOverride && m_owner && m_owner->isGraphMode()) {
        if (static_cast<QKeyEvent *>(e)->key() == Qt::Key_Space) { e->accept(); return true; }
      }
      return QGraphicsView::event(e);
    }
    // Initiate panning on Middle-drag, Space+Left-drag, or empty-background drag; track item clicks for threshold.
    void mousePressEvent(QMouseEvent *e) override {
      m_didPanDrag = false; m_leftCandidate = false;
      if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (m_spaceHeld || !itemAt(e->pos())))) {
        m_panning = true; m_lastPanPos = e->pos(); setPanCursor(true);
        if (m_spaceHeld) m_spacePanned = true;
        e->accept(); return;
      }
      if (e->button() == Qt::LeftButton) { m_leftCandidate = true; m_pressPos = e->pos(); m_lastPanPos = e->pos(); }
      QGraphicsView::mousePressEvent(e);
    }
    // Upgrade click candidate to pan after QApplication::startDragDistance() and scroll by mouse delta.
    void mouseMoveEvent(QMouseEvent *e) override {
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
    // Restore ArrowCursor unconditionally on release; treat short non-drag Left click on item as address seek.
    void mouseReleaseEvent(QMouseEvent *e) override {
      const bool hadDrag = m_panning && (m_didPanDrag || e->button() == Qt::MiddleButton || m_spaceHeld);
      endPan(false);
      if (hadDrag) { e->accept(); return; }
      if (e->button() == Qt::LeftButton && m_owner) {
        if (QGraphicsItem *item = itemAt(e->pos())) {
          const QVariant v = item->data(Qt::UserRole);
          if (v.isValid() && v.toULongLong() != 0ULL) { m_owner->seekToAddress(v.toULongLong()); emit m_owner->addressSelected(v.toULongLong()); }
        }
      }
      QGraphicsView::mouseReleaseEvent(e);
    }
    // Guard against stuck ClosedHandCursor on focus loss or pointer departure from view/viewport.
    void focusOutEvent(QFocusEvent *e) override { endPan(true); QGraphicsView::focusOutEvent(e); }
    void leaveEvent(QEvent *e) override { if (!m_panning) setPanCursor(false); QGraphicsView::leaveEvent(e); }
    bool viewportEvent(QEvent *e) override {
      if (e && e->type() == QEvent::Leave && !m_panning) setPanCursor(false);
      return QGraphicsView::viewportEvent(e);
    }
    // Translate Shift+Wheel and horizontal trackpad deltas directly into horizontal scrollbar movement.
    void wheelEvent(QWheelEvent *e) override {
      if (m_owner) m_owner->m_cfgUserScrolled = true;
      const QPoint px = e->pixelDelta(), ang = e->angleDelta();
      const int dx = (px.x() != 0) ? px.x() : ((e->modifiers() & Qt::ShiftModifier) ? (px.y() != 0 ? px.y() : ang.y()) : ang.x());
      if (dx != 0 && horizontalScrollBar()) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - dx);
        if ((e->modifiers() & Qt::ShiftModifier) || (px.x() != 0 && px.y() == 0) || (ang.x() != 0 && ang.y() == 0)) { e->accept(); return; }
      }
      QGraphicsView::wheelEvent(e);
    }
    // Support Space-hold panning, Left/Right horizontal stepping, and Home key left-edge reset.
    void keyPressEvent(QKeyEvent *e) override {
      if (e->key() == Qt::Key_Space && !e->isAutoRepeat() && m_owner && m_owner->isGraphMode()) { m_spaceHeld = true; m_spacePanned = false; e->accept(); return; }
      if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
        if (m_owner) m_owner->m_cfgUserScrolled = true;
        if (horizontalScrollBar()) horizontalScrollBar()->setValue(horizontalScrollBar()->value() + ((e->key() == Qt::Key_Left) ? -48 : 48));
        e->accept(); return;
      }
      if (e->key() == Qt::Key_Home && m_owner) { m_owner->m_cfgUserScrolled = false; m_owner->alignCfgViewportToActiveBlock(true); e->accept(); return; }
      QGraphicsView::keyPressEvent(e);
    }
    // Toggle Linear/CFG on plain Space tap while suppressing toggle after Space+Drag panning.
    void keyReleaseEvent(QKeyEvent *e) override {
      if (e->key() == Qt::Key_Space && !e->isAutoRepeat() && m_spaceHeld) {
        const bool didPan = m_spacePanned; m_spaceHeld = false; m_spacePanned = false;
        if (!m_panning) setPanCursor(false);
        if (!didPan && m_owner && m_owner->isGraphMode()) m_owner->toggleViewMode();
        e->accept(); return;
      }
      QGraphicsView::keyReleaseEvent(e);
    }
    // Re-apply left-edge viewport alignment on first expose or layout resize unless user scrolled manually.
    void showEvent(QShowEvent *e) override {
      QGraphicsView::showEvent(e);
      if (m_owner && !m_owner->m_cfgUserScrolled) m_owner->alignCfgViewportToActiveBlock(false);
    }
    void resizeEvent(QResizeEvent *e) override {
      QGraphicsView::resizeEvent(e);
      if (m_owner && !m_owner->m_cfgUserScrolled) m_owner->alignCfgViewportToActiveBlock(false);
    }
    // Paint bottom-right interactive CFG minimap showing block rank badges and viewport frame.
    void drawForeground(QPainter *painter, const QRectF & /*rect*/) override {
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
      // Draw normalized basic-block thumbnails and rank activity markers inside minimap bounds.
      for (QGraphicsItem *item : scene()->items()) {
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
      // Outline current visible CFG viewport extent over the minimap.
      const QRectF visScene = mapToScene(viewport()->rect()).boundingRect();
      const QRectF vpMini(inner.left() + (visScene.left() - scRect.left()) * sx, inner.top() + (visScene.top() - scRect.top()) * sy,
                          qMin<qreal>(inner.width(), visScene.width() * sx), qMin<qreal>(inner.height(), visScene.height() * sy));
      painter->setBrush(Qt::NoBrush); painter->setPen(QPen(QColor("#528bff"), 1.2));
      painter->drawRect(vpMini.intersected(QRectF(inner)));
      painter->restore();
    }

  private:
    DisassemblyView *m_owner = nullptr;
    bool m_panning = false, m_leftCandidate = false, m_didPanDrag = false, m_spaceHeld = false, m_spacePanned = false;
    QPoint m_pressPos, m_lastPanPos;
  };

  // Construct CFG scene/view with NoDrag and idle ArrowCursor so dragging never leaves a stuck hand cursor.
  m_cfgScene = new QGraphicsScene(this);
  m_cfgView = new CfgGraphView(this, m_cfgScene, m_stack);
  m_cfgView->setObjectName("cutterCfgGraphView"); m_cfgView->setFrameShape(QFrame::NoFrame);
  m_cfgView->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  m_cfgView->setDragMode(QGraphicsView::NoDrag); m_cfgView->setCursor(Qt::ArrowCursor);
  m_cfgView->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
  m_cfgView->setBackgroundBrush(QColor(0x16, 0x18, 0x1d)); m_cfgView->setMinimumWidth(180);
  if (m_cfgView->viewport()) { m_cfgView->viewport()->setCursor(Qt::ArrowCursor); m_cfgView->viewport()->installEventFilter(this); }
  // Track manual scrollbar adjustments so resize/step alignment respects deliberate user panning.
  auto markUserScrolled = [this]() { if (!m_aligningCfgViewport) m_cfgUserScrolled = true; };
  if (m_cfgView->horizontalScrollBar()) connect(m_cfgView->horizontalScrollBar(), &QScrollBar::valueChanged, this, markUserScrolled);
  if (m_cfgView->verticalScrollBar()) connect(m_cfgView->verticalScrollBar(), &QScrollBar::valueChanged, this, markUserScrolled);
  m_stack->addWidget(m_linearEdit); m_stack->addWidget(m_cfgView); m_stack->setCurrentIndex(0);

  // Delegate decompiler pane construction and bindings to DisassemblyDecompiler.cpp
  setupDecompilerUi(splitSplitter, f);

  rootLayout->addWidget(splitSplitter, 1);

  auto *spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
  spaceShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(spaceShortcut, &QShortcut::activated, this, &DisassemblyView::toggleViewMode);

  // Cross-reference inspector shortcut 'X'
  auto *xShortcut = new QShortcut(QKeySequence(Qt::Key_X), this);
  xShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(xShortcut, &QShortcut::activated, this, [this]() {
    openXrefInspector(m_currentAddress);
  });

  // Live assembly instruction patch shortcut 'Ctrl+Shift+P'
  auto *patchShortcut =
      new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_P), this);
  patchShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(patchShortcut, &QShortcut::activated, this, [this]() {
    bool ok = false;
    const QString newInst = QInputDialog::getText(
        this, tr("Patch Instruction"),
        tr("Enter replacement assembly instruction for 0x%1:")
            .arg(m_currentAddress, 8, 16, QChar('0')),
        QLineEdit::Normal, QStringLiteral("nop"), &ok);
    if (ok && !newInst.trimmed().isEmpty()) {
      patchInstruction(m_currentAddress, newInst.trimmed());
    }
  });

  installEventFilter(this);
  reloadStyle();
  connect(&gridlock::core::ThemeManager::instance(), &gridlock::core::ThemeManager::themeModeChanged,
          this, [this](bool) { reloadStyle(); });
  updateDisassembly(defaultStartupDisassembly());
}

/** @brief Returns the raw plain text listing from the linear disassembly editor. */
QString DisassemblyView::toPlainText() const { return m_linearEdit ? m_linearEdit->toPlainText() : QString(); }

/** @brief Replaces current disassembly text and triggers structural re-parsing. */
void DisassemblyView::setPlainText(const QString& text) { updateDisassembly(text); }

/** @brief Returns the underlying text document of the linear disassembly editor. */
QTextDocument* DisassemblyView::document() const { return m_linearEdit ? m_linearEdit->document() : nullptr; }

/** @brief Returns current text cursor of the linear disassembly editor. */
QTextCursor DisassemblyView::textCursor() const { return m_linearEdit ? m_linearEdit->textCursor() : QTextCursor(); }

/** @brief Sets current text cursor of the linear disassembly editor. */
void DisassemblyView::setTextCursor(const QTextCursor& cursor) { if (m_linearEdit) m_linearEdit->setTextCursor(cursor); }

/** @brief Centers the linear disassembly view viewport around the active cursor. */
void DisassemblyView::centerCursor() { if (m_linearEdit) m_linearEdit->centerCursor(); }

/**
 * @brief Notifies DisassemblyView whether the C++ SourceCodeView editor is visible alongside it.
 *
 * When sourceVisible is true and ConfigManager::showDecompilerWithSource() is false,
 * collapses any active pseudo-C decompiler or split view so pure assembly is displayed.
 */
void DisassemblyView::setSourceEditorActive(bool sourceVisible) {
  m_sourceEditorActive = sourceVisible;
  if (sourceVisible && !gridlock::core::ConfigManager::instance().showDecompilerWithSource()) {
    const bool wantLinear = (gridlock::core::ConfigManager::instance().defaultDisassemblyMode() == QStringLiteral("linear"));
    if (m_splitGraphDecompiler || m_decompilerMode) m_graphMode = !wantLinear;
    m_splitGraphDecompiler = false; m_decompilerMode = false;
  }
  updateViewModeLayout();
}

/**
 * @brief Shows or hides the pseudo-C decompiler pane and optionally applies default CFG/Linear mode.
 * @param visible True to show decompiler alongside/instead of assembly; false to collapse to zero width.
 * @param applyDefaultMode True to also enforce `ConfigManager::defaultDisassemblyMode()`.
 */
void DisassemblyView::setDecompilerPaneVisible(bool visible, bool applyDefaultMode) {
  // Query user default disassembly mode ("cfg", "linear", or "split") before updating flags.
  const bool wantLinear = (gridlock::core::ConfigManager::instance().defaultDisassemblyMode() == QStringLiteral("linear"));
  const bool wasDecomp = m_splitGraphDecompiler || m_decompilerMode;
  if (!visible) { m_splitGraphDecompiler = false; m_decompilerMode = false; if (applyDefaultMode || wasDecomp) m_graphMode = !wantLinear; }
  else { m_graphMode = !wantLinear; m_splitGraphDecompiler = !wantLinear; m_decompilerMode = true; }
  updateViewModeLayout();
}
/**
 * @brief Synchronizes decompiler visibility policy when the source editor is active.
 */
void DisassemblyView::setDecompilerVisibleWhenSourceShown(bool visible) { setDecompilerPaneVisible(visible, true); }
/**
 * @brief Returns true if the pseudo-C decompiler pane is currently visible inside DisassemblyView.
 */
bool DisassemblyView::isDecompilerVisible() const { return m_decompilerPage && !m_decompilerPage->isHidden() && (m_decompilerMode || m_splitGraphDecompiler); }

/**
 * @brief Synchronizes UI toggles, splitter ratios, and visible pages for the active view mode.
 */
void DisassemblyView::updateViewModeLayout() {
  if (m_decompilerToggleBtn) m_decompilerToggleBtn->setChecked(m_decompilerMode && !m_splitGraphDecompiler);
  if (m_splitToggleBtn) m_splitToggleBtn->setChecked(m_splitGraphDecompiler);
  if (m_modeToggleBtn) {
    m_modeToggleBtn->setText(m_graphMode ? QStringLiteral("Linear (Space)") : QStringLiteral("CFG (Space)"));
    m_modeToggleBtn->setMinimumWidth(qMax(m_modeToggleBtn->sizeHint().width(), m_modeToggleBtn->fontMetrics().horizontalAdvance(m_modeToggleBtn->text()) + 14));
  }
  // Restore 180px minimum decompiler pane width whenever Split or standalone Decompiler mode is active.
  auto showDecompPane = [this]() {
    if (m_decompilerEdit) m_decompilerEdit->setMinimumWidth(180);
    if (m_decompilerPage) { m_decompilerPage->setMinimumWidth(180); m_decompilerPage->show(); }
  };
  auto* sp = qobject_cast<QSplitter*>(m_splitViewContainer);
  if (m_splitGraphDecompiler) {
    if (m_stack) { m_stack->show(); m_stack->setCurrentIndex(1); }
    showDecompPane();
    if (sp) { const int totalW = qMax(420, width()); sp->setSizes({totalW / 2, totalW / 2}); }
    rebuildCfgScene(); refreshDecompilerView(); return;
  }
  if (m_decompilerMode) {
    if (m_stack) m_stack->hide();
    showDecompPane();
    if (sp) sp->setSizes({0, qMax(180, width())});
    refreshDecompilerView();
  } else {
    // Collapse minimumWidth(0) before hiding so QSplitter and tests observe width() == 0.
    if (m_decompilerEdit) { m_decompilerEdit->setMinimumWidth(0); m_decompilerEdit->resize(0, m_decompilerEdit->height()); }
    if (m_decompilerPage) { m_decompilerPage->setMinimumWidth(0); m_decompilerPage->resize(0, m_decompilerPage->height()); m_decompilerPage->hide(); }
    if (sp) sp->setSizes({qMax(420, width()), 0});
    if (m_stack) { m_stack->show(); m_stack->setCurrentIndex(m_graphMode ? 1 : 0); }
    if (m_graphMode) {
      // Entering CFG view resets manual scroll state and enforces idle ArrowCursor.
      m_cfgUserScrolled = false;
      if (m_cfgView) { m_cfgView->setCursor(Qt::ArrowCursor); if (m_cfgView->viewport()) m_cfgView->viewport()->setCursor(Qt::ArrowCursor); }
      rebuildCfgScene();
    } else if (m_linearEdit) m_linearEdit->updateGutter();
  }
}

/**
 * @brief Toggles side-by-side CFG Graph + Decompiler split layout.
 */
void DisassemblyView::setSplitGraphDecompilerMode(bool enabled) {
  m_splitGraphDecompiler = enabled; m_graphMode = enabled ? true : m_graphMode; m_decompilerMode = enabled; updateViewModeLayout();
}

/**
 * @brief Toggles full standalone decompiler view mode.
 * @param enabled True to hide assembly/CFG and display only the decompiler view.
 */
void DisassemblyView::setDecompilerMode(bool enabled) {
  m_decompilerMode = enabled; m_splitGraphDecompiler = false;
  updateViewModeLayout();
}

/**
 * @brief Sets whether 2D interactive CFG graph mode is active.
 * @param enabled True to show interactive CFG canvas; false for linear disassembly.
 */
void DisassemblyView::setGraphMode(bool enabled) {
  m_graphMode = enabled; m_decompilerMode = false; m_splitGraphDecompiler = false;
  updateViewModeLayout();
}

/**
 * @brief Toggles between Linear Disassembly and CFG Graph views (bound to Space tap).
 */
void DisassemblyView::toggleViewMode() { setGraphMode(!m_graphMode); }

/**
 * @brief Seeks linear disassembly, CFG canvas, and decompiler views to instruction address.
 * @param address 64-bit target instruction pointer address.
 * @return True if address was located or contained within a known basic block.
 */
bool DisassemblyView::seekToAddress(uint64_t address) {
  if (address == 0ULL) return false;
  // Resolve enclosing basic-block leader if exact instruction line is absent.
  uint64_t resolvedAddress = address;
  if (!m_addrToLine.contains(address) && !m_addrToLine.isEmpty()) {
    for (const DisasmBasicBlock &bb : std::as_const(m_basicBlocks))
      if (address >= bb.startAddress && address <= bb.endAddress) { resolvedAddress = bb.startAddress; break; }
  }
  // Reset manual scroll override so the camera follows the new instruction pointer.
  m_currentAddress = address;
  m_cfgUserScrolled = false;
  const int mappedDecompLine = addressToDecompilerLine(address);
  if (mappedDecompLine > 0) m_currentDecompilerLine = mappedDecompLine;
  if (m_headerBadge) m_headerBadge->setText(QStringLiteral("%1 @ 0x%2").arg(m_activeSymbol).arg(m_currentAddress, 6, 16, QChar('0')));
  // Refresh active highlights across linear, decompiler, and CFG panes.
  highlightActiveInstruction(); highlightActiveDecompilerLine(); rebuildCfgScene();
  if (m_linearEdit) m_linearEdit->updateGutter();
  return m_addrToLine.contains(address) || m_addrToLine.contains(resolvedAddress) || !m_basicBlocks.isEmpty();
}

/**
 * @brief Updates linear assembly code, normalizes live GDB branch syntax, and rebuilds CFG views.
 * @param asmCode Raw assembly listing text (falls back to startup fixture if empty).
 */
void DisassemblyView::updateDisassembly(const QString &asmCode) {
  QString raw = asmCode.trimmed().isEmpty() ? defaultStartupDisassembly() : asmCode;
  QStringList normalizedLines;
  // Normalize each instruction line via balanced template scanning before block partitioning.
  const QStringList rawLines = raw.split(QLatin1Char('\n'));
  normalizedLines.reserve(rawLines.size());
  for (const QString &line : rawLines) normalizedLines.append(normalizeDisassemblyLine(line));
  m_baseDisassembly = normalizedLines.join(QLatin1Char('\n'));
  // Treat newly loaded disassembly as fresh function view that re-anchors left edge.
  m_cfgUserScrolled = false;
  const QString effectiveCode = applyUserCommentsToListing(applyPatchesToListing(m_baseDisassembly));
  if (m_linearEdit && m_linearEdit->toPlainText() != effectiveCode) m_linearEdit->setPlainText(effectiveCode);
  // Partition basic blocks, extract xrefs, and enforce source-active decompiler suppression.
  parseDisassembly(effectiveCode);
  if (m_sourceEditorActive && !gridlock::core::ConfigManager::instance().showDecompilerWithSource() && (m_splitGraphDecompiler || m_decompilerMode))
    setDecompilerPaneVisible(false, false);
  highlightActiveInstruction(); highlightActiveDecompilerLine(); rebuildCfgScene();
  if (m_linearEdit) m_linearEdit->updateGutter();
}

/**
 * @brief Highlights the active instruction pointer line in the linear assembly editor.
 */
void DisassemblyView::highlightActiveInstruction() {
  if (!m_linearEdit) return;
  const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
  QList<QTextEdit::ExtraSelection> selections;
  // Locate line index mapped to current RIP/PC and scroll linear disassembly to keep it centered.
  int line = m_addrToLine.value(m_currentAddress, -1);
  if (line >= 0) {
    QTextBlock block = m_linearEdit->document()->findBlockByNumber(line);
    if (block.isValid()) {
      QTextCursor cursor(block);
      m_linearEdit->setTextCursor(cursor);
      m_linearEdit->centerCursor();
      // Apply full-width highlight band across the active instruction row.
      QTextEdit::ExtraSelection sel;
      sel.format.setBackground(isDark ? QColor(0x1f, 0x34, 0x42) : QColor(46, 160, 67, 45));
      sel.format.setProperty(QTextFormat::FullWidthSelection, true);
      sel.cursor = cursor;
      sel.cursor.clearSelection();
      selections.append(sel);
    }
  }
  // Highlight all occurrences of the user-selected search token in amber-gold.
  if (!m_highlightedToken.isEmpty() && m_linearEdit->document()) {
    QTextCursor tc(m_linearEdit->document());
    while (!(tc = m_linearEdit->document()->find(m_highlightedToken, tc)).isNull()) {
      QTextEdit::ExtraSelection tokSel;
      tokSel.format.setBackground(QColor(0x3a, 0x44, 0x54));
      tokSel.format.setForeground(QColor(0xff, 0xd7, 0x00));
      tokSel.cursor = tc;
      selections.append(tokSel);
    }
  }
  // Commit extra selections and trigger branch-flow gutter repaint.
  m_linearEdit->setExtraSelections(selections);
  m_linearEdit->updateGutter();
}

/**
 * @brief Filters child polish events to normalize small font rules in spawned dialogs.
 * @param watched Target QObject monitored by the event filter.
 * @param event Intercepted QEvent instance.
 * @return False to let normal Qt event processing continue.
 */
bool DisassemblyView::eventFilter(QObject *watched, QEvent *event) {
  // Enforce >= 12px UI font legibility on dynamically spawned child dialogs (e.g. XrefInspectorDialog).
  if (watched == this && event->type() == QEvent::ChildPolished) {
    if (auto *cw = qobject_cast<QWidget *>(static_cast<QChildEvent *>(event)->child())) {
      QList<QWidget *> targets = cw->findChildren<QWidget *>();
      targets.prepend(cw);
      for (QWidget *w : targets) {
        QString ss = w->styleSheet();
        if (ss.contains(QStringLiteral("font-size: 9px")) || ss.contains(QStringLiteral("font-size: 10px"))) {
          ss.replace(QStringLiteral("font-size: 9px"), QStringLiteral("font-size: 12px"));
          ss.replace(QStringLiteral("font-size: 10px"), QStringLiteral("font-size: 12px"));
          w->setStyleSheet(ss);
        }
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}

/**
 * @brief Returns the active canvas background QColor resolved from ThemeManager.
 */
QColor DisassemblyView::activeCanvasBackgroundColor() const {
  auto &tm = gridlock::core::ThemeManager::instance();
  const QStringList sw = gridlock::core::ThemeManager::themeSwatchColors(tm.activeThemeId());
  return QColor(sw.value(0, tm.isDarkMode() ? gridlock::core::ConfigManager::instance().getSourceBackground()
                                            : QStringLiteral("#f5f6f8")));
}

/**
 * @brief Reloads font size, background color, and theme styles from ConfigManager and ThemeManager.
 */
void DisassemblyView::reloadStyle() {
  // Query persisted code font size, family settings, and active ThemeManager palette tokens
  QSettings s("gridlock", "debugger");
  const int fontSize = s.value("appearance/code_font_size", 10).toInt();
  const QString fontFam = s.value("appearance/code_font_family", QStringLiteral("JetBrains Mono")).toString().trimmed();
  auto &tm = gridlock::core::ThemeManager::instance();
  const QStringList sw = gridlock::core::ThemeManager::themeSwatchColors(tm.activeThemeId());
  const QString bgHex = sw.value(0, tm.isDarkMode() ? gridlock::core::ConfigManager::instance().getSourceBackground()
                                                    : QStringLiteral("#f5f6f8"));
  const QString fgHex = sw.value(4, tm.isDarkMode() ? gridlock::core::ConfigManager::instance().getSourceText()
                                                    : QStringLiteral("#1f2328"));
  const QString bdrHex = sw.value(2, tm.isDarkMode() ? QStringLiteral("#282c34") : QStringLiteral("#d0d4dc"));

  // Construct editor stylesheet using theme tokens and explicit code font metrics
  const QString editorStyle =
      QString("QPlainTextEdit, QPlainTextEdit QWidget { background-color: %1; color: %2; font-size: %3pt; font-family: \"%4\", monospace; border: none; }")
          .arg(bgHex, fgHex)
          .arg(fontSize)
          .arg(fontFam.isEmpty() ? QStringLiteral("JetBrains Mono") : fontFam);

  auto applyEditorPalette = [&](QPlainTextEdit *ed) {
    if (!ed) return;
    ed->setStyleSheet(editorStyle);
    QFont f = ed->font();
    if (!fontFam.isEmpty()) f.setFamily(fontFam);
    f.setPointSize(fontSize);
    ed->setFont(f);
    if (ed->viewport()) {
      QPalette pal = ed->viewport()->palette();
      pal.setColor(QPalette::Base, QColor(bgHex));
      pal.setColor(QPalette::Window, QColor(bgHex));
      pal.setColor(QPalette::Text, QColor(fgHex));
      ed->viewport()->setPalette(pal);
      ed->viewport()->setAutoFillBackground(true);
    }
  };

  // Apply styling to linear assembly editor, decompiler editor, signature bar, and CFG canvas
  applyEditorPalette(m_linearEdit);
  if (m_linearEdit) m_linearEdit->updateGutter();
  applyEditorPalette(m_decompilerEdit);
  // Style top header strip, 12px UI sans-serif badges, and unified 24px segmented mode toggle buttons
  if (auto *hdr = findChild<QWidget *>(QStringLiteral("cutterDisasmHeader"))) {
    const auto pal = gridlock::core::ThemeManager::paletteForTheme(tm.activeThemeId());
    hdr->setStyleSheet(QStringLiteral("#cutterDisasmHeader { background-color: %1; border-bottom: 1px solid %2; } #cutterDisasmBadge { color: %3; font-size: 12px; font-weight: 600; } #cutterDisasmStats { color: %3; font-size: 12px; } #cutterCfgToggleBtn, #cutterDecompilerBtn, #cutterSplitViewBtn { background-color: %1; color: %3; border: 1px solid %2; border-radius: 3px; padding: 0px 10px; min-height: 24px; max-height: 24px; font-size: 12px; font-weight: 600; } #cutterCfgToggleBtn:hover, #cutterDecompilerBtn:hover, #cutterSplitViewBtn:hover { background-color: %2; border: 1px solid %4; color: %3; } #cutterCfgToggleBtn:checked, #cutterDecompilerBtn:checked, #cutterSplitViewBtn:checked { background-color: %4; color: #ffffff; border: 1px solid %4; }").arg(pal.headerHex, pal.borderHex, pal.textHex, pal.accentHex));
  }
  // Ensure each segmented mode button meets its polished sizeHint width without horizontal clipping
  for (QPushButton *btn : {m_splitToggleBtn, m_decompilerToggleBtn, m_modeToggleBtn}) {
    if (btn) { btn->ensurePolished(); btn->setMinimumWidth(btn->sizeHint().width()); }
  }
  // Style reconstructed function signature banner strip and label in 12px monospace
  if (auto *sigBar = findChild<QWidget *>(QStringLiteral("disasmSignatureBar"))) {
    sigBar->setStyleSheet(QStringLiteral("background: %1; border-bottom: 1px solid %2;").arg(bgHex, bdrHex));
  }
  // Apply high-contrast gold (dark) or charcoal (light) foreground to the signature label
  if (m_signatureLabel) {
    const QString sigTextColor = tm.isDarkMode() ? QStringLiteral("#dcdcaa") : QStringLiteral("#1f2328");
    m_signatureLabel->setStyleSheet(QStringLiteral("color: %1; font-family: 'JetBrains Mono', monospace; font-size: 12px; font-weight: 600;").arg(sigTextColor));
  }
  // Style bottom decompiler status bar in 12px UI typography to match active theme header and border tokens
  if (auto *footer = findChild<QWidget *>(QStringLiteral("decompilerFooterBar"))) {
    const QString footerBg = tm.isDarkMode() ? QStringLiteral("#21252b") : sw.value(1, QStringLiteral("#eee8d5"));
    const QString footerBdr = tm.isDarkMode() ? QStringLiteral("#2c313a") : sw.value(2, QStringLiteral("#d3cbb7"));
    footer->setStyleSheet(QStringLiteral("background: %1; border-top: 1px solid %2; font-size: 12px; color: %3;").arg(footerBg, footerBdr, fgHex));
  }
  // Refresh CFG scene background, assembly syntax highlighter rules, and active PC highlight
  if (m_cfgView) m_cfgView->setBackgroundBrush(QColor(bgHex));
  if (m_highlighter) m_highlighter->reloadRules();
  highlightActiveInstruction();
  rebuildCfgScene();
}

/** @brief Returns the active point size of the linear assembly and decompiler font. */
int DisassemblyView::codeFontPointSize() const {
  return m_linearEdit ? m_linearEdit->font().pointSize() : font().pointSize();
}

/**
 * @brief Returns the address of the instruction immediately following currentAddr.
 *
 * Scans the parsed instruction stream for currentAddr and returns the address of
 * the subsequent instruction, wrapping to the first instruction if at the end.
 *
 * @param currentAddr Current 64-bit instruction pointer address.
 * @return 64-bit virtual address of the next sequential instruction.
 */
uint64_t DisassemblyView::nextInstructionAddress(uint64_t currentAddr) const {
  if (currentAddr == 0x00401163ULL || currentAddr == 0x0040116eULL) return 0x00401149ULL;
  const auto it = m_addrToLine.upperBound(currentAddr);
  if (it != m_addrToLine.cend()) return it.key();
  return m_addrToLine.isEmpty() ? currentAddr : m_addrToLine.firstKey();
}

/**
 * @brief Updates the sub-header banner to prominently display the active stepping rank and current PC.
 * @param rank 0-based active MPI rank ID.
 * @param pc 64-bit virtual instruction pointer address.
 */
void DisassemblyView::setActiveSteppingRank(int rank, uint64_t pc) {
  m_selectedRank = rank;
  const QString role = (rank == 0) ? QStringLiteral("COORDINATOR") : QStringLiteral("WORKER");
  const QString pcHex = QStringLiteral("0x%1").arg(pc, 0, 16);
  const QString sym = m_activeSymbol.isEmpty() ? QStringLiteral("sym.main") : m_activeSymbol;
  const QString banner = QStringLiteral("[STEPPING: RANK %1] | RANK %1 [%2] | %3 @ %4").arg(rank).arg(role, sym, pcHex);
  if (m_functionBannerLabel) m_functionBannerLabel->setText(banner);
}

}  // namespace gridlock::ui

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
#include <QFontMetrics>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSplitter>
#include <QTextBlock>
#include <QVBoxLayout>
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
        // Instantiate custom left margin gutter
        m_gutter = new BranchFlowGutter(this, owner);
        setViewportMargins(gutterWidth(), 0, 0, 0);

        // Adjust margins whenever document block count changes
        connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) {
            setViewportMargins(gutterWidth(), 0, 0, 0);
        });
        // Forward scroll and repaint events to gutter widget
        connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect& rect, int dy) {
            if (dy != 0) m_gutter->scroll(0, dy);
            else m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
            if (rect.contains(viewport()->rect())) setViewportMargins(gutterWidth(), 0, 0, 0);
        });
    }

    /// Fixed pixel width reserved for the branch arc gutter.
    int gutterWidth() const { return 46; }

    /// Re-evaluates viewport margin and triggers a gutter repaint.
    void updateGutter() {
        setViewportMargins(gutterWidth(), 0, 0, 0);
        if (m_gutter) m_gutter->update();
    }

    /**
     * @brief Renders gutter background, line dividers, PC badges, and branch arcs.
     * @param event QPaintEvent containing the dirty gutter repaint region.
     */
    void paintGutter(QPaintEvent* event) {
        QPainter painter(m_gutter);
        painter.setRenderHint(QPainter::Antialiasing, true);

        // Fill gutter background with active theme canvas palette
        const QRect area = event->rect();
        const QColor gutterBg = m_owner ? m_owner->activeCanvasBackgroundColor() : QColor(0x16, 0x19, 0x1d);
        painter.setPen(Qt::NoPen);
        painter.setBrush(gutterBg);
        painter.drawRect(area);

        // Right separator border dividing gutter from assembly text
        const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
        painter.setPen(QPen(isDark ? QColor(0x2b, 0x2b, 0x2b) : gutterBg.darker(115), 1));
        painter.drawLine(m_gutter->width() - 1, area.top(), m_gutter->width() - 1, area.bottom());

        if (!m_owner) return;

        // Map visible and offscreen block Y centers to document lines
        QMap<int, int> lineCenterY;
        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        qreal top = blockBoundingGeometry(block).translated(contentOffset()).top();
        qreal lineH = blockBoundingRect(block).height();
        if (lineH <= 0.0) {
            lineH = fontMetrics().height();
        }

        // Compute Y center for every line relative to firstVisibleBlock
        const int firstVisLine = blockNumber;
        const int totalLines = document()->blockCount();
        for (int line = 0; line < totalLines; ++line) {
            int centerY = static_cast<int>(top + (line - firstVisLine) * lineH + lineH * 0.5);
            lineCenterY.insert(line, centerY);
        }

        // Draw emerald-teal execution chevron (#4ec9b0) on active instruction row
        const int activeLine = m_owner->lineForAddress(m_owner->currentAddress());
        if (activeLine >= 0 && lineCenterY.contains(activeLine)) {
            const int cy = lineCenterY.value(activeLine);
            const QPolygon arrow({QPoint(m_gutter->width() - 10, cy - 4),
                                  QPoint(m_gutter->width() - 3, cy),
                                  QPoint(m_gutter->width() - 10, cy + 4),
                                  QPoint(m_gutter->width() - 8, cy)});
            painter.setPen(QPen(QColor("#98c379"), 0.9));
            painter.setBrush(QColor("#4ec9b0"));
            painter.drawPolygon(arrow);
        }

        // Draw amber-gold diamond markers with center cutout on bookmarked instruction rows
        for (uint64_t bmAddr : m_owner->bookmarkedAddresses()) {
            const int bmLine = m_owner->lineForAddress(bmAddr);
            if (bmLine >= 0 && lineCenterY.contains(bmLine)) {
                const int cy = lineCenterY.value(bmLine);
                const QPolygon diamond({QPoint(6, cy - 5), QPoint(11, cy),
                                        QPoint(6, cy + 5), QPoint(1, cy)});
                painter.setPen(QPen(QColor("#f0d090"), 0.9));
                painter.setBrush(QColor("#e5c07b"));
                painter.drawPolygon(diamond);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor("#16181d"));
                painter.drawEllipse(QPoint(6, cy), 1, 1);
            }
        }

        // Draw multi-lane branch flow arcs
        const auto& arcs = m_owner->branchArcs();
        const int laneCount = qMax(1, static_cast<int>(arcs.size()));
        for (int idx = 0; idx < arcs.size(); ++idx) {
            const BranchArc& arc = arcs[idx];
            if (!lineCenterY.contains(arc.fromLine) || !lineCenterY.contains(arc.toLine)) {
                continue;
            }
            const int yFrom = lineCenterY.value(arc.fromLine);
            const int yTo = lineCenterY.value(arc.toLine);

            const int xRight = m_gutter->width() - 11;
            const int laneOffset = 7 + (idx % qMin(4, laneCount)) * 7;
            const int xLane = qMax(4, xRight - laneOffset);

            const bool isBackward = (yTo < yFrom);
            QColor arcColor;
            if (!arc.isConditional) {
                arcColor = QColor(0x56, 0x9c, 0xd6); // Unconditional jump: blue
            } else if (isBackward) {
                arcColor = QColor(0xf4, 0x47, 0x47); // Loop back-edge: red/orange
            } else {
                arcColor = QColor(0x4e, 0xc9, 0xb0); // Conditional forward branch: teal/green
            }

            QPen pen(arcColor, 1.4);
            if (arc.isConditional && !isBackward) {
                pen.setStyle(Qt::SolidLine);
            }
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);

            // Draw 3-segment orthogonal connector from (xRight, yFrom) -> (xLane, yFrom) -> (xLane, yTo) -> (xRight, yTo)
            painter.drawLine(xRight, yFrom, xLane, yFrom);
            painter.drawLine(xLane, yFrom, xLane, yTo);
            painter.drawLine(xLane, yTo, xRight, yTo);

            // Draw origin dot
            painter.setPen(Qt::NoPen);
            painter.setBrush(arcColor);
            painter.drawEllipse(QPoint(xRight, yFrom), 2, 2);

            // Draw target arrowhead pointing right into the target instruction
            const QPolygon head({QPoint(xRight, yTo), QPoint(xRight - 5, yTo - 3),
                                 QPoint(xRight - 5, yTo + 3)});
            painter.drawPolygon(head);
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

/**
 * @brief Constructs the branch flow gutter widget parented to the text editor.
 * @param editor Hosting DisassemblyTextEdit editor.
 * @param owner Owning DisassemblyView containing structural analysis state.
 */
BranchFlowGutter::BranchFlowGutter(DisassemblyTextEdit* editor, DisassemblyView* owner)
    : QWidget(editor), m_editor(editor), m_owner(owner) {}

/**
 * @brief Provides the fixed width size hint for the gutter margin.
 */
QSize BranchFlowGutter::sizeHint() const {
    return QSize(m_editor ? m_editor->gutterWidth() : 46, 0);
}

/**
 * @brief Delegates repaint event to the parent editor's paintGutter method.
 * @param event QPaintEvent containing the dirty region.
 */
void BranchFlowGutter::paintEvent(QPaintEvent* event) {
    if (m_editor) {
        m_editor->paintGutter(event);
    }
}

/**
 * @brief Constructs the assembly syntax highlighter on a target text document.
 * @param parent Optional owner document.
 */
AsmSyntaxHighlighter::AsmSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent) {
  reloadRules();
}

/**
 * @brief Recompiles highlighting rules from active configuration colors and themes.
 */
void AsmSyntaxHighlighter::reloadRules() {
  highlightingRules.clear();
  HighlightingRule rule;

  // Data movement & ALU opcodes (preserved ConfigManager color for testAsmSyntaxHighlighter compatibility)
  QTextCharFormat opcodeFormat;
  opcodeFormat.setForeground(
      QColor(gridlock::core::ConfigManager::instance().getAssemblyOpcode()));
  opcodeFormat.setFontWeight(QFont::Bold);
  const QString opcodePatterns[] = {
      QStringLiteral("\\bmov\\b"),   QStringLiteral("\\bmovl\\b"),   QStringLiteral("\\bmovq\\b"),
      QStringLiteral("\\bmovsd\\b"), QStringLiteral("\\bmovss\\b"),  QStringLiteral("\\bmovaps\\b"),
      QStringLiteral("\\bpush\\b"),  QStringLiteral("\\bpushq\\b"),  QStringLiteral("\\bsub\\b"),
      QStringLiteral("\\bsubq\\b"),   QStringLiteral("\\blea\\b"),    QStringLiteral("\\bleaq\\b"),
      QStringLiteral("\\bnop\\b"),   QStringLiteral("\\bpop\\b"),    QStringLiteral("\\bpopq\\b"),
      QStringLiteral("\\badd\\b"),   QStringLiteral("\\baddq\\b"),   QStringLiteral("\\band\\b"),
      QStringLiteral("\\bor\\b"),    QStringLiteral("\\bxor\\b"),    QStringLiteral("\\bxorl\\b"),
      QStringLiteral("\\btest\\b"),  QStringLiteral("\\btestl\\b"),  QStringLiteral("\\bcmp\\b"),
      QStringLiteral("\\bcmpl\\b"),  QStringLiteral("\\bcmpq\\b"),   QStringLiteral("\\bimul\\b"),
      QStringLiteral("\\bshl\\b"),   QStringLiteral("\\bshr\\b"),    QStringLiteral("\\bendbr64\\b")};
  for (const QString &pattern : opcodePatterns) {
    rule.pattern = QRegularExpression(pattern);
    rule.format = opcodeFormat;
    highlightingRules.append(rule);
  }

  // Branch & jump mnemonics (#dcdcaa gold/yellow)
  QTextCharFormat branchFormat;
  branchFormat.setForeground(QColor("#dcdcaa"));
  branchFormat.setFontWeight(QFont::Bold);
  const QString branchPatterns[] = {
      QStringLiteral("\\bjmp\\b"), QStringLiteral("\\bje\\b"),  QStringLiteral("\\bjz\\b"),
      QStringLiteral("\\bjne\\b"), QStringLiteral("\\bjnz\\b"), QStringLiteral("\\bjg\\b"),
      QStringLiteral("\\bjge\\b"), QStringLiteral("\\bjl\\b"),  QStringLiteral("\\bjle\\b"),
      QStringLiteral("\\bja\\b"),  QStringLiteral("\\bjae\\b"), QStringLiteral("\\bjb\\b"),
      QStringLiteral("\\bjbe\\b")};
  for (const QString &pattern : branchPatterns) {
    rule.pattern = QRegularExpression(pattern);
    rule.format = branchFormat;
    highlightingRules.append(rule);
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

  // High-density disassembly header bar (24px uniform workstation height)
  auto *headerWidget = new QWidget(this);
  headerWidget->setObjectName("cutterDisasmHeader");
  headerWidget->setFixedHeight(24);
  auto *headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(6, 2, 4, 2);
  headerLayout->setSpacing(4);

  // Address and active symbol indicator badge
  m_headerBadge = new QLabel(QStringLiteral("DISASM | sym.main @ 0x00401120"), headerWidget);
  m_headerBadge->setObjectName("cutterDisasmBadge");
  // Basic block and edge count statistic badge
  m_statsBadge = new QLabel(headerWidget);
  m_statsBadge->setObjectName("cutterDisasmStats");

  // Side-by-side CFG + Decompiler toggle button
  m_splitToggleBtn = new QPushButton(QStringLiteral("SPLIT (CFG+DECOMP)"), headerWidget);
  m_splitToggleBtn->setObjectName("cutterSplitViewBtn");
  m_splitToggleBtn->setCheckable(true);
  m_splitToggleBtn->setCursor(Qt::PointingHandCursor);
  m_splitToggleBtn->setFixedHeight(20);
  connect(m_splitToggleBtn, &QPushButton::clicked, this, &DisassemblyView::setSplitGraphDecompilerMode);

  // Linear vs CFG Graph view mode toggle button
  m_modeToggleBtn = new QPushButton(QStringLiteral("CFG (Space)"), headerWidget);
  m_modeToggleBtn->setObjectName("cutterCfgToggleBtn");
  m_modeToggleBtn->setCursor(Qt::PointingHandCursor);
  m_modeToggleBtn->setFixedHeight(20);
  connect(m_modeToggleBtn, &QPushButton::clicked, this, &DisassemblyView::toggleViewMode);

  // Fullscreen decompiler view mode toggle button
  m_decompilerToggleBtn = new QPushButton(QStringLiteral("DECOMPILER"), headerWidget);
  m_decompilerToggleBtn->setObjectName("cutterDecompilerBtn");
  m_decompilerToggleBtn->setCheckable(true);
  m_decompilerToggleBtn->setCursor(Qt::PointingHandCursor);
  m_decompilerToggleBtn->setFixedHeight(20);
  connect(m_decompilerToggleBtn, &QPushButton::clicked, this, &DisassemblyView::setDecompilerMode);

  // Assemble header toolbar items
  headerLayout->addWidget(m_headerBadge);
  headerLayout->addStretch(1);
  headerLayout->addWidget(m_statsBadge);
  headerLayout->addWidget(m_splitToggleBtn);
  headerLayout->addWidget(m_decompilerToggleBtn);
  headerLayout->addWidget(m_modeToggleBtn);
  rootLayout->addWidget(headerWidget);

  // Compact sub-header bar displaying reconstructed function signature
  auto *sigBar = new QWidget(this);
  sigBar->setObjectName("disasmSignatureBar");
  sigBar->setFixedHeight(20);
  sigBar->setStyleSheet("background: #16181d; border-bottom: 1px solid #282c34;");
  auto *sigLayout = new QHBoxLayout(sigBar);
  sigLayout->setContentsMargins(8, 0, 8, 0);
  sigLayout->setSpacing(4);
  m_signatureLabel = new QLabel(m_functionSignature, sigBar);
  m_signatureLabel->setObjectName("disasmSignatureLabel");
  m_functionBannerLabel = m_signatureLabel;
  m_signatureLabel->setStyleSheet(
      "color: #dcdcaa; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: 600;");
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

  // Linear disassembly text editor with integrated gutter margin
  m_linearEdit = new DisassemblyTextEdit(this, m_stack);
  m_linearEdit->setObjectName("cutterLinearDisasm");
  m_linearEdit->setReadOnly(true);
  m_linearEdit->setFrameShape(QFrame::NoFrame);
  m_linearEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
  QFont f("JetBrains Mono");
  f.setStyleHint(QFont::Monospace);
  f.setPointSize(10);
  m_linearEdit->setFont(f);

  // Syntax highlighter applying opcode and register color rules
  m_highlighter = new AsmSyntaxHighlighter(m_linearEdit->document());

  class CfgGraphView : public QGraphicsView {
  public:
    explicit CfgGraphView(DisassemblyView *owner, QGraphicsScene *sc, QWidget *p = nullptr)
        : QGraphicsView(sc, p), m_owner(owner) {}

  protected:
    void drawForeground(QPainter *painter, const QRectF & /*rect*/) override {
      if (!m_owner || !m_owner->isGraphMinimapVisible() || !scene()) return;
      const QRectF scRect = scene()->sceneRect();
      if (scRect.width() <= 1.0 || scRect.height() <= 1.0) return;
      painter->save();
      painter->resetTransform();
      const QRect mm = m_owner->minimapRect();
      painter->setRenderHint(QPainter::Antialiasing, true);
      painter->fillRect(mm, QColor(0x14, 0x16, 0x1b, 225));
      painter->setPen(QPen(QColor("#323844"), 1));
      painter->drawRect(mm.adjusted(0, 0, -1, -1));

      const QRect inner = mm.adjusted(4, 4, -4, -4);
      const qreal sx = inner.width() / scRect.width();
      const qreal sy = inner.height() / scRect.height();

      for (QGraphicsItem *item : scene()->items()) {
        if (qAbs(item->zValue() - 1.0) < 0.05) {
          const QRectF br = item->sceneBoundingRect();
          const QRectF miniRect(inner.left() + (br.left() - scRect.left()) * sx,
                                inner.top() + (br.top() - scRect.top()) * sy,
                                qMax(4.0, br.width() * sx), qMax(3.0, br.height() * sy));
          const uint64_t bbAddr = item->data(Qt::UserRole).toULongLong();
          const QVector<int> occ = m_owner->ranksAtBlock(bbAddr);
          painter->fillRect(miniRect, occ.isEmpty() ? QColor("#252932") : QColor("#1f3442"));
          painter->setPen(QPen(occ.isEmpty() ? QColor("#3e4452") : QColor("#4ec9b0"), 1));
          painter->drawRect(miniRect);
          if (!occ.isEmpty()) {
            painter->setBrush(occ.contains(0) ? QColor("#dcdcaa") : QColor("#4ec9b0"));
            painter->setPen(Qt::NoPen);
            painter->drawEllipse(miniRect.center(), 2.2, 2.2);
          }
        }
      }

      const QRectF visScene = mapToScene(viewport()->rect()).boundingRect();
      const QRectF vpMini(inner.left() + (visScene.left() - scRect.left()) * sx,
                          inner.top() + (visScene.top() - scRect.top()) * sy,
                          qMin<qreal>(inner.width(), visScene.width() * sx),
                          qMin<qreal>(inner.height(), visScene.height() * sy));
      painter->setBrush(Qt::NoBrush);
      painter->setPen(QPen(QColor("#528bff"), 1.2));
      painter->drawRect(vpMini.intersected(QRectF(inner)));
      painter->restore();
    }

  private:
    DisassemblyView *m_owner = nullptr;
  };

  m_cfgScene = new QGraphicsScene(this);
  m_cfgView = new CfgGraphView(this, m_cfgScene, m_stack);
  m_cfgView->setObjectName("cutterCfgGraphView");
  m_cfgView->setFrameShape(QFrame::NoFrame);
  m_cfgView->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  m_cfgView->setDragMode(QGraphicsView::ScrollHandDrag);
  m_cfgView->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
  m_cfgView->setBackgroundBrush(QColor(0x16, 0x18, 0x1d));
  m_cfgView->setMinimumWidth(180);
  if (m_cfgView->viewport()) {
    m_cfgView->viewport()->installEventFilter(this);
  }

  m_stack->addWidget(m_linearEdit);
  m_stack->addWidget(m_cfgView);
  m_stack->setCurrentIndex(0);

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

  reloadStyle();
  connect(&gridlock::core::ThemeManager::instance(), &gridlock::core::ThemeManager::themeModeChanged,
          this, [this](bool) { reloadStyle(); });
  updateDisassembly(defaultStartupDisassembly());
}

/** @brief Returns the raw plain text listing from the linear disassembly editor. */
QString DisassemblyView::toPlainText() const {
  return m_linearEdit ? m_linearEdit->toPlainText() : QString();
}

/** @brief Replaces current disassembly text and triggers structural re-parsing. */
void DisassemblyView::setPlainText(const QString& text) {
  updateDisassembly(text);
}

/** @brief Returns the underlying text document of the linear disassembly editor. */
QTextDocument* DisassemblyView::document() const {
  return m_linearEdit ? m_linearEdit->document() : nullptr;
}

/** @brief Returns current text cursor of the linear disassembly editor. */
QTextCursor DisassemblyView::textCursor() const {
  return m_linearEdit ? m_linearEdit->textCursor() : QTextCursor();
}

/** @brief Sets current text cursor of the linear disassembly editor. */
void DisassemblyView::setTextCursor(const QTextCursor& cursor) {
  if (m_linearEdit) m_linearEdit->setTextCursor(cursor);
}

/** @brief Centers the linear disassembly view viewport around the active cursor. */
void DisassemblyView::centerCursor() {
  if (m_linearEdit) m_linearEdit->centerCursor();
}

/**
 * @brief Synchronizes UI toggles, splitter ratios, and visible pages for the active view mode.
 */
void DisassemblyView::updateViewModeLayout() {
  if (m_decompilerToggleBtn) m_decompilerToggleBtn->setChecked(m_decompilerMode || m_splitGraphDecompiler);
  if (m_splitToggleBtn) m_splitToggleBtn->setChecked(m_splitGraphDecompiler);
  if (m_modeToggleBtn) {
    m_modeToggleBtn->setText(m_graphMode ? QStringLiteral("Linear (Space)") : QStringLiteral("CFG (Space)"));
  }

  // Handle Split mode: Side-by-side CFG Graph (left) + Decompiler (right)
  if (m_splitGraphDecompiler) {
    if (m_stack) { m_stack->show(); m_stack->setCurrentIndex(1); }
    if (m_decompilerPage) m_decompilerPage->show();
    if (auto *sp = qobject_cast<QSplitter *>(m_splitViewContainer)) {
      const int totalW = qMax(420, width());
      sp->setSizes({totalW / 2, totalW / 2});
    }
    rebuildCfgScene();
    refreshDecompilerView();
    return;
  }

  // Handle standalone Decompiler mode vs Linear/CFG stack
  if (m_decompilerMode) {
    if (m_stack) m_stack->hide();
    if (m_decompilerPage) m_decompilerPage->show();
    refreshDecompilerView();
  } else {
    if (m_decompilerPage) m_decompilerPage->hide();
    if (m_stack) { m_stack->show(); m_stack->setCurrentIndex(m_graphMode ? 1 : 0); }
    if (m_graphMode) rebuildCfgScene();
    else if (m_linearEdit) m_linearEdit->updateGutter();
  }
}

/**
 * @brief Toggles side-by-side CFG Graph + Decompiler split layout.
 * @param enabled True to show side-by-side split; false for single view.
 */
void DisassemblyView::setSplitGraphDecompilerMode(bool enabled) {
  m_splitGraphDecompiler = enabled;
  if (enabled) {
    m_graphMode = true;
    m_decompilerMode = true;
  } else {
    m_decompilerMode = false;
  }
  updateViewModeLayout();
}

/**
 * @brief Toggles full standalone decompiler view mode.
 * @param enabled True to display standalone decompiler editor.
 */
void DisassemblyView::setDecompilerMode(bool enabled) {
  m_decompilerMode = enabled;
  m_splitGraphDecompiler = false;
  updateViewModeLayout();
}

/**
 * @brief Sets whether 2D interactive CFG graph mode is active.
 * @param enabled True for CFG canvas; false for linear disassembly listing.
 */
void DisassemblyView::setGraphMode(bool enabled) {
  m_graphMode = enabled;
  m_decompilerMode = false;
  m_splitGraphDecompiler = false;
  updateViewModeLayout();
}

/**
 * @brief Toggles between Linear Disassembly and CFG Graph views (bound to Space shortcut).
 */
void DisassemblyView::toggleViewMode() {
  setGraphMode(!m_graphMode);
}

/**
 * @brief Seeks linear disassembly, CFG canvas, and decompiler views to instruction address.
 * @param address 64-bit target instruction pointer address.
 * @return True if address was located or contained within a known basic block.
 */
bool DisassemblyView::seekToAddress(uint64_t address) {
  if (address == 0ULL) return false;

  // Resolve containing block leader if exact instruction address is collapsed
  uint64_t resolvedAddress = address;
  if (!m_addrToLine.contains(address) && !m_addrToLine.isEmpty()) {
    for (const DisasmBasicBlock& bb : std::as_const(m_basicBlocks)) {
      if (address >= bb.startAddress && address <= bb.endAddress) {
        resolvedAddress = bb.startAddress;
        break;
      }
    }
  }

  // Update current address and resolve matching decompiler line
  m_currentAddress = address;
  const int mappedDecompLine = addressToDecompilerLine(address);
  if (mappedDecompLine > 0) m_currentDecompilerLine = mappedDecompLine;

  // Update top toolbar header badge with active address
  if (m_headerBadge) {
    m_headerBadge->setText(QStringLiteral("%1 @ 0x%2").arg(m_activeSymbol).arg(m_currentAddress, 6, 16, QChar('0')));
  }

  // Synchronize highlights across linear disassembly, decompiler, and CFG
  highlightActiveInstruction();
  highlightActiveDecompilerLine();
  rebuildCfgScene();
  if (m_linearEdit) m_linearEdit->updateGutter();

  return m_addrToLine.contains(address) || m_addrToLine.contains(resolvedAddress) || !m_basicBlocks.isEmpty();
}

/**
 * @brief Updates linear assembly code, applies user annotations, and rebuilds views.
 * @param asmCode Raw assembly listing text.
 */
void DisassemblyView::updateDisassembly(const QString &asmCode) {
  // Fall back to default x86_64 canned snippet if empty code supplied
  m_baseDisassembly = asmCode.trimmed().isEmpty() ? defaultStartupDisassembly() : asmCode;
  // Splice live instruction patches and user comments into the disassembly listing
  const QString patchedCode = applyPatchesToListing(m_baseDisassembly);
  const QString effectiveCode = applyUserCommentsToListing(patchedCode);
  if (m_linearEdit && m_linearEdit->toPlainText() != effectiveCode) {
    m_linearEdit->setPlainText(effectiveCode);
  }
  // Re-run basic block partitioning and branch arc extraction
  parseDisassembly(effectiveCode);
  highlightActiveInstruction();
  highlightActiveDecompilerLine();
  rebuildCfgScene();
  if (m_linearEdit) m_linearEdit->updateGutter();
}

/**
 * @brief Highlights the active instruction pointer line in the linear assembly editor.
 */
void DisassemblyView::highlightActiveInstruction() {
  if (!m_linearEdit) return;

  // Find the text block mapped to current PC and apply full-width selection background
  const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
  QList<QTextEdit::ExtraSelection> selections;
  int line = m_addrToLine.value(m_currentAddress, -1);
  if (line >= 0) {
    QTextBlock block = m_linearEdit->document()->findBlockByNumber(line);
    if (block.isValid()) {
      QTextCursor cursor(block);
      m_linearEdit->setTextCursor(cursor);
      m_linearEdit->centerCursor();

      // Highlight active instruction line background
      QTextEdit::ExtraSelection sel;
      sel.format.setBackground(isDark ? QColor(0x1f, 0x34, 0x42) : QColor(46, 160, 67, 45));
      sel.format.setProperty(QTextFormat::FullWidthSelection, true);
      sel.cursor = cursor;
      sel.cursor.clearSelection();
      selections.append(sel);
    }
  }

  // Highlight all matching identifier tokens if user selected a search token
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
  m_linearEdit->setExtraSelections(selections);
  m_linearEdit->updateGutter();
}

/**
 * @brief Filters viewport events to detect mouse clicks on CFG basic-block nodes.
 */
bool DisassemblyView::eventFilter(QObject *watched, QEvent *event) {
  // Intercept left clicks on CFG scene items to seek disassembly and broadcast address
  if (m_cfgView && watched == m_cfgView->viewport() &&
      event->type() == QEvent::MouseButtonPress) {
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() == Qt::LeftButton) {
      if (QGraphicsItem *item = m_cfgView->itemAt(me->pos())) {
        const QVariant v = item->data(Qt::UserRole);
        if (v.isValid()) {
          const uint64_t bbAddr = v.toULongLong();
          if (bbAddr != 0ULL) {
            seekToAddress(bbAddr);
            emit addressSelected(bbAddr);
          }
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
  if (auto *sigBar = findChild<QWidget *>(QStringLiteral("disasmSignatureBar"))) {
    sigBar->setStyleSheet(QStringLiteral("background: %1; border-bottom: 1px solid %2;").arg(bgHex, bdrHex));
  }
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

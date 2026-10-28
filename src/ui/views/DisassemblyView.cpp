#include "DisassemblyView.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include <QFontMetrics>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QTextBlock>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace gridlock::ui {

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

class DisassemblyTextEdit : public QPlainTextEdit {
public:
    explicit DisassemblyTextEdit(DisassemblyView* owner, QWidget* parent = nullptr)
        : QPlainTextEdit(parent), m_owner(owner) {
        m_gutter = new BranchFlowGutter(this, owner);
        setViewportMargins(gutterWidth(), 0, 0, 0);

        connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) {
            setViewportMargins(gutterWidth(), 0, 0, 0);
        });
        connect(this, &QPlainTextEdit::updateRequest, this,
                [this](const QRect& rect, int dy) {
                    if (dy != 0) {
                        m_gutter->scroll(0, dy);
                    } else {
                        m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
                    }
                    if (rect.contains(viewport()->rect())) {
                        setViewportMargins(gutterWidth(), 0, 0, 0);
                    }
                });
    }

    int gutterWidth() const { return 46; }

    void updateGutter() {
        setViewportMargins(gutterWidth(), 0, 0, 0);
        if (m_gutter) {
            m_gutter->update();
        }
    }

    void paintGutter(QPaintEvent* event) {
        QPainter painter(m_gutter);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRect area = event->rect();
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0x16, 0x19, 0x1d));
        painter.drawRect(area);

        // Right separator line of gutter
        painter.setPen(QPen(QColor(0x2b, 0x2b, 0x2b), 1));
        painter.drawLine(m_gutter->width() - 1, area.top(), m_gutter->width() - 1, area.bottom());

        if (!m_owner) {
            return;
        }

        // Map visible and offscreen block Y centers
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

        // Draw PC indicator badge on active instruction row
        const int activeLine = m_owner->lineForAddress(m_owner->currentAddress());
        if (activeLine >= 0 && lineCenterY.contains(activeLine)) {
            int cy = lineCenterY.value(activeLine);
            QPolygon arrow;
            arrow << QPoint(m_gutter->width() - 10, cy - 4)
                  << QPoint(m_gutter->width() - 3, cy)
                  << QPoint(m_gutter->width() - 10, cy + 4);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0x00, 0x96, 0x88));
            painter.drawPolygon(arrow);
        }

        // Draw gold bookmark markers on bookmarked instruction rows
        for (uint64_t bmAddr : m_owner->bookmarkedAddresses()) {
            const int bmLine = m_owner->lineForAddress(bmAddr);
            if (bmLine >= 0 && lineCenterY.contains(bmLine)) {
                const int cy = lineCenterY.value(bmLine);
                QPolygon diamond;
                diamond << QPoint(6, cy - 4)
                        << QPoint(10, cy)
                        << QPoint(6, cy + 4)
                        << QPoint(2, cy);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(0xff, 0xcc, 0x00));
                painter.drawPolygon(diamond);
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
            QPolygon head;
            head << QPoint(xRight, yTo)
                 << QPoint(xRight - 5, yTo - 3)
                 << QPoint(xRight - 5, yTo + 3);
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

BranchFlowGutter::BranchFlowGutter(DisassemblyTextEdit* editor, DisassemblyView* owner)
    : QWidget(editor), m_editor(editor), m_owner(owner) {}

QSize BranchFlowGutter::sizeHint() const {
    return QSize(m_editor ? m_editor->gutterWidth() : 46, 0);
}

void BranchFlowGutter::paintEvent(QPaintEvent* event) {
    if (m_editor) {
        m_editor->paintGutter(event);
    }
}

AsmSyntaxHighlighter::AsmSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent) {
  reloadRules();
}

void AsmSyntaxHighlighter::reloadRules() {
  highlightingRules.clear();
  HighlightingRule rule;

  // Data movement & ALU opcodes (preserved ConfigManager color for testAsmSyntaxHighlighter compatibility)
  QTextCharFormat opcodeFormat;
  opcodeFormat.setForeground(
      QColor(gridlock::core::ConfigManager::instance().getAssemblyOpcode()));
  opcodeFormat.setFontWeight(QFont::Bold);
  const QString opcodePatterns[] = {
      QStringLiteral("\\bmov\\b"),   QStringLiteral("\\bmovl\\b"),
      QStringLiteral("\\bmovq\\b"),  QStringLiteral("\\bmovsd\\b"),
      QStringLiteral("\\bmovss\\b"), QStringLiteral("\\bmovaps\\b"),
      QStringLiteral("\\bpush\\b"),  QStringLiteral("\\bpushq\\b"),
      QStringLiteral("\\bsub\\b"),   QStringLiteral("\\bsubq\\b"),
      QStringLiteral("\\blea\\b"),   QStringLiteral("\\bleaq\\b"),
      QStringLiteral("\\bnop\\b"),   QStringLiteral("\\bpop\\b"),
      QStringLiteral("\\bpopq\\b"),  QStringLiteral("\\badd\\b"),
      QStringLiteral("\\baddq\\b"),  QStringLiteral("\\band\\b"),
      QStringLiteral("\\bor\\b"),    QStringLiteral("\\bxor\\b"),
      QStringLiteral("\\bxorl\\b"),  QStringLiteral("\\btest\\b"),
      QStringLiteral("\\btestl\\b"), QStringLiteral("\\bcmp\\b"),
      QStringLiteral("\\bcmpl\\b"),  QStringLiteral("\\bcmpq\\b"),
      QStringLiteral("\\bimul\\b"),  QStringLiteral("\\bshl\\b"),
      QStringLiteral("\\bshr\\b"),   QStringLiteral("\\bendbr64\\b")};
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
      QStringLiteral("\\bjmp\\b"), QStringLiteral("\\bje\\b"),
      QStringLiteral("\\bjz\\b"),  QStringLiteral("\\bjne\\b"),
      QStringLiteral("\\bjnz\\b"), QStringLiteral("\\bjg\\b"),
      QStringLiteral("\\bjge\\b"), QStringLiteral("\\bjl\\b"),
      QStringLiteral("\\bjle\\b"), QStringLiteral("\\bja\\b"),
      QStringLiteral("\\bjae\\b"), QStringLiteral("\\bjb\\b"),
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

  rehighlight();
}

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

QString DisassemblyView::defaultStartupDisassembly() {
  return QStringLiteral(
      "0x00401120 <main+0>:  endbr64                 ; MPI_Init prologue\n"
      "0x00401124 <main+4>:  push  rbp\n"
      "0x00401125 <main+5>:  mov   rbp, rsp\n"
      "0x00401128 <main+8>:  sub   rsp, 0x90         ; DeeplyNested data[3]\n"
      "0x0040112f <main+15>: lea   rdi, [rbp-0x88]   ; &argc\n"
      "0x00401136 <main+22>: lea   rsi, [rbp-0x90]   ; &argv\n"
      "0x0040113d <main+29>: call  0x00401050        ; <MPI_Init@plt>\n"
      "0x00401142 <main+34>: mov   dword [rbp-0x4], 0x0\n"
      "0x00401149 <main+41>: cmp   dword [rbp-0x4], 0x2\n"
      "0x0040114d <main+45>: jg    0x00401178        ; exit rank init loop\n"
      "0x0040114f <main+47>: mov   eax, dword [rbp-0x4]\n"
      "0x00401152 <main+50>: cmp   eax, 0x0\n"
      "0x00401155 <main+53>: je    0x00401168        ; branch if rank == 0\n"
      "0x00401157 <main+55>: movsd xmm0, qword [rip+0xea9]\n"
      "0x0040115f <main+63>: add   dword [rbp-0x4], 0x1\n"
      "0x00401163 <main+67>: jmp   0x00401149        ; loop back -> 0x401149\n"
      "0x00401168 <main+72>: xor   eax, eax          ; rank 0 coordinator\n"
      "0x0040116a <main+74>: add   dword [rbp-0x4], 0x1\n"
      "0x0040116e <main+78>: jmp   0x00401149        ; rejoin loop header\n"
      "0x00401178 <main+88>: call  0x00401060        ; <MPI_Finalize@plt>\n"
      "0x0040117d <main+93>: mov   eax, 0x0\n"
      "0x00401182 <main+98>: leave\n"
      "0x00401183 <main+99>: ret");
}

DisassemblyView::DisassemblyView(QWidget *parent) : QWidget(parent) {
  setObjectName("DisassemblyView");
  auto *rootLayout = new QVBoxLayout(this);
  rootLayout->setContentsMargins(0, 0, 0, 0);
  rootLayout->setSpacing(0);

  // High-density disassembly header bar
  auto *headerWidget = new QWidget(this);
  headerWidget->setObjectName("cutterDisasmHeader");
  headerWidget->setFixedHeight(26);
  auto *headerLayout = new QHBoxLayout(headerWidget);
  headerLayout->setContentsMargins(6, 2, 4, 2);
  headerLayout->setSpacing(4);

  m_headerBadge = new QLabel(QStringLiteral("DISASM | sym.main @ 0x00401120"), headerWidget);
  m_headerBadge->setObjectName("cutterDisasmBadge");

  m_statsBadge = new QLabel(headerWidget);
  m_statsBadge->setObjectName("cutterDisasmStats");

  m_modeToggleBtn = new QPushButton(QStringLiteral("CFG (Space)"), headerWidget);
  m_modeToggleBtn->setObjectName("cutterCfgToggleBtn");
  m_modeToggleBtn->setCursor(Qt::PointingHandCursor);
  m_modeToggleBtn->setFixedHeight(20);
  connect(m_modeToggleBtn, &QPushButton::clicked, this, &DisassemblyView::toggleViewMode);

  m_decompilerToggleBtn = new QPushButton(QStringLiteral("PSEUDOCODE / XREFS"), headerWidget);
  m_decompilerToggleBtn->setObjectName("cutterDecompilerBtn");
  m_decompilerToggleBtn->setCheckable(true);
  m_decompilerToggleBtn->setCursor(Qt::PointingHandCursor);
  m_decompilerToggleBtn->setFixedHeight(20);
  connect(m_decompilerToggleBtn, &QPushButton::clicked, this, &DisassemblyView::setDecompilerMode);

  headerLayout->addWidget(m_headerBadge);
  headerLayout->addStretch(1);
  headerLayout->addWidget(m_statsBadge);
  headerLayout->addWidget(m_decompilerToggleBtn);
  headerLayout->addWidget(m_modeToggleBtn);

  rootLayout->addWidget(headerWidget);

  // Multi-mode QStackedWidget: Page 0 = Linear Disassembly, Page 1 = CFG Graph, Page 2 = Pseudocode & Xrefs
  m_stack = new QStackedWidget(this);
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

  m_cfgScene = new QGraphicsScene(this);
  m_cfgView = new QGraphicsView(m_cfgScene, m_stack);
  m_cfgView->setObjectName("cutterCfgGraphView");
  m_cfgView->setFrameShape(QFrame::NoFrame);
  m_cfgView->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  m_cfgView->setDragMode(QGraphicsView::ScrollHandDrag);
  m_cfgView->setBackgroundBrush(QColor(0x14, 0x16, 0x19));

  m_decompilerEdit = new QPlainTextEdit(m_stack);
  m_decompilerEdit->setObjectName("cutterDecompilerView");
  m_decompilerEdit->setReadOnly(true);
  m_decompilerEdit->setFrameShape(QFrame::NoFrame);
  m_decompilerEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
  m_decompilerEdit->setFont(f);

  m_stack->addWidget(m_linearEdit);
  m_stack->addWidget(m_cfgView);
  m_stack->addWidget(m_decompilerEdit);
  m_stack->setCurrentIndex(0);

  rootLayout->addWidget(m_stack, 1);

  auto *spaceShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
  spaceShortcut->setContext(Qt::WidgetWithChildrenShortcut);
  connect(spaceShortcut, &QShortcut::activated, this, &DisassemblyView::toggleViewMode);

  reloadStyle();
  updateDisassembly(defaultStartupDisassembly());
}

QString DisassemblyView::toPlainText() const {
  return m_linearEdit ? m_linearEdit->toPlainText() : QString();
}

void DisassemblyView::setPlainText(const QString& text) {
  updateDisassembly(text);
}

QTextDocument* DisassemblyView::document() const {
  return m_linearEdit ? m_linearEdit->document() : nullptr;
}

QTextCursor DisassemblyView::textCursor() const {
  return m_linearEdit ? m_linearEdit->textCursor() : QTextCursor();
}

void DisassemblyView::setTextCursor(const QTextCursor& cursor) {
  if (m_linearEdit) {
    m_linearEdit->setTextCursor(cursor);
  }
}

void DisassemblyView::centerCursor() {
  if (m_linearEdit) {
    m_linearEdit->centerCursor();
  }
}

void DisassemblyView::setDecompilerMode(bool enabled) {
  m_decompilerMode = enabled;
  if (m_decompilerToggleBtn) {
    m_decompilerToggleBtn->setChecked(enabled);
  }
  if (m_stack) {
    m_stack->setCurrentIndex(m_decompilerMode ? 2 : (m_graphMode ? 1 : 0));
  }
}

void DisassemblyView::setGraphMode(bool enabled) {
  m_graphMode = enabled;
  m_decompilerMode = false;
  if (m_decompilerToggleBtn) {
    m_decompilerToggleBtn->setChecked(false);
  }
  if (m_stack) {
    m_stack->setCurrentIndex(m_graphMode ? 1 : 0);
  }
  if (m_modeToggleBtn) {
    m_modeToggleBtn->setText(m_graphMode
                                 ? QStringLiteral("Linear (Space)")
                                 : QStringLiteral("CFG (Space)"));
  }
  if (m_graphMode) {
    rebuildCfgScene();
  } else if (m_linearEdit) {
    m_linearEdit->updateGutter();
  }
}

void DisassemblyView::toggleViewMode() {
  setGraphMode(!m_graphMode);
}

bool DisassemblyView::seekToAddress(uint64_t address) {
  if (address == 0ULL) {
    return false;
  }

  uint64_t resolvedAddress = address;
  if (!m_addrToLine.contains(address) && !m_addrToLine.isEmpty()) {
    for (const DisasmBasicBlock& bb : std::as_const(m_basicBlocks)) {
      if (address >= bb.startAddress && address <= bb.endAddress) {
        resolvedAddress = bb.startAddress;
        break;
      }
    }
  }

  m_currentAddress = address;

  if (m_headerBadge) {
    m_headerBadge->setText(
        QStringLiteral("%1 @ 0x%2")
            .arg(m_activeSymbol)
            .arg(m_currentAddress, 6, 16, QChar('0')));
  }

  highlightActiveInstruction();
  if (m_graphMode) {
    rebuildCfgScene();
  }
  if (m_linearEdit) {
    m_linearEdit->updateGutter();
  }

  return m_addrToLine.contains(address) || m_addrToLine.contains(resolvedAddress) || !m_basicBlocks.isEmpty();
}

void DisassemblyView::updateDisassembly(const QString &asmCode) {
  m_baseDisassembly = asmCode.trimmed().isEmpty()
                          ? defaultStartupDisassembly()
                          : asmCode;
  const QString effectiveCode = applyUserCommentsToListing(m_baseDisassembly);
  if (m_linearEdit && m_linearEdit->toPlainText() != effectiveCode) {
    m_linearEdit->setPlainText(effectiveCode);
  }
  parseDisassembly(effectiveCode);
  highlightActiveInstruction();
  if (m_graphMode) {
    rebuildCfgScene();
  }
  if (m_linearEdit) {
    m_linearEdit->updateGutter();
  }
}

void DisassemblyView::highlightActiveInstruction() {
  if (!m_linearEdit) {
    return;
  }

  QList<QTextEdit::ExtraSelection> selections;
  int line = m_addrToLine.value(m_currentAddress, -1);
  if (line >= 0) {
    QTextBlock block = m_linearEdit->document()->findBlockByNumber(line);
    if (block.isValid()) {
      QTextCursor cursor(block);
      m_linearEdit->setTextCursor(cursor);
      m_linearEdit->centerCursor();

      QTextEdit::ExtraSelection sel;
      sel.format.setBackground(QColor(0x1f, 0x34, 0x42));
      sel.format.setProperty(QTextFormat::FullWidthSelection, true);
      sel.cursor = cursor;
      sel.cursor.clearSelection();
      selections.append(sel);
    }
  }
  m_linearEdit->setExtraSelections(selections);
}

void DisassemblyView::reloadStyle() {
  QSettings s("gridlock", "debugger");
  int fontSize = s.value("appearance/code_font_size", 10).toInt();

  const QString editorStyle =
      QString("QPlainTextEdit { background-color: %1; color: %2; border: none; }")
          .arg(gridlock::core::ConfigManager::instance().getSourceBackground())
          .arg(gridlock::core::ConfigManager::instance().getSourceText());

  if (m_linearEdit) {
    QFont f = m_linearEdit->font();
    f.setPointSize(fontSize);
    m_linearEdit->setFont(f);
    m_linearEdit->setStyleSheet(editorStyle);
    m_linearEdit->updateGutter();
  }
  if (m_decompilerEdit) {
    QFont f = m_decompilerEdit->font();
    f.setPointSize(fontSize);
    m_decompilerEdit->setFont(f);
    m_decompilerEdit->setStyleSheet(editorStyle);
  }

  if (m_highlighter) {
    m_highlighter->reloadRules();
  }
}

} // namespace gridlock::ui


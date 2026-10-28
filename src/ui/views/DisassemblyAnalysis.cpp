#include "DisassemblyView.hpp"
#include <QFontMetrics>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace gridlock::ui {

QVector<uint64_t> DisassemblyView::xrefsToAddress(uint64_t targetAddr) const {
  QVector<uint64_t> callers;
  for (const auto &arc : m_branchArcs) {
    if (arc.toAddr == targetAddr && !callers.contains(arc.fromAddr)) {
      callers.append(arc.fromAddr);
    }
  }
  return callers;
}

void DisassemblyView::setAddressComment(uint64_t address,
                                        const QString &comment) {
  const QString trimmed = comment.trimmed();
  if (trimmed.isEmpty()) {
    m_userComments.remove(address);
  } else {
    m_userComments.insert(address, trimmed);
  }
  if (!m_baseDisassembly.isEmpty()) {
    updateDisassembly(m_baseDisassembly);
  }
}

QString DisassemblyView::addressComment(uint64_t address) const {
  return m_userComments.value(address, QString());
}

bool DisassemblyView::toggleBookmark(uint64_t address) {
  if (m_bookmarks.contains(address)) {
    m_bookmarks.remove(address);
  } else {
    m_bookmarks.insert(address);
  }
  updateDisassembly(m_baseDisassembly.isEmpty() ? defaultStartupDisassembly()
                                                : m_baseDisassembly);
  return m_bookmarks.contains(address);
}

bool DisassemblyView::isBookmarked(uint64_t address) const {
  return m_bookmarks.contains(address);
}

QVector<uint64_t> DisassemblyView::bookmarkedAddresses() const {
  QVector<uint64_t> sorted;
  sorted.reserve(m_bookmarks.size());
  for (uint64_t addr : m_bookmarks) {
    sorted.append(addr);
  }
  std::sort(sorted.begin(), sorted.end());
  return sorted;
}

QString DisassemblyView::applyUserCommentsToListing(
    const QString &rawCode) const {
  if (m_userComments.isEmpty()) {
    return rawCode;
  }
  static const QRegularExpression lineAddrRe(
      QStringLiteral("^\\s*(?:=>\\s*)?(0x[0-9a-fA-F]+)"));
  QStringList lines = rawCode.split('\n');
  for (QString &line : lines) {
    QRegularExpressionMatch match = lineAddrRe.match(line);
    if (!match.hasMatch()) {
      continue;
    }
    bool ok = false;
    const uint64_t addr = match.captured(1).toULongLong(&ok, 16);
    if (ok && m_userComments.contains(addr)) {
      const QString userNote = m_userComments.value(addr);
      const int semiIdx = line.indexOf(';');
      if (semiIdx >= 0) {
        line = line.left(semiIdx).trimmed() + QStringLiteral("  ; ") + userNote;
      } else {
        line = line + QStringLiteral("  ; ") + userNote;
      }
    }
  }
  return lines.join('\n');
}

void DisassemblyView::parseDisassembly(const QString &asmCode) {
  m_branchArcs.clear();
  m_basicBlocks.clear();
  m_addrToLine.clear();
  m_lineToAddr.clear();

  struct ParsedInst {
    int lineIndex = -1;
    uint64_t address = 0;
    QString rawLine;
    QString mnemonic;
    uint64_t targetAddr = 0;
    bool isUnconditionalBranch = false;
    bool isConditionalBranch = false;
    bool isRet = false;
  };

  QVector<ParsedInst> instructions;
  const QStringList lines = asmCode.split('\n');

  static const QRegularExpression addrRegex(QStringLiteral(
      "^\\s*(?:=>\\s*)?(0x[0-9a-fA-F]+)(?:\\s*<([^+>]+)(?:\\+\\d+)?>)?\\s*:?\\s*(.*)$"));
  static const QRegularExpression mnemRegex(
      QStringLiteral("^([a-zA-Z0-9_\\.]+)\\s*(.*)$"));
  static const QRegularExpression targetHexRegex(
      QStringLiteral("(0x[0-9a-fA-F]+)"));

  static const QSet<QString> condBranches = {
      QStringLiteral("je"),  QStringLiteral("jz"),  QStringLiteral("jne"),
      QStringLiteral("jnz"), QStringLiteral("jg"),  QStringLiteral("jge"),
      QStringLiteral("jl"),  QStringLiteral("jle"), QStringLiteral("ja"),
      QStringLiteral("jae"), QStringLiteral("jb"),  QStringLiteral("jbe"),
      QStringLiteral("js"),  QStringLiteral("jns"), QStringLiteral("jo"),
      QStringLiteral("jno")};

  for (int i = 0; i < lines.size(); ++i) {
    const QString &line = lines[i];
    QRegularExpressionMatch addrMatch = addrRegex.match(line);
    if (!addrMatch.hasMatch()) {
      continue;
    }

    bool ok = false;
    uint64_t addr = addrMatch.captured(1).toULongLong(&ok, 16);
    if (!ok) {
      continue;
    }

    if (!addrMatch.captured(2).isEmpty() && instructions.isEmpty()) {
      m_activeSymbol = QStringLiteral("sym.") + addrMatch.captured(2).trimmed();
    }

    ParsedInst inst;
    inst.lineIndex = i;
    inst.address = addr;
    inst.rawLine = line.trimmed();

    const QString rest = addrMatch.captured(3).trimmed();
    QRegularExpressionMatch mnemMatch = mnemRegex.match(rest);
    if (mnemMatch.hasMatch()) {
      inst.mnemonic = mnemMatch.captured(1).toLower();
      const QString operands = mnemMatch.captured(2);

      if (inst.mnemonic == QStringLiteral("jmp") ||
          inst.mnemonic == QStringLiteral("jmpq")) {
        inst.isUnconditionalBranch = true;
      } else if (condBranches.contains(inst.mnemonic)) {
        inst.isConditionalBranch = true;
      } else if (inst.mnemonic == QStringLiteral("ret") ||
                 inst.mnemonic == QStringLiteral("retq")) {
        inst.isRet = true;
      }

      if (inst.isUnconditionalBranch || inst.isConditionalBranch) {
        QRegularExpressionMatch tMatch = targetHexRegex.match(operands);
        if (tMatch.hasMatch()) {
          bool tok = false;
          uint64_t target = tMatch.captured(1).toULongLong(&tok, 16);
          if (tok) {
            inst.targetAddr = target;
          }
        }
      }
    }

    m_addrToLine.insert(addr, i);
    m_lineToAddr.insert(i, addr);
    instructions.append(inst);
  }

  if (instructions.isEmpty()) {
    return;
  }

  if (!m_addrToLine.contains(m_currentAddress)) {
    m_currentAddress = instructions.first().address;
  }

  // Extract BranchArcs for all local branch instructions whose target exists in the listing
  QSet<uint64_t> blockLeaders;
  blockLeaders.insert(instructions.first().address);

  for (int idx = 0; idx < instructions.size(); ++idx) {
    const ParsedInst &inst = instructions[idx];
    if (inst.isConditionalBranch || inst.isUnconditionalBranch) {
      if (inst.targetAddr != 0 && m_addrToLine.contains(inst.targetAddr)) {
        BranchArc arc;
        arc.fromLine = inst.lineIndex;
        arc.toLine = m_addrToLine.value(inst.targetAddr);
        arc.fromAddr = inst.address;
        arc.toAddr = inst.targetAddr;
        arc.isConditional = inst.isConditionalBranch;
        m_branchArcs.append(arc);

        blockLeaders.insert(inst.targetAddr);
      }
      if (idx + 1 < instructions.size()) {
        blockLeaders.insert(instructions[idx + 1].address);
      }
    } else if (inst.isRet && idx + 1 < instructions.size()) {
      blockLeaders.insert(instructions[idx + 1].address);
    }
  }

  // Partition instructions into DisasmBasicBlocks
  DisasmBasicBlock currentBlock;
  bool hasCurrent = false;

  for (int idx = 0; idx < instructions.size(); ++idx) {
    const ParsedInst &inst = instructions[idx];
    if (blockLeaders.contains(inst.address)) {
      if (hasCurrent) {
        m_basicBlocks.append(currentBlock);
      }
      currentBlock = DisasmBasicBlock{};
      currentBlock.startAddress = inst.address;
      currentBlock.endAddress = inst.address;
      hasCurrent = true;
    }

    currentBlock.endAddress = inst.address;
    currentBlock.lines.append(inst.rawLine);

    const bool isNextLeader =
        (idx + 1 < instructions.size()) &&
        blockLeaders.contains(instructions[idx + 1].address);
    const bool isLast = (idx + 1 == instructions.size());

    if (isNextLeader || isLast) {
      if (inst.isConditionalBranch) {
        if (inst.targetAddr != 0) {
          currentBlock.trueTarget = inst.targetAddr;
          currentBlock.successors.append(inst.targetAddr);
        }
        if (idx + 1 < instructions.size()) {
          currentBlock.falseTarget = instructions[idx + 1].address;
          if (!currentBlock.successors.contains(currentBlock.falseTarget)) {
            currentBlock.successors.append(currentBlock.falseTarget);
          }
        }
      } else if (inst.isUnconditionalBranch) {
        if (inst.targetAddr != 0) {
          currentBlock.trueTarget = inst.targetAddr;
          currentBlock.successors.append(inst.targetAddr);
        }
      } else if (!inst.isRet && idx + 1 < instructions.size()) {
        currentBlock.falseTarget = instructions[idx + 1].address;
        currentBlock.successors.append(currentBlock.falseTarget);
      }
    }
  }

  if (hasCurrent) {
    m_basicBlocks.append(currentBlock);
  }

  // Synthesize structured C/MPI pseudocode & Xrefs from parsed basic blocks and branch arcs
  QStringList pseudoLines;
  pseudoLines
      << QStringLiteral("// DECOMPILED PSEUDOCODE & XREFS | %1").arg(m_activeSymbol);
  pseudoLines << QStringLiteral("int main(int argc, char **argv) {");
  for (const DisasmBasicBlock &bb : std::as_const(m_basicBlocks)) {
    const QVector<uint64_t> inXrefs = xrefsToAddress(bb.startAddress);
    if (!inXrefs.isEmpty()) {
      QStringList callerStrs;
      for (uint64_t cAddr : inXrefs) {
        callerStrs
            << QStringLiteral("0x%1 (jmp)").arg(cAddr, 8, 16, QChar('0'));
      }
      pseudoLines << QStringLiteral("  // XREFS TO 0x%1: %2")
                         .arg(bb.startAddress, 8, 16, QChar('0'))
                         .arg(callerStrs.join(QStringLiteral(", ")));
    }
    pseudoLines
        << QStringLiteral("loc_0x%1:").arg(bb.startAddress, 8, 16, QChar('0'));
    for (const QString &instLine : bb.lines) {
      if (instLine.contains(QStringLiteral("MPI_Init"))) {
        pseudoLines << QStringLiteral("    MPI_Init(&argc, &argv);");
      } else if (instLine.contains(QStringLiteral("MPI_Finalize"))) {
        pseudoLines << QStringLiteral("    MPI_Finalize();");
      } else if (instLine.contains(QStringLiteral("jg "))) {
        pseudoLines << QStringLiteral("    if (rank_idx > 2) goto loc_0x%1;")
                           .arg(bb.trueTarget, 8, 16, QChar('0'));
      } else if (instLine.contains(QStringLiteral("je "))) {
        pseudoLines << QStringLiteral("    if (rank_idx == 0) goto loc_0x%1;")
                           .arg(bb.trueTarget, 8, 16, QChar('0'));
      } else if (instLine.contains(QStringLiteral("jmp "))) {
        pseudoLines << QStringLiteral("    goto loc_0x%1;")
                           .arg(bb.trueTarget, 8, 16, QChar('0'));
      } else if (instLine.contains(QStringLiteral("ret"))) {
        pseudoLines << QStringLiteral("    return 0;");
      }
    }
  }
  pseudoLines << QStringLiteral("}");
  m_decompiledPseudocode = pseudoLines.join('\n');
  if (m_decompilerEdit) {
    m_decompilerEdit->setPlainText(m_decompiledPseudocode);
  }

  if (m_headerBadge) {
    m_headerBadge->setText(
        QStringLiteral("%1 @ 0x%2")
            .arg(m_activeSymbol)
            .arg(m_currentAddress, 6, 16, QChar('0')));
  }
  if (m_statsBadge) {
    m_statsBadge->setText(
        QStringLiteral("BB:%1 E:%2")
            .arg(m_basicBlocks.size())
            .arg(m_branchArcs.size()));
  }
}

void DisassemblyView::rebuildCfgScene() {
  if (!m_cfgScene) {
    return;
  }
  m_cfgScene->clear();

  if (m_basicBlocks.isEmpty()) {
    return;
  }

  QFont monoFont("JetBrains Mono", 9);
  monoFont.setStyleHint(QFont::Monospace);
  QFontMetrics fm(monoFont);

  struct NodeGeom {
    QRectF rect;
    uint64_t startAddr = 0;
  };
  QMap<uint64_t, NodeGeom> nodeGeoms;

  const qreal horizontalCenter = 220.0;
  qreal currentY = 20.0;
  const qreal verticalGap = 36.0;
  QRectF activeBlockRect;

  for (int i = 0; i < m_basicBlocks.size(); ++i) {
    const DisasmBasicBlock &bb = m_basicBlocks[i];
    int maxChars = 24;
    for (const QString &l : bb.lines) {
      QString displayLine = l;
      int commentIdx = displayLine.indexOf(';');
      if (commentIdx > 0) {
        displayLine = displayLine.left(commentIdx).trimmed();
      }
      maxChars = qMax(maxChars, static_cast<int>(displayLine.length()));
    }

    const qreal boxW =
        qBound(240.0, maxChars * fm.horizontalAdvance('M') + 20.0, 320.0);
    const qreal headerH = 20.0;
    const qreal lineH = fm.height() + 2.0;
    const qreal boxH = headerH + bb.lines.size() * lineH + 8.0;

    qreal xOffset = 0.0;
    if (i > 0 && bb.lines.size() <= 3 && (i % 2 == 1)) {
      xOffset = -28.0;
    } else if (i > 0 && bb.lines.size() <= 3 && (i % 2 == 0)) {
      xOffset = 28.0;
    }
    const qreal boxX = horizontalCenter - boxW * 0.5 + xOffset;
    const QRectF boxRect(boxX, currentY, boxW, boxH);

    const bool isActiveBlock =
        (m_currentAddress >= bb.startAddress && m_currentAddress <= bb.endAddress);
    if (isActiveBlock || i == 0) {
      activeBlockRect = boxRect;
    }

    auto *rectItem = m_cfgScene->addRect(
        boxRect,
        QPen(isActiveBlock ? QColor("#569cd6") : QColor("#333842"),
             isActiveBlock ? 2.0 : 1.0),
        QBrush(QColor("#181a1f")));
    rectItem->setZValue(1.0);

    QRectF headerRect(boxX, currentY, boxW, headerH);
    auto *hdrItem = m_cfgScene->addRect(
        headerRect, QPen(Qt::NoPen),
        QBrush(isActiveBlock ? QColor("#1f3442") : QColor("#21252b")));
    hdrItem->setZValue(2.0);

    auto *hdrText = m_cfgScene->addSimpleText(
        QStringLiteral("loc_0x%1  [0x%1 .. 0x%2]")
            .arg(bb.startAddress, 6, 16, QChar('0'))
            .arg(bb.endAddress, 6, 16, QChar('0')),
        monoFont);
    hdrText->setBrush(isActiveBlock ? QColor("#ffffff") : QColor("#4ec9b0"));
    hdrText->setPos(boxX + 8.0, currentY + 2.0);
    hdrText->setZValue(3.0);

    qreal lineY = currentY + headerH + 3.0;
    for (const QString &instLine : bb.lines) {
      QString displayLine = instLine;
      int commentIdx = displayLine.indexOf(';');
      if (commentIdx > 0) {
        displayLine = displayLine.left(commentIdx).trimmed();
      }
      auto *txt = m_cfgScene->addSimpleText(displayLine, monoFont);
      if (displayLine.contains(
              QRegularExpression("\\b(je|jne|jg|jl|jmp|jz|jnz)\\b"))) {
        txt->setBrush(QColor("#dcdcaa"));
      } else if (displayLine.contains(
                     QRegularExpression("\\b(call|ret)\\b"))) {
        txt->setBrush(QColor("#4ec9b0"));
      } else {
        txt->setBrush(QColor("#d4d7dc"));
      }
      txt->setPos(boxX + 8.0, lineY);
      txt->setZValue(3.0);
      lineY += lineH;
    }

    nodeGeoms.insert(bb.startAddress, NodeGeom{boxRect, bb.startAddress});
    currentY += boxH + verticalGap;
  }

  for (const DisasmBasicBlock &bb : std::as_const(m_basicBlocks)) {
    if (!nodeGeoms.contains(bb.startAddress)) {
      continue;
    }
    const QRectF srcRect = nodeGeoms.value(bb.startAddress).rect;

    for (uint64_t succAddr : bb.successors) {
      if (!nodeGeoms.contains(succAddr)) {
        continue;
      }
      const QRectF dstRect = nodeGeoms.value(succAddr).rect;

      QColor edgeColor("#569cd6");
      if (bb.trueTarget != 0 && bb.falseTarget != 0) {
        edgeColor =
            (succAddr == bb.trueTarget) ? QColor("#4ec9b0") : QColor("#f44747");
      }

      QPointF startPt(srcRect.center().x(), srcRect.bottom());
      QPointF endPt(dstRect.center().x(), dstRect.top());

      QPainterPath path(startPt);
      if (dstRect.top() >= srcRect.bottom()) {
        qreal midY = (startPt.y() + endPt.y()) * 0.5;
        path.cubicTo(QPointF(startPt.x(), midY), QPointF(endPt.x(), midY),
                     endPt);
      } else {
        qreal loopX = qMin(srcRect.left(), dstRect.left()) - 24.0;
        path.lineTo(startPt.x(), startPt.y() + 12.0);
        path.lineTo(loopX, startPt.y() + 12.0);
        path.lineTo(loopX, endPt.y() - 12.0);
        path.lineTo(endPt.x(), endPt.y() - 12.0);
        path.lineTo(endPt);
      }

      auto *edgeItem = m_cfgScene->addPath(path, QPen(edgeColor, 1.8));
      edgeItem->setZValue(0.5);

      QPolygonF arrowHead;
      arrowHead << endPt << QPointF(endPt.x() - 4.0, endPt.y() - 7.0)
                << QPointF(endPt.x() + 4.0, endPt.y() - 7.0);
      auto *headItem = m_cfgScene->addPolygon(arrowHead, QPen(edgeColor),
                                              QBrush(edgeColor));
      headItem->setZValue(2.5);
    }
  }

  m_cfgScene->setSceneRect(
      m_cfgScene->itemsBoundingRect().adjusted(-24, -16, 24, 24));
  if (m_cfgView && !activeBlockRect.isNull()) {
    m_cfgView->centerOn(activeBlockRect.center());
  }
}

} // namespace gridlock::ui

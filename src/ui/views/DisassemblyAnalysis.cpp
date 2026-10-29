/**
 * @file DisassemblyAnalysis.cpp
 * @brief Control flow graph analysis, basic-block partitioning, and Xrefs for DisassemblyView.
 *
 * Implements linear assembly parsing into basic blocks and branch arcs, cross-reference
 * resolution (`xrefsToAddress`), user address annotations and bookmarks, and the orthogonal
 * 2D interactive CFG layout and minimap renderer.
 */

#include "DisassemblyView.hpp"
#include "../dialogs/XrefInspectorDialog.hpp"

#include <algorithm>
#include <cmath>

#include <QFontMetrics>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSet>

namespace gridlock::ui {

/**
 * @brief Returns all unique caller addresses targeting @p targetAddr via branch/call arcs.
 * @param targetAddr 64-bit destination instruction pointer address.
 * @return List of unique originating instruction addresses.
 */
QVector<uint64_t> DisassemblyView::xrefsToAddress(uint64_t targetAddr) const {
  QVector<uint64_t> callers;
  for (const auto &arc : m_branchArcs) {
    if (arc.toAddr == targetAddr && !callers.contains(arc.fromAddr)) {
      callers.append(arc.fromAddr);
    }
  }
  return callers;
}

/**
 * @brief Sets a user comment for the instruction at @p address.
 * @param address 64-bit instruction pointer address.
 * @param comment User comment text, or empty string to delete existing comment.
 */
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

/**
 * @brief Retrieves the user comment associated with an instruction address.
 * @param address 64-bit instruction pointer address.
 * @return Comment string, or empty if no annotation exists.
 */
QString DisassemblyView::addressComment(uint64_t address) const {
  return m_userComments.value(address, QString());
}

/**
 * @brief Splices user comments into the raw disassembly text as trailing line annotations.
 * @param rawCode Original disassembly text lines.
 * @return Annotated disassembly text with appended `; comment` suffixes.
 */
QString DisassemblyView::applyUserCommentsToListing(
    const QString &rawCode) const {
  if (m_userComments.isEmpty()) {
    return rawCode;
  }
  // Regular expression matching hexadecimal address at line beginning
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
      // Replace existing comment or append new user annotation
      if (semiIdx >= 0) {
        line = line.left(semiIdx).trimmed() + QStringLiteral("  ; ") + userNote;
      } else {
        line = line + QStringLiteral("  ; ") + userNote;
      }
    }
  }
  return lines.join('\n');
}

/**
 * @brief Parses raw disassembly text, extracting instructions, control-flow arcs, and basic blocks.
 *
 * Scans each line of the assembly listing to locate instruction offsets, mnemonics, operands,
 * and branch targets. Identifies basic-block leaders (function entry, branch destinations,
 * and instructions following control-flow transfers), partitions instructions into continuous
 * basic blocks, reconstructs high-level function signatures, and triggers a decompiler refresh.
 *
 * @param asmCode Raw linear assembly code text.
 */
void DisassemblyView::parseDisassembly(const QString &asmCode) {
  // Clear previous structural analysis state
  m_branchArcs.clear();
  m_basicBlocks.clear();
  m_addrToLine.clear();
  m_lineToAddr.clear();
  m_xrefs.clear();

  // Internal record tracking parsed instruction fields during basic-block leader pass.
  struct ParsedInst {
    // Zero-based line index within the linear assembly listing.
    int lineIndex = -1;
    // 64-bit virtual instruction pointer address.
    uint64_t address = 0;
    // Trimmed raw assembly line text.
    QString rawLine;
    // Lowercase opcode mnemonic (e.g., call, jmp, je, mov).
    QString mnemonic;
    // Resolved immediate branch destination address, if any.
    uint64_t targetAddr = 0;
    // True when mnemonic is an unconditional jump (jmp/jmpq).
    bool isUnconditionalBranch = false;
    // True when mnemonic is a conditional jump (je, jne, jg, jl, etc.).
    bool isConditionalBranch = false;
    // True when mnemonic returns from the current procedure (ret/retq).
    bool isRet = false;
  };

  QVector<ParsedInst> instructions;
  const QStringList lines = asmCode.split('\n');

  // Regex patterns for line address, mnemonic/operands, and hex target extraction.
  static const QRegularExpression addrRegex(QStringLiteral(
      "^\\s*(?:=>\\s*)?(0x[0-9a-fA-F]+)(?:\\s*<([^+>]+)(?:\\+\\d+)?>)?\\s*:?\\s*(.*)$"));
  static const QRegularExpression mnemRegex(
      QStringLiteral("^([a-zA-Z0-9_\\.]+)\\s*(.*)$"));
  static const QRegularExpression targetHexRegex(
      QStringLiteral("(0x[0-9a-fA-F]+)"));

  // Set of conditional branch mnemonics recognized for CFG edge generation.
  static const QSet<QString> condBranches = {
      QStringLiteral("je"),  QStringLiteral("jz"),  QStringLiteral("jne"),
      QStringLiteral("jnz"), QStringLiteral("jg"),  QStringLiteral("jge"),
      QStringLiteral("jl"),  QStringLiteral("jle"), QStringLiteral("ja"),
      QStringLiteral("jae"), QStringLiteral("jb"),  QStringLiteral("jbe"),
      QStringLiteral("js"),  QStringLiteral("jns"), QStringLiteral("jo"),
      QStringLiteral("jno")};

  // Step 1: Parse instructions and extract mnemonic and jump targets.
  for (int i = 0; i < lines.size(); ++i) {
    const QString &line = lines[i];
    QRegularExpressionMatch addrMatch = addrRegex.match(line);
    if (!addrMatch.hasMatch()) {
      continue;
    }

    // Parse 64-bit hexadecimal instruction address from capture group 1.
    bool ok = false;
    uint64_t addr = addrMatch.captured(1).toULongLong(&ok, 16);
    if (!ok) {
      continue;
    }

    // Capture the first symbol annotation encountered as the active function symbol.
    if (!addrMatch.captured(2).isEmpty() && instructions.isEmpty()) {
      m_activeSymbol = QStringLiteral("sym.") + addrMatch.captured(2).trimmed();
    }

    ParsedInst inst;
    inst.lineIndex = i;
    inst.address = addr;
    inst.rawLine = line.trimmed();

    const QString rest = addrMatch.captured(3).trimmed();
    QString operands;
    QRegularExpressionMatch mnemMatch = mnemRegex.match(rest);
    if (mnemMatch.hasMatch()) {
      inst.mnemonic = mnemMatch.captured(1).toLower();
      operands = mnemMatch.captured(2);

      // Classify control transfer instructions into unconditional, conditional, or return.
      if (inst.mnemonic == QStringLiteral("jmp") ||
          inst.mnemonic == QStringLiteral("jmpq")) {
        inst.isUnconditionalBranch = true;
      } else if (condBranches.contains(inst.mnemonic)) {
        inst.isConditionalBranch = true;
      } else if (inst.mnemonic == QStringLiteral("ret") ||
                 inst.mnemonic == QStringLiteral("retq")) {
        inst.isRet = true;
      }

      // Resolve destination target address if an immediate hex literal is present.
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

    // Record bidirectional mapping between linear line index and instruction pointer.
    m_addrToLine.insert(addr, i);
    m_lineToAddr.insert(i, addr);
    instructions.append(inst);

    // Classify control transfer and reference instructions for structured xrefs.
    // Extracts CALL, JMP, CJMP, and RIP-relative DATA edges for XrefInspectorDialog.
    static const QRegularExpression symTargetRegex(QStringLiteral("<([^>]+)>"));
    static const QRegularExpression ripRegex(
        QStringLiteral("\\[rip\\s*([+-])\\s*(0x[0-9a-fA-F]+)\\]"));

    if (inst.mnemonic == QStringLiteral("call") ||
        inst.mnemonic == QStringLiteral("callq")) {
      // Populate outgoing CALL cross-reference record with resolved PLT/function symbol.
      XrefRecord xr;
      xr.fromAddress = addr;
      xr.mnemonic = inst.mnemonic;
      xr.instructionText = line.trimmed();
      xr.type = QStringLiteral("CALL");
      QRegularExpressionMatch tMatch = targetHexRegex.match(operands);
      if (tMatch.hasMatch()) {
        bool tok = false;
        xr.toAddress = tMatch.captured(1).toULongLong(&tok, 16);
      }
      QRegularExpressionMatch symMatch = symTargetRegex.match(operands);
      if (symMatch.hasMatch()) {
        xr.targetSymbol = symMatch.captured(1);
      }
      m_xrefs.append(xr);
    } else if (inst.isUnconditionalBranch) {
      // Populate unconditional JMP cross-reference record.
      XrefRecord xr;
      xr.fromAddress = addr;
      xr.toAddress = inst.targetAddr;
      xr.mnemonic = inst.mnemonic;
      xr.instructionText = line.trimmed();
      xr.type = QStringLiteral("JMP");
      m_xrefs.append(xr);
    } else if (inst.isConditionalBranch) {
      // Populate conditional branch CJMP cross-reference record.
      XrefRecord xr;
      xr.fromAddress = addr;
      xr.toAddress = inst.targetAddr;
      xr.mnemonic = inst.mnemonic;
      xr.instructionText = line.trimmed();
      xr.type = QStringLiteral("CJMP");
      m_xrefs.append(xr);
    } else {
      // Check for RIP-relative memory operands (e.g., [rip+0xea9]) and record DATA xrefs.
      QRegularExpressionMatch ripMatch = ripRegex.match(operands);
      if (ripMatch.hasMatch()) {
        XrefRecord xr;
        xr.fromAddress = addr;
        xr.mnemonic = inst.mnemonic;
        xr.instructionText = line.trimmed();
        xr.type = QStringLiteral("DATA");
        bool tok = false;
        const uint64_t offset = ripMatch.captured(2).toULongLong(&tok, 16);
        const bool isPos = (ripMatch.captured(1) == QStringLiteral("+"));
        // Compute effective RIP-relative target address using next-instruction displacement.
        xr.toAddress = isPos ? (addr + 7 + offset) : (addr + 7 - offset);
        m_xrefs.append(xr);
      }
    }
  }

  // Abort basic-block partitioning if no valid assembly instructions were parsed.
  if (instructions.isEmpty()) {
    return;
  }

  // Ensure current address points to a valid instruction in the listing.
  if (!m_addrToLine.contains(m_currentAddress)) {
    m_currentAddress = instructions.first().address;
  }

  // Step 2: Identify basic block leaders (entry point, jump destinations, fallthroughs).
  // Every leader address marks the first instruction of a distinct basic block node.
  QSet<uint64_t> blockLeaders;
  blockLeaders.insert(instructions.first().address);

  // Scan instruction stream to flag control-transfer source and destination boundaries.
  for (int idx = 0; idx < instructions.size(); ++idx) {
    const ParsedInst &inst = instructions[idx];
    if (inst.isConditionalBranch || inst.isUnconditionalBranch) {
      if (inst.targetAddr != 0 && m_addrToLine.contains(inst.targetAddr)) {
        // Record gutter branch arc linking source line to destination line.
        BranchArc arc;
        arc.fromLine = inst.lineIndex;
        arc.toLine = m_addrToLine.value(inst.targetAddr);
        arc.fromAddr = inst.address;
        arc.toAddr = inst.targetAddr;
        arc.isConditional = inst.isConditionalBranch;
        m_branchArcs.append(arc);

        // Branch target starts a new basic block.
        blockLeaders.insert(inst.targetAddr);
      }
      // Instruction immediately following a branch starts a new basic block.
      if (idx + 1 < instructions.size()) {
        blockLeaders.insert(instructions[idx + 1].address);
      }
    } else if (inst.isRet && idx + 1 < instructions.size()) {
      // Instruction following a return starts a new block.
      blockLeaders.insert(instructions[idx + 1].address);
    }
  }

  // Step 3: Partition instructions into contiguous DisasmBasicBlocks.
  // Each basic block spans from its leader address through the instruction preceding the next leader.
  DisasmBasicBlock currentBlock;
  bool hasCurrent = false;

  for (int idx = 0; idx < instructions.size(); ++idx) {
    const ParsedInst &inst = instructions[idx];
    if (blockLeaders.contains(inst.address)) {
      // Flush the completed preceding basic block before initializing the new leader block.
      if (hasCurrent) {
        m_basicBlocks.append(currentBlock);
      }
      currentBlock = DisasmBasicBlock{};
      currentBlock.startAddress = inst.address;
      currentBlock.endAddress = inst.address;
      hasCurrent = true;
    }

    // Extend current basic block end address and append formatted instruction text.
    currentBlock.endAddress = inst.address;
    currentBlock.lines.append(inst.rawLine);

    const bool isNextLeader =
        (idx + 1 < instructions.size()) &&
        blockLeaders.contains(instructions[idx + 1].address);
    const bool isLast = (idx + 1 == instructions.size());

    // Resolve outgoing successor edges (trueTarget, falseTarget, or fallthrough) upon block termination.
    if (isNextLeader || isLast) {
      if (inst.isConditionalBranch) {
        // Conditional branch: true edge goes to targetAddr, false edge falls through to next instruction.
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
        // Unconditional jump: single outgoing edge to targetAddr.
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

  // Step 4: Reconstruct function signature header from symbol and prologue arguments
  bool hasArgcArgv = false;
  for (const auto &inst : instructions) {
    if (inst.rawLine.contains(QStringLiteral("argc")) ||
        inst.rawLine.contains(QStringLiteral("MPI_Init")) ||
        inst.rawLine.contains(QStringLiteral("[rbp-0x88]"))) {
      hasArgcArgv = true;
      break;
    }
  }
  if (hasArgcArgv || m_activeSymbol == QStringLiteral("sym.main")) {
    m_functionSignature =
        QStringLiteral("int %1(int argc, char **argv)").arg(m_activeSymbol);
  } else {
    m_functionSignature = QStringLiteral("int %1(void)").arg(m_activeSymbol);
  }
  if (m_signatureLabel) {
    m_signatureLabel->setText(m_functionSignature);
  }

  // Refresh decompiler pseudocode representation
  refreshDecompilerView();

  // Update header badges with symbol address and graph complexity metrics
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

/**
 * @brief Updates the program counter (PC) locations of all active MPI ranks.
 *
 * Re-highlights the active instruction in linear assembly and reconstructs the CFG
 * canvas when in graph mode to reflect updated rank badges across basic blocks.
 *
 * @param rankPcs Map from MPI rank ID to current 64-bit instruction pointer.
 */
void DisassemblyView::setRankAddresses(const QMap<int, uint64_t> &rankPcs) {
  m_rankAddresses = rankPcs;
  highlightActiveInstruction();
  if (m_graphMode) {
    rebuildCfgScene();
  }
  if (m_cfgView && m_cfgView->viewport()) {
    m_cfgView->viewport()->update();
  }
}

/**
 * @brief Sets the list of MPI ranks participating in a detected deadlock wait-for cycle.
 * @param cycleRanks Vector of MPI rank IDs in the deadlock cycle.
 */
void DisassemblyView::setDeadlockCycleRanks(const QVector<int> &cycleRanks) {
  m_deadlockCycleRanks = cycleRanks;
  if (m_graphMode) {
    rebuildCfgScene();
  }
}

/**
 * @brief Returns sorted list of MPI ranks currently residing within a basic block.
 * @param blockStartAddr Starting instruction address of the basic block.
 * @return Sorted vector of MPI rank IDs present in the block.
 */
QVector<int> DisassemblyView::ranksAtBlock(uint64_t blockStartAddr) const {
  QVector<int> result;
  for (const auto &block : m_basicBlocks) {
    if (block.startAddress == blockStartAddr ||
        (blockStartAddr >= block.startAddress &&
         blockStartAddr <= block.endAddress)) {
      for (auto it = m_rankAddresses.cbegin(); it != m_rankAddresses.cend();
           ++it) {
        if (it.value() >= block.startAddress &&
            it.value() <= block.endAddress) {
          result.append(it.key());
        }
      }
      std::sort(result.begin(), result.end());
      break;
    }
  }
  return result;
}

/**
 * @brief Sets the actively highlighted MPI rank in the CFG and linear views.
 * @param rankId Zero-based MPI rank identifier.
 */
void DisassemblyView::setSelectedRank(int rankId) {
  m_selectedRank = rankId;
  if (m_graphMode) {
    rebuildCfgScene();
  }
  if (m_cfgView && m_cfgView->viewport()) {
    m_cfgView->viewport()->update();
  }
}

/**
 * @brief Toggles the visibility of the interactive CFG minimap overlay.
 * @param visible True to paint the minimap overlay.
 */
void DisassemblyView::setGraphMinimapVisible(bool visible) {
  m_graphMinimapVisible = visible;
  if (m_cfgView && m_cfgView->viewport()) {
    m_cfgView->viewport()->update();
  }
}

/**
 * @brief Computes the viewport geometry rectangle for the CFG minimap overlay.
 * @return QRect in viewport coordinates, or empty rect if minimap is disabled.
 */
QRect DisassemblyView::minimapRect() const {
  if (!m_graphMinimapVisible) {
    return QRect();
  }
  const int mmW = 120;
  const int mmH = 90;
  if (m_cfgView && m_cfgView->viewport() &&
      m_cfgView->viewport()->width() >= mmW + 16 &&
      m_cfgView->viewport()->height() >= mmH + 16) {
    const QRect vp = m_cfgView->viewport()->rect();
    // Anchor minimap in the bottom-right corner of the CFG viewport
    return QRect(vp.right() - mmW - 8, vp.bottom() - mmH - 8, mmW, mmH);
  }
  return QRect(8, 8, mmW, mmH);
}

/**
 * @brief Computes the unclipped pixel width for the CFG basic block starting at blockStartAddr.
 *
 * Uses QFontMetrics::horizontalAdvance over every formatted instruction line (including
 * inline comments) and the block header bar (including rank occupancy and deadlock badges)
 * plus 32px horizontal padding so zero text or badges are ever clipped at the right border.
 *
 * @param blockStartAddr 64-bit starting instruction address of the basic block.
 * @return Computed basic-block box width in pixels, or 0 if blockStartAddr is not found.
 */
int DisassemblyView::blockWidthForAddress(uint64_t blockStartAddr) const {
  QFont monoFont(QStringLiteral("JetBrains Mono"), 9);
  monoFont.setStyleHint(QFont::Monospace);
  const QFontMetrics fm(monoFont);

  for (int i = 0; i < m_basicBlocks.size(); ++i) {
    const DisasmBasicBlock &bb = m_basicBlocks[i];
    if (bb.startAddress != blockStartAddr) {
      continue;
    }
    QStringList renderLines;
    if (i == 0) {
      renderLines << QStringLiteral("(fcn) %1").arg(m_functionSignature);
      renderLines << QStringLiteral("; arg int argc @ rbp-0x88");
      renderLines << QStringLiteral("; arg char **argv @ rbp-0x90");
    }
    for (const QString &l : bb.lines) {
      renderLines << l;
    }

    // Measure header title + rank occupancy badge width.
    const QString hdrTitle = QStringLiteral("loc_0x%1  [0x%1 .. 0x%2]")
                                 .arg(bb.startAddress, 6, 16, QChar('0'))
                                 .arg(bb.endAddress, 6, 16, QChar('0'));
    int maxPixelW = fm.horizontalAdvance(hdrTitle) + 96;
    for (const QString &l : renderLines) {
      // Measure full instruction + inline comment line using both exact advance and char-cell floor.
      const int charCellW = static_cast<int>(l.length()) * qMax(7, fm.horizontalAdvance(QLatin1Char('M')));
      maxPixelW = qMax(maxPixelW, qMax(fm.horizontalAdvance(l), charCellW));
    }
    return qMax(280, maxPixelW + 32);
  }
  return 0;
}

/**
 * @brief Constructs the 2D interactive Control Flow Graph (CFG) scene.
 *
 * Traverses basic blocks, formats instruction text items with syntax highlighting
 * (mnemonics, calls, branches, function headers), calculates bounding boxes, adds
 * block headers with address intervals and MPI rank occupancy badges, and connects
 * blocks with orthogonal Manhattan true (green), false (red), and unconditional (blue) edges.
 */
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

  const qreal horizontalCenter = 240.0;
  qreal currentY = 20.0;
  const qreal verticalGap = 40.0;
  QRectF activeBlockRect;

  for (int i = 0; i < m_basicBlocks.size(); ++i) {
    const DisasmBasicBlock &bb = m_basicBlocks[i];
    QStringList renderLines;
    if (i == 0) {
      renderLines << QStringLiteral("(fcn) %1").arg(m_functionSignature);
      renderLines << QStringLiteral("; arg int argc @ rbp-0x88");
      renderLines << QStringLiteral("; arg char **argv @ rbp-0x90");
    }
    for (const QString &l : bb.lines) {
      renderLines << l;
    }

    // Dynamically compute unclipped basic-block width across all instruction + comment lines and header badges.
    const qreal boxW = static_cast<qreal>(blockWidthForAddress(bb.startAddress));
    const qreal headerH = 20.0;
    const qreal lineH = fm.height() + 2.0;
    const qreal boxH = headerH + renderLines.size() * lineH + 8.0;

    // Apply alternating horizontal offsets for compact branch blocks so sibling
    // true/false basic blocks visually separate along the horizontal axis.
    qreal xOffset = 0.0;
    if (i > 0 && bb.lines.size() <= 4 && (i % 2 == 1)) {
      xOffset = -44.0;
    } else if (i > 0 && bb.lines.size() <= 4 && (i % 2 == 0)) {
      xOffset = 44.0;
    }
    const qreal boxX = horizontalCenter - boxW * 0.5 + xOffset;
    const QRectF boxRect(boxX, currentY, boxW, boxH);

    // Determine whether this block is currently active (contains instruction pointer or selected rank).
    // Active blocks receive a #528bff 2px highlight border and #1f3442 header fill.
    const QVector<int> occRanks = ranksAtBlock(bb.startAddress);
    const bool hasSelectedRank = occRanks.contains(m_selectedRank);
    const bool isActiveBlock =
        (m_currentAddress >= bb.startAddress && m_currentAddress <= bb.endAddress) ||
        hasSelectedRank;
    if ((m_currentAddress >= bb.startAddress && m_currentAddress <= bb.endAddress) ||
        activeBlockRect.isNull()) {
      activeBlockRect = boxRect;
    }

    // Outer card container rectangle on #181a1f dark-slate surface.
    auto *rectItem = m_cfgScene->addRect(
        boxRect,
        QPen(isActiveBlock ? QColor("#528bff") : QColor("#333842"),
             isActiveBlock ? 2.0 : 1.0),
        QBrush(QColor("#181a1f")));
    rectItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(bb.startAddress));
    rectItem->setZValue(1.0);

    // Block header bar background strip at top of basic-block node card.
    QRectF headerRect(boxX, currentY, boxW, headerH);
    auto *hdrItem = m_cfgScene->addRect(
        headerRect, QPen(Qt::NoPen),
        QBrush(isActiveBlock ? QColor("#1f3442") : QColor("#21252b")));
    hdrItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(bb.startAddress));
    hdrItem->setZValue(2.0);

    // Block title label displaying symbolic leader label and address interval [start .. end].
    auto *hdrText = m_cfgScene->addSimpleText(
        QStringLiteral("loc_0x%1  [0x%1 .. 0x%2]")
            .arg(bb.startAddress, 6, 16, QChar('0'))
            .arg(bb.endAddress, 6, 16, QChar('0')),
        monoFont);
    hdrText->setBrush(isActiveBlock ? QColor("#ffffff") : QColor("#4ec9b0"));
    hdrText->setPos(boxX + 8.0, currentY + 2.0);
    hdrText->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(bb.startAddress));
    hdrText->setZValue(3.0);

    // Render MPI rank occupancy badge and deadlock cycle alert indicator in block header.
    // Collapses contiguous rank intervals (e.g., [R0-R3 (4)]) for compact high-density display.
    if (!occRanks.isEmpty()) {
      QString rankLabel;
      bool hasDeadlock = false;
      // Check whether any rank paused inside this basic block participates in a wait-for cycle.
      for (int r : occRanks) {
        if (m_deadlockCycleRanks.contains(r)) {
          hasDeadlock = true;
          break;
        }
      }
      // Format single-rank, contiguous-range, or comma-separated rank occupancy badge.
      if (occRanks.size() == 1) {
        rankLabel = QStringLiteral("[R%1]").arg(occRanks.first());
      } else if (occRanks.last() - occRanks.first() + 1 == occRanks.size()) {
        rankLabel = QStringLiteral("[R%1-R%2 (%3)]")
                        .arg(occRanks.first())
                        .arg(occRanks.last())
                        .arg(occRanks.size());
      } else {
        QStringList rStrs;
        for (int r : occRanks) {
          rStrs << QStringLiteral("R%1").arg(r);
        }
        rankLabel = QStringLiteral("[%1]").arg(rStrs.join(','));
      }
      // Prepend explicit [DEADLOCK] warning tag and switch badge to #f44747 crimson when deadlocked.
      if (hasDeadlock) {
        rankLabel = QStringLiteral("[DEADLOCK] ") + rankLabel;
      }
      const QColor badgeCol =
          hasDeadlock ? QColor("#f44747")
                      : (occRanks.contains(0) ? QColor("#dcdcaa") : QColor("#4ec9b0"));
      // Anchor the rank occupancy badge flush-right inside the basic-block header strip.
      auto *rankBadgeText = m_cfgScene->addSimpleText(rankLabel, monoFont);
      rankBadgeText->setBrush(badgeCol);
      const qreal badgeW = rankBadgeText->boundingRect().width();
      rankBadgeText->setPos(boxX + boxW - badgeW - 8.0, currentY + 2.0);
      rankBadgeText->setData(Qt::UserRole,
                             QVariant::fromValue<qulonglong>(bb.startAddress));
      rankBadgeText->setZValue(3.2);
    }

    // Step 1.2: Render individual instruction lines within the basic-block body
    qreal lineY = currentY + headerH + 3.0;
    for (const QString &instLine : renderLines) {
      // Highlight matching decompiler/assembly search token background if active
      if (!m_highlightedToken.isEmpty() &&
          instLine.contains(m_highlightedToken, Qt::CaseInsensitive)) {
        auto *tokBg = m_cfgScene->addRect(
            QRectF(boxX + 4.0, lineY - 1.0, boxW - 8.0, lineH),
            QPen(Qt::NoPen), QBrush(QColor(0x3a, 0x3d, 0x41, 180)));
        tokBg->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(bb.startAddress));
        tokBg->setZValue(2.5);
      }
      auto *txt = m_cfgScene->addSimpleText(instLine, monoFont);
      txt->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(bb.startAddress));

      // Apply syntax palette depending on mnemonic category
      if (instLine.startsWith(QStringLiteral("(fcn)"))) {
        // Function signature definition (yellow)
        txt->setBrush(QColor("#dcdcaa"));
      } else if (instLine.startsWith(';')) {
        // Assembly comments / stack argument notes (green)
        txt->setBrush(QColor("#6a9955"));
      } else if (instLine.contains(
                     QRegularExpression("\\b(je|jne|jg|jl|jmp|jz|jnz)\\b"))) {
        // Control flow conditional branches and jumps (orange-red)
        txt->setBrush(QColor("#ce9178"));
      } else if (instLine.contains(
                     QRegularExpression("\\b(call|ret|leave)\\b"))) {
        // Procedure calls and frame teardown (teal)
        txt->setBrush(QColor("#4ec9b0"));
      } else if (instLine.contains(
                     QRegularExpression("\\b(push|pop|sub|add|lea|mov)\\b"))) {
        // Arithmetic, data movement, and stack ops (blue)
        txt->setBrush(QColor("#569cd6"));
      } else {
        // General instructions and register operands (light slate)
        txt->setBrush(QColor("#d4d7dc"));
      }
      txt->setPos(boxX + 8.0, lineY);
      txt->setZValue(3.0);
      lineY += lineH;
    }

    nodeGeoms.insert(bb.startAddress, NodeGeom{boxRect, bb.startAddress});
    currentY += boxH + verticalGap;
  }

  // Step 2: Route orthogonal Manhattan edge connections between blocks
  for (const DisasmBasicBlock &bb : std::as_const(m_basicBlocks)) {
    if (!nodeGeoms.contains(bb.startAddress)) continue;
    const QRectF srcRect = nodeGeoms.value(bb.startAddress).rect;

    for (uint64_t succAddr : bb.successors) {
      if (!nodeGeoms.contains(succAddr)) continue;
      const QRectF dstRect = nodeGeoms.value(succAddr).rect;

      // Color edges: green for true branch, red for false/fallthrough branch, blue for unconditional
      QColor edgeColor("#569cd6");
      qreal portOffset = 0.0;
      if (bb.trueTarget != 0 && bb.falseTarget != 0) {
        edgeColor = (succAddr == bb.trueTarget) ? QColor("#4ec9b0") : QColor("#f44747");
        portOffset = (succAddr == bb.trueTarget) ? 14.0 : -14.0;
      }

      const QPointF startPt(srcRect.center().x() + portOffset, srcRect.bottom());
      const QPointF endPt(dstRect.center().x(), dstRect.top());

      // Manhattan orthogonal path routing
      QPainterPath path(startPt);
      if (dstRect.top() >= srcRect.bottom()) {
        // Forward edge: step vertically half-way, step horizontally to destination X, step down
        const qreal midY = (startPt.y() + endPt.y()) * 0.5;
        path.lineTo(startPt.x(), midY);
        path.lineTo(endPt.x(), midY);
        path.lineTo(endPt);
      } else {
        // Backward loop edge: route outward to the left margin, loop upward, and re-enter destination
        const qreal loopX = qMin(srcRect.left(), dstRect.left()) - 28.0;
        path.lineTo(startPt.x(), startPt.y() + 12.0);
        path.lineTo(loopX, startPt.y() + 12.0);
        path.lineTo(loopX, endPt.y() - 12.0);
        path.lineTo(endPt.x(), endPt.y() - 12.0);
        path.lineTo(endPt);
      }

      m_cfgScene->addPath(path, QPen(edgeColor, 1.8))->setZValue(0.5);

      // Render triangular arrowhead pointing at destination block top center
      QPolygonF arrowHead;
      arrowHead << endPt << QPointF(endPt.x() - 4.0, endPt.y() - 7.0)
                << QPointF(endPt.x() + 4.0, endPt.y() - 7.0);
      m_cfgScene->addPolygon(arrowHead, QPen(edgeColor), QBrush(edgeColor))->setZValue(2.5);
    }
  }

  // Adjust scene rectangle with padding and center on the active block
  m_cfgScene->setSceneRect(m_cfgScene->itemsBoundingRect().adjusted(-24, -16, 24, 24));
  if (m_cfgView && !activeBlockRect.isNull()) m_cfgView->centerOn(activeBlockRect.center());
}

/**
 * @brief Provides the default canned x86_64 MPI initialization assembly snippet.
 * @return Multi-line disassembly listing featuring an MPI rank initialization loop.
 */
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

}  // namespace gridlock::ui

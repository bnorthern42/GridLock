/**
 * @file DisassemblyAnalysis.cpp
 * @brief Control flow graph analysis, basic-block partitioning, and Xrefs for DisassemblyView.
 *
 * Implements linear assembly parsing into basic blocks and branch arcs, cross-reference
 * resolution (`xrefsToAddress`), user address annotations and bookmarks, and the orthogonal
 * 2D interactive CFG layout and minimap renderer.
 */

#include "DisassemblyView.hpp"
#include "../dialogs/debugger/XrefInspectorDialog.hpp"
#include "../../core/managers/ThemeManager.hpp"

#include <algorithm>
#include <cmath>

#include <QFontMetrics>
#include <QGraphicsPathItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
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

// Finds matching '>' for an outer '<...>' template symbol while skipping C++ operator tokens and '->'.
static int findClosingSymbolAngle(const QString &s, int openIdx) {
  int depth = 1;
  for (int i = openIdx + 1; i < s.size(); ++i) {
    // Skip overloaded operator tokens (<<, >>, <=, >=, <=>, ->*, ->, <, >) that unbalance depth.
    if (s.mid(i).startsWith(QLatin1String("operator")) && (i == openIdx + 1 || (!s[i - 1].isLetterOrNumber() && s[i - 1] != QLatin1Char('_')))) {
      i += 8;
      while (i < s.size() && s[i] == QLatin1Char(' ')) { ++i; }
      for (const QLatin1String op : {QLatin1String("<<="), QLatin1String(">>="), QLatin1String("<=>"), QLatin1String("->*"), QLatin1String("<<"), QLatin1String(">>"), QLatin1String("<="), QLatin1String(">="), QLatin1String("->"), QLatin1String("<"), QLatin1String(">")}) {
        if (s.mid(i).startsWith(op)) { i += op.size() - 1; break; }
      }
      continue;
    }
    if (s[i] == QLatin1Char('>') && i > openIdx + 1 && s[i - 1] == QLatin1Char('-')) { continue; }
    if (s[i] == QLatin1Char('<')) { ++depth; }
    else if (s[i] == QLatin1Char('>') && --depth == 0) { return i; }
  }
  const int colonFallback = s.indexOf(QStringLiteral(">:"), openIdx + 1);
  return (colonFallback > openIdx) ? colonFallback : s.lastIndexOf(QLatin1Char('>'));
}

// Middle-elides long strings while keeping two-thirds prefix context and trailing template/signature characters.
static QString elideMiddleText(const QString &text, int maxLen) {
  if (text.length() <= maxLen || maxLen <= 5) { return text.left(qMax(text.length(), 0) <= maxLen ? text.length() : maxLen); }
  const int keepLeft = ((maxLen - 3) * 2) / 3;
  const int keepRight = (maxLen - 3) - keepLeft;
  return text.left(keepLeft) + QStringLiteral("...") + text.right(keepRight);
}

/**
 * @brief Normalizes a single GDB/objdump disassembly line, cleanly separating template symbols and mnemonics.
 */
QString DisassemblyView::normalizeDisassemblyLine(const QString &rawLine) {
  static const QRegularExpression leadAddrRe(QStringLiteral("^\\s*(=>\\s*)?(0x[0-9a-fA-F]+)\\s*(.*)$"));
  static const QRegularExpression hexOffRe(QStringLiteral("^(.+?)\\+0x([0-9a-fA-F]+)$"));
  // Match optional live RIP chevron + hex address, glued branch/call mnemonics, and unprefixed hex targets.
  static const QRegularExpression xMnemRe(QStringLiteral("^x(jmpq?|callq?|je|jz|jne|jnz|jg|jge|jl|jle|ja|jae|jb|jbe|js|jns|jo|jno)\\b"), QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression gluedRe(QStringLiteral("([)>])(callq?|jmpq?|je|jz|jne|jnz|jg|jge|jl|jle|ja|jae|jb|jbe|js|jns|jo|jno|pushq?|popq?|mov[a-z]*|lea[a-z]*|cmp[a-z]*|test[a-z]*|xor[a-z]*|add[a-z]*|sub[a-z]*|retq?)\\b"), QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression bareTargetRe(QStringLiteral("\\b(jmpq?|callq?|je|jz|jne|jnz|jg|jge|jl|jle|ja|jae|jb|jbe|js|jns|jo|jno)\\s+(?!0x)([0-9a-fA-F]{4,16})\\b"), QRegularExpression::CaseInsensitiveOption);
  const auto lm = leadAddrRe.match(rawLine.trimmed());
  if (!lm.hasMatch()) return rawLine;
  const QString arrow = lm.captured(1).trimmed().isEmpty() ? QString() : QStringLiteral("=> ");
  QString rest = lm.captured(3).trimmed();
  if (rest.startsWith(QLatin1Char('<'))) {
    // Use balanced angle-bracket depth instead of regex [^>]+ so nested STL/MFEM templates parse accurately.
    const int closeIdx = findClosingSymbolAngle(rest, 0);
    if (closeIdx > 0) {
      QString symInner = rest.mid(1, closeIdx - 1).trimmed();
      // Convert hex symbol offset (+0x10 -> +16) cleanly without touching template interior.
      const auto hm = hexOffRe.match(symInner);
      if (hm.hasMatch()) {
        bool ok = false; const uint64_t off = hm.captured(2).toULongLong(&ok, 16);
        if (ok) symInner = QStringLiteral("%1+%2").arg(hm.captured(1)).arg(off);
      }
      QString after = rest.mid(closeIdx + 1).trimmed();
      if (after.startsWith(QLatin1Char(':'))) after = after.mid(1).trimmed();
      after.replace(xMnemRe, QStringLiteral("\\1"));
      rest = QStringLiteral("<%1>: %2").arg(symInner, after);
    }
  }
  // Separate objdump/GDB glued tokens (e.g. "const&)>call" -> "const&)>: call") and add missing 0x prefix.
  rest.replace(gluedRe, QStringLiteral("\\1: \\2"));
  rest.replace(bareTargetRe, QStringLiteral("\\1 0x\\2"));
  return arrow + lm.captured(2) + QLatin1Char(' ') + rest;
}

/**
 * @brief Extracts address, symbol name, mnemonic, and operands using balanced template scanning.
 *
 * Avoids greedy or negated character classes that break on nested C++ template specializations
 * or glued mnemonic tokens after closing angle brackets/parentheses.
 */
bool DisassemblyView::splitDisassemblyLine(const QString &line, uint64_t *outAddr, QString *outSymbol,
                                           QString *outMnemonic, QString *outOperands) {
  static const QRegularExpression leadAddrRe(QStringLiteral("^\\s*(?:=>\\s*)?(0x[0-9a-fA-F]+)\\s*(.*)$"));
  static const QRegularExpression tailOffRe(QStringLiteral("^(.+?)(?:\\+(?:0x[0-9a-fA-F]+|\\d+))$"));
  static const QRegularExpression mnemRegex(QStringLiteral("^([a-zA-Z0-9_\\.]+)\\s*(.*)$"));
  const auto lm = leadAddrRe.match(line);
  if (!lm.hasMatch()) return false;
  bool ok = false; const uint64_t addr = lm.captured(1).toULongLong(&ok, 16);
  if (!ok) return false;
  if (outAddr) *outAddr = addr;
  QString rest = lm.captured(2).trimmed();
  QString sym;
  // Walk nested template angle brackets to separate the enclosing symbol tag from the instruction mnemonic.
  if (rest.startsWith(QLatin1Char('<'))) {
    const int closeIdx = findClosingSymbolAngle(rest, 0);
    if (closeIdx > 0) {
      sym = rest.mid(1, closeIdx - 1).trimmed();
      const auto om = tailOffRe.match(sym);
      if (om.hasMatch()) sym = om.captured(1).trimmed();
      rest = rest.mid(closeIdx + 1).trimmed();
      if (rest.startsWith(QLatin1Char(':'))) rest = rest.mid(1).trimmed();
    }
  } else if (rest.startsWith(QLatin1Char(':'))) rest = rest.mid(1).trimmed();
  if (outSymbol) *outSymbol = sym;
  const auto mm = mnemRegex.match(rest);
  if (outMnemonic) *outMnemonic = mm.hasMatch() ? mm.captured(1).toLower() : QString();
  if (outOperands) *outOperands = mm.hasMatch() ? mm.captured(2) : rest;
  return true;
}

/**
 * @brief Compactly elides long demangled template lines for CFG block rendering while keeping address/mnemonic clear.
 *
 * Enforces bounded card text length while keeping the raw un-elided signature intact for tooltips.
 */
QString DisassemblyView::formatCfgDisplayLine(const QString &rawLine) {
  constexpr int kMaxCfgLineChars = 90;
  if (rawLine.length() <= kMaxCfgLineChars) return rawLine;
  // Middle-elide header function declarations and standalone comment lines.
  if (rawLine.startsWith(QStringLiteral("(fcn)")) || rawLine.startsWith(QLatin1Char(';'))) {
    return elideMiddleText(rawLine, kMaxCfgLineChars);
  }
  static const QRegularExpression leadAddrRe(QStringLiteral("^\\s*(?:=>\\s*)?(0x[0-9a-fA-F]+)\\s*(.*)$"));
  static const QRegularExpression tailDecRe(QStringLiteral("^(.+?)(\\+\\d+)$"));
  const auto lm = leadAddrRe.match(rawLine);
  if (!lm.hasMatch()) return elideMiddleText(rawLine, kMaxCfgLineChars);
  const QString addrStr = lm.captured(1);
  QString rest = lm.captured(2).trimmed(), symPrefix;
  // Bound long template symbol tags (<std::basic_string<...>+32>) before rendering mnemonic/operands.
  if (rest.startsWith(QLatin1Char('<'))) {
    const int closeIdx = findClosingSymbolAngle(rest, 0);
    if (closeIdx > 0) {
      QString tag = rest.mid(1, closeIdx - 1).trimmed();
      // Preserve +offset suffix when shortening long enclosing template names.
      if (tag.length() > 26) {
        const auto tm = tailDecRe.match(tag);
        tag = tm.hasMatch() ? (elideMiddleText(tm.captured(1), qMax(8, 26 - tm.captured(2).length())) + tm.captured(2)) : elideMiddleText(tag, 26);
      }
      symPrefix = QStringLiteral(" <%1>: ").arg(tag);
      rest = rest.mid(closeIdx + 1).trimmed();
      if (rest.startsWith(QLatin1Char(':'))) rest = rest.mid(1).trimmed();
    }
  } else {
    if (rest.startsWith(QLatin1Char(':'))) rest = rest.mid(1).trimmed();
    symPrefix = QStringLiteral(": ");
  }
  // Shorten oversized <callee> template annotations inside operands while keeping trailing "; line NNN".
  const int semiIdx = rest.indexOf(QLatin1Char(';'));
  QString codePart = (semiIdx >= 0) ? rest.left(semiIdx).trimmed() : rest;
  const QString commentPart = (semiIdx >= 0) ? rest.mid(semiIdx).trimmed() : QString();
  const int opAng = codePart.indexOf(QLatin1Char('<'));
  if (opAng >= 0 && codePart.length() > 48) {
    const int opClose = findClosingSymbolAngle(codePart, opAng);
    if (opClose > opAng + 28) {
      codePart = codePart.left(opAng + 1) + elideMiddleText(codePart.mid(opAng + 1, opClose - opAng - 1), 26) + codePart.mid(opClose);
    }
  }
  const int budget = qMax(24, kMaxCfgLineChars - addrStr.length() - symPrefix.length());
  QString body = commentPart.isEmpty() ? codePart : (codePart + QLatin1Char(' ') + commentPart);
  if (body.length() > budget) {
    body = (!commentPart.isEmpty() && commentPart.length() <= 14 && budget > commentPart.length() + 14)
               ? (elideMiddleText(codePart, budget - commentPart.length() - 1) + QLatin1Char(' ') + commentPart)
               : elideMiddleText(body, budget);
  }
  return addrStr + symPrefix + body;
}

/**
 * @brief Parses raw disassembly text, extracting instructions, control-flow arcs, and basic blocks.
 * @param asmCode Raw linear assembly code text.
 */
void DisassemblyView::parseDisassembly(const QString &asmCode) {
  m_branchArcs.clear(); m_basicBlocks.clear(); m_addrToLine.clear(); m_lineToAddr.clear(); m_xrefs.clear();
  struct ParsedInst {
    int lineIndex = -1; uint64_t address = 0; QString rawLine, mnemonic; uint64_t targetAddr = 0;
    bool isUnconditionalBranch = false, isConditionalBranch = false, isRet = false;
  };
  QVector<ParsedInst> instructions;
  const QStringList lines = asmCode.split('\n');
  static const QRegularExpression targetHexRegex(QStringLiteral("(0x[0-9a-fA-F]+)"));
  static const QRegularExpression ripRegex(QStringLiteral("\\[rip\\s*([+-])\\s*(0x[0-9a-fA-F]+)\\]"));
  static const QSet<QString> condBranches = {QStringLiteral("je"), QStringLiteral("jz"), QStringLiteral("jne"), QStringLiteral("jnz"), QStringLiteral("jg"), QStringLiteral("jge"), QStringLiteral("jl"), QStringLiteral("jle"), QStringLiteral("ja"), QStringLiteral("jae"), QStringLiteral("jb"), QStringLiteral("jbe"), QStringLiteral("js"), QStringLiteral("jns"), QStringLiteral("jo"), QStringLiteral("jno")};

  // Step 1: Parse instructions and extract mnemonic, jump targets, and cross-references.
  for (int i = 0; i < lines.size(); ++i) {
    const QString &line = lines[i];
    uint64_t addr = 0; QString symName, mnem, operands;
    if (!splitDisassemblyLine(line, &addr, &symName, &mnem, &operands)) { continue; }
    if (!symName.isEmpty() && instructions.isEmpty()) { m_activeSymbol = QStringLiteral("sym.") + symName; }

    ParsedInst inst{i, addr, line.trimmed(), mnem, 0, false, false, false};
    if (mnem == QStringLiteral("jmp") || mnem == QStringLiteral("jmpq")) { inst.isUnconditionalBranch = true; }
    else if (condBranches.contains(mnem)) { inst.isConditionalBranch = true; }
    else if (mnem == QStringLiteral("ret") || mnem == QStringLiteral("retq")) { inst.isRet = true; }
    if (inst.isUnconditionalBranch || inst.isConditionalBranch) {
      const auto tMatch = targetHexRegex.match(operands);
      if (tMatch.hasMatch()) { bool tok = false; const uint64_t target = tMatch.captured(1).toULongLong(&tok, 16); if (tok) { inst.targetAddr = target; } }
    }
    m_addrToLine.insert(addr, i); m_lineToAddr.insert(i, addr);
    instructions.append(inst);

    // Populate structured outgoing CALL, JMP, CJMP, or RIP-relative DATA xref records.
    if (mnem == QStringLiteral("call") || mnem == QStringLiteral("callq")) {
      const auto tMatch = targetHexRegex.match(operands);
      const uint64_t toAddr = tMatch.hasMatch() ? tMatch.captured(1).toULongLong(nullptr, 16) : 0ULL;
      const int angOpen = operands.indexOf(QLatin1Char('<'));
      const int angClose = (angOpen >= 0) ? findClosingSymbolAngle(operands, angOpen) : -1;
      const QString calleeSym = (angClose > angOpen) ? operands.mid(angOpen + 1, angClose - angOpen - 1) : QString();
      m_xrefs.append(XrefRecord{addr, toAddr, QStringLiteral("CALL"), mnem, inst.rawLine, calleeSym});
    } else if (inst.isUnconditionalBranch || inst.isConditionalBranch) {
      m_xrefs.append(XrefRecord{addr, inst.targetAddr, inst.isUnconditionalBranch ? QStringLiteral("JMP") : QStringLiteral("CJMP"), mnem, inst.rawLine, QString()});
    } else if (const auto ripMatch = ripRegex.match(operands); ripMatch.hasMatch()) {
      const uint64_t offset = ripMatch.captured(2).toULongLong(nullptr, 16);
      const uint64_t eff = (ripMatch.captured(1) == QStringLiteral("+")) ? (addr + 7 + offset) : (addr + 7 - offset);
      m_xrefs.append(XrefRecord{addr, eff, QStringLiteral("DATA"), mnem, inst.rawLine, QString()});
    }
  }

  // Abort basic-block partitioning and reset view badges if no valid instructions were parsed.
  if (instructions.isEmpty()) {
    // Clear symbol, signature, and decompiler output when assembly input is empty.
    m_activeSymbol.clear(); m_functionSignature.clear(); m_decompiledPseudocode.clear(); refreshDecompilerView();
    // Reset header, signature, and basic-block counter badges to idle placeholders.
    if (m_headerBadge) { m_headerBadge->setText(QStringLiteral("DISASM | No binary loaded")); m_headerBadge->setToolTip(QString()); }
    if (m_signatureLabel) { m_signatureLabel->setText(QString()); m_signatureLabel->setToolTip(QString()); }
    if (m_statsBadge) m_statsBadge->setText(QStringLiteral("BB:0 E:0"));
    return;
  }

  // Ensure current address points to a valid instruction in the listing.
  if (!m_addrToLine.contains(m_currentAddress)) m_currentAddress = instructions.first().address;
  // Synthesize an entry-address sub_<addr> label when disassembly lines omit a symbol name.
  if (m_activeSymbol.isEmpty()) m_activeSymbol = QStringLiteral("sub_%1").arg(instructions.first().address, 0, 16);

  // Step 2: Identify basic block leaders (entry point, jump destinations, fallthroughs).
  // Every leader address marks the first instruction of a distinct basic block node.
  QSet<uint64_t> blockLeaders; blockLeaders.insert(instructions.first().address);

  // Scan instruction stream to flag control-transfer source and destination boundaries.
  for (int idx = 0; idx < instructions.size(); ++idx) {
    const ParsedInst &inst = instructions[idx];
    if (inst.isConditionalBranch || inst.isUnconditionalBranch) {
      if (inst.targetAddr != 0 && m_addrToLine.contains(inst.targetAddr)) {
        // Record gutter branch arc linking source line to destination line.
        m_branchArcs.append(BranchArc{inst.lineIndex, m_addrToLine.value(inst.targetAddr), inst.address, inst.targetAddr, inst.isConditionalBranch});
        // Branch target starts a new basic block.
        blockLeaders.insert(inst.targetAddr);
      }
      // Instruction immediately following a branch starts a new basic block.
      if (idx + 1 < instructions.size()) blockLeaders.insert(instructions[idx + 1].address);
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
  m_functionSignature = (hasArgcArgv || m_activeSymbol == QStringLiteral("sym.main"))
                            ? QStringLiteral("int %1(int argc, char **argv)").arg(m_activeSymbol)
                            : QStringLiteral("int %1(void)").arg(m_activeSymbol);
  if (m_signatureLabel) {
    m_signatureLabel->setText(elideMiddleText(m_functionSignature, 92));
    m_signatureLabel->setToolTip(m_functionSignature);
  }
  refreshDecompilerView();
  if (m_headerBadge) {
    m_headerBadge->setText(QStringLiteral("%1 @ 0x%2").arg(elideMiddleText(m_activeSymbol, 44)).arg(m_currentAddress, 6, 16, QChar('0')));
    m_headerBadge->setToolTip(QStringLiteral("%1 @ 0x%2").arg(m_activeSymbol).arg(m_currentAddress, 6, 16, QChar('0')));
  }
  if (m_statsBadge) {
    m_statsBadge->setText(QStringLiteral("BB:%1 E:%2").arg(m_basicBlocks.size()).arg(m_branchArcs.size()));
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
  if (m_graphMode) rebuildCfgScene();
  if (m_cfgView && m_cfgView->viewport()) m_cfgView->viewport()->update();
}

/**
 * @brief Sets the list of MPI ranks participating in a detected deadlock wait-for cycle.
 * @param cycleRanks Vector of MPI rank IDs in the deadlock cycle.
 */
void DisassemblyView::setDeadlockCycleRanks(const QVector<int> &cycleRanks) {
  m_deadlockCycleRanks = cycleRanks;
  if (m_graphMode) rebuildCfgScene();
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
        (blockStartAddr >= block.startAddress && blockStartAddr <= block.endAddress)) {
      for (auto it = m_rankAddresses.cbegin(); it != m_rankAddresses.cend(); ++it) {
        if (it.value() >= block.startAddress && it.value() <= block.endAddress) result.append(it.key());
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
  if (m_graphMode) rebuildCfgScene();
  if (m_cfgView && m_cfgView->viewport()) m_cfgView->viewport()->update();
}

/**
 * @brief Toggles the visibility of the interactive CFG minimap overlay.
 * @param visible True to paint the minimap overlay.
 */
void DisassemblyView::setGraphMinimapVisible(bool visible) {
  m_graphMinimapVisible = visible;
  if (m_cfgView && m_cfgView->viewport()) m_cfgView->viewport()->update();
}

/**
 * @brief Computes the viewport geometry rectangle for the CFG minimap overlay.
 * @return QRect in viewport coordinates, or empty rect if minimap is disabled.
 */
QRect DisassemblyView::minimapRect() const {
  if (!m_graphMinimapVisible) return QRect();
  constexpr int mmW = 120, mmH = 90;
  if (m_cfgView && m_cfgView->viewport() &&
      m_cfgView->viewport()->width() >= mmW + 16 &&
      m_cfgView->viewport()->height() >= mmH + 16) {
    const QRect vp = m_cfgView->viewport()->rect();
    // Anchor minimap in the bottom-right corner of the CFG viewport.
    return QRect(vp.right() - mmW - 8, vp.bottom() - mmH - 8, mmW, mmH);
  }
  return QRect(8, 8, mmW, mmH);
}

/**
 * @brief Collects formatted display lines (and optional raw un-elided lines) for CFG block @p blockIndex.
 * @param blockIndex Zero-based index into m_basicBlocks.
 * @param outRawLines Optional output list populated with un-elided lines for full hover tooltips.
 * @return List of display lines bounded for CFG card width estimation and rendering.
 */
QStringList DisassemblyView::collectCfgBlockLines(int blockIndex, QStringList *outRawLines) const {
  QStringList displayLines, rawLines;
  if (blockIndex < 0 || blockIndex >= m_basicBlocks.size()) return displayLines;
  // Prepend reconstructed function signature and ABI argument hints on entry block 0.
  if (blockIndex == 0) {
    rawLines << QStringLiteral("(fcn) %1").arg(m_functionSignature)
             << QStringLiteral("; arg int argc @ rbp-0x88")
             << QStringLiteral("; arg char **argv @ rbp-0x90");
  }
  for (const QString &l : m_basicBlocks[blockIndex].lines) rawLines << l;
  displayLines.reserve(rawLines.size());
  for (const QString &raw : rawLines) displayLines << formatCfgDisplayLine(raw);
  if (outRawLines) *outRawLines = rawLines;
  return displayLines;
}

/**
 * @brief Computes the unclipped, ceiling-bounded pixel width for the CFG basic block at blockStartAddr.
 *
 * Uses QFontMetrics::horizontalAdvance over every formatted display line, clamped to
 * kMaxCfgBlockWidth (940px) so long demangled STL/MFEM templates never blow out horizontal layout.
 *
 * @param blockStartAddr 64-bit starting instruction address of the basic block.
 * @return Computed basic-block box width in pixels, or 0 if blockStartAddr is not found.
 */
int DisassemblyView::blockWidthForAddress(uint64_t blockStartAddr) const {
  QFont monoFont(QStringLiteral("JetBrains Mono"), 9);
  monoFont.setStyleHint(QFont::Monospace);
  const QFontMetrics fm(monoFont);
  constexpr int kMaxCfgBlockWidth = 940;
  for (int i = 0; i < m_basicBlocks.size(); ++i) {
    const DisasmBasicBlock &bb = m_basicBlocks[i];
    if (bb.startAddress != blockStartAddr) continue;
    const QStringList renderLines = collectCfgBlockLines(i);
    // Measure header title + rank occupancy badge and bounded instruction display lines.
    const QString hdrTitle = QStringLiteral("loc_0x%1  [0x%1 .. 0x%2]")
                                 .arg(bb.startAddress, 6, 16, QChar('0'))
                                 .arg(bb.endAddress, 6, 16, QChar('0'));
    int maxPixelW = fm.horizontalAdvance(hdrTitle) + 96;
    const int charCellUnit = qMax(7, fm.horizontalAdvance(QLatin1Char('M')));
    for (const QString &l : renderLines) {
      const int charCellW = static_cast<int>(l.length()) * charCellUnit;
      maxPixelW = qMax(maxPixelW, qMax(fm.horizontalAdvance(l), charCellW));
    }
    return qBound(280, maxPixelW + 32, kMaxCfgBlockWidth);
  }
  return 0;
}

/**
 * @brief Constructs the 2D interactive Control Flow Graph (CFG) scene.
 */
void DisassemblyView::rebuildCfgScene() {
  if (!m_cfgScene) return;
  m_cfgScene->clear();
  m_activeCfgBlockRect = QRectF();
  m_activeCfgInstY = -1.0;
  if (m_basicBlocks.isEmpty()) return;

  // Use fixed-width 9pt monospace metrics to size CFG cards and instruction columns consistently.
  QFont monoFont("JetBrains Mono", 9);
  monoFont.setStyleHint(QFont::Monospace);
  QFontMetrics fm(monoFont);
  QMap<uint64_t, QRectF> nodeGeoms;
  const qreal horizontalCenter = 240.0;
  qreal currentY = 20.0;
  const qreal verticalGap = 40.0;
  // Resolve theme swatch colors once for all basic-block cards and edges.
  auto &tm = gridlock::core::ThemeManager::instance();
  const bool isDark = tm.isDarkMode();
  const QStringList sw = gridlock::core::ThemeManager::themeSwatchColors(tm.activeThemeId());
  const QColor cardBg(isDark ? QStringLiteral("#181a1f") : sw.value(0, QStringLiteral("#ffffff")));
  const QColor cardBorder(isDark ? QStringLiteral("#333842") : sw.value(2, QStringLiteral("#d0d4dc")));
  const QColor bodyFg(isDark ? QStringLiteral("#d4d7dc") : sw.value(4, QStringLiteral("#1f2328")));
  static const QRegularExpression instAddrRe(QStringLiteral("^\\s*(?:=>\\s*)?(0x[0-9a-fA-F]+)"));

  for (int i = 0; i < m_basicBlocks.size(); ++i) {
    const DisasmBasicBlock &bb = m_basicBlocks[i];
    QStringList rawLines;
    // Fetch bounded display lines alongside raw un-elided text for hover inspection.
    const QStringList renderLines = collectCfgBlockLines(i, &rawLines);
    const qreal boxW = static_cast<qreal>(blockWidthForAddress(bb.startAddress));
    const qreal headerH = 20.0, lineH = fm.height() + 2.0;
    const qreal boxH = headerH + renderLines.size() * lineH + 8.0;

    // Offset short sibling blocks left/right around the vertical centerline to separate true/false branches.
    const qreal xOffset = (i > 0 && bb.lines.size() <= 4) ? ((i % 2 == 1) ? -44.0 : 44.0) : 0.0;
    const qreal boxX = horizontalCenter - boxW * 0.5 + xOffset;
    const QRectF boxRect(boxX, currentY, boxW, boxH);

    // Highlight block containing active RIP or selected MPI rank.
    const QVector<int> occRanks = ranksAtBlock(bb.startAddress);
    const bool isActiveBlock = (m_currentAddress >= bb.startAddress && m_currentAddress <= bb.endAddress) ||
                               occRanks.contains(m_selectedRank);
    if ((m_currentAddress >= bb.startAddress && m_currentAddress <= bb.endAddress) || m_activeCfgBlockRect.isNull()) {
      m_activeCfgBlockRect = boxRect;
    }

    // Draw outer block border/background and attach un-elided full listing as hover tooltip.
    const QColor hdrBg(isDark ? (isActiveBlock ? QStringLiteral("#1f3442") : QStringLiteral("#21252b"))
                              : sw.value(1, QStringLiteral("#e6e9ef")));
    auto *rectItem = m_cfgScene->addRect(
        boxRect, QPen(isActiveBlock ? QColor("#528bff") : cardBorder, isActiveBlock ? 2.0 : 1.0), QBrush(cardBg));
    rectItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(bb.startAddress));
    rectItem->setZValue(1.0);
    if (!rawLines.isEmpty()) rectItem->setToolTip(rawLines.join(QLatin1Char('\n')));

    // Draw filled title header bar at top of basic-block node.
    auto *hdrItem = m_cfgScene->addRect(QRectF(boxX, currentY, boxW, headerH), QPen(Qt::NoPen), QBrush(hdrBg));
    hdrItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(bb.startAddress));
    hdrItem->setZValue(2.0);

    // Block title label displaying symbolic leader label and [start .. end].
    auto *hdrText = m_cfgScene->addSimpleText(
        QStringLiteral("loc_0x%1  [0x%1 .. 0x%2]").arg(bb.startAddress, 6, 16, QChar('0')).arg(bb.endAddress, 6, 16, QChar('0')),
        monoFont);
    hdrText->setBrush(isActiveBlock ? (isDark ? QColor("#ffffff") : QColor("#0969da"))
                                    : (isDark ? QColor("#4ec9b0") : QColor("#116329")));
    hdrText->setPos(boxX + 8.0, currentY + 2.0);
    hdrText->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(bb.startAddress));
    hdrText->setZValue(3.0);

    // Render MPI rank occupancy badge and deadlock indicator in block header.
    if (!occRanks.isEmpty()) {
      bool hasDeadlock = false;
      for (int r : occRanks) {
        if (m_deadlockCycleRanks.contains(r)) { hasDeadlock = true; break; }
      }
      QString rankLabel;
      if (occRanks.size() == 1) {
        rankLabel = QStringLiteral("[R%1]").arg(occRanks.first());
      } else if (occRanks.last() - occRanks.first() + 1 == occRanks.size()) {
        rankLabel = QStringLiteral("[R%1-R%2 (%3)]").arg(occRanks.first()).arg(occRanks.last()).arg(occRanks.size());
      } else {
        QStringList rStrs;
        for (int r : occRanks) rStrs << QStringLiteral("R%1").arg(r);
        rankLabel = QStringLiteral("[%1]").arg(rStrs.join(','));
      }
      if (hasDeadlock) rankLabel = QStringLiteral("[DEADLOCK] ") + rankLabel;
      auto *rankBadgeText = m_cfgScene->addSimpleText(rankLabel, monoFont);
      rankBadgeText->setBrush(hasDeadlock ? QColor("#f44747") : (occRanks.contains(0) ? QColor("#dcdcaa") : QColor("#4ec9b0")));
      rankBadgeText->setPos(boxX + boxW - rankBadgeText->boundingRect().width() - 8.0, currentY + 2.0);
      rankBadgeText->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(bb.startAddress));
      rankBadgeText->setZValue(3.2);
    }

    // Render instruction lines elided to card width while preserving full raw text in tooltips.
    qreal lineY = currentY + headerH + 3.0;
    const int maxTextPx = qMax(120, static_cast<int>(boxW) - 18);
    for (int li = 0; li < renderLines.size(); ++li) {
      const QString &instLine = renderLines[li];
      const QString &rawLine = rawLines.value(li, instLine);
      uint64_t instAddr = 0ULL;
      const QRegularExpressionMatch addrMatch = instAddrRe.match(instLine);
      if (addrMatch.hasMatch()) {
        bool ok = false;
        instAddr = addrMatch.captured(1).toULongLong(&ok, 16);
      }
      const bool isCurrentInst = (instAddr != 0ULL && instAddr == m_currentAddress);
      const bool isBookmarkedInst = (instAddr != 0ULL && m_bookmarks.contains(instAddr));
      if (isCurrentInst) m_activeCfgInstY = lineY + lineH * 0.5;

      const qreal cy = lineY + lineH * 0.5 - 1.0, bx = boxX + 9.0;
      if (isCurrentInst || isBookmarkedInst) {
        auto *rowBg = m_cfgScene->addRect(
            QRectF(boxX + 2.0, lineY - 1.0, boxW - 4.0, lineH),
            isCurrentInst ? QPen(QColor("#4ec9b0"), 1.0) : QPen(Qt::NoPen),
            QBrush(isCurrentInst ? (isDark ? QColor("#20342c") : QColor(46, 160, 67, 45)) : QColor(224, 108, 117, 25)));
        rowBg->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(instAddr));
        rowBg->setZValue(isCurrentInst ? 2.4 : 2.3);
      } else if (!m_highlightedToken.isEmpty() && rawLine.contains(m_highlightedToken, Qt::CaseInsensitive)) {
        auto *tokBg = m_cfgScene->addRect(QRectF(boxX + 4.0, lineY - 1.0, boxW - 8.0, lineH),
                                          QPen(Qt::NoPen), QBrush(QColor(0x3a, 0x3d, 0x41, 180)));
        tokBg->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(bb.startAddress));
        tokBg->setZValue(2.5);
      }
      if (isBookmarkedInst) {
        m_cfgScene->addEllipse(QRectF(bx - 4.5, cy - 4.5, 9.0, 9.0), QPen(QColor("#ff949d"), 1.1),
                               QBrush(isCurrentInst ? QColor("#24181c") : QColor("#e06c75")))->setZValue(3.1);
      }
      if (isCurrentInst) {
        const QPolygonF chev({QPointF(bx - 2.5, cy - 3.2), QPointF(bx + 3.2, cy),
                              QPointF(bx - 2.5, cy + 3.2), QPointF(bx - 1.0, cy)});
        m_cfgScene->addPolygon(chev, QPen(QColor("#98c379"), 0.8), QBrush(QColor("#4ec9b0")))->setZValue(3.2);
      }

      const QString prefix = (isCurrentInst && isBookmarkedInst) ? QStringLiteral("●▶ ")
                           : (isCurrentInst ? QStringLiteral("▶ ") : (isBookmarkedInst ? QStringLiteral("● ") : QString()));
      const QString fittedLine = fm.elidedText(prefix + instLine, Qt::ElideRight, maxTextPx);
      auto *txt = m_cfgScene->addSimpleText(fittedLine, monoFont);
      txt->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(instAddr != 0ULL ? instAddr : bb.startAddress));
      if (rawLine != instLine || fittedLine != (prefix + instLine)) txt->setToolTip(rawLine);

      // Apply syntax palette depending on active RIP status or mnemonic category.
      if (isCurrentInst) txt->setBrush(isDark ? QColor("#ffffff") : bodyFg);
      else if (instLine.startsWith(QStringLiteral("(fcn)"))) txt->setBrush(isDark ? QColor("#dcdcaa") : QColor("#9a6700"));
      else if (instLine.startsWith(';')) txt->setBrush(isDark ? QColor("#6a9955") : QColor("#116329"));
      else if (instLine.contains(QRegularExpression("\\b(je|jne|jg|jl|jmp|jz|jnz)\\b"))) txt->setBrush(isDark ? QColor("#ce9178") : QColor("#cf222e"));
      else if (instLine.contains(QRegularExpression("\\b(call|ret|leave)\\b"))) txt->setBrush(isDark ? QColor("#4ec9b0") : QColor("#0550ae"));
      else if (instLine.contains(QRegularExpression("\\b(push|pop|sub|add|lea|mov)\\b"))) txt->setBrush(isDark ? QColor("#569cd6") : QColor("#0969da"));
      else txt->setBrush(bodyFg);
      txt->setPos(boxX + 8.0, lineY);
      txt->setZValue(3.0);
      lineY += lineH;
    }

    nodeGeoms.insert(bb.startAddress, boxRect);
    currentY += boxH + verticalGap;
  }

  // Route orthogonal Manhattan edge connections between blocks.
  for (const DisasmBasicBlock &bb : std::as_const(m_basicBlocks)) {
    if (!nodeGeoms.contains(bb.startAddress)) continue;
    const QRectF srcRect = nodeGeoms.value(bb.startAddress);
    for (uint64_t succAddr : bb.successors) {
      if (!nodeGeoms.contains(succAddr)) continue;
      const QRectF dstRect = nodeGeoms.value(succAddr);
      QColor edgeColor("#569cd6");
      qreal portOffset = 0.0;
      if (bb.trueTarget != 0 && bb.falseTarget != 0) {
        edgeColor = (succAddr == bb.trueTarget) ? QColor("#4ec9b0") : QColor("#f44747");
        portOffset = (succAddr == bb.trueTarget) ? 14.0 : -14.0;
      }
      const QPointF startPt(srcRect.center().x() + portOffset, srcRect.bottom());
      const QPointF endPt(dstRect.center().x(), dstRect.top());
      QPainterPath path(startPt);
      if (dstRect.top() >= srcRect.bottom()) {
        const qreal midY = (startPt.y() + endPt.y()) * 0.5;
        path.lineTo(startPt.x(), midY);
        path.lineTo(endPt.x(), midY);
        path.lineTo(endPt);
      } else {
        const qreal loopX = qMin(srcRect.left(), dstRect.left()) - 28.0;
        path.lineTo(startPt.x(), startPt.y() + 12.0);
        path.lineTo(loopX, startPt.y() + 12.0);
        path.lineTo(loopX, endPt.y() - 12.0);
        path.lineTo(endPt.x(), endPt.y() - 12.0);
        path.lineTo(endPt);
      }
      m_cfgScene->addPath(path, QPen(edgeColor, 1.8))->setZValue(0.5);
      QPolygonF arrowHead;
      arrowHead << endPt << QPointF(endPt.x() - 4.0, endPt.y() - 7.0) << QPointF(endPt.x() + 4.0, endPt.y() - 7.0);
      m_cfgScene->addPolygon(arrowHead, QPen(edgeColor), QBrush(edgeColor))->setZValue(2.5);
    }
  }

  // Pad sceneRect generously around all blocks so leftmost edges are always scrollable, then align left edge.
  m_cfgScene->setSceneRect(m_cfgScene->itemsBoundingRect().adjusted(-80, -40, 80, 48));
  alignCfgViewportToActiveBlock(false);
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

/**
 * @brief Anchors the CFG viewport so the active basic block's left edge remains visible without clipping.
 *
 * When forceLeftAnchor is true (e.g. Home key) or the active block exceeds viewportWidth - 48px,
 * sets horizontal scroll so the left edge sits at +24px inside the viewport instead of centering
 * horizontally on wide template signatures. Vertical tracking centers on the active PC row.
 *
 * @param forceLeftAnchor True to force left-edge snap regardless of block width.
 */
void DisassemblyView::alignCfgViewportToActiveBlock(bool forceLeftAnchor) {
  if (!m_cfgView || !m_cfgScene || m_activeCfgBlockRect.isNull()) return;
  const QWidget *vp = m_cfgView->viewport();
  const int vpW = vp ? vp->width() : m_cfgView->width();
  const int vpH = vp ? vp->height() : m_cfgView->height();
  if (vpW <= 0 || vpH <= 0) return;
  // Determine whether wide basic block or explicit Home request requires left-edge alignment.
  const bool anchorLeft = forceLeftAnchor || (m_activeCfgBlockRect.width() > static_cast<qreal>(vpW - 48));
  const qreal targetCenterX = anchorLeft ? (m_activeCfgBlockRect.left() - 24.0 + static_cast<qreal>(vpW) * 0.5)
                                         : m_activeCfgBlockRect.center().x();
  qreal targetCenterY = m_activeCfgBlockRect.center().y();
  // Keep active instruction chevron inside vertical viewport bounds for tall basic blocks.
  if (m_activeCfgInstY >= 0.0 && m_activeCfgBlockRect.height() > static_cast<qreal>(vpH - 32)) {
    targetCenterY = qBound(m_activeCfgBlockRect.top() + static_cast<qreal>(vpH) * 0.35, m_activeCfgInstY, m_activeCfgBlockRect.bottom());
  }
  // Suppress scrollbar user-scroll tracking while applying programmatic alignment.
  m_aligningCfgViewport = true;
  m_cfgView->centerOn(targetCenterX, targetCenterY);
  m_aligningCfgViewport = false;
}

}  // namespace gridlock::ui

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

  // Reconstruct function signature header from symbol and prologue arguments
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

  refreshDecompilerView();

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

void DisassemblyView::setDecompilerTargetLanguage(const QString &lang) {
  m_targetLang =
      lang.trimmed().isEmpty() ? QStringLiteral("C") : lang.trimmed();
  if (m_targetLangCombo) {
    for (int i = 0; i < m_targetLangCombo->count(); ++i) {
      if (m_targetLangCombo->itemData(i).toString().compare(
              m_targetLang, Qt::CaseInsensitive) == 0 ||
          m_targetLangCombo->itemText(i).compare(
              m_targetLang, Qt::CaseInsensitive) == 0) {
        if (m_targetLangCombo->currentIndex() != i) {
          m_targetLangCombo->blockSignals(true);
          m_targetLangCombo->setCurrentIndex(i);
          m_targetLangCombo->blockSignals(false);
        }
        break;
      }
    }
  }
  refreshDecompilerView();
}

void DisassemblyView::refreshDecompilerView() {
  m_decompiledPseudocode = decompileToLanguage(m_targetLang);
  if (m_decompilerEdit) {
    m_updatingDecompilerCursor = true;
    m_decompilerEdit->setPlainText(m_decompiledPseudocode);
    m_updatingDecompilerCursor = false;
  }
  const int mappedLine = addressToDecompilerLine(m_currentAddress);
  if (mappedLine > 0) {
    m_currentDecompilerLine = mappedLine;
  }
  highlightActiveDecompilerLine();
}

uint64_t DisassemblyView::decompilerLineToAddress(int lineNumber) const {
  return m_decompilerLineToAddr.value(lineNumber, 0ULL);
}

int DisassemblyView::addressToDecompilerLine(uint64_t address) const {
  if (m_addrToDecompilerLine.contains(address)) {
    return m_addrToDecompilerLine.value(address);
  }
  for (const DisasmBasicBlock &bb : m_basicBlocks) {
    if (address >= bb.startAddress && address <= bb.endAddress) {
      if (m_addrToDecompilerLine.contains(bb.startAddress)) {
        return m_addrToDecompilerLine.value(bb.startAddress);
      }
      for (auto it = m_addrToDecompilerLine.constBegin();
           it != m_addrToDecompilerLine.constEnd(); ++it) {
        if (it.key() >= bb.startAddress && it.key() <= bb.endAddress) {
          return it.value();
        }
      }
    }
  }
  return -1;
}

bool DisassemblyView::selectDecompilerLine(int lineNumber) {
  if (!m_decompilerLineToAddr.contains(lineNumber)) {
    return false;
  }
  m_currentDecompilerLine = lineNumber;
  const uint64_t addr = m_decompilerLineToAddr.value(lineNumber);
  seekToAddress(addr);
  emit addressSelected(addr);
  return true;
}

void DisassemblyView::highlightToken(const QString &token) {
  m_highlightedToken = token.trimmed();
  highlightActiveInstruction();
  highlightActiveDecompilerLine();
  if (m_graphMode) {
    rebuildCfgScene();
  }
}

QString DisassemblyView::decompileToLanguage(const QString &lang) const {
  const QString normLang =
      lang.trimmed().isEmpty() ? QStringLiteral("C") : lang.trimmed();
  const bool isCpp =
      (normLang.compare(QStringLiteral("C++"), Qt::CaseInsensitive) == 0 ||
       normLang.contains(QStringLiteral("C++"), Qt::CaseInsensitive));
  const bool isPseudo =
      (normLang.compare(QStringLiteral("Pseudocode"), Qt::CaseInsensitive) == 0 ||
       normLang.compare(QStringLiteral("Rust"), Qt::CaseInsensitive) == 0 ||
       normLang.contains(QStringLiteral("Pseudo"), Qt::CaseInsensitive));

  static const QRegularExpression stackVarRe(
      QStringLiteral("\\[rbp-(0x[0-9a-fA-F]+)\\]"));
  static const QRegularExpression callSymRe(
      QStringLiteral("<([^+>]+?)(?:@plt)?(?:\\+\\d+)?>"));
  static const QRegularExpression lineAddrRe(
      QStringLiteral("^\\s*(?:=>\\s*)?(0x[0-9a-fA-F]+)"));

  QSet<QString> stackOffsets;
  QMap<QString, uint64_t> stackOffsetFirstAddr;
  QStringList callsFound;
  bool hasLoopBackEdge = false;
  bool hasCondBranch = false;

  for (const auto &arc : m_branchArcs) {
    if (arc.toAddr <= arc.fromAddr) {
      hasLoopBackEdge = true;
    }
    if (arc.isConditional) {
      hasCondBranch = true;
    }
  }

  for (const DisasmBasicBlock &bb : m_basicBlocks) {
    for (const QString &line : bb.lines) {
      uint64_t lineAddr = bb.startAddress;
      QRegularExpressionMatch am = lineAddrRe.match(line);
      if (am.hasMatch()) {
        bool ok = false;
        const uint64_t parsed = am.captured(1).toULongLong(&ok, 16);
        if (ok) {
          lineAddr = parsed;
        }
      }
      QRegularExpressionMatch sm = stackVarRe.match(line);
      if (sm.hasMatch()) {
        const QString off = sm.captured(1).mid(2);
        stackOffsets.insert(off);
        if (!stackOffsetFirstAddr.contains(off)) {
          stackOffsetFirstAddr.insert(off, lineAddr);
        }
      }
      if (line.contains(QStringLiteral("call"))) {
        QRegularExpressionMatch cm = callSymRe.match(line);
        if (cm.hasMatch()) {
          callsFound.append(cm.captured(1));
        }
      }
    }
  }

  QStringList sortedOffsets = stackOffsets.values();
  std::sort(sortedOffsets.begin(), sortedOffsets.end());

  m_decompilerLineToAddr.clear();
  m_addrToDecompilerLine.clear();

  QStringList out;
  auto emitLine = [&](const QString &text, uint64_t addr, bool isPrimaryStmt) {
    out << text;
    const int lineNum = out.size();
    if (addr != 0ULL) {
      m_decompilerLineToAddr.insert(lineNum, addr);
      if (isPrimaryStmt || !m_addrToDecompilerLine.contains(addr)) {
        m_addrToDecompilerLine.insert(addr, lineNum);
      }
    }
  };

  const uint64_t entryAddr =
      m_basicBlocks.isEmpty() ? 0x00401120ULL : m_basicBlocks.first().startAddress;

  if (isCpp) {
    emitLine(QStringLiteral("// DECOMPILED C++ / MPI | %1").arg(m_activeSymbol), entryAddr, false);
    emitLine(QStringLiteral("#include <mpi.h>"), entryAddr, false);
    emitLine(QStringLiteral("#include <cstdint>"), entryAddr, false);
    emitLine(QStringLiteral("int %1(int argc, char **argv) {").arg(m_activeSymbol), entryAddr, true);
    for (const QString &off : sortedOffsets) {
      emitLine(QStringLiteral("  std::int32_t var_%1h = 0; // [rbp-0x%1]").arg(off),
               stackOffsetFirstAddr.value(off, entryAddr), false);
    }
  } else if (isPseudo) {
    emitLine(QStringLiteral("// DECOMPILED PSEUDOCODE & XREFS | %1").arg(m_activeSymbol), entryAddr, false);
    emitLine(QStringLiteral("proc %1(argc, argv):").arg(m_activeSymbol), entryAddr, true);
    for (const QString &off : sortedOffsets) {
      emitLine(QStringLiteral("  local var_%1h := 0").arg(off),
               stackOffsetFirstAddr.value(off, entryAddr), false);
    }
  } else {
    emitLine(QStringLiteral("/* DECOMPILED APPROX C & XREFS | %1 */").arg(m_activeSymbol), entryAddr, false);
    emitLine(QStringLiteral("#include <stdint.h>"), entryAddr, false);
    emitLine(QStringLiteral("int %1(int argc, char **argv) {").arg(m_activeSymbol), entryAddr, true);
    for (const QString &off : sortedOffsets) {
      emitLine(QStringLiteral("  int32_t var_%1h = 0; /* [rbp-0x%1] */").arg(off),
               stackOffsetFirstAddr.value(off, entryAddr), false);
    }
  }

  for (const DisasmBasicBlock &bb : m_basicBlocks) {
    const QVector<uint64_t> inXrefs = xrefsToAddress(bb.startAddress);
    if (!inXrefs.isEmpty()) {
      QStringList callerStrs;
      for (uint64_t cAddr : inXrefs) {
        callerStrs
            << QStringLiteral("0x%1 (jmp)").arg(cAddr, 8, 16, QChar('0'));
      }
      emitLine(QStringLiteral("  // XREFS TO 0x%1: %2")
                   .arg(bb.startAddress, 8, 16, QChar('0'))
                   .arg(callerStrs.join(QStringLiteral(", "))),
               bb.startAddress, false);
    }
    emitLine(QStringLiteral("loc_0x%1:").arg(bb.startAddress, 8, 16, QChar('0')),
             bb.startAddress, true);

    int blockFirstStmtLine = out.size();
    QVector<uint64_t> unmappedBlockAddrs;

    for (const QString &instLine : bb.lines) {
      uint64_t instAddr = bb.startAddress;
      QRegularExpressionMatch am = lineAddrRe.match(instLine);
      if (am.hasMatch()) {
        bool ok = false;
        const uint64_t parsed = am.captured(1).toULongLong(&ok, 16);
        if (ok) {
          instAddr = parsed;
        }
      }

      if (instLine.contains(QStringLiteral("call")) &&
          instLine.contains(QStringLiteral("MPI_Init"))) {
        if (isCpp) {
          emitLine(QStringLiteral("  MPI::Init(argc, argv); // MPI_Init(&argc, &argv)"), instAddr, true);
        } else if (isPseudo) {
          emitLine(QStringLiteral("  call MPI_Init(&argc, &argv)"), instAddr, true);
        } else {
          emitLine(QStringLiteral("  MPI_Init(&argc, &argv);"), instAddr, true);
        }
        blockFirstStmtLine = out.size();
      } else if (instLine.contains(QStringLiteral("call")) &&
                 instLine.contains(QStringLiteral("MPI_Finalize"))) {
        if (isCpp) {
          emitLine(QStringLiteral("  MPI::Finalize();"), instAddr, true);
        } else if (isPseudo) {
          emitLine(QStringLiteral("  call MPI_Finalize()"), instAddr, true);
        } else {
          emitLine(QStringLiteral("  MPI_Finalize();"), instAddr, true);
        }
        blockFirstStmtLine = out.size();
      } else if (instLine.contains(QStringLiteral("jg "))) {
        if (isPseudo) {
          emitLine(QStringLiteral("  while (var_4h <= 2) do:"), instAddr, true);
        } else {
          emitLine(QStringLiteral("  while (var_4h <= 2) { if (var_4h > 2) goto loc_0x%1; }")
                       .arg(bb.trueTarget, 8, 16, QChar('0')),
                   instAddr, true);
        }
        m_addrToDecompilerLine.insert(bb.startAddress, out.size());
        m_decompilerLineToAddr.insert(out.size(), bb.startAddress);
        blockFirstStmtLine = out.size();
      } else if (instLine.contains(QStringLiteral("je "))) {
        if (isPseudo) {
          emitLine(QStringLiteral("  if (var_4h == 0) then goto loc_0x%1")
                       .arg(bb.trueTarget, 8, 16, QChar('0')),
                   instAddr, true);
        } else {
          emitLine(QStringLiteral("  if (var_4h == 0) { goto loc_0x%1; } else { var_4h += 1; }")
                       .arg(bb.trueTarget, 8, 16, QChar('0')),
                   instAddr, true);
        }
        blockFirstStmtLine = out.size();
      } else if (instLine.contains(QStringLiteral("jmp "))) {
        if (isPseudo) {
          emitLine(QStringLiteral("  jump loc_0x%1")
                       .arg(bb.trueTarget, 8, 16, QChar('0')),
                   instAddr, true);
        } else {
          emitLine(QStringLiteral("  goto loc_0x%1;")
                       .arg(bb.trueTarget, 8, 16, QChar('0')),
                   instAddr, true);
        }
        blockFirstStmtLine = out.size();
      } else if (instLine.contains(QStringLiteral("ret"))) {
        emitLine(isPseudo ? QStringLiteral("  return 0")
                          : QStringLiteral("  return 0;"),
                 instAddr, true);
        blockFirstStmtLine = out.size();
      } else {
        unmappedBlockAddrs.append(instAddr);
      }
    }

    for (uint64_t uAddr : unmappedBlockAddrs) {
      if (!m_addrToDecompilerLine.contains(uAddr)) {
        m_addrToDecompilerLine.insert(uAddr, blockFirstStmtLine);
      }
    }
  }

  if (!hasLoopBackEdge && !hasCondBranch && m_basicBlocks.isEmpty()) {
    emitLine(QStringLiteral("  int32_t var_4h = 0;"), entryAddr, false);
    emitLine(QStringLiteral("  if (var_4h == 0) { MPI_Init(&argc, &argv); }"), entryAddr, true);
  }
  if (!isPseudo) {
    const uint64_t endAddr =
        m_basicBlocks.isEmpty() ? entryAddr : m_basicBlocks.last().endAddress;
    emitLine(QStringLiteral("}"), endAddr, false);
  }
  return out.join('\n');
}

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

void DisassemblyView::setSelectedRank(int rankId) {
  m_selectedRank = rankId;
  if (m_graphMode) {
    rebuildCfgScene();
  }
  if (m_cfgView && m_cfgView->viewport()) {
    m_cfgView->viewport()->update();
  }
}

void DisassemblyView::setGraphMinimapVisible(bool visible) {
  m_graphMinimapVisible = visible;
  if (m_cfgView && m_cfgView->viewport()) {
    m_cfgView->viewport()->update();
  }
}

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
    return QRect(vp.right() - mmW - 8, vp.bottom() - mmH - 8, mmW, mmH);
  }
  return QRect(8, 8, mmW, mmH);
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

    int maxChars = 28;
    for (const QString &l : renderLines) {
      maxChars = qMax(maxChars, static_cast<int>(l.length()));
    }

    const qreal boxW =
        qBound(260.0, maxChars * fm.horizontalAdvance('M') + 20.0, 420.0);
    const qreal headerH = 20.0;
    const qreal lineH = fm.height() + 2.0;
    const qreal boxH = headerH + renderLines.size() * lineH + 8.0;

    qreal xOffset = 0.0;
    if (i > 0 && bb.lines.size() <= 4 && (i % 2 == 1)) {
      xOffset = -44.0;
    } else if (i > 0 && bb.lines.size() <= 4 && (i % 2 == 0)) {
      xOffset = 44.0;
    }
    const qreal boxX = horizontalCenter - boxW * 0.5 + xOffset;
    const QRectF boxRect(boxX, currentY, boxW, boxH);

    const QVector<int> occRanks = ranksAtBlock(bb.startAddress);
    const bool hasSelectedRank = occRanks.contains(m_selectedRank);
    const bool isActiveBlock =
        (m_currentAddress >= bb.startAddress && m_currentAddress <= bb.endAddress) ||
        hasSelectedRank;
    if ((m_currentAddress >= bb.startAddress && m_currentAddress <= bb.endAddress) ||
        activeBlockRect.isNull()) {
      activeBlockRect = boxRect;
    }

    auto *rectItem = m_cfgScene->addRect(
        boxRect,
        QPen(isActiveBlock ? QColor("#528bff") : QColor("#333842"),
             isActiveBlock ? 2.0 : 1.0),
        QBrush(QColor("#181a1f")));
    rectItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(bb.startAddress));
    rectItem->setZValue(1.0);

    QRectF headerRect(boxX, currentY, boxW, headerH);
    auto *hdrItem = m_cfgScene->addRect(
        headerRect, QPen(Qt::NoPen),
        QBrush(isActiveBlock ? QColor("#1f3442") : QColor("#21252b")));
    hdrItem->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(bb.startAddress));
    hdrItem->setZValue(2.0);

    auto *hdrText = m_cfgScene->addSimpleText(
        QStringLiteral("loc_0x%1  [0x%1 .. 0x%2]")
            .arg(bb.startAddress, 6, 16, QChar('0'))
            .arg(bb.endAddress, 6, 16, QChar('0')),
        monoFont);
    hdrText->setBrush(isActiveBlock ? QColor("#ffffff") : QColor("#4ec9b0"));
    hdrText->setPos(boxX + 8.0, currentY + 2.0);
    hdrText->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(bb.startAddress));
    hdrText->setZValue(3.0);

    if (!occRanks.isEmpty()) {
      QString rankLabel;
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
      const QColor badgeCol =
          occRanks.contains(0) ? QColor("#dcdcaa") : QColor("#4ec9b0");
      auto *rankBadgeText = m_cfgScene->addSimpleText(rankLabel, monoFont);
      rankBadgeText->setBrush(badgeCol);
      const qreal badgeW = rankBadgeText->boundingRect().width();
      rankBadgeText->setPos(boxX + boxW - badgeW - 8.0, currentY + 2.0);
      rankBadgeText->setData(Qt::UserRole,
                             QVariant::fromValue<qulonglong>(bb.startAddress));
      rankBadgeText->setZValue(3.2);
    }

    qreal lineY = currentY + headerH + 3.0;
    for (const QString &instLine : renderLines) {
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
      if (instLine.startsWith(QStringLiteral("(fcn)"))) {
        txt->setBrush(QColor("#dcdcaa"));
      } else if (instLine.startsWith(';')) {
        txt->setBrush(QColor("#6a9955"));
      } else if (instLine.contains(
                     QRegularExpression("\\b(je|jne|jg|jl|jmp|jz|jnz)\\b"))) {
        txt->setBrush(QColor("#ce9178"));
      } else if (instLine.contains(
                     QRegularExpression("\\b(call|ret|leave)\\b"))) {
        txt->setBrush(QColor("#4ec9b0"));
      } else if (instLine.contains(
                     QRegularExpression("\\b(push|pop|sub|add|lea|mov)\\b"))) {
        txt->setBrush(QColor("#569cd6"));
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
      qreal portOffset = 0.0;
      if (bb.trueTarget != 0 && bb.falseTarget != 0) {
        if (succAddr == bb.trueTarget) {
          edgeColor = QColor("#4ec9b0");
          portOffset = 14.0;
        } else {
          edgeColor = QColor("#f44747");
          portOffset = -14.0;
        }
      }

      QPointF startPt(srcRect.center().x() + portOffset, srcRect.bottom());
      QPointF endPt(dstRect.center().x(), dstRect.top());

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

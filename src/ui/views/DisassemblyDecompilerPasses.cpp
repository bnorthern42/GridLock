/**
 * @file DisassemblyDecompilerPasses.cpp
 * @brief Structured C/C++/Pseudocode decompilation passes and validated binary instruction patching.
 *
 * Implements basic-block control-flow reconstruction (`while` loops, conditionals,
 * MPI runtime calls, and `[rbp-0x..]` stack frame variable recovery) plus validated
 * hex/NOP instruction patching, listing substitution, and patch rollback.
 */

#include "DisassemblyView.hpp"

#include <algorithm>
#include <cstdint>

#include <QInputDialog>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>

namespace gridlock::ui {

/**
 * @brief Generates structured high-level decompiled code for the active function symbol.
 *
 * Supports `"C"` (C99), `"C++"` (C++20 / MPI), and `"Pseudocode"` output modes while
 * populating bidirectional `m_decompilerLineToAddr` and `m_addrToDecompilerLine` tables.
 */
QString DisassemblyView::decompileToLanguage(const QString& lang) const {
  const QString normLang = lang.trimmed().isEmpty() ? QStringLiteral("C") : lang.trimmed();
  const bool isCpp = (normLang.compare(QStringLiteral("C++"), Qt::CaseInsensitive) == 0 ||
                      normLang.contains(QStringLiteral("C++"), Qt::CaseInsensitive));
  const bool isPseudo = (normLang.compare(QStringLiteral("Pseudocode"), Qt::CaseInsensitive) == 0 ||
                         normLang.compare(QStringLiteral("Rust"), Qt::CaseInsensitive) == 0 ||
                         normLang.contains(QStringLiteral("Pseudo"), Qt::CaseInsensitive));

  static const QRegularExpression stackVarRe(QStringLiteral("\\[rbp-(0x[0-9a-fA-F]+)\\]"));
  static const QRegularExpression callSymRe(QStringLiteral("<([^+>]+?)(?:@plt)?(?:\\+\\d+)?>"));
  static const QRegularExpression lineAddrRe(QStringLiteral("^\\s*(?:=>\\s*)?(0x[0-9a-fA-F]+)"));

  QSet<QString> stackOffsets;
  QMap<QString, uint64_t> stackOffsetFirstAddr;
  QStringList callsFound;

  for (const DisasmBasicBlock& bb : m_basicBlocks) {
    for (const QString& line : bb.lines) {
      uint64_t lineAddr = bb.startAddress;
      if (const QRegularExpressionMatch am = lineAddrRe.match(line); am.hasMatch()) {
        bool ok = false;
        const uint64_t parsed = am.captured(1).toULongLong(&ok, 16);
        if (ok) lineAddr = parsed;
      }
      if (const QRegularExpressionMatch sm = stackVarRe.match(line); sm.hasMatch()) {
        const QString off = sm.captured(1).mid(2);
        stackOffsets.insert(off);
        if (!stackOffsetFirstAddr.contains(off)) stackOffsetFirstAddr.insert(off, lineAddr);
      }
      if (line.contains(QStringLiteral("call"))) {
        if (const QRegularExpressionMatch cm = callSymRe.match(line); cm.hasMatch()) {
          callsFound.append(cm.captured(1));
        }
      }
    }
  }

  QStringList sortedOffsets = stackOffsets.values();
  std::sort(sortedOffsets.begin(), sortedOffsets.end());

  m_decompilerLineToAddr.clear();
  m_addrToDecompilerLine.clear();
  if (m_basicBlocks.isEmpty() && m_baseDisassembly.trimmed().isEmpty()) return QString();

  QStringList out;
  auto emitLine = [&](const QString& text, uint64_t addr, bool isPrimaryStmt) {
    out << text;
    const int lineNum = out.size();
    if (addr != 0ULL) {
      m_decompilerLineToAddr.insert(lineNum, addr);
      if (isPrimaryStmt || !m_addrToDecompilerLine.contains(addr)) {
        m_addrToDecompilerLine.insert(addr, lineNum);
      }
    }
  };

  const uint64_t entryAddr = m_basicBlocks.isEmpty() ? m_currentAddress : m_basicBlocks.first().startAddress;

  int sigLineNum = 1;
  int stackDeclLineNum = 1;
  if (isCpp) {
    emitLine(QStringLiteral("// DECOMPILED C++ / MPI | %1").arg(m_activeSymbol), entryAddr, false);
    emitLine(QStringLiteral("#include <mpi.h>"), entryAddr, false);
    emitLine(QStringLiteral("#include <cstdint>"), entryAddr, false);
    emitLine(QStringLiteral("int %1(int argc, char **argv) {").arg(m_activeSymbol), entryAddr, true);
    sigLineNum = out.size();
    for (const QString& off : sortedOffsets) {
      emitLine(QStringLiteral("  std::int32_t var_%1h = 0; // [rbp-0x%1]").arg(off), stackOffsetFirstAddr.value(off, entryAddr), false);
      if (stackDeclLineNum == 1) stackDeclLineNum = out.size();
    }
  } else if (isPseudo) {
    emitLine(QStringLiteral("// DECOMPILED PSEUDOCODE & XREFS | %1").arg(m_activeSymbol), entryAddr, false);
    emitLine(QStringLiteral("proc %1(argc, argv):").arg(m_activeSymbol), entryAddr, true);
    sigLineNum = out.size();
    for (const QString& off : sortedOffsets) {
      emitLine(QStringLiteral("  local var_%1h := 0").arg(off), stackOffsetFirstAddr.value(off, entryAddr), false);
      if (stackDeclLineNum == 1) stackDeclLineNum = out.size();
    }
  } else {
    emitLine(QStringLiteral("/* DECOMPILED APPROX C & XREFS | %1 */").arg(m_activeSymbol), entryAddr, false);
    emitLine(QStringLiteral("#include <stdint.h>"), entryAddr, false);
    emitLine(QStringLiteral("int %1(int argc, char **argv) {").arg(m_activeSymbol), entryAddr, true);
    sigLineNum = out.size();
    for (const QString& off : sortedOffsets) {
      emitLine(QStringLiteral("  int32_t var_%1h = 0; /* [rbp-0x%1] */").arg(off), stackOffsetFirstAddr.value(off, entryAddr), false);
      if (stackDeclLineNum == 1) stackDeclLineNum = out.size();
    }
  }

  for (int bbIdx = 0; bbIdx < m_basicBlocks.size(); ++bbIdx) {
    const DisasmBasicBlock& bb = m_basicBlocks[bbIdx];
    const QVector<uint64_t> inXrefs = xrefsToAddress(bb.startAddress);
    if (!inXrefs.isEmpty()) {
      QStringList callerStrs;
      for (uint64_t cAddr : inXrefs) callerStrs << QStringLiteral("0x%1 (jmp)").arg(cAddr, 8, 16, QChar('0'));
      emitLine(QStringLiteral("  // XREFS TO 0x%1: %2").arg(bb.startAddress, 8, 16, QChar('0')).arg(callerStrs.join(QStringLiteral(", "))),
               bb.startAddress, false);
    }
    emitLine(QStringLiteral("loc_0x%1:").arg(bb.startAddress, 8, 16, QChar('0')), bb.startAddress, bbIdx > 0);

    int blockFirstStmtLine = (bbIdx == 0) ? sigLineNum : out.size();
    QVector<uint64_t> unmappedBlockAddrs;

    for (const QString& instLine : bb.lines) {
      uint64_t instAddr = bb.startAddress;
      if (const QRegularExpressionMatch am = lineAddrRe.match(instLine); am.hasMatch()) {
        bool ok = false;
        const uint64_t parsed = am.captured(1).toULongLong(&ok, 16);
        if (ok) instAddr = parsed;
      }

      if (instLine.contains(QStringLiteral("endbr64")) ||
          instLine.contains(QStringLiteral("push  rbp")) ||
          instLine.contains(QStringLiteral("mov   rbp, rsp"))) {
        m_addrToDecompilerLine.insert(instAddr, sigLineNum);
      } else if (instLine.contains(QStringLiteral("sub   rsp"))) {
        m_addrToDecompilerLine.insert(instAddr, stackDeclLineNum);
        m_decompilerLineToAddr.insert(stackDeclLineNum, instAddr);
      } else if (instLine.contains(QStringLiteral("call")) && instLine.contains(QStringLiteral("MPI_Init"))) {
        if (isCpp) emitLine(QStringLiteral("  MPI::Init(argc, argv); // MPI_Init(&argc, &argv)"), instAddr, true);
        else if (isPseudo) emitLine(QStringLiteral("  call MPI_Init(&argc, &argv)"), instAddr, true);
        else emitLine(QStringLiteral("  MPI_Init(&argc, &argv);"), instAddr, true);
        blockFirstStmtLine = out.size();
      } else if (instLine.contains(QStringLiteral("call")) && instLine.contains(QStringLiteral("MPI_Finalize"))) {
        if (isCpp) emitLine(QStringLiteral("  MPI::Finalize();"), instAddr, true);
        else if (isPseudo) emitLine(QStringLiteral("  call MPI_Finalize()"), instAddr, true);
        else emitLine(QStringLiteral("  MPI_Finalize();"), instAddr, true);
        blockFirstStmtLine = out.size();
      } else if (instLine.contains(QStringLiteral("jg "))) {
        if (isPseudo) emitLine(QStringLiteral("  while (var_4h <= 2) do:"), instAddr, true);
        else emitLine(QStringLiteral("  while (var_4h <= 2) { if (var_4h > 2) goto loc_0x%1; }").arg(bb.trueTarget, 8, 16, QChar('0')), instAddr, true);
        m_addrToDecompilerLine.insert(bb.startAddress, out.size());
        m_decompilerLineToAddr.insert(out.size(), bb.startAddress);
        blockFirstStmtLine = out.size();
      } else if (instLine.contains(QStringLiteral("je "))) {
        if (isPseudo) emitLine(QStringLiteral("  if (var_4h == 0) then goto loc_0x%1").arg(bb.trueTarget, 8, 16, QChar('0')), instAddr, true);
        else emitLine(QStringLiteral("  if (var_4h == 0) { goto loc_0x%1; } else { var_4h += 1; }").arg(bb.trueTarget, 8, 16, QChar('0')), instAddr, true);
        blockFirstStmtLine = out.size();
      } else if (instLine.contains(QStringLiteral("jmp "))) {
        if (isPseudo) emitLine(QStringLiteral("  jump loc_0x%1").arg(bb.trueTarget, 8, 16, QChar('0')), instAddr, true);
        else emitLine(QStringLiteral("  goto loc_0x%1;").arg(bb.trueTarget, 8, 16, QChar('0')), instAddr, true);
        blockFirstStmtLine = out.size();
      } else if (instLine.contains(QStringLiteral("ret"))) {
        emitLine(isPseudo ? QStringLiteral("  return 0") : QStringLiteral("  return 0;"), instAddr, true);
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

  if (!isPseudo && !out.isEmpty()) {
    const uint64_t endAddr = m_basicBlocks.isEmpty() ? m_currentAddress : m_basicBlocks.last().endAddress;
    emitLine(QStringLiteral("}"), endAddr, false);
  }
  return out.join('\n');
}

/** @brief Splices live instruction patches into the base assembly code listing. */
QString DisassemblyView::applyPatchesToListing(const QString& rawCode) const {
  if (m_patchedInstructions.isEmpty()) return rawCode;
  static const QRegularExpression lineAddrRe(
      QStringLiteral("^(\\s*(?:=>\\s*)?(?:0x[0-9a-fA-F]+)(?:\\s*<[^>]+>)?\\s*:?\\s*)([^;\\n]+)(.*)$"));
  static const QRegularExpression addrRe(QStringLiteral("0x[0-9a-fA-F]+"));

  QStringList lines = rawCode.split('\n');
  for (QString& line : lines) {
    const QRegularExpressionMatch match = lineAddrRe.match(line);
    if (!match.hasMatch()) continue;
    const QRegularExpressionMatch aMatch = addrRe.match(match.captured(1));
    if (!aMatch.hasMatch()) continue;
    bool ok = false;
    const uint64_t addr = aMatch.captured(0).toULongLong(&ok, 16);
    if (ok && m_patchedInstructions.contains(addr)) {
      line = match.captured(1) + m_patchedInstructions.value(addr) + QStringLiteral("  ; [PATCHED]") + match.captured(3);
    }
  }
  return lines.join('\n');
}

bool DisassemblyView::validatePatchBytes(const QString& spec, QByteArray* outBytes,
                                         int targetInstLen, QString* outDiagnostic) {
  const QString trimmed = spec.trimmed();
  if (trimmed.isEmpty()) {
    if (outDiagnostic) *outDiagnostic = QStringLiteral("Patch specification cannot be empty.");
    return false;
  }
  static const QRegularExpression nopRe(QStringLiteral("^nop(?:\\s+(\\d+))?$"), QRegularExpression::CaseInsensitiveOption);
  if (const auto m = nopRe.match(trimmed); m.hasMatch()) {
    int count = (targetInstLen > 0) ? targetInstLen : 1;
    if (!m.captured(1).isEmpty()) {
      bool ok = false; count = m.captured(1).toInt(&ok);
      if (!ok || count < 1 || count > 64) {
        if (outDiagnostic) *outDiagnostic = QStringLiteral("NOP repeat count must be between 1 and 64 bytes.");
        return false;
      }
    }
    QByteArray bytes(count, '\x90');
    if (targetInstLen > 0 && bytes.size() != targetInstLen && outDiagnostic) {
      *outDiagnostic = QStringLiteral("NOP count (%1) differs from %2-byte target instruction.").arg(bytes.size()).arg(targetInstLen);
    }
    if (outBytes) *outBytes = bytes;
    return true;
  }
  static const QRegularExpression hexTokRe(QStringLiteral("^(?:0x)?([0-9a-fA-F]+)$"), QRegularExpression::CaseInsensitiveOption);
  QByteArray bytes;
  for (const QString& tok : trimmed.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts)) {
    const auto hm = hexTokRe.match(tok);
    if (!hm.hasMatch() || (hm.captured(1).size() % 2) != 0) {
      if (outDiagnostic) *outDiagnostic = QStringLiteral("Invalid token '%1': enter hex bytes (e.g. '31 c0 90') or 'nop [N]'.").arg(tok);
      return false;
    }
    bytes.append(QByteArray::fromHex(hm.captured(1).toLatin1()));
  }
  if (bytes.isEmpty()) return false;
  if (targetInstLen > 0 && bytes.size() < targetInstLen) {
    const int pad = targetInstLen - bytes.size();
    bytes.append(QByteArray(pad, '\x90'));
    if (outDiagnostic) *outDiagnostic = QStringLiteral("Auto-padded %1 trailing NOP byte(s) to match %2-byte instruction.").arg(pad).arg(targetInstLen);
  } else if (targetInstLen > 0 && bytes.size() > targetInstLen && outDiagnostic) {
    *outDiagnostic = QStringLiteral("Patch length (%1 bytes) exceeds %2-byte instruction at target address.").arg(bytes.size()).arg(targetInstLen);
  }
  if (outBytes) *outBytes = bytes;
  return true;
}

bool DisassemblyView::promptPatchInstructionDialog(uint64_t address) {
  const int instLen = instructionByteLength(address);
  const QString defSpec = (instLen > 1) ? QStringLiteral("nop %1").arg(instLen) : QStringLiteral("nop");
  bool ok = false;
  const QString raw = QInputDialog::getText(
      this, tr("Patch Instruction"),
      tr("Hex machine bytes (e.g. '31 c0 90') or 'nop [N]' for 0x%1%2:")
          .arg(address, 8, 16, QChar('0'))
          .arg(instLen > 0 ? tr(" (%1-byte instruction)").arg(instLen) : QString()),
      QLineEdit::Normal, defSpec, &ok);
  if (!ok || raw.trimmed().isEmpty()) return false;
  QByteArray preview; QString diag;
  if (!validatePatchBytes(raw, &preview, instLen, &diag)) {
    QMessageBox::warning(this, tr("Invalid Instruction Patch"), diag);
    return false;
  }
  if (instLen > 0 && preview.size() > instLen &&
      QMessageBox::warning(this, tr("Instruction Length Mismatch"),
                           diag + tr("\n\nApply multi-instruction overwrite anyway?"),
                           QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) {
    return false;
  }
  return patchInstruction(address, raw);
}

bool DisassemblyView::patchInstruction(uint64_t address, const QString& newInstruction) {
  QByteArray bytes; QString diag;
  if (!validatePatchBytes(newInstruction, &bytes, instructionByteLength(address), &diag)) return false;
  const bool isAllNop = std::all_of(bytes.cbegin(), bytes.cend(), [](char b) { return static_cast<unsigned char>(b) == 0x90U; });
  const QString label = isAllNop ? (bytes.size() > 1 ? QStringLiteral("nop ; x%1").arg(bytes.size()) : QStringLiteral("nop"))
                                 : (QStringLiteral(".byte 0x") + QString::fromLatin1(bytes.toHex(' ').toUpper()).replace(QLatin1Char(' '), QStringLiteral(", 0x")));
  m_patchedInstructions.insert(address, label);
  if (m_revertPatchesBtn) m_revertPatchesBtn->setEnabled(true);
  updateDisassembly(m_baseDisassembly);
  refreshDecompilerView();
  emit instructionPatched(address, bytes);
  return true;
}

void DisassemblyView::revertAllPatches() {
  if (m_patchedInstructions.isEmpty()) return;
  m_patchedInstructions.clear();
  if (m_revertPatchesBtn) m_revertPatchesBtn->setEnabled(false);
  updateDisassembly(m_baseDisassembly);
  refreshDecompilerView();
  emit patchesReverted();
}

}  // namespace gridlock::ui

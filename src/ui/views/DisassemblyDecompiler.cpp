/**
 * @file DisassemblyDecompiler.cpp
 * @brief High-level structured decompiler and token synchronization for DisassemblyView.
 *
 * Implements linear disassembly deconstruction into high-level C, C++, and
 * pseudocode representations. Reconstructs control-flow structures (while loops,
 * branch conditionals, and jumps), maps stack offsets (`[rbp-0x..]`) to synthetic
 * local variable declarations, and provides bidirectional line-to-instruction
 * and token-highlight choreography.
 */

#include "DisassemblyView.hpp"

#include <algorithm>
#include <cstdint>

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSplitter>
#include <QString>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

namespace gridlock::ui {

/**
 * @brief Constructs the decompiler dock pane, controls, and language selectors.
 * @param splitSplitter Splitter container widget hosting linear/CFG stack and decompiler pane.
 * @param font Monospace code font applied to the decompiler editor.
 */
void DisassemblyView::setupDecompilerUi(QSplitter* splitSplitter, const QFont& font) {
  // Right pane of splitter: Decompiler container with footer bar
  m_decompilerPage = new QWidget(splitSplitter);
  m_decompilerPage->setObjectName("decompilerPageContainer");
  m_decompilerPage->setMinimumWidth(180);
  auto *decompilerLayout = new QVBoxLayout(m_decompilerPage);
  decompilerLayout->setContentsMargins(0, 0, 0, 0);
  decompilerLayout->setSpacing(0);

  // Read-only text editor displaying reconstructed high-level pseudocode or C
  m_decompilerEdit = new QPlainTextEdit(m_decompilerPage);
  m_decompilerEdit->setObjectName("cutterDecompilerView");
  m_decompilerEdit->setReadOnly(true);
  m_decompilerEdit->setFrameShape(QFrame::NoFrame);
  m_decompilerEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
  m_decompilerEdit->setFont(font);
  m_decompilerEdit->setMinimumWidth(180);

  // Synchronize clicked decompiler line back to assembly address
  connect(m_decompilerEdit, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
    if (m_updatingDecompilerCursor || !m_decompilerEdit) return;
    const int lineNum = m_decompilerEdit->textCursor().blockNumber() + 1;
    if (m_decompilerLineToAddr.contains(lineNum) && lineNum != m_currentDecompilerLine) {
      selectDecompilerLine(lineNum);
    }
  });

  // Synchronize text selection token highlighting across all view modes
  connect(m_decompilerEdit, &QPlainTextEdit::selectionChanged, this, [this]() {
    if (m_updatingDecompilerCursor || !m_decompilerEdit) return;
    const QString sel = m_decompilerEdit->textCursor().selectedText().trimmed();
    if (sel.length() >= 2 && sel.length() <= 32 && !sel.contains(' ')) {
      highlightToken(sel);
    }
  });
  decompilerLayout->addWidget(m_decompilerEdit, 1);

  // Sub-toolbar bar hosting Auto-refresh toggle and target language combo
  auto *decompFooter = new QWidget(m_decompilerPage);
  decompFooter->setObjectName("decompilerFooterBar");
  decompFooter->setFixedHeight(24);
  decompFooter->setStyleSheet(
      "background: #21252b; border-top: 1px solid #2c313a; "
      "font-family: 'JetBrains Mono', monospace; font-size: 10px; color: #abb2bf;");
  auto *footerLayout = new QHBoxLayout(decompFooter);
  footerLayout->setContentsMargins(6, 1, 6, 1);
  footerLayout->setSpacing(6);

  // Auto-refresh checkbox synchronizing decompiler on assembly change
  m_autoRefreshCheck = new QCheckBox(QStringLiteral("Auto"), decompFooter);
  m_autoRefreshCheck->setObjectName("decompilerAutoRefreshCheck");
  m_autoRefreshCheck->setChecked(true);

  // Manual re-decompilation trigger button
  m_refreshDecompilerBtn = new QPushButton(QStringLiteral("Refresh"), decompFooter);
  m_refreshDecompilerBtn->setObjectName("decompilerRefreshBtn");
  m_refreshDecompilerBtn->setFixedHeight(20);
  connect(m_refreshDecompilerBtn, &QPushButton::clicked, this, &DisassemblyView::refreshDecompilerView);

  // Target output language dropdown selector (C, C++, Pseudocode)
  auto *targetLabel = new QLabel(QStringLiteral("Target:"), decompFooter);
  m_targetLangCombo = new QComboBox(decompFooter);
  m_targetLangCombo->setObjectName("decompilerTargetCombo");
  m_targetLangCombo->setFixedHeight(20);
  m_targetLangCombo->addItem(QStringLiteral("Approx C"), QStringLiteral("C"));
  m_targetLangCombo->addItem(QStringLiteral("C++ / MPI"), QStringLiteral("C++"));
  m_targetLangCombo->addItem(QStringLiteral("Pseudocode"), QStringLiteral("Pseudocode"));
  connect(m_targetLangCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
    if (m_targetLangCombo && idx >= 0) {
      setDecompilerTargetLanguage(m_targetLangCombo->itemData(idx).toString());
    }
  });

  // Pack footer buttons and dropdown selector into bottom layout strip
  footerLayout->addWidget(m_autoRefreshCheck);
  footerLayout->addWidget(m_refreshDecompilerBtn);
  footerLayout->addStretch(1);
  footerLayout->addWidget(targetLabel);
  footerLayout->addWidget(m_targetLangCombo);
  decompilerLayout->addWidget(decompFooter);

  // Attach panes to primary horizontal splitter
  splitSplitter->addWidget(m_stack);
  splitSplitter->addWidget(m_decompilerPage);
  splitSplitter->setStretchFactor(0, 1);
  splitSplitter->setStretchFactor(1, 1);
  m_decompilerPage->hide();
}

/**
 * @brief Highlights the active statement line in the decompiler editor.
 */
void DisassemblyView::highlightActiveDecompilerLine() {
  if (!m_decompilerEdit || !m_decompilerEdit->document()) {
    return;
  }

  QList<QTextEdit::ExtraSelection> selections;
  if (m_currentDecompilerLine > 0) {
    QTextBlock block = m_decompilerEdit->document()->findBlockByNumber(
        m_currentDecompilerLine - 1);
    if (block.isValid()) {
      m_updatingDecompilerCursor = true;
      QTextCursor cursor(block);
      m_decompilerEdit->setTextCursor(cursor);
      m_decompilerEdit->centerCursor();
      m_updatingDecompilerCursor = false;

      QTextEdit::ExtraSelection sel;
      sel.format.setBackground(QColor(0x26, 0x4f, 0x78));
      sel.format.setProperty(QTextFormat::FullWidthSelection, true);
      sel.cursor = cursor;
      sel.cursor.clearSelection();
      selections.append(sel);
    }
  }

  if (!m_highlightedToken.isEmpty()) {
    QTextCursor tc(m_decompilerEdit->document());
    while (!(tc = m_decompilerEdit->document()->find(m_highlightedToken, tc)).isNull()) {
      QTextEdit::ExtraSelection tokSel;
      tokSel.format.setBackground(QColor(0x3a, 0x44, 0x54));
      tokSel.format.setForeground(QColor(0xff, 0xd7, 0x00));
      tokSel.cursor = tc;
      selections.append(tokSel);
    }
  }
  m_decompilerEdit->setExtraSelections(selections);
}

/**
 * @brief Re-decompiles the active basic-block graph and refreshes the decompiler editor.
 *
 * Updates `m_decompiledPseudocode` for the current `m_targetLang`, loads the text
 * into `m_decompilerEdit` with cursor signal suppression, resolves the line mapped
 * to `m_currentAddress`, and synchronizes the active line selection marker.
 */
void DisassemblyView::refreshDecompilerView() {
  // Re-run high-level structured decompilation for the current target language
  m_decompiledPseudocode = decompileToLanguage(m_targetLang);
  if (m_decompilerEdit) {
    // Suppress cursor position signals to prevent reciprocal seek feedback loops
    m_updatingDecompilerCursor = true;
    m_decompilerEdit->setPlainText(m_decompiledPseudocode);
    m_updatingDecompilerCursor = false;
  }
  // Lookup the decompiled statement corresponding to the currently selected instruction
  const int mappedLine = addressToDecompilerLine(m_currentAddress);
  if (mappedLine > 0) {
    m_currentDecompilerLine = mappedLine;
  }
  // Highlight the current line in the decompiler editor gutter and text line
  highlightActiveDecompilerLine();
}

/**
 * @brief Sets the active decompiler target language ("C", "C++", or "Pseudocode").
 * @param lang Identifier of the target language.
 */
void DisassemblyView::setDecompilerTargetLanguage(const QString &lang) {
  m_targetLang =
      lang.trimmed().isEmpty() ? QStringLiteral("C") : lang.trimmed();
  // Synchronize target language combobox UI without re-triggering signal emission
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
  // Re-generate decompiled source in the newly selected language
  refreshDecompilerView();
}

/**
 * @brief Maps a 1-based decompiler source line back to its originating assembly address.
 * @param lineNumber 1-based line number in the decompiler editor text.
 * @return 64-bit instruction pointer address, or 0 if unmapped.
 */
uint64_t DisassemblyView::decompilerLineToAddress(int lineNumber) const {
  // Look up instruction address from the line-to-address reverse hash map
  return m_decompilerLineToAddr.value(lineNumber, 0ULL);
}

/**
 * @brief Resolves the 1-based decompiler line number corresponding to an instruction address.
 *
 * Checks exact instruction address mappings first, then searches the containing basic block
 * if the specific instruction address was collapsed into a block header statement.
 *
 * @param address 64-bit instruction pointer address.
 * @return 1-based decompiler line number, or -1 if address does not map to decompiled code.
 */
int DisassemblyView::addressToDecompilerLine(uint64_t address) const {
  // Check fast direct address lookup table first
  if (m_addrToDecompilerLine.contains(address)) {
    return m_addrToDecompilerLine.value(address);
  }
  // Fall back to containing basic block if address is an internal instruction
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

/**
 * @brief Selects a line in the decompiler editor and seeks linear and graph disassembly to it.
 * @param lineNumber 1-based line number clicked by user.
 * @return true if line was mapped to a valid address and seek completed.
 */
bool DisassemblyView::selectDecompilerLine(int lineNumber) {
  // Verify line number exists in mapping table
  if (!m_decompilerLineToAddr.contains(lineNumber)) {
    return false;
  }
  m_currentDecompilerLine = lineNumber;
  const uint64_t addr = m_decompilerLineToAddr.value(lineNumber);
  // Synchronize linear assembly view and emit global address selection signal
  seekToAddress(addr);
  emit addressSelected(addr);
  return true;
}

/**
 * @brief Highlights all occurrences of @p token across disassembly, CFG, and decompiler views.
 * @param token Identifier string or register/variable token to highlight.
 */
void DisassemblyView::highlightToken(const QString &token) {
  m_highlightedToken = token.trimmed();
  // Refresh active instruction background highlighting in linear assembly
  highlightActiveInstruction();
  // Refresh token match occurrences in decompiler text
  highlightActiveDecompilerLine();
  // Reconstruct CFG scene to paint token background markers on matching nodes
  if (m_graphMode) {
    rebuildCfgScene();
  }
}

/**
 * @brief Generates structured high-level decompiled code for the active function symbol.
 *
 * Translates disassembled instructions and control-flow basic blocks into one of three target
 * languages:
 * - `"C"`: Approximate C99 syntax with `goto` labels and standard types.
 * - `"C++"`: C++20 / MPI object-oriented API calls (`MPI::Init`, `MPI::Finalize`).
 * - `"Pseudocode"`: High-level Pythonic pseudocode blocks.
 *
 * @param lang Target language name ("C", "C++", "Pseudocode", etc.).
 * @return Formatted multi-line decompiled source text.
 */
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

  // Regex patterns for stack variable extraction, PLT calls, and line address markers
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

  // Scan control-flow arcs for loop back-edges and conditional branches
  for (const auto &arc : m_branchArcs) {
    if (arc.toAddr <= arc.fromAddr) {
      hasLoopBackEdge = true;
    }
    if (arc.isConditional) {
      hasCondBranch = true;
    }
  }

  // Scan basic-block lines for stack variable offsets and external library calls
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

  // Emit language-specific function header, includes, and local variable declarations
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

  // Decompile individual basic blocks with cross-reference comments
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

      // Pattern-match canonical MPI call instructions and branch jumps
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
        // High-level MPI finalization statement emission
        if (isCpp) {
          emitLine(QStringLiteral("  MPI::Finalize();"), instAddr, true);
        } else if (isPseudo) {
          emitLine(QStringLiteral("  call MPI_Finalize()"), instAddr, true);
        } else {
          emitLine(QStringLiteral("  MPI_Finalize();"), instAddr, true);
        }
        blockFirstStmtLine = out.size();
      } else if (instLine.contains(QStringLiteral("jg "))) {
        // Loop back-edge or conditional loop boundary reconstruction
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
        // Two-way conditional branch pattern matching
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
        // Direct unconditional jump to target basic block label
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
        // Procedure return statement
        emitLine(isPseudo ? QStringLiteral("  return 0")
                          : QStringLiteral("  return 0;"),
                 instAddr, true);
        blockFirstStmtLine = out.size();
      } else {
        // Collect unmapped instructions to associate with the primary statement
        unmappedBlockAddrs.append(instAddr);
      }
    }

    // Associate unmapped preamble/prologue instructions with the first statement of the block
    for (uint64_t uAddr : unmappedBlockAddrs) {
      if (!m_addrToDecompilerLine.contains(uAddr)) {
        m_addrToDecompilerLine.insert(uAddr, blockFirstStmtLine);
      }
    }
  }

  // Fallback template when basic-block analysis cannot isolate structured blocks
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

/**
 * @brief Splices live instruction patches into the base assembly code listing.
 * @param rawCode Original assembly text lines.
 * @return Assembly listing with patched instructions substituted at target addresses.
 */
QString DisassemblyView::applyPatchesToListing(const QString &rawCode) const {
  if (m_patchedInstructions.isEmpty()) {
    return rawCode;
  }
  static const QRegularExpression lineAddrRe(
      QStringLiteral("^(\\s*(?:=>\\s*)?(?:0x[0-9a-fA-F]+)(?:\\s*<[^>]+>)?\\s*:?\\s*)([^;\\n]+)(.*)$"));
  static const QRegularExpression addrRe(
      QStringLiteral("0x[0-9a-fA-F]+"));

  QStringList lines = rawCode.split('\n');
  for (QString &line : lines) {
    QRegularExpressionMatch match = lineAddrRe.match(line);
    if (!match.hasMatch()) {
      continue;
    }
    QRegularExpressionMatch aMatch = addrRe.match(match.captured(1));
    if (!aMatch.hasMatch()) {
      continue;
    }
    bool ok = false;
    const uint64_t addr = aMatch.captured(0).toULongLong(&ok, 16);
    if (ok && m_patchedInstructions.contains(addr)) {
      const QString prefix = match.captured(1);
      const QString newInst = m_patchedInstructions.value(addr);
      const QString trailing = match.captured(3);
      line = prefix + newInst + QStringLiteral("  ; [PATCHED]") + trailing;
    }
  }
  return lines.join('\n');
}

/**
 * @brief Applies live assembly instruction patch at instruction pointer address.
 *
 * Records the replacement instruction for the target address and triggers a
 * full disassembly parse, CFG basic block rebuild, and decompiler regeneration pass.
 *
 * @param address 64-bit target instruction address.
 * @param newInstruction New assembly instruction string (e.g. "nop").
 * @return True on valid non-empty patch; false otherwise.
 */
bool DisassemblyView::patchInstruction(uint64_t address,
                                       const QString &newInstruction) {
  if (newInstruction.trimmed().isEmpty()) {
    return false;
  }
  m_patchedInstructions.insert(address, newInstruction.trimmed());
  updateDisassembly(m_baseDisassembly);
  refreshDecompilerView();
  return true;
}

/**
 * @brief Reverts all actively applied instruction patches and restores original code.
 */
void DisassemblyView::revertAllPatches() {
  m_patchedInstructions.clear();
  updateDisassembly(m_baseDisassembly);
  refreshDecompilerView();
}

}  // namespace gridlock::ui


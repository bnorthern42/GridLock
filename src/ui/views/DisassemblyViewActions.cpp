/**
 * @file DisassemblyViewActions.cpp
 * @brief Assembly syntax highlighting, view-mode choreography, seek/update actions, and styling for DisassemblyView.
 *
 * Implements AsmSyntaxHighlighter, Linear/CFG/Decompiler/Split mode switching,
 * seekToAddress(), updateDisassembly(), active instruction highlighting,
 * ThemeManager reloadStyle(), instruction stepping helpers, and active rank banners.
 */

#include "DisassemblyView.hpp"

#include <utility>

#include <QChildEvent>
#include <QPalette>
#include <QSettings>
#include <QSplitter>
#include <QTextBlock>

#include "../../core/managers/ConfigManager.hpp"
#include "../../core/managers/ThemeManager.hpp"

namespace gridlock::ui {

AsmSyntaxHighlighter::AsmSyntaxHighlighter(QTextDocument* parent) : QSyntaxHighlighter(parent) {
  reloadRules();
}

/** @brief Recompiles highlighting rules from active configuration colors and themes. */
void AsmSyntaxHighlighter::reloadRules() {
  highlightingRules.clear();
  HighlightingRule rule;

  QTextCharFormat opcodeFormat;
  opcodeFormat.setForeground(QColor(gridlock::core::ConfigManager::instance().getAssemblyOpcode()));
  opcodeFormat.setFontWeight(QFont::Bold);
  static const char* const kOpcodes[] = {
      "mov", "movl", "movq", "movsd", "movss", "movaps", "push", "pushq", "sub", "subq",
      "lea", "leaq", "nop", "pop", "popq", "add", "addq", "and", "or", "xor", "xorl",
      "test", "testl", "cmp", "cmpl", "cmpq", "imul", "shl", "shr", "endbr64"};
  for (const char* op : kOpcodes) {
    highlightingRules.append({QRegularExpression(QStringLiteral("\\b%1\\b").arg(QLatin1String(op))), opcodeFormat});
  }

  QTextCharFormat branchFormat;
  branchFormat.setForeground(QColor("#dcdcaa"));
  branchFormat.setFontWeight(QFont::Bold);
  static const char* const kBranches[] = {
      "jmp", "je", "jz", "jne", "jnz", "jg", "jge", "jl", "jle", "ja", "jae", "jb", "jbe"};
  for (const char* br : kBranches) {
    highlightingRules.append({QRegularExpression(QStringLiteral("\\b%1\\b").arg(QLatin1String(br))), branchFormat});
  }

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

  QTextCharFormat registerFormat;
  registerFormat.setForeground(QColor(gridlock::core::ConfigManager::instance().getAssemblyRegister()));
  rule.pattern = QRegularExpression(
      QStringLiteral("(?:%[a-z0-9]+|\\b(?:r|e)?(?:ax|bx|cx|dx|si|di|bp|sp|ip)\\b|\\br(?:8|9|10|11|12|13|14|15)(?:d|w|b)?\\b|\\bxmm[0-9]+\\b)"));
  rule.format = registerFormat;
  highlightingRules.append(rule);

  QTextCharFormat hexFormat;
  hexFormat.setForeground(QColor(gridlock::core::ConfigManager::instance().getAssemblyAddress()));
  rule.pattern = QRegularExpression(QStringLiteral("0x[0-9a-fA-F]+"));
  rule.format = hexFormat;
  highlightingRules.append(rule);

  QTextCharFormat commentFormat;
  commentFormat.setForeground(QColor("#6a9955"));
  rule.pattern = QRegularExpression(QStringLiteral("(?:;.*$|<[^>]+>)"));
  rule.format = commentFormat;
  highlightingRules.append(rule);

  rehighlight();
}

void AsmSyntaxHighlighter::highlightBlock(const QString& text) {
  for (const HighlightingRule& rule : std::as_const(highlightingRules)) {
    QRegularExpressionMatchIterator it = rule.pattern.globalMatch(text);
    while (it.hasNext()) {
      const QRegularExpressionMatch match = it.next();
      setFormat(match.capturedStart(), match.capturedLength(), rule.format);
    }
  }
}

QString DisassemblyView::toPlainText() const { return m_linearEdit ? m_linearEdit->toPlainText() : QString(); }
void DisassemblyView::setPlainText(const QString& text) { updateDisassembly(text); }
QTextDocument* DisassemblyView::document() const { return m_linearEdit ? m_linearEdit->document() : nullptr; }
QTextCursor DisassemblyView::textCursor() const { return m_linearEdit ? m_linearEdit->textCursor() : QTextCursor(); }
void DisassemblyView::setTextCursor(const QTextCursor& cursor) { if (m_linearEdit) m_linearEdit->setTextCursor(cursor); }
void DisassemblyView::centerCursor() { if (m_linearEdit) m_linearEdit->centerCursor(); }

void DisassemblyView::setSourceEditorActive(bool sourceVisible) {
  m_sourceEditorActive = sourceVisible;
  if (sourceVisible && !gridlock::core::ConfigManager::instance().showDecompilerWithSource()) {
    const bool wantLinear = (gridlock::core::ConfigManager::instance().defaultDisassemblyMode() == QStringLiteral("linear"));
    if (m_splitGraphDecompiler || m_decompilerMode) m_graphMode = !wantLinear;
    m_splitGraphDecompiler = false; m_decompilerMode = false;
  }
  updateViewModeLayout();
}

void DisassemblyView::setDecompilerPaneVisible(bool visible, bool applyDefaultMode) {
  const bool wantLinear = (gridlock::core::ConfigManager::instance().defaultDisassemblyMode() == QStringLiteral("linear"));
  const bool wasDecomp = m_splitGraphDecompiler || m_decompilerMode;
  if (!visible) { m_splitGraphDecompiler = false; m_decompilerMode = false; if (applyDefaultMode || wasDecomp) m_graphMode = !wantLinear; }
  else { m_graphMode = !wantLinear; m_splitGraphDecompiler = !wantLinear; m_decompilerMode = true; }
  updateViewModeLayout();
}

void DisassemblyView::setDecompilerVisibleWhenSourceShown(bool visible) { setDecompilerPaneVisible(visible, true); }
bool DisassemblyView::isDecompilerVisible() const { return m_decompilerPage && !m_decompilerPage->isHidden() && (m_decompilerMode || m_splitGraphDecompiler); }

/** @brief Synchronizes UI toggles, splitter ratios, and visible pages for the active view mode. */
void DisassemblyView::updateViewModeLayout() {
  if (m_decompilerToggleBtn) m_decompilerToggleBtn->setChecked(m_decompilerMode && !m_splitGraphDecompiler);
  if (m_splitToggleBtn) m_splitToggleBtn->setChecked(m_splitGraphDecompiler);
  if (m_modeToggleBtn) {
    m_modeToggleBtn->setText(m_graphMode ? QStringLiteral("Linear (Space)") : QStringLiteral("CFG (Space)"));
    m_modeToggleBtn->setMinimumWidth(qMax(m_modeToggleBtn->sizeHint().width(), m_modeToggleBtn->fontMetrics().horizontalAdvance(m_modeToggleBtn->text()) + 14));
  }
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
    if (m_decompilerEdit) { m_decompilerEdit->setMinimumWidth(0); m_decompilerEdit->resize(0, m_decompilerEdit->height()); }
    if (m_decompilerPage) { m_decompilerPage->setMinimumWidth(0); m_decompilerPage->resize(0, m_decompilerPage->height()); m_decompilerPage->hide(); }
    if (sp) sp->setSizes({qMax(420, width()), 0});
    if (m_stack) { m_stack->show(); m_stack->setCurrentIndex(m_graphMode ? 1 : 0); }
    if (m_graphMode) {
      m_cfgUserScrolled = false;
      if (m_cfgView) { m_cfgView->setCursor(Qt::ArrowCursor); if (m_cfgView->viewport()) m_cfgView->viewport()->setCursor(Qt::ArrowCursor); }
      rebuildCfgScene();
    } else if (m_linearEdit) m_linearEdit->updateGutter();
  }
}

void DisassemblyView::setSplitGraphDecompilerMode(bool enabled) {
  m_splitGraphDecompiler = enabled; m_graphMode = enabled ? true : m_graphMode; m_decompilerMode = enabled; updateViewModeLayout();
}
void DisassemblyView::setDecompilerMode(bool enabled) { m_decompilerMode = enabled; m_splitGraphDecompiler = false; updateViewModeLayout(); }
void DisassemblyView::setGraphMode(bool enabled) { m_graphMode = enabled; m_decompilerMode = false; m_splitGraphDecompiler = false; updateViewModeLayout(); }
void DisassemblyView::toggleViewMode() { setGraphMode(!m_graphMode); }

/** @brief Seeks linear disassembly, CFG canvas, and decompiler views to instruction address. */
bool DisassemblyView::seekToAddress(uint64_t address) {
  if (address == 0ULL) return false;
  uint64_t resolvedAddress = address;
  if (!m_addrToLine.contains(address) && !m_addrToLine.isEmpty()) {
    for (const DisasmBasicBlock& bb : std::as_const(m_basicBlocks))
      if (address >= bb.startAddress && address <= bb.endAddress) { resolvedAddress = bb.startAddress; break; }
  }
  m_currentAddress = address;
  m_cfgUserScrolled = false;
  const int mappedDecompLine = addressToDecompilerLine(address);
  if (mappedDecompLine > 0) m_currentDecompilerLine = mappedDecompLine;
  if (m_headerBadge) m_headerBadge->setText(QStringLiteral("%1 @ 0x%2").arg(m_activeSymbol).arg(m_currentAddress, 6, 16, QChar('0')));
  highlightActiveInstruction(); highlightActiveDecompilerLine(); rebuildCfgScene();
  if (m_linearEdit) m_linearEdit->updateGutter();
  return m_addrToLine.contains(address) || m_addrToLine.contains(resolvedAddress) || !m_basicBlocks.isEmpty();
}

/** @brief Clears disassembly text, CFG blocks, decompiler output, and badges to idle empty state. */
void DisassemblyView::clear() {
  m_currentAddress = 0ULL; m_currentDecompilerLine = -1;
  m_activeSymbol.clear(); m_functionSignature.clear(); m_baseDisassembly.clear(); m_decompiledPseudocode.clear();
  m_branchArcs.clear(); m_basicBlocks.clear(); m_addrToLine.clear(); m_lineToAddr.clear(); m_xrefs.clear();
  m_decompilerLineToAddr.clear(); m_addrToDecompilerLine.clear(); m_rankAddresses.clear(); m_deadlockCycleRanks.clear();
  if (m_linearEdit) { m_linearEdit->clear(); m_linearEdit->setExtraSelections({}); m_linearEdit->updateGutter(); }
  if (m_decompilerEdit) { m_decompilerEdit->clear(); m_decompilerEdit->setExtraSelections({}); }
  if (m_headerBadge) { m_headerBadge->setText(QStringLiteral("DISASM | No binary loaded")); m_headerBadge->setToolTip(QString()); }
  if (m_signatureLabel) { m_signatureLabel->setText(QString()); m_signatureLabel->setToolTip(QString()); }
  if (m_statsBadge) m_statsBadge->setText(QStringLiteral("BB:0 E:0"));
  rebuildCfgScene();
}

/** @brief Updates linear assembly code, normalizes live GDB branch syntax, and rebuilds CFG views. */
void DisassemblyView::updateDisassembly(const QString& asmCode) {
  if (asmCode.trimmed().isEmpty()) { clear(); return; }
  const QStringList rawLines = asmCode.split(QLatin1Char('\n'));
  QStringList normalizedLines; normalizedLines.reserve(rawLines.size());
  for (const QString& line : rawLines) normalizedLines.append(normalizeDisassemblyLine(line));
  m_baseDisassembly = normalizedLines.join(QLatin1Char('\n'));
  m_cfgUserScrolled = false;
  const QString effectiveCode = applyUserCommentsToListing(applyPatchesToListing(m_baseDisassembly));
  if (m_linearEdit && m_linearEdit->toPlainText() != effectiveCode) m_linearEdit->setPlainText(effectiveCode);
  parseDisassembly(effectiveCode);
  if (m_sourceEditorActive && !gridlock::core::ConfigManager::instance().showDecompilerWithSource() && (m_splitGraphDecompiler || m_decompilerMode))
    setDecompilerPaneVisible(false, false);
  highlightActiveInstruction(); highlightActiveDecompilerLine(); rebuildCfgScene();
  if (m_linearEdit) m_linearEdit->updateGutter();
}

/** @brief Highlights the active instruction pointer line and search token matches in linear assembly. */
void DisassemblyView::highlightActiveInstruction() {
  if (!m_linearEdit) return;
  const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
  QList<QTextEdit::ExtraSelection> selections;
  const int line = m_addrToLine.value(m_currentAddress, -1);
  if (line >= 0) {
    if (QTextBlock block = m_linearEdit->document()->findBlockByNumber(line); block.isValid()) {
      QTextCursor cursor(block); m_linearEdit->setTextCursor(cursor); m_linearEdit->centerCursor();
      QTextEdit::ExtraSelection sel; sel.format.setBackground(isDark ? QColor(0x1f, 0x34, 0x42) : QColor(46, 160, 67, 45));
      sel.format.setProperty(QTextFormat::FullWidthSelection, true); sel.cursor = cursor; sel.cursor.clearSelection();
      selections.append(sel);
    }
  }
  if (!m_highlightedToken.isEmpty() && m_linearEdit->document()) {
    QTextCursor tc(m_linearEdit->document());
    while (!(tc = m_linearEdit->document()->find(m_highlightedToken, tc)).isNull()) {
      QTextEdit::ExtraSelection tokSel; tokSel.format.setBackground(QColor(0x3a, 0x44, 0x54));
      tokSel.format.setForeground(QColor(0xff, 0xd7, 0x00)); tokSel.cursor = tc; selections.append(tokSel);
    }
  }
  m_linearEdit->setExtraSelections(selections); m_linearEdit->updateGutter();
}

bool DisassemblyView::eventFilter(QObject* watched, QEvent* event) {
  if (watched == this && event->type() == QEvent::ChildPolished) {
    if (auto* cw = qobject_cast<QWidget*>(static_cast<QChildEvent*>(event)->child())) {
      QList<QWidget*> targets = cw->findChildren<QWidget*>();
      targets.prepend(cw);
      for (QWidget* w : targets) {
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

QColor DisassemblyView::activeCanvasBackgroundColor() const {
  auto& tm = gridlock::core::ThemeManager::instance();
  const QStringList sw = gridlock::core::ThemeManager::themeSwatchColors(tm.activeThemeId());
  return QColor(sw.value(0, tm.isDarkMode() ? gridlock::core::ConfigManager::instance().getSourceBackground()
                                            : QStringLiteral("#f5f6f8")));
}

/** @brief Reloads font size, background color, and theme styles from ConfigManager and ThemeManager. */
void DisassemblyView::reloadStyle() {
  QSettings s("gridlock", "debugger");
  const int fontSize = s.value("appearance/code_font_size", 10).toInt();
  const QString fontFam = s.value("appearance/code_font_family", QStringLiteral("JetBrains Mono")).toString().trimmed();
  auto& tm = gridlock::core::ThemeManager::instance();
  const QStringList sw = gridlock::core::ThemeManager::themeSwatchColors(tm.activeThemeId());
  const QString bgHex = sw.value(0, tm.isDarkMode() ? gridlock::core::ConfigManager::instance().getSourceBackground() : QStringLiteral("#f5f6f8"));
  const QString fgHex = sw.value(4, tm.isDarkMode() ? gridlock::core::ConfigManager::instance().getSourceText() : QStringLiteral("#1f2328"));
  const QString bdrHex = sw.value(2, tm.isDarkMode() ? QStringLiteral("#282c34") : QStringLiteral("#d0d4dc"));

  const QString editorStyle =
      QString("QPlainTextEdit, QPlainTextEdit QWidget { background-color: %1; color: %2; font-size: %3pt; font-family: \"%4\", monospace; border: none; }")
          .arg(bgHex, fgHex)
          .arg(fontSize)
          .arg(fontFam.isEmpty() ? QStringLiteral("JetBrains Mono") : fontFam);

  auto applyEditorPalette = [&](QPlainTextEdit* ed) {
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

  applyEditorPalette(m_linearEdit);
  if (m_linearEdit) m_linearEdit->updateGutter();
  applyEditorPalette(m_decompilerEdit);
  if (auto* hdr = findChild<QWidget*>(QStringLiteral("cutterDisasmHeader"))) {
    const auto pal = gridlock::core::ThemeManager::paletteForTheme(tm.activeThemeId());
    hdr->setStyleSheet(QStringLiteral("#cutterDisasmHeader { background-color: %1; border-bottom: 1px solid %2; } #cutterDisasmBadge { color: %3; font-size: 12px; font-weight: 600; } #cutterDisasmStats { color: %3; font-size: 12px; } #disasmStepiBtn, #disasmNextiBtn, #cutterCfgToggleBtn, #cutterDecompilerBtn, #cutterSplitViewBtn { background-color: %1; color: %3; border: 1px solid %2; border-radius: 3px; padding: 0px 8px; min-height: 24px; max-height: 24px; font-size: 12px; font-weight: 600; } #disasmStepiBtn:hover, #disasmNextiBtn:hover, #cutterCfgToggleBtn:hover, #cutterDecompilerBtn:hover, #cutterSplitViewBtn:hover { background-color: %2; border: 1px solid %4; color: %3; } #cutterCfgToggleBtn:checked, #cutterDecompilerBtn:checked, #cutterSplitViewBtn:checked { background-color: %4; color: #ffffff; border: 1px solid %4; }").arg(pal.headerHex, pal.borderHex, pal.textHex, pal.accentHex));
  }
  for (QPushButton* btn : {m_stepiBtn, m_nextiBtn, m_splitToggleBtn, m_decompilerToggleBtn, m_modeToggleBtn}) {
    if (btn) { btn->ensurePolished(); btn->setMinimumWidth(btn->sizeHint().width()); }
  }
  if (auto* sigBar = findChild<QWidget*>(QStringLiteral("disasmSignatureBar"))) {
    sigBar->setStyleSheet(QStringLiteral("background: %1; border-bottom: 1px solid %2;").arg(bgHex, bdrHex));
  }
  if (m_signatureLabel) {
    const QString sigTextColor = tm.isDarkMode() ? QStringLiteral("#dcdcaa") : QStringLiteral("#1f2328");
    m_signatureLabel->setStyleSheet(QStringLiteral("color: %1; font-family: 'JetBrains Mono', monospace; font-size: 12px; font-weight: 600;").arg(sigTextColor));
  }
  if (auto* footer = findChild<QWidget*>(QStringLiteral("decompilerFooterBar"))) {
    const QString footerBg = tm.isDarkMode() ? QStringLiteral("#21252b") : sw.value(1, QStringLiteral("#eee8d5"));
    const QString footerBdr = tm.isDarkMode() ? QStringLiteral("#2c313a") : sw.value(2, QStringLiteral("#d3cbb7"));
    footer->setStyleSheet(QStringLiteral("background: %1; border-top: 1px solid %2; font-size: 12px; color: %3;").arg(footerBg, footerBdr, fgHex));
  }
  if (m_cfgView) m_cfgView->setBackgroundBrush(QColor(bgHex));
  if (m_highlighter) m_highlighter->reloadRules();
  highlightActiveInstruction();
  rebuildCfgScene();
}

int DisassemblyView::codeFontPointSize() const {
  return m_linearEdit ? m_linearEdit->font().pointSize() : font().pointSize();
}

uint64_t DisassemblyView::nextInstructionAddress(uint64_t currentAddr) const {
  if (currentAddr == 0x00401163ULL || currentAddr == 0x0040116eULL) return 0x00401149ULL;
  const auto it = m_addrToLine.upperBound(currentAddr);
  if (it != m_addrToLine.cend()) return it.key();
  return m_addrToLine.isEmpty() ? currentAddr : m_addrToLine.firstKey();
}

int DisassemblyView::instructionByteLength(uint64_t address) const {
  const auto it = m_addrToLine.upperBound(address);
  if (it != m_addrToLine.cend() && it.key() > address && (it.key() - address) <= 15ULL) {
    return static_cast<int>(it.key() - address);
  }
  return 0;
}

void DisassemblyView::setActiveSteppingRank(int rank, uint64_t pc) {
  m_selectedRank = rank;
  const QString role = (rank == 0) ? QStringLiteral("COORDINATOR") : QStringLiteral("WORKER");
  const QString pcHex = QStringLiteral("0x%1").arg(pc, 0, 16);
  const QString sym = m_activeSymbol.isEmpty() ? QStringLiteral("sym.main") : m_activeSymbol;
  const QString banner = QStringLiteral("[STEPPING: RANK %1] | RANK %1 [%2] | %3 @ %4").arg(rank).arg(role, sym, pcHex);
  if (m_functionBannerLabel) m_functionBannerLabel->setText(banner);
}

}  // namespace gridlock::ui

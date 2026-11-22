/**
 * @file DisassemblyDecompiler.cpp
 * @brief Decompiler pane UI, bidirectional line/token synchronization, bookmarks, Xrefs, and context menus.
 *
 * Implements the embedded decompiler pane and language selector bar, bidirectional
 * instruction-to-statement cursor synchronization, cross-view token highlighting,
 * user bookmarks, XrefInspectorDialog integration, and instruction context menus.
 */

#include "DisassemblyView.hpp"

#include <algorithm>
#include <cstdint>

#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>
#include <QWidget>

#include "../../core/managers/ThemeManager.hpp"
#include "../dialogs/debugger/XrefInspectorDialog.hpp"

namespace gridlock::ui {

/** @brief Constructs the decompiler dock pane, controls, and language selectors. */
void DisassemblyView::setupDecompilerUi(QSplitter* splitSplitter, const QFont& font) {
  m_decompilerPage = new QWidget(splitSplitter);
  m_decompilerPage->setObjectName("decompilerPageContainer");
  m_decompilerPage->setMinimumWidth(180);
  auto* decompilerLayout = new QVBoxLayout(m_decompilerPage);
  decompilerLayout->setContentsMargins(0, 0, 0, 0);
  decompilerLayout->setSpacing(0);

  m_decompilerEdit = new QPlainTextEdit(m_decompilerPage);
  m_decompilerEdit->setObjectName("cutterDecompilerView");
  m_decompilerEdit->setReadOnly(true);
  m_decompilerEdit->setFrameShape(QFrame::NoFrame);
  m_decompilerEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
  m_decompilerEdit->setFont(font);
  m_decompilerEdit->setMinimumWidth(180);

  connect(m_decompilerEdit, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
    if (m_updatingDecompilerCursor || !m_decompilerEdit) return;
    const int lineNum = m_decompilerEdit->textCursor().blockNumber() + 1;
    if (m_decompilerLineToAddr.contains(lineNum) && lineNum != m_currentDecompilerLine) {
      selectDecompilerLine(lineNum);
    }
  });

  connect(m_decompilerEdit, &QPlainTextEdit::selectionChanged, this, [this]() {
    if (m_updatingDecompilerCursor || !m_decompilerEdit) return;
    const QString sel = m_decompilerEdit->textCursor().selectedText().trimmed();
    if (sel.length() >= 2 && sel.length() <= 32 && !sel.contains(' ')) {
      highlightToken(sel);
    }
  });
  decompilerLayout->addWidget(m_decompilerEdit, 1);

  auto* decompFooter = new QWidget(m_decompilerPage);
  decompFooter->setObjectName("decompilerFooterBar");
  decompFooter->setFixedHeight(24);
  const auto& tm = gridlock::core::ThemeManager::instance();
  const auto pal = tm.paletteForTheme(tm.activeThemeId());
  decompFooter->setStyleSheet(QStringLiteral(
      "background: %1; border-top: 1px solid %2; "
      "font-family: 'JetBrains Mono', monospace; font-size: 10px; color: %3;")
      .arg(pal.headerHex, pal.borderHex, pal.textHex));
  auto* footerLayout = new QHBoxLayout(decompFooter);
  footerLayout->setContentsMargins(6, 1, 6, 1);
  footerLayout->setSpacing(6);

  m_autoRefreshCheck = new QCheckBox(QStringLiteral("Auto"), decompFooter);
  m_autoRefreshCheck->setObjectName("decompilerAutoRefreshCheck");
  m_autoRefreshCheck->setChecked(true);

  m_refreshDecompilerBtn = new QPushButton(QStringLiteral("Refresh"), decompFooter);
  m_refreshDecompilerBtn->setObjectName("decompilerRefreshBtn");
  m_refreshDecompilerBtn->setFixedHeight(20);
  connect(m_refreshDecompilerBtn, &QPushButton::clicked, this, &DisassemblyView::refreshDecompilerView);

  auto* targetLabel = new QLabel(QStringLiteral("Target:"), decompFooter);
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

  footerLayout->addWidget(m_autoRefreshCheck);
  footerLayout->addWidget(m_refreshDecompilerBtn);
  footerLayout->addStretch(1);
  footerLayout->addWidget(targetLabel);
  footerLayout->addWidget(m_targetLangCombo);
  decompilerLayout->addWidget(decompFooter);

  splitSplitter->addWidget(m_stack);
  splitSplitter->addWidget(m_decompilerPage);
  splitSplitter->setStretchFactor(0, 1);
  splitSplitter->setStretchFactor(1, 1);
  m_decompilerPage->hide();

  if (m_cfgView) {
    m_cfgView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_cfgView, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
      uint64_t addr = m_currentAddress;
      for (QGraphicsItem* it = m_cfgView->itemAt(pos); it; it = it->parentItem()) {
        if (const uint64_t a = it->data(Qt::UserRole).toULongLong(); a != 0ULL) { addr = a; break; }
      }
      execAddressContextMenu(addr, m_cfgView->viewport()->mapToGlobal(pos));
    });
  }
  if (m_decompilerEdit) {
    m_decompilerEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_decompilerEdit, &QPlainTextEdit::customContextMenuRequested, this, [this](const QPoint& pos) {
      const int ln = m_decompilerEdit->cursorForPosition(pos).blockNumber() + 1;
      const uint64_t mapped = decompilerLineToAddress(ln);
      execAddressContextMenu(mapped != 0ULL ? mapped : m_currentAddress,
                             m_decompilerEdit->viewport()->mapToGlobal(pos),
                             m_decompilerEdit->createStandardContextMenu());
    });
  }
}

/** @brief Highlights the active statement line and token matches in the decompiler editor. */
void DisassemblyView::highlightActiveDecompilerLine() {
  if (!m_decompilerEdit || !m_decompilerEdit->document()) return;

  QList<QTextEdit::ExtraSelection> selections;
  if (m_currentDecompilerLine > 0) {
    QTextBlock block = m_decompilerEdit->document()->findBlockByNumber(m_currentDecompilerLine - 1);
    if (block.isValid()) {
      m_updatingDecompilerCursor = true;
      QTextCursor cursor(block);
      m_decompilerEdit->setTextCursor(cursor);
      m_decompilerEdit->centerCursor();
      m_updatingDecompilerCursor = false;

      const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
      QTextEdit::ExtraSelection sel;
      sel.format.setBackground(isDark ? QColor(0x26, 0x4f, 0x78) : QColor(0xcf, 0xe1, 0xf7));
      sel.format.setProperty(QTextFormat::FullWidthSelection, true);
      sel.cursor = cursor;
      sel.cursor.clearSelection();
      selections.append(sel);
    }
  }

  if (!m_highlightedToken.isEmpty()) {
    const bool isDark = gridlock::core::ThemeManager::instance().isDarkMode();
    QTextCursor tc(m_decompilerEdit->document());
    while (!(tc = m_decompilerEdit->document()->find(m_highlightedToken, tc)).isNull()) {
      QTextEdit::ExtraSelection tokSel;
      tokSel.format.setBackground(isDark ? QColor(0x3a, 0x44, 0x54) : QColor(0xff, 0xee, 0xaa));
      tokSel.format.setForeground(isDark ? QColor(0xff, 0xd7, 0x00) : QColor(0x80, 0x55, 0x00));
      tokSel.cursor = tc;
      selections.append(tokSel);
    }
  }
  m_decompilerEdit->setExtraSelections(selections);
}

/** @brief Re-decompiles the active basic-block graph and refreshes the decompiler editor. */
void DisassemblyView::refreshDecompilerView() {
  m_decompiledPseudocode = decompileToLanguage(m_targetLang);
  if (m_decompilerEdit) {
    m_updatingDecompilerCursor = true;
    m_decompilerEdit->setPlainText(m_decompiledPseudocode);
    m_updatingDecompilerCursor = false;
  }
  const int mappedLine = addressToDecompilerLine(m_currentAddress);
  if (mappedLine > 0) m_currentDecompilerLine = mappedLine;
  highlightActiveDecompilerLine();
}

void DisassemblyView::setDecompilerTargetLanguage(const QString& lang) {
  m_targetLang = lang.trimmed().isEmpty() ? QStringLiteral("C") : lang.trimmed();
  if (m_targetLangCombo) {
    for (int i = 0; i < m_targetLangCombo->count(); ++i) {
      if (m_targetLangCombo->itemData(i).toString().compare(m_targetLang, Qt::CaseInsensitive) == 0 ||
          m_targetLangCombo->itemText(i).compare(m_targetLang, Qt::CaseInsensitive) == 0) {
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

uint64_t DisassemblyView::decompilerLineToAddress(int lineNumber) const {
  return m_decompilerLineToAddr.value(lineNumber, 0ULL);
}

int DisassemblyView::addressToDecompilerLine(uint64_t address) const {
  if (m_addrToDecompilerLine.contains(address)) return m_addrToDecompilerLine.value(address);
  for (const DisasmBasicBlock& bb : m_basicBlocks) {
    if (address >= bb.startAddress && address <= bb.endAddress) {
      if (m_addrToDecompilerLine.contains(bb.startAddress)) return m_addrToDecompilerLine.value(bb.startAddress);
      for (auto it = m_addrToDecompilerLine.constBegin(); it != m_addrToDecompilerLine.constEnd(); ++it) {
        if (it.key() >= bb.startAddress && it.key() <= bb.endAddress) return it.value();
      }
    }
  }
  return -1;
}

bool DisassemblyView::selectDecompilerLine(int lineNumber) {
  if (!m_decompilerLineToAddr.contains(lineNumber)) return false;
  m_currentDecompilerLine = lineNumber;
  const uint64_t addr = m_decompilerLineToAddr.value(lineNumber);
  seekToAddress(addr);
  emit addressSelected(addr);
  return true;
}

void DisassemblyView::highlightToken(const QString& token) {
  m_highlightedToken = token.trimmed();
  highlightActiveInstruction();
  highlightActiveDecompilerLine();
  if (m_graphMode) rebuildCfgScene();
}

bool DisassemblyView::toggleBookmark(uint64_t address) {
  if (m_bookmarks.contains(address)) m_bookmarks.remove(address);
  else m_bookmarks.insert(address);
  if (!m_baseDisassembly.isEmpty()) updateDisassembly(m_baseDisassembly);
  return m_bookmarks.contains(address);
}

bool DisassemblyView::isBookmarked(uint64_t address) const {
  return m_bookmarks.contains(address);
}

QVector<uint64_t> DisassemblyView::bookmarkedAddresses() const {
  QVector<uint64_t> sorted;
  sorted.reserve(m_bookmarks.size());
  for (uint64_t addr : m_bookmarks) sorted.append(addr);
  std::sort(sorted.begin(), sorted.end());
  return sorted;
}

QVector<XrefRecord> DisassemblyView::incomingXrefs(uint64_t targetAddr) const {
  QVector<XrefRecord> results;
  for (const auto& xr : m_xrefs) {
    if (xr.toAddress == targetAddr) results.append(xr);
  }
  return results;
}

QVector<XrefRecord> DisassemblyView::outgoingXrefs(uint64_t sourceAddr) const {
  QVector<XrefRecord> results;
  uint64_t funcStart = 0;
  uint64_t funcEnd = UINT64_MAX;
  if (!m_basicBlocks.isEmpty()) {
    funcStart = m_basicBlocks.first().startAddress;
    funcEnd = m_basicBlocks.last().endAddress;
  }
  for (const auto& xr : m_xrefs) {
    if (sourceAddr == funcStart || (xr.fromAddress >= funcStart && xr.fromAddress <= funcEnd)) {
      results.append(xr);
    } else if (xr.fromAddress == sourceAddr) {
      results.append(xr);
    }
  }
  return results;
}

/** @brief Opens or refreshes the interactive Xref and call-graph inspector dialog. */
XrefInspectorDialog* DisassemblyView::openXrefInspector(uint64_t targetAddr) {
  if (!m_xrefDialog) {
    m_xrefDialog = new XrefInspectorDialog(this);
    connect(m_xrefDialog, &XrefInspectorDialog::addressActivated, this, &DisassemblyView::addressSelected);
  }

  const auto incoming = incomingXrefs(targetAddr);
  const auto outgoing = outgoingXrefs(targetAddr);

  QString preview;
  const int lineIdx = m_addrToLine.value(targetAddr, -1);
  const QString fullText = toPlainText();
  if (lineIdx >= 0 && !fullText.isEmpty()) {
    const QStringList allLines = fullText.split('\n');
    const int start = std::max(0, lineIdx - 2);
    const int end = std::min(static_cast<int>(allLines.size()), lineIdx + 3);
    for (int i = start; i < end; ++i) {
      preview += (i == lineIdx ? QStringLiteral("=> ") : QStringLiteral("   ")) + allLines[i] + QStringLiteral("\n");
    }
  } else {
    preview = QStringLiteral("Address: 0x%1\nIncoming: %2\nOutgoing: %3")
                  .arg(targetAddr, 8, 16, QChar('0'))
                  .arg(incoming.size())
                  .arg(outgoing.size());
  }

  m_xrefDialog->setTargetAddress(targetAddr);
  m_xrefDialog->setXrefs(incoming, outgoing, preview);
  m_xrefDialog->show();
  m_xrefDialog->raise();
  m_xrefDialog->activateWindow();
  return m_xrefDialog;
}

QStringList DisassemblyView::contextActionNamesForAddress(uint64_t address) const {
  Q_UNUSED(address);
  return {QStringLiteral("Seek in Memory View"), QStringLiteral("Inspect Cross-References (X)"),
          QStringLiteral("Lookup in Reference Manual (F1)"), QStringLiteral("Toggle Bookmark"),
          QStringLiteral("Patch Instruction"), QStringLiteral("Revert All Patches")};
}

bool DisassemblyView::triggerContextAction(uint64_t address, const QString& actionName) {
  if (actionName == QStringLiteral("Seek in Memory View")) {
    seekToAddress(address); emit addressSelected(address); emit seekInMemoryViewRequested(address); return true;
  }
  if (actionName == QStringLiteral("Inspect Cross-References (X)")) { openXrefInspector(address); return true; }
  if (actionName == QStringLiteral("Lookup in Reference Manual (F1)")) {
    QString symbol;
    const int lineIdx = m_addrToLine.value(address, -1);
    const QStringList lines = toPlainText().split('\n');
    if (lineIdx >= 0 && lineIdx < lines.size()) {
      const QString& line = lines[lineIdx];
      static const QRegularExpression callRe(QStringLiteral("\\bcall\\b[^<]*<([^>@]+)(?:@plt)?>"), QRegularExpression::CaseInsensitiveOption);
      if (const auto callMatch = callRe.match(line); callMatch.hasMatch()) {
        symbol = callMatch.captured(1).trimmed();
      } else {
        const int colonIdx = line.indexOf(QLatin1Char(':'));
        const QString stripped = (colonIdx != -1) ? line.mid(colonIdx + 1).trimmed() : line.trimmed();
        const QString mnemonic = stripped.section(QRegularExpression(QStringLiteral("\\s+")), 0, 0).trimmed();
        if (!mnemonic.isEmpty()) symbol = mnemonic;
      }
    }
    emit documentationRequested(symbol.isEmpty() ? QStringLiteral("mov") : symbol);
    return true;
  }
  if (actionName == QStringLiteral("Toggle Bookmark")) { toggleBookmark(address); return true; }
  if (actionName == QStringLiteral("Patch Instruction")) return promptPatchInstructionDialog(address);
  if (actionName == QStringLiteral("Revert All Patches")) { revertAllPatches(); return true; }
  return false;
}

bool DisassemblyView::activeCfgBlockContainsInstructionMarker(uint64_t addr) const {
  if (!m_cfgScene) return false;
  const QString hex8 = QStringLiteral("0x%1").arg(addr, 8, 16, QChar('0'));
  const QString hex6 = QStringLiteral("0x%1").arg(addr, 6, 16, QChar('0'));
  for (QGraphicsItem* item : m_cfgScene->items()) {
    if (auto* txt = dynamic_cast<QGraphicsSimpleTextItem*>(item)) {
      const QString t = txt->text();
      if (t.contains(QStringLiteral("▶")) && (t.contains(hex8, Qt::CaseInsensitive) || t.contains(hex6, Qt::CaseInsensitive))) return true;
    }
  }
  return false;
}

void DisassemblyView::execAddressContextMenu(uint64_t addr, const QPoint& globalPos, QMenu* baseMenu) {
  ++m_contextMenuPopupCount;
  m_lastContextMenuAddress = addr;
  const QString addrHex = QStringLiteral("0x%1").arg(addr, 0, 16).toUpper();
  const bool ownsMenu = (baseMenu == nullptr);
  QMenu* menu = baseMenu ? baseMenu : new QMenu(this);
  if (!ownsMenu) menu->addSeparator();
  menu->addAction(QStringLiteral("Run to Instruction Address (%1)").arg(addrHex), this, [this, addr]() { emit runToAddressRequested(addr); });
  menu->addAction(QStringLiteral("Jump / Set PC to Address (%1)").arg(addrHex), this, [this, addr]() { emit jumpToAddressRequested(addr); });
  menu->addSeparator();
  QAction* calcAct = menu->addAction(QStringLiteral("Open Address in Programmer's Calculator (%1)").arg(addrHex));
  for (const QString& actLabel : contextActionNamesForAddress(addr)) {
    QAction* a = menu->addAction(actLabel);
    if (actLabel == QStringLiteral("Revert All Patches")) a->setEnabled(!m_patchedInstructions.isEmpty());
    connect(a, &QAction::triggered, this, [this, addr, actLabel]() { triggerContextAction(addr, actLabel); });
  }
  if (!property("gl_suppress_modal_exec").toBool()) {
    if (menu->exec(globalPos) == calcAct) emit sendToCalculatorRequested(addrHex);
  } else if (const QString autoAct = property("gl_auto_trigger_context_action").toString(); !autoAct.isEmpty()) {
    if (autoAct.contains(QStringLiteral("Calculator"), Qt::CaseInsensitive)) emit sendToCalculatorRequested(addrHex);
    else triggerContextAction(addr, autoAct);
  }
  delete menu;
}

}  // namespace gridlock::ui

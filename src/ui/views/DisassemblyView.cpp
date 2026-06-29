#include "DisassemblyView.hpp"
#include "../../core/managers/ConfigManager.hpp"

namespace gridlock::ui {

AsmSyntaxHighlighter::AsmSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent) {
  HighlightingRule rule;

  // Opcodes: Light Green / Cyan
  QTextCharFormat opcodeFormat;
  opcodeFormat.setForeground(
      QColor(gridlock::core::ConfigManager::instance().getAssemblyOpcode()));
  opcodeFormat.setFontWeight(QFont::Bold);
  const QString opcodePatterns[] = {
      QStringLiteral("\\bmov\\b"),  QStringLiteral("\\bpush\\b"),
      QStringLiteral("\\bsub\\b"),  QStringLiteral("\\bcall\\b"),
      QStringLiteral("\\blea\\b"),  QStringLiteral("\\bnop\\b"),
      QStringLiteral("\\bpop\\b"),  QStringLiteral("\\bret\\b"),
      QStringLiteral("\\badd\\b"),  QStringLiteral("\\band\\b"),
      QStringLiteral("\\bor\\b"),   QStringLiteral("\\bxor\\b"),
      QStringLiteral("\\btest\\b"), QStringLiteral("\\bcmp\\b"),
      QStringLiteral("\\bjmp\\b"),  QStringLiteral("\\bje\\b"),
      QStringLiteral("\\bmovl\\b"), QStringLiteral("\\bmovq\\b")};
  for (const QString &pattern : opcodePatterns) {
    rule.pattern = QRegularExpression(pattern);
    rule.format = opcodeFormat;
    highlightingRules.append(rule);
  }

  // Registers: Light Red / Orange
  QTextCharFormat registerFormat;
  registerFormat.setForeground(
      QColor(gridlock::core::ConfigManager::instance().getAssemblyRegister()));
  rule.pattern = QRegularExpression(QStringLiteral("%[a-z0-9]+"));
  rule.format = registerFormat;
  highlightingRules.append(rule);

  // Hex values: Muted Green / Purple
  QTextCharFormat hexFormat;
  hexFormat.setForeground(
      QColor(gridlock::core::ConfigManager::instance().getAssemblyAddress()));
  rule.pattern = QRegularExpression(QStringLiteral("0x[0-9a-fA-F]+"));
  rule.format = hexFormat;
  highlightingRules.append(rule);
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

DisassemblyView::DisassemblyView(QWidget *parent) : QPlainTextEdit(parent) {
  setReadOnly(true);
  QFont font("monospace");
  font.setStyleHint(QFont::Monospace);
  font.setPointSize(11);
  setFont(font);

  QPalette p = this->palette();
  p.setColor(
      QPalette::Base,
      QColor(gridlock::core::ConfigManager::instance().getSourceBackground()));
  p.setColor(QPalette::Text,
             QColor(gridlock::core::ConfigManager::instance().getSourceText()));
  this->setPalette(p);

  m_highlighter = new AsmSyntaxHighlighter(document());

  setPlainText(
      "No disassembly available. Start the program or select a running rank.");
}

void DisassemblyView::updateDisassembly(const QString &asmCode) {
  if (toPlainText() != asmCode) {
    setPlainText(asmCode.isEmpty() ? "; Select a rank to view disassembly..."
                                   : asmCode);
  }
}

} // namespace gridlock::ui

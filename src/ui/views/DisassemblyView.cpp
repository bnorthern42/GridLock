#include "DisassemblyView.hpp"
#include "../../core/managers/ConfigManager.hpp"
#include <QSettings>

namespace gridlock::ui {

AsmSyntaxHighlighter::AsmSyntaxHighlighter(QTextDocument *parent)
    : QSyntaxHighlighter(parent) {
  reloadRules();
}

void AsmSyntaxHighlighter::reloadRules() {
  highlightingRules.clear();
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

DisassemblyView::DisassemblyView(QWidget *parent) : QPlainTextEdit(parent) {
  setReadOnly(true);
  QFont f("monospace");
  f.setStyleHint(QFont::Monospace);
  setFont(f);

  m_highlighter = new AsmSyntaxHighlighter(document());
  reloadStyle();

  setPlainText(
      "No disassembly available. Start the program or select a running rank.");
}

void DisassemblyView::updateDisassembly(const QString &asmCode) {
  if (toPlainText() != asmCode) {
    setPlainText(asmCode.isEmpty() ? "; Select a rank to view disassembly..."
                                   : asmCode);
  }
}

void DisassemblyView::reloadStyle() {
  QSettings s("gridlock", "debugger");
  int fontSize = s.value("appearance/code_font_size", 12).toInt();

  QFont f = font();
  f.setPointSize(fontSize);
  setFont(f);

  setStyleSheet(QString("QPlainTextEdit { background-color: %1; color: %2; }")
                    .arg(gridlock::core::ConfigManager::instance().getSourceBackground())
                    .arg(gridlock::core::ConfigManager::instance().getSourceText()));

  if (m_highlighter) {
    m_highlighter->reloadRules();
  }
}

} // namespace gridlock::ui

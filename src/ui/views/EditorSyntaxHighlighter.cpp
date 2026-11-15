/**
 * @file EditorSyntaxHighlighter.cpp
 * @brief Implementation of multi-format syntax highlighting and stateful Rainbow Brackets.
 *
 * Implements theme-adaptive token coloring for C/C++, TOML, YAML, Markdown, and
 * RFC-4180 CSV/TSV files alongside cumulative depth tracking for nested (), {}, [] brackets.
 */

#include "EditorSyntaxHighlighter.hpp"

#include "../../core/managers/ConfigManager.hpp"

#include <QCoreApplication>
#include <QFileInfo>

namespace gridlock::ui {

namespace {
bool isDarkPaletteActive() {
    if (qApp && qApp->property("gridlock_dark_mode").isValid()) {
        return qApp->property("gridlock_dark_mode").toBool();
    }
    return true;
}

QTextCharFormat makeFormat(const QColor& color, int weight = QFont::Normal, bool italic = false) {
    QTextCharFormat fmt;
    fmt.setForeground(color);
    if (weight != QFont::Normal) fmt.setFontWeight(weight);
    if (italic) fmt.setFontItalic(true);
    return fmt;
}
}  // namespace

/**
 * @brief Constructs the highlighter, reads ConfigManager defaults, and builds theme formats.
 * @param parent Underlying QTextDocument instance.
 */
EditorSyntaxHighlighter::EditorSyntaxHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent) {
    auto& cfg = gridlock::core::ConfigManager::instance();
    m_rainbowBracketsEnabled = cfg.rainbowBracketsEnabled();
    m_dataDocHighlightingEnabled = cfg.dataDocHighlightingEnabled();
    m_commentStartRe = QRegularExpression(QStringLiteral("/\\*"));
    m_commentEndRe = QRegularExpression(QStringLiteral("\\*/"));
    rebuildFormatsForTheme(isDarkPaletteActive());
}

/**
 * @brief Infers the highlighting LanguageMode from a file extension.
 * @param filePath Workspace file path whose suffix selects the active grammar.
 * @param dataDocEnabled True to enable TOML, YAML, Markdown, and CSV/TSV modes.
 * @return Matching LanguageMode enum value.
 */
EditorSyntaxHighlighter::LanguageMode EditorSyntaxHighlighter::detectLanguageForFile(
    const QString& filePath, bool dataDocEnabled) {
    // Default unnamed scratch buffers to C/C++ highlighting.
    if (filePath.isEmpty()) return LanguageMode::Cpp;
    const QString ext = QFileInfo(filePath).suffix().toLower();
    if (ext == QStringLiteral("c") || ext == QStringLiteral("cc") || ext == QStringLiteral("cpp") ||
        ext == QStringLiteral("cxx") || ext == QStringLiteral("c++") || ext == QStringLiteral("h") ||
        ext == QStringLiteral("hh") || ext == QStringLiteral("hpp") || ext == QStringLiteral("hxx") ||
        ext == QStringLiteral("ipp") || ext == QStringLiteral("cu") || ext == QStringLiteral("cuh")) {
        return LanguageMode::Cpp;
    }
    // Check auxiliary configuration, documentation, and delimited dataset extensions when enabled.
    if (dataDocEnabled) {
        if (ext == QStringLiteral("toml")) return LanguageMode::Toml;
        if (ext == QStringLiteral("yaml") || ext == QStringLiteral("yml")) return LanguageMode::Yaml;
        if (ext == QStringLiteral("md") || ext == QStringLiteral("markdown")) return LanguageMode::Markdown;
        if (ext == QStringLiteral("csv") || ext == QStringLiteral("tsv")) return LanguageMode::Csv;
    }
    return LanguageMode::PlainText;
}

/**
 * @brief Updates the file path, recalculates language/delimiter mode, and rehighlights when changed.
 * @param filePath New active file path loaded into the editor view.
 */
void EditorSyntaxHighlighter::setFilePath(const QString& filePath) {
    m_filePath = filePath;
    // Choose tab delimiter for TSV datasets and comma delimiter for CSV datasets.
    const QChar newDelim = filePath.endsWith(QStringLiteral(".tsv"), Qt::CaseInsensitive)
                               ? QLatin1Char('\t')
                               : QLatin1Char(',');
    const LanguageMode nextMode = detectLanguageForFile(filePath, m_dataDocHighlightingEnabled);
    if (nextMode != m_languageMode || newDelim != m_csvDelimiter) {
        m_languageMode = nextMode;
        m_csvDelimiter = newDelim;
        rehighlight();
    }
}

/**
 * @brief Explicitly sets the LanguageMode and rehighlights the document.
 * @param mode Target highlighting grammar mode.
 */
void EditorSyntaxHighlighter::setLanguageMode(LanguageMode mode) {
    if (m_languageMode == mode) return;
    m_languageMode = mode;
    rehighlight();
}

/**
 * @brief Updates Rainbow Brackets enablement and immediately rehighlights the document.
 * @param enabled True to colorize nested brackets by depth; false to disable.
 */
void EditorSyntaxHighlighter::setRainbowBracketsEnabled(bool enabled) {
    if (m_rainbowBracketsEnabled == enabled) return;
    m_rainbowBracketsEnabled = enabled;
    rehighlight();
}

/**
 * @brief Updates auxiliary data/doc highlighting toggle and refreshes active file language mode.
 * @param enabled True to highlight TOML, YAML, Markdown, and CSV/TSV files.
 */
void EditorSyntaxHighlighter::setDataDocHighlightingEnabled(bool enabled) {
    const LanguageMode prevMode = m_languageMode;
    m_dataDocHighlightingEnabled = enabled;
    if (!m_filePath.isEmpty()) {
        m_languageMode = detectLanguageForFile(m_filePath, m_dataDocHighlightingEnabled);
    }
    if (m_languageMode != prevMode) rehighlight();
}

/**
 * @brief Rebuilds dark/light theme formats from active ConfigManager and ThemeManager settings.
 */
void EditorSyntaxHighlighter::reloadPalette() {
    auto& cfg = gridlock::core::ConfigManager::instance();
    m_rainbowBracketsEnabled = cfg.rainbowBracketsEnabled();
    m_dataDocHighlightingEnabled = cfg.dataDocHighlightingEnabled();
    if (!m_filePath.isEmpty()) {
        m_languageMode = detectLanguageForFile(m_filePath, m_dataDocHighlightingEnabled);
    }
    rebuildFormatsForTheme(isDarkPaletteActive());
    rehighlight();
}

/** @brief Compiles token formats and regex rule tables tuned for either Dark or Light canvas contrast. */
void EditorSyntaxHighlighter::rebuildFormatsForTheme(bool isDark) {
    m_cppRules.clear();
    m_tomlRules.clear();
    m_yamlRules.clear();
    m_markdownRules.clear();
    m_rainbowFormats.clear();
    m_csvColumnFormats.clear();

    // Shared semantic palette adapted for dark and light workstation themes.
    // Dark tones favor muted pastels on #16181d; light tones deepen saturation for >=4.5:1 contrast.
    const QColor cType    = isDark ? QColor(86, 156, 214)  : QColor(0, 92, 197);
    const QColor cKeyword = isDark ? QColor(224, 108, 175) : QColor(175, 30, 100);
    const QColor cPreproc = isDark ? QColor(229, 149, 75)  : QColor(176, 70, 0);
    const QColor cMpi     = isDark ? QColor(186, 138, 232) : QColor(111, 66, 193);
    const QColor cString  = isDark ? QColor(115, 201, 128) : QColor(34, 134, 58);
    const QColor cNumber  = isDark ? QColor(209, 154, 102) : QColor(152, 104, 1);
    const QColor cComment = isDark ? QColor(125, 132, 145) : QColor(106, 115, 125);
    const QColor cKey     = isDark ? QColor(86, 182, 194)  : QColor(1, 132, 188);
    const QColor cHeader  = isDark ? QColor(97, 175, 239)  : QColor(5, 80, 174);
    const QColor cAnchor  = isDark ? QColor(235, 175, 85)  : QColor(168, 85, 0);
    const QColor cCode    = isDark ? QColor(229, 192, 123) : QColor(149, 56, 0);

    // Pre-build reusable stateful formats referenced directly by multi-line scanners.
    m_commentFormat = makeFormat(cComment, QFont::Normal, true);
    m_stringFormat  = makeFormat(cString);
    m_headingFormat = makeFormat(cHeader, QFont::Bold);
    m_boldFormat    = makeFormat(cKeyword, QFont::Bold);
    m_italicFormat  = makeFormat(cMpi, QFont::Normal, true);
    m_codeFormat    = makeFormat(cCode);
    m_linkFormat    = makeFormat(cType);
    m_linkFormat.setFontUnderline(true);
    m_quoteFormat      = makeFormat(cComment, QFont::Normal, true);
    m_listMarkerFormat = makeFormat(cPreproc, QFont::Bold);

    // Build C/C++ token rules in precedence order (earlier formats can be overridden by strings/comments).
    // Covers C++20/23 keywords, Qt slots/signals, preprocessor directives, MPI_* APIs, and numeric literals.
    const QTextCharFormat typeFmt = makeFormat(cType, QFont::Bold);
    const QTextCharFormat kwFmt   = makeFormat(cKeyword, QFont::Bold);
    m_cppRules.append({QRegularExpression(QStringLiteral(
        "\\b(?:auto|bool|char|class|concept|const|consteval|constexpr|constinit|decltype|double|enum|"
        "explicit|float|friend|inline|int|long|mutable|namespace|operator|private|protected|public|"
        "short|signals|signed|slots|static|struct|template|typedef|typename|union|unsigned|virtual|void|volatile)\\b")), typeFmt});
    m_cppRules.append({QRegularExpression(QStringLiteral(
        "\\b(?:break|case|catch|co_await|co_return|co_yield|continue|default|delete|do|else|false|final|"
        "for|if|new|noexcept|nullptr|override|requires|return|sizeof|switch|this|throw|true|try|using|while)\\b")), kwFmt});
    m_cppRules.append({QRegularExpression(QStringLiteral("#\\s*[A-Za-z_][A-Za-z0-9_]*[^\n]*")), makeFormat(cPreproc)});
    m_cppRules.append({QRegularExpression(QStringLiteral("\\bMPI_[A-Za-z0-9_]+\\b")), makeFormat(cMpi, QFont::Bold)});
    m_cppRules.append({QRegularExpression(QStringLiteral("\\b(?:0[xXbB][0-9A-Fa-f_']+|(?:\\d[\\d']*(?:\\.\\d[\\d']*)?(?:[eE][+-]?\\d+)?[fFlLuU]*))\\b")), makeFormat(cNumber)});
    // Apply single-line double/single quoted string literals and // line comments last.
    m_cppRules.append({QRegularExpression(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\"")), m_stringFormat});
    m_cppRules.append({QRegularExpression(QStringLiteral("'(?:\\\\.|[^'\\\\])'")), m_stringFormat});
    m_cppRules.append({QRegularExpression(QStringLiteral("//[^\n]*")), m_commentFormat});

    // Build TOML rules: [table] and [[array_of_tables]] headers, bare/quoted keys before '=',
    // booleans, RFC-3339 timestamps/dates, hex/octal/binary/float numbers, and single-line strings.
    // Note: # comments and """ / ''' multiline strings are handled post-regex to respect string quotes.
    m_tomlRules.append({QRegularExpression(QStringLiteral("^\\s*\\[\\[?[^\\]\\r\\n]+\\]?\\]")), makeFormat(cHeader, QFont::Bold)});
    m_tomlRules.append({QRegularExpression(QStringLiteral("^\\s*(?:\"[^\"\\r\\n]+\"|'[^'\\r\\n]+'|[A-Za-z0-9_-]+)\\s*(?==)")), makeFormat(cKey, QFont::Bold)});
    m_tomlRules.append({QRegularExpression(QStringLiteral("\\b(?:true|false)\\b")), kwFmt});
    m_tomlRules.append({QRegularExpression(QStringLiteral("\\b\\d{4}-\\d{2}-\\d{2}(?:[T ]\\d{2}:\\d{2}:\\d{2}(?:\\.\\d+)?(?:Z|[+-]\\d{2}:\\d{2})?)?\\b")), makeFormat(cNumber)});
    m_tomlRules.append({QRegularExpression(QStringLiteral("\\b[+-]?(?:0[xob][0-9A-Fa-f_]+|\\d[\\d_]*(?:\\.\\d[\\d_]*)?(?:[eE][+-]?\\d[\\d_]*)?|inf|nan)\\b")), makeFormat(cNumber)});
    m_tomlRules.append({QRegularExpression(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\"")), m_stringFormat});
    m_tomlRules.append({QRegularExpression(QStringLiteral("'[^'\\r\\n]*'")), m_stringFormat});

    // Build YAML rules: ---/... document markers, list '-' indicators, mapping keys before ':',
    // &anchor / *alias references, YAML booleans/null, numbers, and single/double-quoted strings.
    m_yamlRules.append({QRegularExpression(QStringLiteral("^\\s*(?:---|\\.\\.\\.)\\s*$")), makeFormat(cMpi, QFont::Bold)});
    m_yamlRules.append({QRegularExpression(QStringLiteral("^\\s*-(?=\\s|$)")), m_listMarkerFormat});
    m_yamlRules.append({QRegularExpression(QStringLiteral("^\\s*(?:-\\s+)?(?:\"[^\"\\r\\n]+\"|'[^'\\r\\n]+'|[A-Za-z0-9_.-]+)\\s*(?=:)")), makeFormat(cKey, QFont::Bold)});
    m_yamlRules.append({QRegularExpression(QStringLiteral("(?:&|\\*)[A-Za-z0-9_.-]+")), makeFormat(cAnchor, QFont::Bold)});
    m_yamlRules.append({QRegularExpression(QStringLiteral("\\b(?:true|false|null|yes|no|on|off)\\b")), kwFmt});
    m_yamlRules.append({QRegularExpression(QStringLiteral("\\b[+-]?(?:0[xob][0-9A-Fa-f_]+|\\d+(?:\\.\\d+)?(?:[eE][+-]?\\d+)?)\\b")), makeFormat(cNumber)});
    m_yamlRules.append({QRegularExpression(QStringLiteral("\"(?:\\\\.|[^\"\\\\])*\"")), m_stringFormat});
    m_yamlRules.append({QRegularExpression(QStringLiteral("'[^'\\r\\n]*'")), m_stringFormat});

    // Build Markdown inline rules: ATX headings (#..######), > blockquotes, unordered/ordered list bullets,
    // **bold** / __bold__, *italic* / _italic_, `inline code`, and [label](url) links.
    m_markdownRules.append({QRegularExpression(QStringLiteral("^\\s*#{1,6}\\s+.*$")), m_headingFormat});
    m_markdownRules.append({QRegularExpression(QStringLiteral("^\\s*>.*$")), m_quoteFormat});
    m_markdownRules.append({QRegularExpression(QStringLiteral("^\\s*(?:[-*+]|\\d+\\.)\\s+")), m_listMarkerFormat});
    m_markdownRules.append({QRegularExpression(QStringLiteral("\\*\\*[^*\\r\\n]+\\*\\*|__[^_\\r\\n]+__")), m_boldFormat});
    m_markdownRules.append({QRegularExpression(QStringLiteral("(?<!\\*)\\*[^*\\r\\n]+\\*(?!\\*)|(?<!_)_[^_\\r\\n]+_(?!_)")), m_italicFormat});
    m_markdownRules.append({QRegularExpression(QStringLiteral("`[^`\\r\\n]+`")), m_codeFormat});
    m_markdownRules.append({QRegularExpression(QStringLiteral("\\[[^\\]\\r\\n]+\\]\\([^\\)\\r\\n]+\\)")), m_linkFormat});

    // Construct 5 cyclic Rainbow Brackets nesting depth colors (depth % 5) plus an underlined
    // crimson error format used when a closing ), }, or ] occurs at depth <= 0.
    const QVector<QColor> rbColors = isDark
        ? QVector<QColor>{{255, 215, 0}, {218, 112, 214}, {86, 182, 194}, {152, 195, 121}, {97, 175, 239}}
        : QVector<QColor>{{181, 137, 0}, {166, 38, 164}, {1, 132, 188}, {64, 120, 242}, {152, 104, 1}};
    for (const QColor& c : rbColors) m_rainbowFormats.append(makeFormat(c, QFont::Bold));

    m_unmatchedBracketFormat = makeFormat(isDark ? QColor(255, 85, 85) : QColor(207, 34, 46), QFont::Bold);
    m_unmatchedBracketFormat.setFontUnderline(true);

    // Construct 6 alternating high-contrast column colors rotated across CSV / TSV columns.
    const QVector<QColor> csvColors = isDark
        ? QVector<QColor>{{97, 175, 239}, {152, 195, 121}, {229, 192, 123}, {198, 120, 221}, {86, 182, 194}, {224, 108, 117}}
        : QVector<QColor>{{5, 80, 174}, {17, 99, 41}, {154, 103, 0}, {130, 80, 223}, {1, 132, 188}, {164, 14, 38}};
    for (const QColor& c : csvColors) m_csvColumnFormats.append(makeFormat(c));
}

/** @brief Dispatches block highlighting according to the active LanguageMode. */
void EditorSyntaxHighlighter::highlightBlock(const QString& text) {
    // Qt initializes unvisited blocks with userState() == -1; normalize negative sentinel to 0.
    // Bits [1:0] store active multi-line token substate; bits [15:2] pack cumulative bracket depth.
    const int prevRaw = (previousBlockState() < 0) ? 0 : previousBlockState();
    setCurrentBlockState(0);
    switch (m_languageMode) {
        case LanguageMode::PlainText: break;
        case LanguageMode::Cpp:      highlightCppBlock(text, prevRaw); break;
        case LanguageMode::Toml:     highlightTomlBlock(text, prevRaw); break;
        case LanguageMode::Yaml:     highlightYamlBlock(text); break;
        case LanguageMode::Markdown: highlightMarkdownBlock(text, prevRaw); break;
        case LanguageMode::Csv:      highlightCsvBlock(text); break;
    }
}

/** @brief Highlights a C/C++ block including multi-line block comments and cross-line Rainbow Brackets. */
void EditorSyntaxHighlighter::highlightCppBlock(const QString& text, int prevRaw) {
    // Apply single-line C++ lexical rules (types, keywords, preprocessor directives, numbers, strings, // comments).
    for (const HighlightingRule& rule : std::as_const(m_cppRules)) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), rule.format);
        }
    }
    // Unpack multi-line comment continuation bit (bit 0) and incoming bracket depth (bits 15:2).
    const bool startsInComment = (prevRaw & 0x1) != 0;
    const int prevDepth = (prevRaw >> 2) & 0x3FFF;
    int subState = 0;
    // Scan left-to-right across multi-line block comment spans starting either at col 0 or next opening delimiter.
    int startIndex = startsInComment ? 0 : text.indexOf(m_commentStartRe);
    while (startIndex >= 0) {
        // Advance past opening '/*' unless resuming an already-open comment at column 0.
        const auto m = m_commentEndRe.match(text, startIndex + (startsInComment && startIndex == 0 ? 0 : 2));
        const int endIndex = m.capturedStart();
        int commentLen = 0;
        if (endIndex == -1) {
            // Unclosed block comment carries subState = 1 into the following paragraph block.
            subState = 1;
            commentLen = text.length() - startIndex;
        } else {
            commentLen = endIndex - startIndex + m.capturedLength();
        }
        setFormat(startIndex, commentLen, m_commentFormat);
        startIndex = text.indexOf(m_commentStartRe, startIndex + commentLen);
    }
    // Apply cross-line Rainbow Brackets depth overlay when enabled; otherwise preserve comment bit only.
    if (m_rainbowBracketsEnabled) {
        applyRainbowBrackets(text, prevDepth, startsInComment, subState);
    } else {
        setCurrentBlockState(subState);
    }
}

/** @brief Colors nested (), {}, [] by cumulative depth while skipping strings, chars, and comments. */
void EditorSyntaxHighlighter::applyRainbowBrackets(
    const QString& text, int prevDepth, bool startsInComment, int blockSubState) {
    int depth = prevDepth;
    bool inComment = startsInComment;
    // Walk character by character so string/char escapes and comments mask bracket delimiters accurately.
    for (int i = 0; i < text.length(); ++i) {
        const QChar ch = text.at(i);
        const QChar next = (i + 1 < text.length()) ? text.at(i + 1) : QChar();
        // Remain inert inside multi-line block comments until matching closing delimiter.
        if (inComment) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) { inComment = false; ++i; }
            continue;
        }
        // Halt scanning at '//' line comments because the remainder of the line is non-code prose.
        if (ch == QLatin1Char('/') && next == QLatin1Char('/')) break;
        // Enter multi-line block comment state when encountering '/*'.
        if (ch == QLatin1Char('/') && next == QLatin1Char('*')) { inComment = true; ++i; continue; }
        // Skip string literals so brackets inside "..." keep string coloring.
        if (ch == QLatin1Char('"')) {
            ++i;
            while (i < text.length()) {
                // Skip escaped character sequences like \" or \\ cleanly.
                if (text.at(i) == QLatin1Char('\\')) { ++i; }
                else if (text.at(i) == QLatin1Char('"')) break;
                ++i;
            }
            continue;
        }
        // Skip C++ character literals without misinterpreting C++14 digit separators (e.g. 1'000).
        if (ch == QLatin1Char('\'') && (i == 0 || !text.at(i - 1).isDigit())) {
            ++i;
            while (i < text.length()) {
                // Handle escaped quotes '\'' cleanly inside character literals.
                if (text.at(i) == QLatin1Char('\\')) { ++i; }
                else if (text.at(i) == QLatin1Char('\'')) break;
                ++i;
            }
            continue;
        }
        // Color opening delimiter at current depth before incrementing depth for children.
        if (ch == QLatin1Char('(') || ch == QLatin1Char('{') || ch == QLatin1Char('[')) {
            setFormat(i, 1, m_rainbowFormats.at(depth % m_rainbowFormats.size()));
            ++depth;
        } else if (ch == QLatin1Char(')') || ch == QLatin1Char('}') || ch == QLatin1Char(']')) {
            // Decrement first on valid close so paired open/close brackets share the identical tier color.
            if (depth > 0) {
                --depth;
                setFormat(i, 1, m_rainbowFormats.at(depth % m_rainbowFormats.size()));
            } else {
                // Flag extraneous closing brackets with high-visibility error styling.
                setFormat(i, 1, m_unmatchedBracketFormat);
            }
        }
    }
    // Pack updated nesting depth into upper bits alongside comment substate in lower 2 bits.
    setCurrentBlockState(((depth & 0x3FFF) << 2) | (blockSubState & 0x3));
}

/** @brief Applies # comment formatting cleanly from the first '#' outside single/double quotes. */
void EditorSyntaxHighlighter::applyQuoteAwareHashComment(const QString& text) {
    bool inDouble = false, inSingle = false;
    // Track active quote state left-to-right so hex colors or strings containing '#' stay valid literals.
    for (int i = 0; i < text.length(); ++i) {
        const QChar c = text.at(i);
        // Backslash escapes only apply inside double-quoted strings in TOML/YAML basic strings.
        if (c == QLatin1Char('\\') && inDouble) { ++i; continue; }
        // Toggle double-quoted string state unless currently inside single quotes.
        if (c == QLatin1Char('"') && !inSingle) { inDouble = !inDouble; continue; }
        // Toggle single-quoted literal string state unless currently inside double quotes.
        if (c == QLatin1Char('\'') && !inDouble) { inSingle = !inSingle; continue; }
        // First unquoted '#' terminates code tokens and starts a line comment to end of block.
        if (c == QLatin1Char('#') && !inDouble && !inSingle) {
            setFormat(i, text.length() - i, m_commentFormat);
            return;
        }
    }
}

/** @brief Highlights a TOML block including multi-line triple-quoted strings and # comments. */
void EditorSyntaxHighlighter::highlightTomlBlock(const QString& text, int prevRaw) {
    // Substates: 0 = normal code, 1 = inside multi-line """ basic string, 2 = inside ''' literal string.
    int state = prevRaw & 0x3;
    int offset = 0;
    if (state == 1 || state == 2) {
        // Consume characters until matching closing triple-quote delimiter on this line.
        const QString closing = (state == 1) ? QStringLiteral("\"\"\"") : QStringLiteral("'''");
        const int endIdx = text.indexOf(closing);
        if (endIdx < 0) {
            // Entire block remains part of the open multi-line string; preserve state for next line.
            setFormat(0, text.length(), m_stringFormat);
            setCurrentBlockState(state);
            return;
        }
        // Highlight prefix up through closing triple quote and resume normal TOML scanning after it.
        const int span = endIdx + 3;
        setFormat(0, span, m_stringFormat);
        offset = span;
        state = 0;
    }
    // Apply single-line TOML grammar rules starting past any terminated multi-line string prefix.
    for (const HighlightingRule& rule : std::as_const(m_tomlRules)) {
        auto it = rule.pattern.globalMatch(text, offset);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), rule.format);
        }
    }
    // Check if an unclosed """ or ''' multi-line string begins after offset on this line.
    const int dq3 = text.indexOf(QStringLiteral("\"\"\""), offset);
    const int sq3 = text.indexOf(QStringLiteral("'''"), offset);
    if (dq3 >= 0 && text.indexOf(QStringLiteral("\"\"\""), dq3 + 3) < 0) {
        setFormat(dq3, text.length() - dq3, m_stringFormat);
        setCurrentBlockState(1);
        return;
    }
    if (sq3 >= 0 && text.indexOf(QStringLiteral("'''"), sq3 + 3) < 0) {
        setFormat(sq3, text.length() - sq3, m_stringFormat);
        setCurrentBlockState(2);
        return;
    }
    // Overlay trailing # comments safely outside string literals.
    applyQuoteAwareHashComment(text);
    setCurrentBlockState(0);
}

/** @brief Highlights a YAML block (keys, list markers, anchors/aliases, separators, strings, # comments). */
void EditorSyntaxHighlighter::highlightYamlBlock(const QString& text) {
    // Evaluate document markers, keys, anchors/aliases, scalars, and quoted strings in order.
    for (const HighlightingRule& rule : std::as_const(m_yamlRules)) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), rule.format);
        }
    }
    // Apply # comment overlay last while preserving hash characters inside quoted strings.
    applyQuoteAwareHashComment(text);
}

/** @brief Highlights a Markdown block with stateful fenced code blocks and inline formatting. */
void EditorSyntaxHighlighter::highlightMarkdownBlock(const QString& text, int prevRaw) {
    // Track fenced code-block toggle state via bit 0 across paragraph boundaries.
    bool inFence = (prevRaw & 0x1) != 0;
    static const QRegularExpression fenceRe(QStringLiteral("^\\s*(?:```|~~~)"));
    if (fenceRe.match(text).hasMatch()) {
        // Fence delimiter line toggles inFence state cleanly.
        setFormat(0, text.length(), m_codeFormat);
        setCurrentBlockState(inFence ? 0 : 1);
        return;
    }
    if (inFence) {
        // Render every line inside an active fenced code block uniformly using monospace code color.
        setFormat(0, text.length(), m_codeFormat);
        setCurrentBlockState(1);
        return;
    }
    // Apply Markdown headings, blockquotes, lists, bold/italic spans, inline code, and links.
    for (const HighlightingRule& rule : std::as_const(m_markdownRules)) {
        auto it = rule.pattern.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), rule.format);
        }
    }
}

/** @brief Highlights RFC 4180 CSV/TSV columns with alternating per-column palette colors. */
void EditorSyntaxHighlighter::highlightCsvBlock(const QString& text) {
    if (m_csvColumnFormats.isEmpty()) return;
    bool inQuotes = false;
    int col = 0;
    int fieldStart = 0;
    // Split on m_csvDelimiter (',' for .csv, '\t' for .tsv) strictly outside quoted RFC-4180 cells.
    for (int i = 0; i < text.length(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('"')) {
            // Treat doubled "" inside an active quoted cell as an escaped literal quote, not a terminator.
            if (inQuotes && i + 1 < text.length() && text.at(i + 1) == QLatin1Char('"')) {
                ++i;
                continue;
            }
            inQuotes = !inQuotes;
        } else if (!inQuotes && ch == m_csvDelimiter) {
            // Paint completed field span with its column-indexed hue, leaving the delimiter uncolored.
            if (i > fieldStart) {
                setFormat(fieldStart, i - fieldStart, m_csvColumnFormats.at(col % m_csvColumnFormats.size()));
            }
            col = (col + 1) % m_csvColumnFormats.size();
            fieldStart = i + 1;
        }
    }
    // Paint trailing final column after the last unquoted delimiter on the line.
    if (text.length() > fieldStart) {
        setFormat(fieldStart, text.length() - fieldStart, m_csvColumnFormats.at(col % m_csvColumnFormats.size()));
    }
}

}  // namespace gridlock::ui

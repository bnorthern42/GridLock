#pragma once

/**
 * @file EditorSyntaxHighlighter.hpp
 * @brief Multi-format syntax highlighter (C/C++, TOML, YAML, Markdown, CSV) with stateful Rainbow Brackets.
 *
 * Extracted from SourceCodeView to provide theme-adaptive syntax coloring across source code,
 * build/config files, and datasets along with cross-line Rainbow Bracket nesting depth tracking.
 */

#include <QColor>
#include <QRegularExpression>
#include <QString>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

namespace gridlock::ui {

/**
 * @class EditorSyntaxHighlighter
 * @brief Theme-aware multi-language QSyntaxHighlighter with configurable cross-line Rainbow Brackets.
 */
class EditorSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    /// Supported editor highlighting modes selected from file extension or explicitly.
    enum class LanguageMode {
        PlainText,
        Cpp,
        Toml,
        Yaml,
        Markdown,
        Csv
    };

    /**
     * @brief Constructs the highlighter attached to `parent` and initializes theme/config options.
     * @param parent Underlying QTextDocument instance.
     */
    explicit EditorSyntaxHighlighter(QTextDocument* parent = nullptr);

    /**
     * @brief Infers the appropriate LanguageMode for `filePath` and auxiliary highlighting toggle.
     * @param filePath Path or filename whose extension selects the grammar.
     * @param dataDocEnabled True to enable TOML, YAML, Markdown, and CSV/TSV highlighting.
     * @return Detected LanguageMode value.
     */
    static LanguageMode detectLanguageForFile(const QString& filePath, bool dataDocEnabled = true);

    /** @brief Sets the active file path, detects language/delimiter mode, and refreshes highlighting if changed. */
    void setFilePath(const QString& filePath);

    /** @brief Explicitly overrides the active highlighting LanguageMode and rehighlights the document. */
    void setLanguageMode(LanguageMode mode);

    /** @brief Returns the currently active highlighting LanguageMode. */
    LanguageMode languageMode() const { return m_languageMode; }

    /** @brief Enables or disables nested (), {}, [] depth coloring and triggers immediate rehighlighting. */
    void setRainbowBracketsEnabled(bool enabled);

    /** @brief Returns whether nested Rainbow Brackets depth coloring is enabled. */
    bool rainbowBracketsEnabled() const { return m_rainbowBracketsEnabled; }

    /** @brief Enables or disables TOML/YAML/Markdown/CSV highlighting and re-evaluates the current file mode. */
    void setDataDocHighlightingEnabled(bool enabled);

    /** @brief Returns whether TOML/YAML/Markdown/CSV syntax highlighting is enabled. */
    bool dataDocHighlightingEnabled() const { return m_dataDocHighlightingEnabled; }

    /** @brief Rebuilds dark/light-contrast token formats from the active ThemeManager palette and rehighlights. */
    void reloadPalette();

protected:
    /** @brief Highlights a single paragraph/block using the active LanguageMode and block state. */
    void highlightBlock(const QString& text) override;

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };

    void rebuildFormatsForTheme(bool isDark);
    void highlightCppBlock(const QString& text, int prevRaw);
    void highlightTomlBlock(const QString& text, int prevRaw);
    void highlightYamlBlock(const QString& text);
    void highlightMarkdownBlock(const QString& text, int prevRaw);
    void highlightCsvBlock(const QString& text);
    void applyRainbowBrackets(const QString& text, int prevDepth, bool startsInComment, int blockSubState);
    void applyQuoteAwareHashComment(const QString& text);

    LanguageMode m_languageMode = LanguageMode::Cpp;
    QString m_filePath;
    QChar m_csvDelimiter = QLatin1Char(',');
    bool m_rainbowBracketsEnabled = true;
    bool m_dataDocHighlightingEnabled = true;

    // Compiled token rule sets per language mode.
    QVector<HighlightingRule> m_cppRules;
    QVector<HighlightingRule> m_tomlRules;
    QVector<HighlightingRule> m_yamlRules;
    QVector<HighlightingRule> m_markdownRules;

    // Shared and language-specific semantic formats updated cleanly on theme changes.
    QTextCharFormat m_commentFormat;
    QTextCharFormat m_stringFormat;
    QTextCharFormat m_headingFormat;
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_codeFormat;
    QTextCharFormat m_linkFormat;
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_listMarkerFormat;
    QTextCharFormat m_unmatchedBracketFormat;
    QVector<QTextCharFormat> m_rainbowFormats;
    QVector<QTextCharFormat> m_csvColumnFormats;

    QRegularExpression m_commentStartRe;
    QRegularExpression m_commentEndRe;
};

}  // namespace gridlock::ui

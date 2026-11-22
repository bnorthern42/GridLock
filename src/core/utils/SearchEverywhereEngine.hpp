/**
 * @file SearchEverywhereEngine.hpp
 * @brief JetBrains-style CamelCase/word-boundary fuzzy scorer and multi-source workspace indexer.
 *
 * Provides deterministic CamelCase, snake_case, and path-segment fuzzy ranking, cached
 * workspace file and C/C++/Fortran/Python symbol indexing, fast capped workspace text
 * grep, and context-window source snippet extraction for CommandPaletteDialog.
 */

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace gridlock::core::utils {

/**
 * @enum SearchScope
 * @brief Selectable scope tabs in the Search Everywhere dialog.
 */
enum class SearchScope {
    All = 0,
    Files = 1,
    Symbols = 2,
    Text = 3,
    Actions = 4,
    Ranks = 5
};

/**
 * @struct IndexedSymbol
 * @brief Lightweight source-code symbol entry extracted from C/C++, CUDA, Fortran, or Python files.
 */
struct IndexedSymbol {
    QString name;
    QString kind;
    QString relPath;
    QString absPath;
    int lineNumber = 1;
};

/**
 * @struct TextSearchHit
 * @brief Single line match produced by workspace text grep.
 */
struct TextSearchHit {
    QString relPath;
    QString absPath;
    int lineNumber = 1;
    int column = 0;
    QString linePreview;
};

/**
 * @struct IndexedFile
 * @brief Workspace file entry with relative path, absolute path, and file-type badge.
 */
struct IndexedFile {
    QString relPath;
    QString absPath;
    QString badge;
};

/**
 * @class SearchEverywhereEngine
 * @brief Stateful workspace file/symbol cache plus static CamelCase fuzzy matcher and text grep helpers.
 */
class SearchEverywhereEngine {
public:
    SearchEverywhereEngine() = default;

    static int scoreMatch(const QString& query, const QString& candidate,
                          QVector<int>* matchedIndices = nullptr);

    static bool isBuildArtifactPath(const QString& path);

    static QString resolveWorkspaceRoot(const QString& activeDir, const QString& explorerDir);

    const QStringList& indexWorkspaceFiles(const QString& workspaceRoot, bool forceRefresh = false);

    QVector<IndexedFile> discoverPrioritizedFiles(const QString& workspaceRoot);

    /**
     * @brief Indexes C/C++/Fortran/Python symbols across @p workspaceRoot plus live unsaved @p activeBufferText.
     */
    QVector<IndexedSymbol> collectSymbols(const QString& workspaceRoot,
                                          const QString& activeFilePath = QString(),
                                          const QString& activeBufferText = QString());

    /**
     * @brief Performs fast line-by-line case-insensitive text search across indexed workspace files.
     */
    QVector<TextSearchHit> searchWorkspaceText(const QString& workspaceRoot,
                                               const QString& query,
                                               int maxResults = 80);

    /**
     * @brief Loads `+- contextLines` around @p targetOneBasedLine from @p filePath for the live preview pane.
     * @param filePath Target source file on disk.
     * @param targetOneBasedLine 1-based target line number.
     * @param contextLines Number of lines before and after @p targetOneBasedLine to include.
     * @param outStartLine Optional 1-based first line included in the returned snippet.
     * @param outHighlightedBlock Optional 0-based paragraph block index corresponding to @p targetOneBasedLine.
     * @return Raw source lines joined by '\n', or empty QString if unreadable.
     */
    static QString loadFilePreviewSnippet(const QString& filePath,
                                          int targetOneBasedLine,
                                          int contextLines = 6,
                                          int* outStartLine = nullptr,
                                          int* outHighlightedBlock = nullptr);

private:
    static void extractSymbolsFromText(const QString& text,
                                       const QString& relPath,
                                       const QString& absPath,
                                       QVector<IndexedSymbol>& out);

    QString m_cachedRoot;
    QStringList m_cachedFiles;
    QString m_cachedSymbolsRoot;
    QVector<IndexedSymbol> m_cachedDiskSymbols;
};

}  // namespace gridlock::core::utils

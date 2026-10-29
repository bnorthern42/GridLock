/**
 * @file DocsetManager.hpp
 * @brief Offline SQLite Dash/Zeal docset indexer and asynchronous search manager.
 *
 * Attaches up to 10 local `.docset` SQLite index databases (`docSet.dsidx`) into
 * an in-memory SQLite connection, providing fast keyword queries for the Reference
 * Manual dock and background source-tree scanning to suggest relevant HPC docsets.
 */

#pragma once

#include <QFuture>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

namespace gridlock::core {

/**
 * @struct DocsetEntry
 * @brief Represents a documentation entry resolved from offline SQLite indices or ISA reference.
 */
struct DocsetEntry {
    QString name;         ///< Symbol or instruction name.
    QString type;         ///< Entry classification type ("func", "Instruction", etc.).
    QString path;         ///< Filesystem path to documentation HTML.
    QString docsetName;   ///< Source docset bundle name.
    QString htmlContent;  ///< Loaded dark-slate formatted HTML content.

    /// Returns true if entry has a valid symbol name and accessible content.
    bool isValid() const { return !name.isEmpty() && (!path.isEmpty() || !htmlContent.isEmpty()); }
};

/**
 * @brief Singleton manager for discovering, attaching, and querying offline documentation sets.
 */
class DocsetManager : public QObject {
    Q_OBJECT
public:
    /** @brief Returns the process-wide DocsetManager singleton instance. */
    static DocsetManager& instance() {
        static DocsetManager instance;
        return instance;
    }

    /**
     * @brief Updates the root directory searched for `.docset` bundles and reloads indices.
     * @param path Absolute filesystem path to the docset directory.
     */
    void setDocsetDirectory(const QString& path);

    /** @brief Returns the currently configured `.docset` root directory path. */
    QString getDocsetDirectory() const;

    /** @brief Scans the docset directory and attaches SQLite `docSet.dsidx` databases. */
    void loadDocsets();

    /**
     * @brief Returns the standard system and XDG user directories searched for Zeal `.docset` bundles.
     * @return Ordered list of candidate Zeal docset directory paths.
     */
    QStringList defaultZealSearchPaths() const;

    /**
     * @brief Discovers and attaches all `.docset` bundles across configured and standard Zeal paths.
     */
    void discoverAndLoadSystemDocsets();

    /**
     * @brief Returns the display names of all currently loaded `.docset` bundles (e.g., "C", "C++", "Bash").
     * @return List of loaded docset bundle names without the `.docset` suffix.
     */
    QStringList loadedDocsetNames() const;

    /**
     * @brief Loads and styles the HTML documentation file at fullPath with the dark-slate reader theme.
     * @param fullPath Absolute file path inside a `.docset/Contents/Resources/Documents/` directory (may include `#anchor`).
     * @return Dark-slate styled HTML content, or empty string if file cannot be read.
     */
    QString getEntryHtmlForPath(const QString& fullPath) const;

    /**
     * @brief Asynchronously inspects project source headers to recommend docsets (e.g., OpenMPI, C++).
     * @param projectRoot Root directory of the active C/C++ workspace.
     * @return QFuture resolving to a list of recommended docset names.
     */
    QFuture<QStringList> suggestDocsets(const QString& projectRoot);

    /**
     * @brief Searches attached docset `searchIndex` / `ZTOKEN` tables for matching symbol names.
     * @param query Substring keyword to match against symbol names.
     * @param docsetFilter Optional docset bundle name filter (empty or "All Docsets" searches all).
     * @return Up to 50 `<SymbolName, AbsoluteHtmlFilePath>` pairs.
     */
    QList<QPair<QString, QString>> search(const QString& query,
                                          const QString& docsetFilter = QString());

    /** @brief Returns the list of absolute paths to all currently attached `.docset` bundles. */
    QStringList getActiveDocsetPaths() const;

    /**
     * @brief Strips PLT/GOT suffixes, angle brackets, and offsets from a raw symbol token.
     * @param rawSymbol Raw symbol string (e.g., "MPI_Init@plt" or "<MPI_Send@plt>").
     * @return Normalized canonical symbol identifier (e.g., "MPI_Init").
     */
    static QString normalizeSymbolName(const QString& rawSymbol);

    /**
     * @brief Returns ordered list of built-in offline HPC/MPI/ABI reference manual topics.
     * @return List of canonical topic names.
     */
    QStringList builtInTopics() const;

    /**
     * @brief Generates rich dark-slate HTML documentation for a built-in MPI, C/POSIX, or ABI topic.
     * @param rawSymbol Symbol or topic name to look up.
     * @return Styled HTML document string, or empty string if symbol is empty.
     */
    QString lookupBuiltInManualHtml(const QString& rawSymbol) const;

    /**
     * @brief Queries attached SQLite docset databases for exact or prefix symbol match.
     * @param query Symbol or function name to search for.
     * @return Matching DocsetEntry with loaded and styled HTML content, or invalid entry.
     */
    DocsetEntry findBestEntry(const QString& query) const;

    /**
     * @brief Checks if a symbol represents an x86_64 assembly instruction mnemonic.
     * @param sym Mnemonic name.
     * @return True if recognized x86_64 mnemonic.
     */
    static bool isX86Instruction(const QString& sym);

    /**
     * @brief Generates dark-slate x86_64 Architectural Instruction Reference HTML.
     * @param mnemonic x86_64 instruction mnemonic (e.g., "mov", "cmp", "jmp").
     * @return Formatted HTML manual page, or empty string if mnemonic unrecognized.
     */
    QString lookupX86InstructionHtml(const QString& mnemonic) const;

    /**
     * @brief Retrieves and formats system man page content if available.
     * @param query Command or symbol name.
     * @return Formatted HTML man page, or empty string if not found.
     */
    QString lookupSystemManPageHtml(const QString& query) const;

    /**
     * @brief Generates fallback ELF/C symbol card without MPI boilerplate.
     * @param rawSymbol Symbol name.
     * @return Formatted HTML reference card.
     */
    QString lookupGenericSymbolHtml(const QString& rawSymbol) const;

private:
    explicit DocsetManager(QObject* parent = nullptr);
    ~DocsetManager() override;

    QString m_docsetDir;
    QSqlDatabase m_db;
    QStringList m_activeDocsetPaths;
};

} // namespace gridlock::core

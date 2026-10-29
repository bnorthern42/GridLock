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

private:
    explicit DocsetManager(QObject* parent = nullptr);
    ~DocsetManager() override;

    QString m_docsetDir;
    QSqlDatabase m_db;
    QStringList m_activeDocsetPaths;
};

} // namespace gridlock::core

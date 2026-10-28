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

    /** @brief Scans the docset directory and attaches up to 10 SQLite `docSet.dsidx` databases. */
    void loadDocsets();

    /**
     * @brief Asynchronously inspects project source headers to recommend docsets (e.g., OpenMPI, C++).
     * @param projectRoot Root directory of the active C/C++ workspace.
     * @return QFuture resolving to a list of recommended docset names.
     */
    QFuture<QStringList> suggestDocsets(const QString& projectRoot);

    /**
     * @brief Searches all attached docset `searchIndex` tables for matching symbol names.
     * @param query Substring keyword to match against `searchIndex.name`.
     * @return Up to 50 `<SymbolName, AbsoluteHtmlFilePath>` pairs.
     */
    QList<QPair<QString, QString>> search(const QString& query);

    /** @brief Returns the list of absolute paths to all currently attached `.docset` bundles. */
    QStringList getActiveDocsetPaths() const;

private:
    explicit DocsetManager(QObject* parent = nullptr);
    ~DocsetManager() override;

    QString m_docsetDir;
    QSqlDatabase m_db;
    QStringList m_activeDocsetPaths;
};

} // namespace gridlock::core

/**
 * @file DocsetManager.cpp
 * @brief Implementation of SQLite docset attachment, symbol lookup, and header scanning.
 *
 * Uses QtSql (`QSQLITE`) with `ATTACH DATABASE` to query multiple Dash/Zeal
 * docset indices simultaneously and `QtConcurrent::blockingMap` to scan
 * workspace source files for MPI and C++ standard library headers.
 */

#include "DocsetManager.hpp"

#include <atomic>

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSqlError>
#include <QSqlQuery>
#include <QtConcurrent/QtConcurrent>

#include "ConfigManager.hpp"

namespace gridlock::core {

namespace {
// Maximum number of SQLite databases attached simultaneously per SQLite default connection limit.
constexpr int kMaxAttachedDocsets = 10;
// Maximum number of search result rows returned for a single query.
constexpr int kMaxSearchResults = 50;
} // namespace

/**
 * @brief Initializes the named QSQLITE connection and loads docsets from the configured directory.
 */
DocsetManager::DocsetManager(QObject* parent) : QObject(parent) {
    // Register a dedicated SQLite connection handle for docset index queries.
    m_db = QSqlDatabase::addDatabase("QSQLITE", "DocsetDB");
    m_docsetDir = ConfigManager::instance().getDocsetDirectory();
    if (m_docsetDir.isEmpty()) {
        // Fall back to the standard XDG Zeal docset storage path on Linux.
        m_docsetDir = QDir::homePath() + "/.local/share/Zeal/Zeal/docsets";
    }
    loadDocsets();
}

/**
 * @brief Closes the SQLite database connection on shutdown.
 */
DocsetManager::~DocsetManager() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

/**
 * @brief Persists a new docset directory path in ConfigManager and re-indexes `.docset` bundles.
 */
void DocsetManager::setDocsetDirectory(const QString& path) {
    m_docsetDir = path;
    ConfigManager::instance().setDocsetDirectory(path);
    loadDocsets();
}

/**
 * @brief Returns the active docset root directory path.
 */
QString DocsetManager::getDocsetDirectory() const {
    return m_docsetDir;
}

/**
 * @brief Discovers `.docset` subdirectories and attaches their `docSet.dsidx` SQLite files.
 */
void DocsetManager::loadDocsets() {
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_activeDocsetPaths.clear();

    const QDir dir(m_docsetDir);
    if (!dir.exists()) {
        return;
    }

    // Open a primary in-memory SQLite database so individual docsets can be attached as `db0..db9`.
    m_db.setDatabaseName(":memory:");
    if (!m_db.open()) {
        return;
    }

    const QStringList filters = {QStringLiteral("*.docset")};
    const QFileInfoList docsets = dir.entryInfoList(filters, QDir::Dirs | QDir::NoDotAndDotDot);

    int count = 0;
    for (const auto& ds : docsets) {
        const QString dbPath = ds.absoluteFilePath() + "/Contents/Resources/docSet.dsidx";
        if (QFile::exists(dbPath)) {
            const QString attachQuery = QString("ATTACH DATABASE '%1' AS db%2").arg(dbPath).arg(count);
            QSqlQuery q(m_db);
            if (q.exec(attachQuery)) {
                m_activeDocsetPaths.append(ds.absoluteFilePath());
                count++;
                if (count >= kMaxAttachedDocsets) {
                    break;
                }
            }
        }
    }
}

/**
 * @brief Queries `searchIndex` across all attached docset databases for symbols matching query.
 */
QList<QPair<QString, QString>> DocsetManager::search(const QString& query) {
    QList<QPair<QString, QString>> results;
    if (!m_db.isOpen() || m_activeDocsetPaths.isEmpty()) {
        return results;
    }

    // Iterate through each attached schema `db<i>` until up to kMaxSearchResults matches are collected.
    for (int i = 0; i < m_activeDocsetPaths.size(); ++i) {
        QSqlQuery q(m_db);
        const QString sql = QString("SELECT name, path FROM db%1.searchIndex WHERE name LIKE '%%2%' LIMIT %3;")
                                .arg(i)
                                .arg(query)
                                .arg(kMaxSearchResults);
        if (q.exec(sql)) {
            while (q.next()) {
                const QString name = q.value(0).toString();
                // Strip optional `<dash_entry_...>` metadata prefixes embedded in Dash docset paths.
                QString rawPath = q.value(1).toString();
                const int lastBracket = rawPath.lastIndexOf('>');
                if (lastBracket != -1) {
                    rawPath = rawPath.mid(lastBracket + 1).trimmed();
                }

                // Resolve the relative HTML path (including any `#anchor` fragment) inside Documents/.
                const QString path = m_activeDocsetPaths[i] + "/Contents/Resources/Documents/" + rawPath;
                results.append(qMakePair(name, path));
                if (results.size() >= kMaxSearchResults) {
                    return results;
                }
            }
        }
    }
    return results;
}

/**
 * @brief Scans C/C++ files under projectRoot in parallel to detect `<mpi.h>` and STL usage.
 */
QFuture<QStringList> DocsetManager::suggestDocsets(const QString& projectRoot) {
    return QtConcurrent::run([projectRoot]() {
        QStringList suggestions;
        QDirIterator it(projectRoot, QStringList() << "*.c" << "*.cpp" << "*.h" << "*.hpp",
                        QDir::Files, QDirIterator::Subdirectories);

        QStringList files;
        while (it.hasNext()) {
            files << it.next();
        }

        // Atomic flags updated lock-free by worker threads scanning source files.
        std::atomic<bool> wantsMpi{false};
        std::atomic<bool> wantsCpp{false};

        auto checkFile = [&](const QString& filePath) {
            // Short-circuit once both MPI and C++ headers have been detected.
            if (wantsMpi.load(std::memory_order_relaxed) && wantsCpp.load(std::memory_order_relaxed)) {
                return;
            }

            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QString content = QString::fromUtf8(file.readAll());
                if (!wantsMpi.load(std::memory_order_relaxed) && content.contains("<mpi.h>")) {
                    wantsMpi.store(true, std::memory_order_relaxed);
                }
                if (!wantsCpp.load(std::memory_order_relaxed) &&
                    (content.contains("<vector>") || content.contains("<iostream>"))) {
                    wantsCpp.store(true, std::memory_order_relaxed);
                }
            }
        };

        QtConcurrent::blockingMap(files, checkFile);

        if (wantsMpi.load(std::memory_order_relaxed)) suggestions << "OpenMPI";
        if (wantsCpp.load(std::memory_order_relaxed)) suggestions << "C++";

        return suggestions;
    });
}

/**
 * @brief Returns the list of currently mounted `.docset` bundle paths.
 */
QStringList DocsetManager::getActiveDocsetPaths() const {
    return m_activeDocsetPaths;
}

} // namespace gridlock::core

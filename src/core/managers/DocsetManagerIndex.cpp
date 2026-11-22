/**
 * @file DocsetManagerIndex.cpp
 * @brief Zeal/Dash directory discovery, SQLite index attachment, symbol search, and HTML reader.
 *
 * Uses QtSql (`QSQLITE`) with `ATTACH DATABASE` to mount up to 10 `.docset` SQLite
 * indices simultaneously, resolves and styles offline `.docset` HTML pages, and
 * scans workspace source trees concurrently via `QtConcurrent::blockingMap`.
 */

#include "DocsetManager.hpp"

#include <atomic>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUrl>
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
 * @brief Returns standard system and user XDG directories searched for installed Zeal docsets.
 * @return Ordered list of candidate Zeal docset directory paths.
 */
QStringList DocsetManager::defaultZealSearchPaths() const {
    QStringList paths;
    // 1. User-configured docset directory from ConfigManager (if valid).
    const QString cfgPath = ConfigManager::instance().getDocsetDirectory().trimmed();
    if (!cfgPath.isEmpty()) {
        paths.append(cfgPath);
    }

    // 2. Discover custom docset path from ~/.config/Zeal/Zeal.conf and Flatpak Zeal.conf
    const QStringList confCandidates = {
        QDir::homePath() + QStringLiteral("/.config/Zeal/Zeal.conf"),
        QDir::homePath() + QStringLiteral("/.var/app/org.zealdocs.Zeal/config/Zeal/Zeal.conf")
    };
    for (const QString& confFile : confCandidates) {
        if (QFile::exists(confFile)) {
            QSettings zealSettings(confFile, QSettings::IniFormat);
            const QString customPath = zealSettings.value(QStringLiteral("docsets/path")).toString().trimmed();
            if (!customPath.isEmpty() && QDir(customPath).exists()) {
                paths.append(customPath);
            }
        }
    }

    // 3. Standard Flatpak Zeal storage directory
    paths.append(QDir::homePath() + QStringLiteral("/.var/app/org.zealdocs.Zeal/data/Zeal/Zeal/docsets"));

    // 4. Standard Qt GenericDataLocation for Zeal (`~/.local/share/Zeal/Zeal/docsets`).
    const QString xdgData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!xdgData.isEmpty()) {
        paths.append(xdgData + QStringLiteral("/Zeal/Zeal/docsets"));
        paths.append(xdgData + QStringLiteral("/zeal/docsets"));
    }
    // 5. Explicit home directory XDG paths and system-wide `/usr/share` locations.
    paths.append(QDir::homePath() + QStringLiteral("/.local/share/Zeal/Zeal/docsets"));
    paths.append(QDir::homePath() + QStringLiteral("/.local/share/zeal/docsets"));
    paths.append(QStringLiteral("/usr/share/zeal/docsets"));
    paths.append(QStringLiteral("/usr/local/share/zeal/docsets"));
    paths.removeDuplicates();
    return paths;
}

/**
 * @brief Discovers `.docset` subdirectories in `m_docsetDir` and attaches their SQLite indices.
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

    QFileInfoList docsets;
    if (m_docsetDir.endsWith(QStringLiteral(".docset"), Qt::CaseInsensitive)) {
        docsets.append(QFileInfo(m_docsetDir));
    } else {
        const QStringList filters = {QStringLiteral("*.docset")};
        docsets = dir.entryInfoList(filters, QDir::Dirs | QDir::NoDotAndDotDot);
    }

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
 * @brief Scans all configured and default Zeal directories and attaches discovered `.docset` bundles.
 *
 * Iterates through candidate directories (`m_docsetDir` plus `defaultZealSearchPaths()`),
 * supports both parent directories containing `*.docset` subdirectories and direct `.docset`
 * bundle paths, deduplicates by bundle base name, and attaches up to `kMaxAttachedDocsets`
 * SQLite `docSet.dsidx` databases into the in-memory coordinator connection.
 */
void DocsetManager::discoverAndLoadSystemDocsets() {
    // Close any previously opened in-memory SQLite coordinator database.
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_activeDocsetPaths.clear();

    // Open fresh in-memory coordinator database to host attached `db0..db9` schemas.
    m_db.setDatabaseName(":memory:");
    if (!m_db.open()) {
        return;
    }

    // Collect deduplicated candidate search directories from user config and XDG Zeal paths.
    QStringList candidateDirs;
    if (!m_docsetDir.isEmpty()) {
        candidateDirs.append(m_docsetDir);
    }
    candidateDirs.append(defaultZealSearchPaths());
    candidateDirs.removeDuplicates();

    // Track attached docset bundle names so duplicate installations are not attached twice.
    QSet<QString> seenDocsetNames;
    int count = 0;
    for (const QString& rootPath : candidateDirs) {
        const QFileInfo rootInfo(rootPath);
        if (!rootInfo.exists()) {
            continue;
        }
        // Resolve either a direct `.docset` bundle path or all `*.docset` children of a directory.
        QFileInfoList bundles;
        if (rootInfo.isDir() && rootPath.endsWith(QStringLiteral(".docset"), Qt::CaseInsensitive)) {
            bundles.append(rootInfo);
        } else if (rootInfo.isDir()) {
            const QDir dir(rootPath);
            bundles = dir.entryInfoList({QStringLiteral("*.docset")}, QDir::Dirs | QDir::NoDotAndDotDot);
        }

        // Attach each discovered bundle's `Contents/Resources/docSet.dsidx` SQLite index.
        for (const QFileInfo& ds : bundles) {
            const QString bundleName = ds.completeBaseName();
            if (seenDocsetNames.contains(bundleName)) {
                continue;
            }
            const QString dbPath = ds.absoluteFilePath() + QStringLiteral("/Contents/Resources/docSet.dsidx");
            if (!QFile::exists(dbPath)) {
                continue;
            }
            // Execute SQLite ATTACH DATABASE statement to mount bundle schema as `db<count>`.
            const QString attachQuery = QStringLiteral("ATTACH DATABASE '%1' AS db%2").arg(dbPath).arg(count);
            QSqlQuery q(m_db);
            if (q.exec(attachQuery)) {
                seenDocsetNames.insert(bundleName);
                m_activeDocsetPaths.append(ds.absoluteFilePath());
                count++;
                // Stop once SQLite's per-connection attached database ceiling is reached.
                if (count >= kMaxAttachedDocsets) {
                    return;
                }
            }
        }
    }
}

/**
 * @brief Loads and styles a local `.docset` HTML file with the GridLock dark-slate reader theme.
 *
 * Strips Dash `<dash_entry_...>` metadata tags, URL `#fragment` anchors, and `?query` parameters
 * before resolving the physical file on disk, scrubs `<script>` blocks and white backgrounds,
 * and injects a cohesive `#16181d` dark-slate reader stylesheet.
 *
 * @param fullPath Full path to the HTML document inside `Contents/Resources/Documents/`.
 * @return Dark-slate styled HTML string, or empty string if file cannot be read.
 */
QString DocsetManager::getEntryHtmlForPath(const QString& fullPath) const {
    if (fullPath.startsWith(QStringLiteral("builtin://"))) {
        return lookupBuiltInManualHtml(fullPath.mid(10));
    }

    // Strip any embedded <dash_entry_...> metadata tags, #fragment anchors, and ?query strings.
    QString cleanPath = fullPath;
    cleanPath.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    const int hashIdx = cleanPath.indexOf(QLatin1Char('#'));
    if (hashIdx != -1) {
        cleanPath = cleanPath.left(hashIdx);
    }
    const int queryIdx = cleanPath.indexOf(QLatin1Char('?'));
    if (queryIdx != -1) {
        cleanPath = cleanPath.left(queryIdx);
    }
    cleanPath = QUrl::fromPercentEncoding(cleanPath.toUtf8());

    QFile file(cleanPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QString htmlOutput = QString::fromUtf8(file.readAll());
    const auto ciOpt = QRegularExpression::CaseInsensitiveOption |
                       QRegularExpression::DotMatchesEverythingOption;

    // Strip external JavaScript and legacy inline style blocks that force white backgrounds.
    htmlOutput.replace(QRegularExpression(QStringLiteral("<script[^>]*>.*?</script>"), ciOpt), QString());
    htmlOutput.replace(QRegularExpression(QStringLiteral("<style[^>]*>.*?</style>"), ciOpt), QString());
    htmlOutput.replace(
        QRegularExpression(QStringLiteral("<body[^>]*>"), QRegularExpression::CaseInsensitiveOption),
        QStringLiteral("<body style=\"background-color: #16181d; color: #d7dae0;\">"));

    // Inject GridLock dark-slate reader stylesheet so C/C++/Bash/MPI Zeal pages look native.
    const QString darkReaderCss = QStringLiteral(
        "<style>"
        "html, body, div, p, ul, ol, li, dl, dt, dd, span { "
        "background-color: #16181d !important; color: #d7dae0 !important; "
        "font-family: 'Inter', sans-serif; font-size: 12px; line-height: 1.45; }"
        "body { padding: 12px 16px; margin: 0px; }"
        "h1, h2, h3, h4 { color: #e5c07b !important; font-family: 'JetBrains Mono', monospace; }"
        "pre, code, tt, .t-dsc, .mw-geshi { background-color: #1e222a !important; "
        "color: #4ec9b0 !important; border: 1px solid #2c313a !important; "
        "font-family: 'JetBrains Mono', monospace; padding: 4px 6px; }"
        "a, a:visited, a:hover { color: #61afef !important; text-decoration: none; }"
        "table, tr, th, td { border-color: #2c313a !important; background-color: #1b1e24 !important; "
        "color: #d7dae0 !important; border-collapse: collapse; padding: 3px 6px; }"
        "</style>");

    if (htmlOutput.contains(QStringLiteral("</head>"), Qt::CaseInsensitive)) {
        htmlOutput.replace(QRegularExpression(QStringLiteral("</head>"),
                                              QRegularExpression::CaseInsensitiveOption),
                           darkReaderCss + QStringLiteral("</head>"));
    } else {
        htmlOutput = darkReaderCss + htmlOutput;
    }
    return htmlOutput;
}

/**
 * @brief Queries `searchIndex` (with `ZTOKEN` CoreData fallback) across attached docsets.
 */
QList<QPair<QString, QString>> DocsetManager::search(const QString& query,
                                                     const QString& docsetFilter) {
    QList<QPair<QString, QString>> results;
    const QString filterTrimmed = docsetFilter.trimmed();
    const bool filterBuiltInOnly =
        filterTrimmed.compare(QStringLiteral("Built-in MPI/ABI"), Qt::CaseInsensitive) == 0;

    if (!filterBuiltInOnly && m_activeDocsetPaths.isEmpty()) {
        discoverAndLoadSystemDocsets();
    }
    if (!filterBuiltInOnly && m_db.isOpen() && !m_activeDocsetPaths.isEmpty()) {
        // Distribute per-docset row budget when browsing all docsets with an empty query
        // so every installed Zeal docset (C, C++, Bash, etc.) is represented in the list.
        const bool allDocsets =
            filterTrimmed.isEmpty() ||
            filterTrimmed.startsWith(QStringLiteral("All Docsets"), Qt::CaseInsensitive);
        const int perDocsetLimit =
            (query.trimmed().isEmpty() && allDocsets && m_activeDocsetPaths.size() > 1)
                ? qMax(6, kMaxSearchResults / static_cast<int>(m_activeDocsetPaths.size()))
                : kMaxSearchResults;

        for (int i = 0; i < m_activeDocsetPaths.size(); ++i) {
            const QString bundleName = QFileInfo(m_activeDocsetPaths[i]).completeBaseName();
            if (!allDocsets && bundleName.compare(filterTrimmed, Qt::CaseInsensitive) != 0) {
                continue;
            }

            QSqlQuery q(m_db);
            // Primary Dash/Zeal schema: rank canonical std::/MPI_/short identifiers ahead of experimental/sentinel noise.
            const QString sql = QStringLiteral(
                "SELECT name, path FROM db%1.searchIndex WHERE name LIKE '%%2%' "
                "ORDER BY (CASE WHEN name LIKE '%experimental%' OR name LIKE '%sentinel%' OR name LIKE '%deduction guides%' THEN 2 "
                "WHEN name LIKE 'std::%' OR name LIKE 'MPI_%' OR LENGTH(name) <= 16 THEN 0 ELSE 1 END), "
                "LENGTH(name) ASC, name COLLATE NOCASE ASC LIMIT %3;")
                .arg(i).arg(query).arg(perDocsetLimit);
            bool executed = q.exec(sql);
            if (!executed) {
                // Fallback Apple Dash / CoreData ZTOKEN schema with identical canonical ranking.
                const QString zSql = QStringLiteral(
                    "SELECT t.ZTOKENNAME, f.ZPATH FROM db%1.ZTOKEN t "
                    "JOIN db%1.ZTOKENMETAINFORMATION m ON t.ZMETAINFORMATION = m.Z_PK "
                    "JOIN db%1.ZFILEPATH f ON m.ZFILE = f.Z_PK "
                    "WHERE t.ZTOKENNAME LIKE '%%2%' "
                    "ORDER BY (CASE WHEN t.ZTOKENNAME LIKE '%experimental%' OR t.ZTOKENNAME LIKE '%sentinel%' OR t.ZTOKENNAME LIKE '%deduction guides%' THEN 2 "
                    "WHEN t.ZTOKENNAME LIKE 'std::%' OR t.ZTOKENNAME LIKE 'MPI_%' OR LENGTH(t.ZTOKENNAME) <= 16 THEN 0 ELSE 1 END), "
                    "LENGTH(t.ZTOKENNAME) ASC, t.ZTOKENNAME COLLATE NOCASE ASC LIMIT %3;")
                    .arg(i).arg(query).arg(perDocsetLimit);
                executed = q.exec(zSql);
            }
            if (executed) {
                while (q.next()) {
                    const QString name = q.value(0).toString();
                    // Strip all leading `<dash_entry_...>` metadata tags embedded in Dash docset paths.
                    QString rawPath = q.value(1).toString();
                    rawPath.remove(QRegularExpression(QStringLiteral("^(?:<[^>]+>)+")));
                    rawPath = rawPath.trimmed();

                    // Resolve the relative HTML path inside Contents/Resources/Documents/.
                    const QString path =
                        m_activeDocsetPaths[i] + QStringLiteral("/Contents/Resources/Documents/") + rawPath;
                    results.append(qMakePair(name, path));
                    if (results.size() >= kMaxSearchResults) {
                        return results;
                    }
                }
            }
        }
        if (!results.isEmpty() || !allDocsets) {
            return results;
        }
    }

    // Fallback to built-in offline reference manual topics when requested or when no external match is found.
    const QString cleaned = normalizeSymbolName(query);
    for (const QString& topic : builtInTopics()) {
        if (cleaned.isEmpty() || topic.contains(cleaned, Qt::CaseInsensitive)) {
            results.append(qMakePair(topic, QStringLiteral("builtin://") + topic));
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
 * @brief Queries attached SQLite docset databases directly for exact or prefix symbol match.
 * @param query Symbol or function name to search for.
 * @return Matching DocsetEntry with loaded and styled HTML content, or invalid entry.
 */
DocsetEntry DocsetManager::findBestEntry(const QString& query) const {
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty()) {
        return DocsetEntry{};
    }

    if (m_activeDocsetPaths.isEmpty()) {
        const_cast<DocsetManager*>(this)->discoverAndLoadSystemDocsets();
    }
    if (!m_db.isOpen() || m_activeDocsetPaths.isEmpty()) {
        return DocsetEntry{};
    }

    auto extractEntry = [this](QSqlQuery& q, const QString& bundlePath,
                               const QString& bundleName) -> DocsetEntry {
        DocsetEntry entry;
        entry.name = q.value(0).toString();
        entry.type = q.value(1).toString();
        QString rawPath = q.value(2).toString();
        rawPath.remove(QRegularExpression(QStringLiteral("^(?:<[^>]+>)+")));
        // Resolve disk path inside docset Documents folder
        entry.path = bundlePath + QStringLiteral("/Contents/Resources/Documents/") + rawPath.trimmed();
        entry.docsetName = bundleName;
        // Load and format HTML content with dark-slate stylesheet
        entry.htmlContent = getEntryHtmlForPath(entry.path);
        return entry;
    };

    // 1. First priority: Exact case-insensitive match on attached searchIndex tables.
    // This immediately resolves precise C/C++ library functions (e.g. printf, memcpy).
    for (int i = 0; i < m_activeDocsetPaths.size(); ++i) {
        const QString bundlePath = m_activeDocsetPaths[i];
        const QString bundleName = QFileInfo(bundlePath).completeBaseName();
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT name, type, path FROM db%1.searchIndex WHERE name = ? COLLATE NOCASE LIMIT 1;").arg(i));
        q.addBindValue(trimmed);
        if (q.exec() && q.next()) {
            DocsetEntry entry = extractEntry(q, bundlePath, bundleName);
            if (!entry.htmlContent.isEmpty()) return entry;
        }
    }

    // 2. Second priority: Prefix match (name LIKE 'query%') across attached databases.
    // Handles partial symbol typings and namespace-prefixed library queries.
    for (int i = 0; i < m_activeDocsetPaths.size(); ++i) {
        const QString bundlePath = m_activeDocsetPaths[i];
        const QString bundleName = QFileInfo(bundlePath).completeBaseName();
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT name, type, path FROM db%1.searchIndex WHERE name LIKE ? ESCAPE '\\' LIMIT 25;").arg(i));
        q.addBindValue(trimmed + QStringLiteral("%"));
        if (q.exec()) {
            while (q.next()) {
                DocsetEntry entry = extractEntry(q, bundlePath, bundleName);
                if (!entry.htmlContent.isEmpty()) return entry;
            }
        }
    }

    // 3. Third priority: Fallback to CoreData ZTOKEN schema if searchIndex is absent.
    // Supports older Apple Dash docsets formatted with CoreData object graphs.
    for (int i = 0; i < m_activeDocsetPaths.size(); ++i) {
        const QString bundlePath = m_activeDocsetPaths[i];
        const QString bundleName = QFileInfo(bundlePath).completeBaseName();
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT t.ZTOKENNAME, 'Symbol', f.ZPATH FROM db%1.ZTOKEN t "
                                 "JOIN db%1.ZTOKENMETAINFORMATION m ON t.ZMETAINFORMATION = m.Z_PK "
                                 "JOIN db%1.ZFILEPATH f ON m.ZFILE = f.Z_PK "
                                 "WHERE t.ZTOKENNAME LIKE ? LIMIT 10;").arg(i));
        q.addBindValue(trimmed + QStringLiteral("%"));
        if (q.exec()) {
            while (q.next()) {
                DocsetEntry entry = extractEntry(q, bundlePath, bundleName);
                if (!entry.htmlContent.isEmpty()) return entry;
            }
        }
    }

    // Return empty entry if no matching documentation page was discovered
    return DocsetEntry{};
}

} // namespace gridlock::core

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
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
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
 * @brief Initializes the named QSQLITE connection and discovers installed Zeal docsets.
 */
DocsetManager::DocsetManager(QObject* parent) : QObject(parent) {
    // Register a dedicated SQLite connection handle for docset index queries.
    m_db = QSqlDatabase::addDatabase("QSQLITE", "DocsetDB");
    m_docsetDir = ConfigManager::instance().getDocsetDirectory();
    if (m_docsetDir.isEmpty() || !QDir(m_docsetDir).exists()) {
        // Fall back to the standard XDG Zeal docset storage path on Linux when unset or stale.
        m_docsetDir = QDir::homePath() + "/.local/share/Zeal/Zeal/docsets";
    }
    discoverAndLoadSystemDocsets();
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
 * @brief Updates the active docset directory path and re-indexes `.docset` bundles inside it.
 *
 * Avoids persisting temporary `/tmp` test directories into user QSettings so unit tests
 * never overwrite the user's configured Zeal docset path.
 */
void DocsetManager::setDocsetDirectory(const QString& path) {
    m_docsetDir = path;
    const QString tempRoot = QDir::tempPath();
    if (!path.startsWith(tempRoot) && !path.startsWith(QStringLiteral("/tmp/"))) {
        ConfigManager::instance().setDocsetDirectory(path);
    }
    loadDocsets();
}

/**
 * @brief Returns the active docset root directory path.
 */
QString DocsetManager::getDocsetDirectory() const {
    return m_docsetDir;
}

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
 * @brief Returns the display names of all currently attached `.docset` bundles.
 * @return List of loaded docset bundle names without the `.docset` extension.
 */
QStringList DocsetManager::loadedDocsetNames() const {
    QStringList names;
    // Extract the bundle base name (e.g., "C", "C++", "Bash") from each active bundle path.
    for (const QString& bundlePath : m_activeDocsetPaths) {
        const QString base = QFileInfo(bundlePath).completeBaseName();
        if (!base.isEmpty() && !names.contains(base)) {
            names.append(base);
        }
    }
    return names;
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
            // Primary Dash/Zeal schema: searchIndex(name, type, path).
            const QString sql =
                QStringLiteral("SELECT name, path FROM db%1.searchIndex WHERE name LIKE '%%2%' LIMIT %3;")
                    .arg(i)
                    .arg(query)
                    .arg(perDocsetLimit);
            bool executed = q.exec(sql);
            if (!executed) {
                // Fallback Apple Dash / CoreData ZTOKEN schema.
                const QString zSql = QStringLiteral(
                    "SELECT t.ZTOKENNAME, f.ZPATH FROM db%1.ZTOKEN t "
                    "JOIN db%1.ZTOKENMETAINFORMATION m ON t.ZMETAINFORMATION = m.Z_PK "
                    "JOIN db%1.ZFILEPATH f ON m.ZFILE = f.Z_PK "
                    "WHERE t.ZTOKENNAME LIKE '%%2%' LIMIT %3;")
                    .arg(i)
                    .arg(query)
                    .arg(perDocsetLimit);
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
 * @brief Returns the list of currently mounted `.docset` bundle paths.
 */
QStringList DocsetManager::getActiveDocsetPaths() const {
    return m_activeDocsetPaths;
}

/**
 * @brief Strips PLT/GOT suffixes, angle brackets, and offsets from a raw symbol token.
 *
 * Normalizes assembly call target strings (such as `<MPI_Init@plt>` or `MPI_Send+0x14`)
 * into clean C/MPI identifier names suitable for SQLite or built-in manual index lookup.
 *
 * @param rawSymbol Raw symbol string (e.g., "MPI_Init@plt" or "<MPI_Send@plt>").
 * @return Normalized canonical symbol identifier (e.g., "MPI_Init").
 */
QString DocsetManager::normalizeSymbolName(const QString& rawSymbol) {
    // Trim leading/trailing whitespace and strip surrounding angle brackets.
    QString cleaned = rawSymbol.trimmed();
    if (cleaned.startsWith('<')) {
        cleaned.remove(0, 1);
    }
    if (cleaned.endsWith('>')) {
        cleaned.chop(1);
    }
    // Strip @plt, @got, or ELF symbol version suffixes such as @@GLIBC_2.2.5.
    const int atIdx = cleaned.indexOf('@');
    if (atIdx > 0) {
        cleaned = cleaned.left(atIdx);
    }
    // Strip instruction displacement suffixes such as +29 or +0x1d.
    const int plusIdx = cleaned.indexOf('+');
    if (plusIdx > 0) {
        cleaned = cleaned.left(plusIdx);
    }
    return cleaned.trimmed();
}

/**
 * @brief Returns ordered list of built-in offline HPC/MPI/ABI reference manual topics.
 *
 * Provides the canonical set of MPI-3.1 routines, GDB/MI protocol commands,
 * and System V AMD64 ABI calling convention references available offline.
 *
 * @return List of canonical topic names.
 */
QStringList DocsetManager::builtInTopics() const {
    return {
        QStringLiteral("MPI_Init"),
        QStringLiteral("MPI_Send"),
        QStringLiteral("MPI_Recv"),
        QStringLiteral("MPI_Barrier"),
        QStringLiteral("MPI_Allreduce"),
        QStringLiteral("MPI_Finalize"),
        QStringLiteral("MPI_Comm_rank"),
        QStringLiteral("MPI_Comm_size"),
        QStringLiteral("gdb-mi"),
        QStringLiteral("x86_64-abi")
    };
}

/**
 * @brief Generates rich dark-slate HTML documentation for a built-in MPI, C/POSIX, or ABI topic.
 *
 * Renders a self-contained HTML5 document styled for the #16181d dark-slate workstation
 * canvas, featuring #61afef symbol titles, #e5c07b section headers (NAME, SYNOPSIS,
 * PARAMETERS, MPI RANK SEMANTICS), and #1e222a monospace code blocks.
 *
 * @param rawSymbol Symbol or topic name to look up.
 * @return Styled HTML document string, or empty string if symbol is empty.
 */
QString DocsetManager::lookupBuiltInManualHtml(const QString& rawSymbol) const {
    // Normalize symbol token by stripping @plt/@got and displacement suffixes.
    const QString sym = normalizeSymbolName(rawSymbol);
    if (sym.isEmpty()) {
        return QString();
    }

    // Shared workstation dark-slate HTML wrapper matching #16181d canvas and #1e222a code blocks.
    // Uses JetBrains Mono for C prototypes and register mappings to align with DisassemblyView.
    auto formatManPage = [](const QString& title, const QString& category,
                            const QString& brief, const QString& synopsis,
                            const QString& paramsHtml, const QString& rankSemantics) {
        return QStringLiteral(
            "<html><head><style>"
            "body { background-color: #16181d; color: #d7dae0; font-family: 'Inter', sans-serif; "
            "font-size: 12px; margin: 0px; padding: 12px 16px; }"
            ".hdr { background-color: #1e222a; border: 1px solid #2c313a; padding: 6px 10px; "
            "margin-bottom: 10px; }"
            ".title { color: #61afef; font-family: 'JetBrains Mono', monospace; font-size: 14px; "
            "font-weight: bold; }"
            ".badge { color: #98c379; font-family: 'JetBrains Mono', monospace; font-size: 11px; }"
            ".sec { color: #e5c07b; font-family: 'JetBrains Mono', monospace; font-size: 11px; "
            "font-weight: bold; margin-top: 10px; margin-bottom: 4px; letter-spacing: 0.5px; }"
            ".code { background-color: #1e222a; border: 1px solid #2c313a; border-left: 3px solid #528bff; "
            "color: #abb2bf; font-family: 'JetBrains Mono', monospace; font-size: 11px; "
            "padding: 8px 10px; white-space: pre-wrap; margin: 4px 0px; }"
            ".param { color: #61afef; font-family: 'JetBrains Mono', monospace; font-weight: bold; }"
            "p, li { color: #abb2bf; line-height: 1.45; margin: 3px 0px; }"
            "</style></head><body>"
            "<div class='hdr'><span class='title'>%1</span> &nbsp; <span class='badge'>[%2]</span></div>"
            "<div class='sec'>NAME</div><p><b>%1</b> &mdash; %3</p>"
            "<div class='sec'>SYNOPSIS</div><div class='code'>%4</div>"
            "<div class='sec'>PARAMETERS</div>%5"
            "<div class='sec'>MPI RANK SEMANTICS &amp; DEBUGGER TELEMETRY</div><p>%6</p>"
            "</body></html>")
            .arg(title, category, brief, synopsis, paramsHtml, rankSemantics);
    };

    // Match MPI_Init environment initialization routine.
    if (sym.compare(QStringLiteral("MPI_Init"), Qt::CaseInsensitive) == 0) {
        return formatManPage(
            QStringLiteral("MPI_Init"),
            QStringLiteral("MPI-3.1 Standard / Section 8.7"),
            QStringLiteral("Initializes the MPI execution environment across all communicator ranks."),
            QStringLiteral("#include &lt;mpi.h&gt;\nint MPI_Init(int *argc, char ***argv);"),
            QStringLiteral("<ul>"
                           "<li><span class='param'>argc</span> [in/out]: Pointer to argument count from main() (RDI).</li>"
                           "<li><span class='param'>argv</span> [in/out]: Pointer to argument vector from main() (RSI).</li>"
                           "</ul>"),
            QStringLiteral("Must be invoked by every participating rank before any other MPI routine. "
                           "Initializes MPI_COMM_WORLD and establishes shared-memory / RDMA transport rings."));
    }
    // Match MPI_Send blocking point-to-point transmit routine.
    if (sym.compare(QStringLiteral("MPI_Send"), Qt::CaseInsensitive) == 0) {
        return formatManPage(
            QStringLiteral("MPI_Send"),
            QStringLiteral("MPI-3.1 Point-to-Point"),
            QStringLiteral("Performs a standard-mode blocking send operation to a destination rank."),
            QStringLiteral("#include &lt;mpi.h&gt;\nint MPI_Send(const void *buf, int count, MPI_Datatype datatype,\n"
                           "             int dest, int tag, MPI_Comm comm);"),
            QStringLiteral("<ul>"
                           "<li><span class='param'>buf</span> [in]: Initial address of send buffer (RDI).</li>"
                           "<li><span class='param'>count</span> [in]: Number of elements in send buffer (ESI).</li>"
                           "<li><span class='param'>dest</span> [in]: Rank of destination process in communicator (ECX).</li>"
                           "</ul>"),
            QStringLiteral("May block if eager rendezvous threshold is exceeded until matching MPI_Recv "
                           "is posted by dest. Circular blocking MPI_Send calls across ranks cause wait-for deadlocks."));
    }
    // Match MPI_Recv blocking point-to-point receive routine.
    if (sym.compare(QStringLiteral("MPI_Recv"), Qt::CaseInsensitive) == 0) {
        return formatManPage(
            QStringLiteral("MPI_Recv"),
            QStringLiteral("MPI-3.1 Point-to-Point"),
            QStringLiteral("Performs a blocking receive operation for a message matching source and tag."),
            QStringLiteral("#include &lt;mpi.h&gt;\nint MPI_Recv(void *buf, int count, MPI_Datatype datatype,\n"
                           "             int source, int tag, MPI_Comm comm, MPI_Status *status);"),
            QStringLiteral("<ul>"
                           "<li><span class='param'>buf</span> [out]: Initial address of receive buffer (RDI).</li>"
                           "<li><span class='param'>source</span> [in]: Rank of source process or MPI_ANY_SOURCE (ECX).</li>"
                           "<li><span class='param'>status</span> [out]: Status object or MPI_STATUS_IGNORE.</li>"
                           "</ul>"),
            QStringLiteral("Blocks calling rank until message buffer is populated from source rank."));
    }
    // Match MPI_Barrier collective communicator synchronization barrier.
    if (sym.compare(QStringLiteral("MPI_Barrier"), Qt::CaseInsensitive) == 0) {
        return formatManPage(
            QStringLiteral("MPI_Barrier"),
            QStringLiteral("MPI-3.1 Collective Synchronization"),
            QStringLiteral("Blocks the caller until all processes in the communicator have called it."),
            QStringLiteral("#include &lt;mpi.h&gt;\nint MPI_Barrier(MPI_Comm comm);"),
            QStringLiteral("<ul><li><span class='param'>comm</span> [in]: Communicator handle (RDI).</li></ul>"),
            QStringLiteral("Divergent control flow where a subset of ranks bypasses MPI_Barrier permanently "
                           "stalls participating ranks inside the collective progression engine."));
    }
    // Match MPI_Allreduce collective reduction and broadcast routine.
    if (sym.compare(QStringLiteral("MPI_Allreduce"), Qt::CaseInsensitive) == 0) {
        return formatManPage(
            QStringLiteral("MPI_Allreduce"),
            QStringLiteral("MPI-3.1 Collective Reduction"),
            QStringLiteral("Combines values from all processes and distributes the result back to all processes."),
            QStringLiteral("#include &lt;mpi.h&gt;\nint MPI_Allreduce(const void *sendbuf, void *recvbuf, int count,\n"
                           "                  MPI_Datatype datatype, MPI_Op op, MPI_Comm comm);"),
            QStringLiteral("<ul>"
                           "<li><span class='param'>sendbuf</span> [in]: Starting address of send buffer (RDI).</li>"
                           "<li><span class='param'>recvbuf</span> [out]: Starting address of receive buffer (RSI).</li>"
                           "<li><span class='param'>op</span> [in]: Reduction operator such as MPI_SUM or MPI_MAX (R8).</li>"
                           "</ul>"),
            QStringLiteral("All ranks in comm must specify identical count and datatype arguments."));
    }
    // Match MPI_Finalize environment teardown routine.
    if (sym.compare(QStringLiteral("MPI_Finalize"), Qt::CaseInsensitive) == 0) {
        return formatManPage(
            QStringLiteral("MPI_Finalize"),
            QStringLiteral("MPI-3.1 Environment Teardown"),
            QStringLiteral("Terminates the MPI execution environment cleanly across all ranks."),
            QStringLiteral("#include &lt;mpi.h&gt;\nint MPI_Finalize(void);"),
            QStringLiteral("<ul><li><span class='param'>none</span>: Takes no arguments.</li></ul>"),
            QStringLiteral("All pending non-blocking communications must be completed before calling MPI_Finalize."));
    }
    // Match System V AMD64 ABI register calling convention reference.
    if (sym.compare(QStringLiteral("x86_64-abi"), Qt::CaseInsensitive) == 0) {
        return formatManPage(
            QStringLiteral("x86_64 System V ABI"),
            QStringLiteral("Calling Convention & Register Map"),
            QStringLiteral("Register allocation and stack frame layout for 64-bit Linux ELF binaries."),
            QStringLiteral("Integer/Pointer Args : RDI, RSI, RDX, RCX, R8, R9\n"
                           "Floating-Point Args  : XMM0 .. XMM7\n"
                           "Return Values        : RAX (RDX), XMM0\n"
                           "Callee-Saved         : RBX, RBP, R12, R13, R14, R15"),
            QStringLiteral("<ul>"
                           "<li><span class='param'>RSP</span>: 16-byte aligned before CALL instruction.</li>"
                           "<li><span class='param'>Red Zone</span>: 128-byte scratch area below RSP for leaf functions.</li>"
                           "</ul>"),
            QStringLiteral("Used by DisassemblyView and Decompiler to reconstruct high-level C call signatures."));
    }
    // Match GDB/MI v3 Machine Interface command and async record reference.
    if (sym.compare(QStringLiteral("gdb-mi"), Qt::CaseInsensitive) == 0) {
        return formatManPage(
            QStringLiteral("GDB/MI v3 Protocol"),
            QStringLiteral("Machine Interface Reference"),
            QStringLiteral("Structured line-oriented text protocol driving multi-rank GDB instances."),
            QStringLiteral("-exec-run | -exec-continue | -exec-next | -exec-step\n"
                           "-break-insert -f &lt;file:line&gt; | -data-read-memory-bytes &lt;addr&gt; &lt;len&gt;\n"
                           "*stopped,reason=\"breakpoint-hit\",frame={addr=\"0x00401120\",func=\"main\"}"),
            QStringLiteral("<ul>"
                           "<li><span class='param'>^done / ^running / ^error</span>: Synchronous command result records.</li>"
                           "<li><span class='param'>*stopped / *running</span>: Asynchronous target execution records.</li>"
                           "</ul>"),
            QStringLiteral("Multiplexed across N processes by GdbRankCoordinator with non-blocking stdout parsing."));
    }

    // Generic fallback manual card for any C/POSIX/ELF symbol queried via F1 or search bar.
    return formatManPage(
        sym.toHtmlEscaped(),
        QStringLiteral("ELF64 / C & POSIX Symbol Reference"),
        QStringLiteral("Runtime and static binary symbol documentation entry."),
        QStringLiteral("/* Symbol Identifier: %1 */\nextern int %1(...);").arg(sym.toHtmlEscaped()),
        QStringLiteral("<ul><li><span class='param'>Calling Convention</span>: System V AMD64 ABI (RDI, RSI, RDX, RCX, R8, R9).</li></ul>"),
        QStringLiteral("Resolved via F1 symbol lookup. Press X in DisassemblyView to inspect incoming/outgoing cross-references."));
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

/**
 * @brief Checks if a symbol represents a recognized x86_64 assembly instruction mnemonic.
 *
 * Evaluates the query against common control-flow, data transfer, arithmetic,
 * stack manipulation, and system call mnemonics.
 *
 * @param sym Mnemonic name.
 * @return True if recognized x86_64 mnemonic.
 */
bool DocsetManager::isX86Instruction(const QString& sym) {
    // Fast lookup hash set for common x86_64 assembly instruction mnemonics
    static const QSet<QString> mnemonics = {
        QStringLiteral("mov"), QStringLiteral("movsd"), QStringLiteral("lea"),
        QStringLiteral("push"), QStringLiteral("pop"), QStringLiteral("sub"),
        QStringLiteral("add"), QStringLiteral("xor"), QStringLiteral("cmp"),
        QStringLiteral("test"), QStringLiteral("je"), QStringLiteral("jz"),
        QStringLiteral("jne"), QStringLiteral("jnz"), QStringLiteral("jg"),
        QStringLiteral("jge"), QStringLiteral("jl"), QStringLiteral("jle"),
        QStringLiteral("jmp"), QStringLiteral("call"), QStringLiteral("leave"),
        QStringLiteral("ret"), QStringLiteral("nop"), QStringLiteral("endbr64"),
        QStringLiteral("syscall")
    };
    return mnemonics.contains(sym.trimmed().toLower());
}

/**
 * @brief Generates dark-slate x86_64 Architectural Instruction Reference HTML.
 * @param mnemonic x86_64 instruction mnemonic (e.g., "mov", "cmp", "jmp").
 * @return Formatted HTML manual page, or empty string if mnemonic unrecognized.
 */
QString DocsetManager::lookupX86InstructionHtml(const QString& mnemonic) const {
    const QString m = mnemonic.trimmed().toLower();
    if (!isX86Instruction(m)) {
        return QString();
    }

    struct X86Info {
        const char* title;
        const char* brief;
        const char* syntax;
        const char* operation;
        const char* flags;
        const char* details;
    };

    static const QMap<QString, X86Info> kDocs = {
        {QStringLiteral("mov"), {"MOV — Move Data", "Copies data from source operand to destination operand.", "MOV reg, reg/mem\nMOV reg/mem, reg\nMOV reg/mem, imm", "DEST <- SRC;", "None affected.", "In 64-bit mode, 32-bit register writes zero-extend to 64 bits."}},
        {QStringLiteral("movsd"), {"MOVSD — Move Scalar Double-Precision FP", "Moves 64-bit double-precision FP value between XMM registers or memory.", "MOVSD xmm1, xmm2/m64\nMOVSD m64, xmm1", "DEST[63:0] <- SRC[63:0];", "None affected.", "Scalar SSE2/AVX register transfers."}},
        {QStringLiteral("lea"), {"LEA — Load Effective Address", "Computes effective memory address and places it in destination register.", "LEA reg, [base + index*scale + disp]", "DEST <- EffectiveAddress(SRC);", "None affected.", "Performs fast address arithmetic without modifying EFLAGS."}},
        {QStringLiteral("cmp"), {"CMP — Compare Two Operands", "Compares first operand with second by subtracting second from first.", "CMP reg/mem, reg/imm", "temp <- DEST - SRC;", "CF, OF, SF, ZF, AF, PF set by subtraction difference.", "Operands are unmodified; conditional jumps test resulting status flags."}},
        {QStringLiteral("jmp"), {"JMP — Jump Unconditionally", "Transfers program execution control to target instruction.", "JMP rel32\nJMP reg/mem64", "RIP <- TargetAddress;", "None affected.", "Used for loop back-edges, tail calls, and direct control-flow redirection."}},
        {QStringLiteral("push"), {"PUSH — Push Word Onto Stack", "Pushes source operand onto execution stack.", "PUSH reg/mem64", "RSP <- RSP - 8; [RSP] <- SRC;", "None affected.", "RSP must maintain 16-byte alignment prior to CALL per System V ABI."}},
        {QStringLiteral("pop"), {"POP — Pop Top of Stack", "Loads value from top of stack into destination register.", "POP reg/mem64", "DEST <- [RSP]; RSP <- RSP + 8;", "None affected.", "Restores callee-saved or frame base registers."}},
        {QStringLiteral("call"), {"CALL — Call Procedure", "Calls subroutine, pushing return address onto stack.", "CALL rel32 / reg64", "PUSH RIP; RIP <- TargetAddress;", "None affected.", "Subroutine linkage follows System V AMD64 ABI convention."}},
        {QStringLiteral("ret"), {"RET — Return from Procedure", "Transfers control to return address popped from stack.", "RET", "RIP <- POP();", "None affected.", "Restores instruction pointer and unwinds procedure call frame."}},
        {QStringLiteral("leave"), {"LEAVE — High Level Procedure Exit", "Releases stack frame by copying RBP to RSP and popping old RBP.", "LEAVE", "RSP <- RBP; RBP <- POP();", "None affected.", "Equivalent to MOV RSP, RBP followed by POP RBP."}},
        {QStringLiteral("nop"), {"NOP — No Operation", "Performs no execution operation; advances RIP.", "NOP", "/* None */", "None affected.", "Used for code alignment and live instruction patching."}},
        {QStringLiteral("endbr64"), {"ENDBR64 — Terminate Indirect Branch 64-bit", "Signals indirect branch target for Intel CET shadow stack enforcement.", "ENDBR64", "/* CET verification */", "None affected.", "Hardware control-flow enforcement technology marker."}}
    };

    X86Info info;
    if (kDocs.contains(m)) {
        info = kDocs.value(m);
    } else if (m.startsWith('j')) {
        info = {"Conditional Jump", "Conditionally transfers control based on status flags.", "Jcc rel8 / rel32", "IF (ConditionMet) THEN RIP <- TargetAddress;", "None affected.", "Evaluates ZF, SF, OF, and CF status bits set by CMP/TEST."};
    } else {
        info = {"x86_64 Instruction", "Standard AMD64 / Intel 64 ISA instruction.", "INSN [operands]", "DEST <- SRC;", "Evaluated per Intel 64 & IA-32 specification.", "Documented in Intel 64 and IA-32 Architectures Manual."};
    }

    return QStringLiteral(
        "<html><head><style>"
        "body { background-color: #16181d; color: #d7dae0; font-family: 'Inter', sans-serif; "
        "font-size: 12px; margin: 0px; padding: 12px 16px; }"
        ".hdr { background-color: #1e222a; border: 1px solid #2c313a; padding: 6px 10px; margin-bottom: 10px; }"
        ".title { color: #61afef; font-family: 'JetBrains Mono', monospace; font-size: 14px; font-weight: bold; }"
        ".badge { color: #98c379; font-family: 'JetBrains Mono', monospace; font-size: 11px; }"
        ".sec { color: #e5c07b; font-family: 'JetBrains Mono', monospace; font-size: 11px; font-weight: bold; margin-top: 10px; margin-bottom: 4px; letter-spacing: 0.5px; }"
        ".code { background-color: #1e222a; border: 1px solid #2c313a; border-left: 3px solid #528bff; color: #abb2bf; font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 8px 10px; white-space: pre-wrap; margin: 4px 0px; }"
        "p, li { color: #abb2bf; line-height: 1.45; margin: 3px 0px; }"
        "</style></head><body>"
        "<div class='hdr'><span class='title'>%1</span> &nbsp; <span class='badge'>[Intel 64 &amp; IA-32 Architectures Software Developer's Manual — Instruction Set Reference]</span></div>"
        "<div class='sec'>NAME</div><p><b>%2</b> &mdash; %3</p>"
        "<div class='sec'>SYNOPSIS</div><div class='code'>%4</div>"
        "<div class='sec'>OPERATION</div><div class='code'>%5</div>"
        "<div class='sec'>FLAGS AFFECTED</div><p>%6</p>"
        "<div class='sec'>REGISTER &amp; PIPELINE SEMANTICS</div><p>%7</p>"
        "</body></html>")
        .arg(QString::fromUtf8(info.title), m.toUpper(), QString::fromUtf8(info.brief),
             QString::fromUtf8(info.syntax), QString::fromUtf8(info.operation),
             QString::fromUtf8(info.flags), QString::fromUtf8(info.details));
}

/**
 * @brief Retrieves and formats system man page content if available.
 * @param query Command or symbol name.
 * @return Formatted HTML man page, or empty string if not found.
 */
QString DocsetManager::lookupSystemManPageHtml(const QString& query) const {
    const QString cleaned = query.trimmed();
    if (cleaned.isEmpty()) {
        return QString();
    }
    QProcess proc;
    // Launch man command with plain text output formatting (-P cat)
    proc.start(QStringLiteral("man"), {QStringLiteral("-P"), QStringLiteral("cat"), cleaned});
    if (!proc.waitForFinished(1000) || proc.exitCode() != 0) {
        return QString();
    }
    const QString text = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    // Return empty string if system man page contains no printable content
    if (text.isEmpty()) {
        return QString();
    }
    return QStringLiteral(
        "<html><head><style>"
        "body { background-color: #16181d; color: #d7dae0; font-family: 'JetBrains Mono', monospace; "
        "font-size: 11px; margin: 0px; padding: 12px 16px; white-space: pre-wrap; line-height: 1.45; }"
        "h1, h2, h3 { color: #e5c07b; font-family: 'JetBrains Mono', monospace; }"
        "</style></head><body><pre>%1</pre></body></html>").arg(text.toHtmlEscaped());
}

/**
 * @brief Generates fallback ELF/C symbol card without MPI boilerplate.
 * @param rawSymbol Symbol name.
 * @return Formatted HTML reference card.
 */
QString DocsetManager::lookupGenericSymbolHtml(const QString& rawSymbol) const {
    const QString sym = normalizeSymbolName(rawSymbol);
    if (sym.isEmpty()) {
        return QString();
    }
    return QStringLiteral(
        "<html><head><style>"
        "body { background-color: #16181d; color: #d7dae0; font-family: 'Inter', sans-serif; "
        "font-size: 12px; margin: 0px; padding: 12px 16px; }"
        ".hdr { background-color: #1e222a; border: 1px solid #2c313a; padding: 6px 10px; margin-bottom: 10px; }"
        ".title { color: #61afef; font-family: 'JetBrains Mono', monospace; font-size: 14px; font-weight: bold; }"
        ".badge { color: #98c379; font-family: 'JetBrains Mono', monospace; font-size: 11px; }"
        ".sec { color: #e5c07b; font-family: 'JetBrains Mono', monospace; font-size: 11px; font-weight: bold; margin-top: 10px; margin-bottom: 4px; }"
        ".code { background-color: #1e222a; border: 1px solid #2c313a; border-left: 3px solid #528bff; color: #abb2bf; font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 8px 10px; margin: 4px 0px; }"
        "p, li { color: #abb2bf; line-height: 1.45; margin: 3px 0px; }"
        "</style></head><body>"
        "<div class='hdr'><span class='title'>%1</span> &nbsp; <span class='badge'>[ELF64 Symbol Reference]</span></div>"
        "<div class='sec'>IDENTIFIER</div><p><b>%1</b> &mdash; Dynamic or static symbol token in active binary target.</p>"
        "<div class='sec'>PROTOTYPE</div><div class='code'>/* Symbol Identifier: %1 */\nextern void* %1;</div>"
        "<div class='sec'>CALLING CONVENTION</div><p>System V AMD64 ABI (Integer: RDI, RSI, RDX, RCX, R8, R9 | Return: RAX).</p>"
        "</body></html>").arg(sym.toHtmlEscaped());
}

} // namespace gridlock::core

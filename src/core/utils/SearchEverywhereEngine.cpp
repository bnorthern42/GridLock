/**
 * @file SearchEverywhereEngine.cpp
 * @brief Implementation of JetBrains-style CamelCase fuzzy scoring, symbol extraction, and workspace text grep.
 */

#include "SearchEverywhereEngine.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <algorithm>
#include <limits>

namespace gridlock::core::utils {

namespace {

bool isIdentDelimiter(QChar c) {
    return c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char('_') ||
           c == QLatin1Char('-') || c == QLatin1Char(' ') || c == QLatin1Char(':');
}

bool isWordBoundaryAt(const QString& text, int j) {
    if (j <= 0) return true;
    const QChar prev = text.at(j - 1);
    const QChar curr = text.at(j);
    if (isIdentDelimiter(prev) || prev == QLatin1Char('.')) return true;
    if (prev.isLower() && curr.isUpper()) return true;
    if (prev.isLetter() && curr.isDigit()) return true;
    return !prev.isLetterOrNumber() && curr.isLetterOrNumber();
}

bool isIdentHumpAt(const QString& text, int j) {
    if (j <= 0) return true;
    const QChar prev = text.at(j - 1);
    const QChar curr = text.at(j);
    if (!curr.isLetterOrNumber()) return false;
    if (isIdentDelimiter(prev)) return true;
    return prev.isLower() && curr.isUpper();
}

}  // namespace

bool SearchEverywhereEngine::isBuildArtifactPath(const QString& path) {
    const QString lower = path.toLower();
    if (lower.contains(QStringLiteral("builddir/")) || lower.contains(QStringLiteral("/build/")) ||
        lower.startsWith(QStringLiteral("build/")) || lower.contains(QStringLiteral(".cache/")) ||
        lower.contains(QStringLiteral(".git/"))) {
        return true;
    }
    return lower.endsWith(QStringLiteral(".o")) || lower.endsWith(QStringLiteral(".d")) ||
           lower.endsWith(QStringLiteral(".a")) || lower.endsWith(QStringLiteral(".so")) ||
           lower.endsWith(QStringLiteral(".pyc"));
}

int SearchEverywhereEngine::scoreMatch(const QString& query, const QString& candidate,
                                       QVector<int>* matchedIndices) {
    if (matchedIndices) matchedIndices->clear();
    if (query.isEmpty()) return 0;
    const int m = query.size();
    const int n = candidate.size();
    if (n == 0 || m > n) return -1;

    // Fast linear case-insensitive subsequence pre-check.
    int checkIdx = 0;
    for (int j = 0; j < n && checkIdx < m; ++j) {
        if (query.at(checkIdx).toLower() == candidate.at(j).toLower()) ++checkIdx;
    }
    if (checkIdx < m) return -1;

    int basenameStart = 0;
    for (int j = n - 1; j >= 0; --j) {
        if (candidate.at(j) == QLatin1Char('/') || candidate.at(j) == QLatin1Char('\\')) {
            basenameStart = j + 1;
            break;
        }
    }

    // Precompute word boundaries and previous identifier hump index for CamelCase/snake_case acronym chaining.
    QVector<bool> wordBoundary(n, false);
    QVector<bool> identHump(n, false);
    QVector<int> prevIdentHump(n, -1);
    int lastHump = -1;
    for (int j = 0; j < n; ++j) {
        wordBoundary[j] = isWordBoundaryAt(candidate, j);
        identHump[j] = isIdentHumpAt(candidate, j);
        prevIdentHump[j] = lastHump;
        if (identHump[j]) lastHump = j;
    }

    auto charBonus = [&](int i, int j) -> int {
        const QChar qc = query.at(i);
        const QChar cc = candidate.at(j);
        int bonus = 16 + (qc == cc ? 6 : 0);
        if (qc.isUpper() && cc.isUpper()) bonus += 18;
        if (j >= basenameStart) bonus += 14;
        if (j == basenameStart) return bonus + 85;
        if (j == 0) return bonus + 75;
        if (identHump[j]) return bonus + 52;
        if (wordBoundary[j]) return bonus + 36;
        return bonus;
    };

    constexpr int kNegInf = std::numeric_limits<int>::min() / 4;
    QVector<QVector<int>> dp(m, QVector<int>(n, kNegInf));
    QVector<QVector<int>> parent(m, QVector<int>(n, -1));

    const QChar firstLower = query.at(0).toLower();
    for (int j = 0; j < n; ++j) {
        if (candidate.at(j).toLower() == firstLower) {
            const int distPenalty = (j >= basenameStart) ? std::min(j - basenameStart, 12) : std::min(j, 20);
            dp[0][j] = charBonus(0, j) - distPenalty;
        }
    }

    for (int i = 1; i < m; ++i) {
        const QChar patLower = query.at(i).toLower();
        for (int j = i; j < n; ++j) {
            if (candidate.at(j).toLower() != patLower) continue;
            const int cBonus = charBonus(i, j);
            int bestPrevScore = kNegInf;
            int bestPrevIdx = -1;
            for (int prevJ = i - 1; prevJ < j; ++prevJ) {
                if (dp[i - 1][prevJ] <= kNegInf) continue;
                int transition = cBonus;
                if (j == prevJ + 1) {
                    transition += 44;
                } else if (identHump[j] && prevIdentHump[j] == prevJ) {
                    // Consecutive CamelCase / snake_case acronym hump (e.g. MWC -> MainWindowCohesion).
                    transition += 82;
                } else if (identHump[j] && identHump[prevJ]) {
                    transition += 34 - std::min(j - prevJ - 1, 14);
                } else {
                    transition -= std::min(j - prevJ - 1, 18);
                }
                const int total = dp[i - 1][prevJ] + transition;
                if (total > bestPrevScore) {
                    bestPrevScore = total;
                    bestPrevIdx = prevJ;
                }
            }
            dp[i][j] = bestPrevScore;
            parent[i][j] = bestPrevIdx;
        }
    }

    int bestEndJ = -1;
    int bestTotal = kNegInf;
    for (int j = m - 1; j < n; ++j) {
        if (dp[m - 1][j] > bestTotal) {
            bestTotal = dp[m - 1][j];
            bestEndJ = j;
        }
    }
    if (bestEndJ < 0 || bestTotal <= kNegInf) return -1;

    if (matchedIndices) {
        matchedIndices->resize(m);
        int currJ = bestEndJ;
        for (int i = m - 1; i >= 0; --i) {
            (*matchedIndices)[i] = currJ;
            currJ = (i > 0) ? parent[i][currJ] : -1;
        }
    }

    const QString basename = candidate.mid(basenameStart);
    int finalScore = bestTotal + std::max(0, 40 - static_cast<int>(basename.size()) / 2);
    if (query.compare(candidate, Qt::CaseInsensitive) == 0) finalScore += 350;
    else if (query.compare(basename, Qt::CaseInsensitive) == 0) finalScore += 300;
    else if (candidate.startsWith(query, Qt::CaseInsensitive)) finalScore += 220;
    else if (basename.startsWith(query, Qt::CaseInsensitive)) finalScore += 190;
    else if (basename.contains(query, Qt::CaseInsensitive)) finalScore += 95;
    else if (candidate.contains(query, Qt::CaseInsensitive)) finalScore += 60;

    if (isBuildArtifactPath(candidate)) finalScore -= 260;
    return std::max(1, finalScore);
}

const QStringList& SearchEverywhereEngine::indexWorkspaceFiles(const QString& workspaceRoot,
                                                               bool forceRefresh) {
    const QString canon = QFileInfo(workspaceRoot).canonicalFilePath().isEmpty()
                              ? QFileInfo(workspaceRoot).absoluteFilePath()
                              : QFileInfo(workspaceRoot).canonicalFilePath();
    if (!forceRefresh && canon == m_cachedRoot && !m_cachedFiles.isEmpty()) {
        return m_cachedFiles;
    }
    m_cachedRoot = canon;
    m_cachedFiles.clear();
    if (canon.isEmpty() || !QFileInfo::exists(canon)) return m_cachedFiles;

    const QSet<QString> allowedExts = {
        QStringLiteral("cpp"),  QStringLiteral("c"),    QStringLiteral("cc"),
        QStringLiteral("cxx"),  QStringLiteral("hpp"),  QStringLiteral("h"),
        QStringLiteral("hh"),   QStringLiteral("hxx"),  QStringLiteral("cu"),
        QStringLiteral("cuh"),  QStringLiteral("f90"),  QStringLiteral("f95"),
        QStringLiteral("f"),    QStringLiteral("py"),   QStringLiteral("toml"),
        QStringLiteral("yaml"), QStringLiteral("yml"),  QStringLiteral("json"),
        QStringLiteral("cmake"), QStringLiteral("md"),  QStringLiteral("sh")};
    const QSet<QString> allowedNames = {
        QStringLiteral("meson.build"), QStringLiteral("CMakeLists.txt"), QStringLiteral("Makefile")};

    QDir rootDir(canon);
    QDirIterator it(canon, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && m_cachedFiles.size() < 2000) {
        const QString filePath = it.next();
        const QString relPath = rootDir.relativeFilePath(filePath);
        if (relPath.startsWith(QStringLiteral(".git/")) || relPath.startsWith(QStringLiteral("builddir/")) ||
            relPath.startsWith(QStringLiteral("build/")) || relPath.startsWith(QStringLiteral("build_meson/")) ||
            relPath.startsWith(QStringLiteral("build_cmake/")) || relPath.startsWith(QStringLiteral(".gridlock/")) ||
            relPath.startsWith(QStringLiteral("external/")) || relPath.startsWith(QStringLiteral(".cache/")) ||
            relPath.contains(QStringLiteral("/."))) {
            continue;
        }
        const QFileInfo fi(filePath);
        if (allowedExts.contains(fi.suffix().toLower()) || allowedNames.contains(fi.fileName())) {
            m_cachedFiles.append(relPath);
        }
    }
    m_cachedFiles.sort(Qt::CaseInsensitive);
    return m_cachedFiles;
}

QString SearchEverywhereEngine::resolveWorkspaceRoot(const QString& activeDir, const QString& explorerDir) {
    QString wsRoot = !activeDir.isEmpty() ? activeDir : explorerDir;
    if (wsRoot.isEmpty() || !QFileInfo::exists(wsRoot)) {
        wsRoot = QFileInfo::exists(QStringLiteral("tutorial")) ? QDir::currentPath()
                                                               : QDir(QDir::currentPath()).filePath(QStringLiteral(".."));
    } else if (activeDir.isEmpty()) {
        QDir d(wsRoot);
        if ((d.dirName().startsWith(QStringLiteral("build"), Qt::CaseInsensitive) || d.exists(QStringLiteral("build.ninja"))) &&
            !d.exists(QStringLiteral("meson.build")) && !d.exists(QStringLiteral("CMakeLists.txt")) &&
            (d.exists(QStringLiteral("../meson.build")) || d.exists(QStringLiteral("../CMakeLists.txt")) || d.exists(QStringLiteral("../tutorial")))) {
            wsRoot = d.filePath(QStringLiteral(".."));
        }
    }
    const QString canon = QFileInfo(wsRoot).canonicalFilePath();
    return canon.isEmpty() ? QFileInfo(wsRoot).absoluteFilePath() : canon;
}

QVector<IndexedFile> SearchEverywhereEngine::discoverPrioritizedFiles(const QString& workspaceRoot) {
    const QStringList& relList = indexWorkspaceFiles(workspaceRoot);
    QDir rootDir(m_cachedRoot);
    QVector<IndexedFile> result;
    result.reserve(relList.size() + 8);
    QSet<QString> seen;
    auto pushFile = [&](const QString& rel, const QString& abs) {
        if (rel.isEmpty() || seen.contains(rel)) return;
        seen.insert(rel);
        const QFileInfo fi(abs);
        const QString ext = fi.suffix().toLower();
        QString badge = QStringLiteral("File");
        if (ext == QStringLiteral("cpp") || ext == QStringLiteral("cc") || ext == QStringLiteral("c") || ext == QStringLiteral("cu")) badge = QStringLiteral("C/C++");
        else if (ext == QStringLiteral("hpp") || ext == QStringLiteral("h")) badge = QStringLiteral("Header");
        else if (fi.fileName() == QStringLiteral("meson.build") || fi.fileName() == QStringLiteral("CMakeLists.txt")) badge = QStringLiteral("Build");
        result.append(IndexedFile{rel, abs, badge});
    };
    for (const QString& tutRel : {QStringLiteral("tutorial/inspection_demo.cpp"), QStringLiteral("tutorial/deadlock_ring.cpp"),
                                  QStringLiteral("tutorial/collective_mismatch.cpp"), QStringLiteral("tutorial/ghost_cell_drift.cpp"),
                                  QStringLiteral("tutorial/heap_imbalance.cpp"), QStringLiteral("tutorial/tag_mismatch_stall.cpp"),
                                  QStringLiteral("tutorial/simd_struct_watch.cpp")}) {
        QString abs = rootDir.filePath(tutRel);
        if (!QFileInfo::exists(abs) && QFileInfo::exists(tutRel)) abs = QFileInfo(tutRel).absoluteFilePath();
        pushFile(tutRel, abs);
    }
    for (const QString& rel : relList) pushFile(rel, rootDir.filePath(rel));
    return result;
}

void SearchEverywhereEngine::extractSymbolsFromText(const QString& text,
                                                    const QString& relPath,
                                                    const QString& absPath,
                                                    QVector<IndexedSymbol>& out) {
    static const QRegularExpression cppTypeRx(
        QStringLiteral("^\\s*(struct|class|enum\\s+class|enum)\\s+([A-Za-z_][A-Za-z0-9_]*)"));
    static const QRegularExpression cppMacroRx(
        QStringLiteral("^\\s*#\\s*define\\s+([A-Za-z_][A-Za-z0-9_]{2,})\\b"));
    static const QRegularExpression cppFuncRx(
        QStringLiteral("^\\s*(?:[A-Za-z_][A-Za-z0-9_:<>,\\s\\*&~]*\\s+)?([A-Za-z_~][A-Za-z0-9_:]*)\\s*\\([^;]*\\)\\s*(?:const\\s*)?(?:noexcept\\s*)?(?:override\\s*)?\\{?\\s*$"));
    static const QRegularExpression pySymRx(
        QStringLiteral("^\\s*(def|class)\\s+([A-Za-z_][A-Za-z0-9_]*)"));
    static const QRegularExpression fortranSymRx(
        QStringLiteral("^\\s*(subroutine|function|module|program)\\s+([A-Za-z_][A-Za-z0-9_]*)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QSet<QString> controlKeywords = {
        QStringLiteral("if"), QStringLiteral("for"), QStringLiteral("while"),
        QStringLiteral("switch"), QStringLiteral("catch"), QStringLiteral("return"),
        QStringLiteral("sizeof"), QStringLiteral("alignof")};

    const QString ext = QFileInfo(relPath).suffix().toLower();
    const bool isPy = (ext == QStringLiteral("py"));
    const bool isFortran = (ext == QStringLiteral("f90") || ext == QStringLiteral("f95") || ext == QStringLiteral("f"));

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const int ln = i + 1;
        const QString& line = lines.at(i);
        if (line.trimmed().isEmpty()) continue;

        if (isPy) {
            if (const auto m = pySymRx.match(line); m.hasMatch()) {
                const bool isCls = (m.captured(1) == QStringLiteral("class"));
                out.append(IndexedSymbol{isCls ? m.captured(2) : (m.captured(2) + QStringLiteral("()")),
                                         isCls ? QStringLiteral("Class") : QStringLiteral("Function"),
                                         relPath, absPath, ln});
            }
            continue;
        }
        if (isFortran) {
            if (const auto m = fortranSymRx.match(line); m.hasMatch()) {
                out.append(IndexedSymbol{m.captured(2), m.captured(1).toUpper(), relPath, absPath, ln});
            }
            continue;
        }
        if (const auto m = cppTypeRx.match(line); m.hasMatch()) {
            const QString kind = m.captured(1).startsWith(QStringLiteral("class"))
                                     ? QStringLiteral("Class")
                                     : (m.captured(1).startsWith(QStringLiteral("enum")) ? QStringLiteral("Enum") : QStringLiteral("Struct"));
            out.append(IndexedSymbol{m.captured(2), kind, relPath, absPath, ln});
            continue;
        }
        if (const auto m = cppMacroRx.match(line); m.hasMatch()) {
            out.append(IndexedSymbol{m.captured(1), QStringLiteral("Macro"), relPath, absPath, ln});
            continue;
        }
        if (const auto m = cppFuncRx.match(line); m.hasMatch()) {
            const QString fn = m.captured(1);
            const QString bare = fn.section(QStringLiteral("::"), -1);
            if (!controlKeywords.contains(bare)) {
                out.append(IndexedSymbol{fn + QStringLiteral("()"), QStringLiteral("Function"), relPath, absPath, ln});
            }
        }
    }
}

QVector<IndexedSymbol> SearchEverywhereEngine::collectSymbols(const QString& workspaceRoot,
                                                              const QString& activeFilePath,
                                                              const QString& activeBufferText) {
    const QStringList& files = indexWorkspaceFiles(workspaceRoot);
    if (m_cachedSymbolsRoot != m_cachedRoot) {
        m_cachedSymbolsRoot = m_cachedRoot;
        m_cachedDiskSymbols.clear();
        QDir rootDir(m_cachedRoot);
        int scannedFiles = 0;
        for (const QString& rel : files) {
            if (scannedFiles >= 140 || m_cachedDiskSymbols.size() >= 1200) break;
            const QString ext = QFileInfo(rel).suffix().toLower();
            if (ext != QStringLiteral("hpp") && ext != QStringLiteral("h") &&
                ext != QStringLiteral("cpp") && ext != QStringLiteral("c") &&
                ext != QStringLiteral("cc") && ext != QStringLiteral("cu") &&
                ext != QStringLiteral("py") && ext != QStringLiteral("f90")) {
                continue;
            }
            const QString abs = rootDir.filePath(rel);
            QFile f(abs);
            if (f.size() > 256 * 1024 || !f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            extractSymbolsFromText(QString::fromUtf8(f.readAll()), rel, abs, m_cachedDiskSymbols);
            ++scannedFiles;
        }
    }

    QVector<IndexedSymbol> combined;
    QSet<QString> seen;
    auto pushUnique = [&](const IndexedSymbol& s) {
        const QString key = QStringLiteral("%1|%2|%3").arg(s.name, s.absPath).arg(s.lineNumber);
        if (!seen.contains(key)) {
            seen.insert(key);
            combined.append(s);
        }
    };

    if (!activeBufferText.isEmpty()) {
        const QString rel = !activeFilePath.isEmpty() && !m_cachedRoot.isEmpty()
                                ? QDir(m_cachedRoot).relativeFilePath(activeFilePath)
                                : QFileInfo(activeFilePath).fileName();
        QVector<IndexedSymbol> liveSyms;
        extractSymbolsFromText(activeBufferText, rel.isEmpty() ? QStringLiteral("active_buffer.cpp") : rel,
                               activeFilePath, liveSyms);
        for (const auto& s : liveSyms) pushUnique(s);
    }
    for (const auto& s : m_cachedDiskSymbols) pushUnique(s);
    return combined;
}

QVector<TextSearchHit> SearchEverywhereEngine::searchWorkspaceText(const QString& workspaceRoot,
                                                                   const QString& query,
                                                                   int maxResults) {
    QVector<TextSearchHit> hits;
    const QString needle = query.trimmed();
    if (needle.isEmpty() || maxResults <= 0) return hits;

    const QStringList& files = indexWorkspaceFiles(workspaceRoot);
    QDir rootDir(m_cachedRoot);
    for (const QString& rel : files) {
        if (hits.size() >= maxResults) break;
        const QString abs = rootDir.filePath(rel);
        QFile file(abs);
        if (file.size() <= 0 || file.size() > 1024 * 1024 || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        QTextStream in(&file);
        int lineNum = 0;
        while (!in.atEnd() && hits.size() >= 0 && hits.size() < maxResults) {
            const QString line = in.readLine();
            ++lineNum;
            const int col = line.indexOf(needle, 0, Qt::CaseInsensitive);
            if (col >= 0) {
                QString preview = line.trimmed();
                if (preview.size() > 120) preview = preview.left(117) + QStringLiteral("...");
                hits.append(TextSearchHit{rel, abs, lineNum, col, preview});
            }
        }
    }
    return hits;
}

QString SearchEverywhereEngine::loadFilePreviewSnippet(const QString& filePath,
                                                       int targetOneBasedLine,
                                                       int contextLines,
                                                       int* outStartLine,
                                                       int* outHighlightedBlock) {
    if (outStartLine) *outStartLine = 1;
    if (outHighlightedBlock) *outHighlightedBlock = -1;
    if (filePath.isEmpty() || !QFileInfo::exists(filePath)) return QString();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();

    const int target = std::max(1, targetOneBasedLine);
    const int before = std::max(1, contextLines);
    const int windowSize = before * 2 + 5;  // 15 before + target + 19 after = 35 lines
    const int startLine = std::max(1, target - before);
    const int endLine = startLine + windowSize - 1;

    constexpr int kMaxLinesRead = 250;
    constexpr qint64 kMaxBytesRead = 32 * 1024;

    QStringList rawLines;
    QTextStream in(&file);
    int cur = 0;
    qint64 bytesRead = 0;
    while (!in.atEnd() && cur < endLine && cur < kMaxLinesRead && bytesRead < kMaxBytesRead) {
        const QString line = in.readLine();
        ++cur;
        bytesRead += static_cast<qint64>(line.size()) + 1;
        if (cur >= startLine) rawLines.append(line);
    }
    if (rawLines.isEmpty()) return QString();

    const int lastLine = startLine + static_cast<int>(rawLines.size()) - 1;
    const int numWidth = std::max(2, static_cast<int>(QString::number(lastLine).size()));
    QStringList formattedLines;
    formattedLines.reserve(rawLines.size());
    for (int i = 0; i < rawLines.size(); ++i) {
        const int lineNum = startLine + i;
        const QString marker = (lineNum == target) ? QStringLiteral("> ") : QStringLiteral("  ");
        const QString numStr = QString::number(lineNum).rightJustified(numWidth, QLatin1Char(' '));
        formattedLines.append(QStringLiteral("%1%2 | %3").arg(marker, numStr, rawLines.at(i)));
    }

    if (outStartLine) *outStartLine = startLine;
    if (outHighlightedBlock) {
        *outHighlightedBlock = std::clamp(target - startLine, 0, static_cast<int>(formattedLines.size()) - 1);
    }
    return formattedLines.join(QLatin1Char('\n'));
}

}  // namespace gridlock::core::utils

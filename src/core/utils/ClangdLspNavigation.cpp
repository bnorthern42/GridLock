/**
 * @file ClangdLspNavigation.cpp
 * @brief Workspace-aware clangd JSON-RPC 2.0 navigation, references, AST rename, and diagnostics.
 *
 * Implements `compile_commands.json` auto-discovery, live `didOpen`/`didChange` buffer
 * synchronization, `textDocument/publishDiagnostics` parsing, cross-file `textDocument/definition`,
 * `textDocument/references`, and range-accurate `textDocument/rename` `WorkspaceEdit` application.
 */

#include "ClangdLspClient.hpp"

#include <algorithm>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>

namespace gridlock::core {

namespace {

/**
 * @brief Normalizes a local file path or `file://` URI into a canonical filesystem path when present.
 * @param rawPath Input file path or URI string.
 * @return Canonical local path if the file exists on disk, otherwise absolute file path.
 */
QString normalizeLocalPath(const QString& rawPath) {
    if (rawPath.trimmed().isEmpty()) {
        return {};
    }
    // Decode `file://` URIs into native filesystem paths first.
    const QString localCandidate = rawPath.startsWith(QStringLiteral("file://"))
                                       ? QUrl(rawPath).toLocalFile()
                                       : rawPath;
    if (localCandidate.isEmpty()) {
        return {};
    }
    const QFileInfo fi(localCandidate);
    if (fi.exists()) {
        const QString canon = fi.canonicalFilePath();
        if (!canon.isEmpty()) {
            return canon;
        }
    }
    return fi.absoluteFilePath();
}

/**
 * @brief Extracts the C++ identifier token at or immediately left of `zeroBasedCol` on `targetLine`.
 * @param targetLine Single line of C/C++ source text.
 * @param zeroBasedCol 0-based character column index.
 * @return Extracted identifier token, or empty string if none is adjacent.
 */
QString extractSymbolAtColumn(const QString& targetLine, int zeroBasedCol) {
    if (targetLine.isEmpty()) {
        return {};
    }
    int col = std::clamp(zeroBasedCol, 0, static_cast<int>(targetLine.size()) - 1);
    while (col > 0 && !targetLine.at(col).isLetterOrNumber() && targetLine.at(col) != QLatin1Char('_')) {
        --col;
    }
    if (!targetLine.at(col).isLetterOrNumber() && targetLine.at(col) != QLatin1Char('_')) {
        return {};
    }
    int start = col;
    int end = col;
    while (start > 0 && (targetLine.at(start - 1).isLetterOrNumber() || targetLine.at(start - 1) == QLatin1Char('_'))) {
        --start;
    }
    while (end < targetLine.size() && (targetLine.at(end).isLetterOrNumber() || targetLine.at(end) == QLatin1Char('_'))) {
        ++end;
    }
    return targetLine.mid(start, end - start).trimmed();
}

/**
 * @struct LspTextEditSpan
 * @brief Represents a single character-offset edit decoded from an LSP `WorkspaceEdit`.
 */
struct LspTextEditSpan {
    int startLine = 0;   ///< 0-based start line.
    int startChar = 0;   ///< 0-based start character offset.
    int endLine = 0;     ///< 0-based end line.
    int endChar = 0;     ///< 0-based end character offset.
    QString newText;     ///< Replacement text payload.
};

/**
 * @brief Applies a list of LSP `TextEdit` entries bottom-up to `originalText`.
 *
 * Sorts edits in descending `(startOffset, endOffset)` order so earlier line and
 * character offsets remain invariant while applying multi-site replacements.
 *
 * @param originalText Source buffer text with `\n` line endings.
 * @param edits Unordered vector of LSP range edits for a single document.
 * @return Updated document string with all edits applied accurately.
 */
QString applyTextEditsBottomUp(const QString& originalText, const QVector<LspTextEditSpan>& edits) {
    if (edits.isEmpty()) {
        return originalText;
    }
    // Precompute 0-based character start offset of every line in `originalText`.
    QVector<int> lineStarts;
    lineStarts.reserve(originalText.count(QLatin1Char('\n')) + 2);
    lineStarts.append(0);
    for (int i = 0; i < originalText.size(); ++i) {
        if (originalText.at(i) == QLatin1Char('\n')) {
            lineStarts.append(i + 1);
        }
    }

    // Helper converting `(0-based line, 0-based character)` into an absolute QString index.
    auto toOffset = [&](int line, int character) -> int {
        if (line <= 0) {
            const int firstEnd = (lineStarts.size() > 1) ? (lineStarts.at(1) - 1) : static_cast<int>(originalText.size());
            return std::clamp(character, 0, std::max(0, firstEnd));
        }
        if (line >= lineStarts.size()) {
            return static_cast<int>(originalText.size());
        }
        const int startOff = lineStarts.at(line);
        const int endOff = (line + 1 < lineStarts.size())
                               ? (lineStarts.at(line + 1) - 1)
                               : static_cast<int>(originalText.size());
        return std::clamp(startOff + character, startOff, std::max(startOff, endOff));
    };

    struct ResolvedEdit {
        int startOff = 0;
        int endOff = 0;
        QString replacement;
    };
    QVector<ResolvedEdit> resolved;
    resolved.reserve(edits.size());
    for (const auto& e : edits) {
        const int s = toOffset(e.startLine, e.startChar);
        const int f = std::max(s, toOffset(e.endLine, e.endChar));
        resolved.append({s, f, e.newText});
    }

    // Sort descending by start offset so bottom-up replacement never shifts preceding spans.
    std::sort(resolved.begin(), resolved.end(), [](const ResolvedEdit& a, const ResolvedEdit& b) {
        return (a.startOff != b.startOff) ? (a.startOff > b.startOff) : (a.endOff > b.endOff);
    });

    QString updated = originalText;
    for (const auto& r : resolved) {
        updated.replace(r.startOff, r.endOff - r.startOff, r.replacement);
    }
    return updated;
}

/**
 * @brief Lexer-aware identifier scanner used strictly when the `clangd` binary is unavailable on host.
 *
 * Tracks `//` line comments, C-style block comments, double-quoted string literals, and character
 * literals so fallback token queries and renames never corrupt comments or string constants.
 *
 * @param sourceText Input C/C++ buffer.
 * @param targetIdent Exact identifier to match in code tokens.
 * @return Vector of `(startOffset, length, 1-based line, 0-based col)` matches.
 */
struct LexicalTokenMatch {
    int offset = 0;
    int lineOneBased = 1;
    int colZeroBased = 0;
};

QVector<LexicalTokenMatch> scanCodeIdentifierMatches(const QString& sourceText, const QString& targetIdent) {
    QVector<LexicalTokenMatch> matches;
    if (targetIdent.isEmpty()) {
        return matches;
    }
    bool inLineComment = false;
    bool inBlockComment = false;
    bool inString = false;
    bool inChar = false;
    int lineOneBased = 1;
    int lineStartIdx = 0;

    // Walk source buffer character-by-character skipping comments and string/character literals.
    const int n = static_cast<int>(sourceText.size());
    for (int i = 0; i < n; ++i) {
        const QChar c = sourceText.at(i);
        const QChar next = (i + 1 < n) ? sourceText.at(i + 1) : QChar();

        if (c == QLatin1Char('\n')) {
            inLineComment = false;
            ++lineOneBased;
            lineStartIdx = i + 1;
            continue;
        }
        if (inLineComment) continue;
        if (inBlockComment) {
            if (c == QLatin1Char('*') && next == QLatin1Char('/')) {
                inBlockComment = false;
                ++i;
            }
            continue;
        }
        if (inString) {
            if (c == QLatin1Char('\\') && i + 1 < n) ++i;
            else if (c == QLatin1Char('"')) inString = false;
            continue;
        }
        if (inChar) {
            if (c == QLatin1Char('\\') && i + 1 < n) ++i;
            else if (c == QLatin1Char('\'')) inChar = false;
            continue;
        }

        // Transition into comments or string/char literals when delimiters appear in code state.
        if (c == QLatin1Char('/') && next == QLatin1Char('/')) {
            inLineComment = true;
            ++i;
            continue;
        }
        if (c == QLatin1Char('/') && next == QLatin1Char('*')) {
            inBlockComment = true;
            ++i;
            continue;
        }
        if (c == QLatin1Char('"')) {
            inString = true;
            continue;
        }
        if (c == QLatin1Char('\'')) {
            inChar = true;
            continue;
        }

        // Parse maximal C++ identifier token `[A-Za-z_][A-Za-z0-9_]*` and compare against targetIdent.
        if (c.isLetter() || c == QLatin1Char('_')) {
            const int tokStart = i;
            while (i + 1 < n && (sourceText.at(i + 1).isLetterOrNumber() || sourceText.at(i + 1) == QLatin1Char('_'))) {
                ++i;
            }
            const int tokLen = i - tokStart + 1;
            if (sourceText.mid(tokStart, tokLen) == targetIdent) {
                matches.append({tokStart, lineOneBased, tokStart - lineStartIdx});
            }
        }
    }
    return matches;
}

/**
 * @brief Writes an inline C++ snippet to a deterministic temporary file on disk for clangd TU queries.
 */
QString materializeInlineBufferFile(const QString& fileTag, const QString& sourceCode) {
    const QString tempPath = QDir::tempPath() + QLatin1Char('/') + fileTag;
    QFile f(tempPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write(sourceCode.toUtf8());
        f.close();
    }
    return normalizeLocalPath(tempPath);
}

}  // namespace

/**
 * @brief Auto-discovers the directory containing `compile_commands.json` walking upward from `startPath`.
 */
QString ClangdLspClient::discoverCompileCommandsDir(const QString& startPath) {
    const QFileInfo startInfo(startPath.trimmed().isEmpty() ? QDir::currentPath() : startPath);
    QDir dir(startInfo.isDir() ? startInfo.absoluteFilePath() : startInfo.absolutePath());
    if (!dir.exists()) {
        return {};
    }

    // Walk ancestor directories checking `<dir>`, `<dir>/builddir`, and `<dir>/build`.
    while (true) {
        const QString directDb = dir.filePath(QStringLiteral("compile_commands.json"));
        if (QFileInfo::exists(directDb)) {
            return normalizeLocalPath(dir.absolutePath());
        }
        const QString mesonDb = dir.filePath(QStringLiteral("builddir/compile_commands.json"));
        if (QFileInfo::exists(mesonDb)) {
            return normalizeLocalPath(dir.filePath(QStringLiteral("builddir")));
        }
        const QString cmakeDb = dir.filePath(QStringLiteral("build/compile_commands.json"));
        if (QFileInfo::exists(cmakeDb)) {
            return normalizeLocalPath(dir.filePath(QStringLiteral("build")));
        }
        if (!dir.cdUp()) {
            break;
        }
    }
    return {};
}

/**
 * @brief Synchronizes an open editor buffer with clangd via `textDocument/didOpen` or `textDocument/didChange`.
 */
void ClangdLspClient::syncDocument(const QString& filePath, const QString& sourceText) {
    const QString normPath = normalizeLocalPath(filePath);
    if (normPath.isEmpty()) {
        return;
    }
    const QString rootDir = QFileInfo(normPath).absolutePath();
    if (!ensureServerInitialized(rootDir)) {
        return;
    }

    m_openDocumentTexts.insert(normPath, sourceText);
    const QString docUri = QUrl::fromLocalFile(normPath).toString();
    const int version = m_documentVersion++;

    // Send `didOpen` the first time clangd sees this URI, and `didChange` on subsequent edits.
    if (!m_openDocumentVersions.contains(docUri)) {
        m_openDocumentVersions.insert(docUri, version);
        sendJsonRpc(QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("method"), QStringLiteral("textDocument/didOpen")},
            {QStringLiteral("params"), QJsonObject{
                {QStringLiteral("textDocument"), QJsonObject{
                    {QStringLiteral("uri"), docUri},
                    {QStringLiteral("languageId"), QStringLiteral("cpp")},
                    {QStringLiteral("version"), version},
                    {QStringLiteral("text"), sourceText}
                }}
            }}
        });
    } else {
        m_openDocumentVersions.insert(docUri, version);
        sendJsonRpc(QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("method"), QStringLiteral("textDocument/didChange")},
            {QStringLiteral("params"), QJsonObject{
                {QStringLiteral("textDocument"), QJsonObject{
                    {QStringLiteral("uri"), docUri},
                    {QStringLiteral("version"), version}
                }},
                {QStringLiteral("contentChanges"), QJsonArray{
                    QJsonObject{{QStringLiteral("text"), sourceText}}
                }}
            }}
        });
    }
}

/**
 * @brief Returns the most recently published diagnostics cached for `filePath`.
 * @param filePath Local filesystem path or `file://` URI of an opened translation unit.
 * @return Cached `QVector<LspDiagnostic>` from the latest `textDocument/publishDiagnostics` push.
 */
QVector<LspDiagnostic> ClangdLspClient::cachedDiagnosticsForFile(const QString& filePath) const {
    return m_latestDiagnostics.value(normalizeLocalPath(filePath));
}

/**
 * @brief Parses a JSON-RPC `textDocument/publishDiagnostics` notification into LspDiagnostic records.
 * @param jsonMessage Raw JSON payload or `Content-Length` framed LSP notification bytes.
 * @return Vector of 1-based line `LspDiagnostic` entries decoded from `params.diagnostics`.
 */
QVector<LspDiagnostic> ClangdLspClient::parsePublishDiagnostics(const QByteArray& jsonMessage) {
    QVector<LspDiagnostic> result;
    // Strip optional HTTP-like Content-Length header if present.
    QByteArray body = jsonMessage;
    const int sepIdx = body.indexOf("\r\n\r\n");
    if (sepIdx >= 0) {
        body = body.mid(sepIdx + 4);
    }
    // Parse JSON document and validate root object structure.
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return result;
    }
    const QJsonObject root = doc.object();
    const QJsonObject params = root.value(QStringLiteral("params")).toObject();
    const QJsonArray diags = params.contains(QStringLiteral("diagnostics"))
                                 ? params.value(QStringLiteral("diagnostics")).toArray()
                                 : root.value(QStringLiteral("diagnostics")).toArray();

    // Convert each 0-based LSP range entry into a 1-based line LspDiagnostic record.
    for (const QJsonValue val : diags) {
        if (!val.isObject()) continue;
        const QJsonObject dObj = val.toObject();
        const QJsonObject rangeObj = dObj.value(QStringLiteral("range")).toObject();
        const QJsonObject startObj = rangeObj.value(QStringLiteral("start")).toObject();
        const QJsonObject endObj = rangeObj.value(QStringLiteral("end")).toObject();

        LspDiagnostic diag;
        diag.line = startObj.value(QStringLiteral("line")).toInt(0) + 1;
        diag.column = startObj.value(QStringLiteral("character")).toInt(0);
        diag.endColumn = endObj.value(QStringLiteral("character")).toInt(diag.column + 1);
        diag.severity = dObj.value(QStringLiteral("severity")).toInt(1);
        diag.message = dObj.value(QStringLiteral("message")).toString().trimmed();
        result.append(diag);
    }
    return result;
}

/**
 * @brief Analyzes C/C++ source code for compiler errors and warnings via clangd `publishDiagnostics`.
 * @param sourceCode Full C/C++ translation unit text.
 * @param timeoutMs Maximum wait time in milliseconds for asynchronous clangd diagnostics.
 * @return Vector of compiler diagnostics with 1-based line numbers and column spans.
 */
QVector<LspDiagnostic> ClangdLspClient::analyzeSourceDiagnostics(const QString& sourceCode, int timeoutMs) {
    if (sourceCode.trimmed().isEmpty()) {
        return {};
    }
    // Sync inline buffer with clangd and pump stdout until `publishDiagnostics` arrives for it.
    if (timeoutMs > 0) {
        const QString tempPath = materializeInlineBufferFile(QStringLiteral("gridlock_inline_diag.cpp"), sourceCode);
        m_latestDiagnostics.remove(tempPath);
        syncDocument(tempPath, sourceCode);
        if (m_process && m_process->state() == QProcess::Running) {
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < timeoutMs && !m_latestDiagnostics.contains(tempPath)) {
                m_process->waitForReadyRead(std::min(50, std::max(10, timeoutMs - static_cast<int>(timer.elapsed()))));
                drainIncomingMessages();
            }
            if (m_latestDiagnostics.contains(tempPath)) {
                return m_latestDiagnostics.value(tempPath);
            }
        }
    }

    // Fallback scalar-string mismatch check only when clangd is unavailable or timed out.
    QVector<LspDiagnostic> fallback;
    const QStringList lines = sourceCode.split(QLatin1Char('\n'));
    static const QRegularExpression scalarStringInitRe(
        QStringLiteral("^\\s*(?:const\\s+)?(int|long|short|unsigned|double|float|bool|size_t|uint64_t|int64_t)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*\"([^\"]*)\""));
    for (int i = 0; i < lines.size(); ++i) {
        const auto match = scalarStringInitRe.match(lines.at(i));
        if (match.hasMatch()) {
            fallback.append({i + 1, std::max(0, static_cast<int>(lines.at(i).indexOf(match.captured(1)))),
                             static_cast<int>(lines.at(i).length()), 1,
                             QStringLiteral("Cannot initialize a variable of type '%1' with a string literal").arg(match.captured(1))});
        }
    }
    return fallback;
}

/**
 * @brief Queries rich symbol/type/Doxygen hover markdown at a source position via clangd `textDocument/hover`.
 * @param sourceCode Full C/C++ translation unit text.
 * @param oneBasedLine 1-based cursor line number.
 * @param zeroBasedColumn 0-based cursor character column.
 * @param timeoutMs Maximum wait time in milliseconds for clangd response.
 * @return Markdown hover summary string containing resolved declaration signature and docs.
 */
QString ClangdLspClient::requestHoverInfo(const QString& sourceCode, int oneBasedLine, int zeroBasedColumn, int timeoutMs) {
    const QStringList lines = sourceCode.split(QLatin1Char('\n'));
    if (oneBasedLine < 1 || oneBasedLine > lines.size()) {
        return {};
    }
    // Synchronize inline buffer and issue `textDocument/hover` against the live clangd session.
    if (timeoutMs > 0) {
        const QString tempPath = materializeInlineBufferFile(QStringLiteral("gridlock_inline_hover.cpp"), sourceCode);
        syncDocument(tempPath, sourceCode);
        if (m_process && m_process->state() == QProcess::Running) {
            const int hoverId = m_nextRequestId++;
            sendJsonRpc(QJsonObject{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), hoverId},
                {QStringLiteral("method"), QStringLiteral("textDocument/hover")},
                {QStringLiteral("params"), QJsonObject{
                    {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), QUrl::fromLocalFile(tempPath).toString()}}},
                    {QStringLiteral("position"), QJsonObject{{QStringLiteral("line"), oneBasedLine - 1}, {QStringLiteral("character"), zeroBasedColumn}}}
                }}
            });
            // Extract MarkupContent (`contents.value`) or plain string hover payloads.
            const QJsonValue contentsVal = readResponseForId(hoverId, timeoutMs)
                                               .value(QStringLiteral("result")).toObject()
                                               .value(QStringLiteral("contents"));
            if (contentsVal.isObject() && !contentsVal.toObject().value(QStringLiteral("value")).toString().trimmed().isEmpty()) {
                return contentsVal.toObject().value(QStringLiteral("value")).toString().trimmed();
            }
            if (contentsVal.isString() && !contentsVal.toString().trimmed().isEmpty()) {
                return contentsVal.toString().trimmed();
            }
        }
    }

    // Fallback formatting when clangd is absent or cursor sits on whitespace.
    const QString& targetLine = lines.at(oneBasedLine - 1);
    const QString symbol = extractSymbolAtColumn(targetLine, zeroBasedColumn);
    return symbol.isEmpty() ? targetLine.trimmed()
                            : QStringLiteral("```cpp\n%1\n```\nSymbol: `%2`").arg(targetLine.trimmed(), symbol);
}

/**
 * @brief Resolves cross-file declaration/definition via clangd `textDocument/definition`.
 * @param filePath Canonical or absolute path of the active translation unit on disk.
 * @param sourceText Current in-memory editor text for `filePath`.
 * @param zeroBasedLine 0-based cursor line index.
 * @param zeroBasedCol 0-based cursor character column index.
 * @param timeoutMs Maximum JSON-RPC wait budget in milliseconds.
 * @return Resolved `LspLocation` with canonical target `filePath`, 1-based `line`, and 0-based `column`.
 */
LspLocation ClangdLspClient::findDefinition(const QString& filePath, const QString& sourceText,
                                            int zeroBasedLine, int zeroBasedCol, int timeoutMs) {
    const QString normPath = normalizeLocalPath(filePath);
    const QString rootDir = normPath.isEmpty() ? QDir::currentPath() : QFileInfo(normPath).absolutePath();

    // Perform genuine clangd JSON-RPC `textDocument/definition` round-trip when clangd is available.
    if (!normPath.isEmpty() && timeoutMs > 0 && ensureServerInitialized(rootDir)) {
        syncDocument(normPath, sourceText);
        const QString docUri = QUrl::fromLocalFile(normPath).toString();

        // Helper lambda issuing `textDocument/definition` at a specific 0-based character column.
        auto queryAtCol = [&](int col, int budgetMs) -> LspLocation {
            const int reqId = m_nextRequestId++;
            sendJsonRpc(QJsonObject{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), reqId},
                {QStringLiteral("method"), QStringLiteral("textDocument/definition")},
                {QStringLiteral("params"), QJsonObject{
                    {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), docUri}}},
                    {QStringLiteral("position"), QJsonObject{
                        {QStringLiteral("line"), std::max(0, zeroBasedLine)},
                        {QStringLiteral("character"), std::max(0, col)}
                    }}
                }}
            });
            const QJsonObject resp = readResponseForId(reqId, budgetMs);
            const QJsonValue resVal = resp.value(QStringLiteral("result"));
            QJsonObject locObj;
            if (resVal.isArray() && !resVal.toArray().isEmpty()) {
                locObj = resVal.toArray().first().toObject();
            } else if (resVal.isObject()) {
                locObj = resVal.toObject();
            }
            if (locObj.isEmpty()) {
                return {};
            }
            // Support both standard `Location` (`uri` + `range`) and `LocationLink` (`targetUri` + `targetSelectionRange`).
            const QString targetUri = locObj.contains(QStringLiteral("targetUri"))
                                          ? locObj.value(QStringLiteral("targetUri")).toString()
                                          : locObj.value(QStringLiteral("uri")).toString();
            const QJsonObject rangeObj = locObj.contains(QStringLiteral("targetSelectionRange"))
                                             ? locObj.value(QStringLiteral("targetSelectionRange")).toObject()
                                             : (locObj.contains(QStringLiteral("targetRange"))
                                                    ? locObj.value(QStringLiteral("targetRange")).toObject()
                                                    : locObj.value(QStringLiteral("range")).toObject());
            const QJsonObject startObj = rangeObj.value(QStringLiteral("start")).toObject();
            const QJsonObject endObj = rangeObj.value(QStringLiteral("end")).toObject();
            return LspLocation{
                normalizeLocalPath(targetUri),
                startObj.value(QStringLiteral("line")).toInt(0) + 1,
                startObj.value(QStringLiteral("character")).toInt(0),
                endObj.value(QStringLiteral("character")).toInt(0)};
        };

        // Probe exact cursor column first, then step one character left if positioned at identifier end.
        LspLocation resolved = queryAtCol(zeroBasedCol, timeoutMs);
        if (resolved.line <= 0 && zeroBasedCol > 0) {
            resolved = queryAtCol(zeroBasedCol - 1, std::max(150, timeoutMs / 2));
        }
        return resolved;
    }

    // Fallback to lexical scan strictly when `clangd` binary is missing on the host system.
    const QStringList lines = sourceText.split(QLatin1Char('\n'));
    if (zeroBasedLine < 0 || zeroBasedLine >= lines.size()) return {};
    const QString sym = extractSymbolAtColumn(lines.at(zeroBasedLine), zeroBasedCol);
    const auto matches = scanCodeIdentifierMatches(sourceText, sym);
    if (!matches.isEmpty()) {
        return LspLocation{normPath, matches.first().lineOneBased, matches.first().colZeroBased,
                           matches.first().colZeroBased + static_cast<int>(sym.size())};
    }
    return {};
}

/**
 * @brief Resolves the declaration/definition location in an inline source buffer (1-based line).
 * @param sourceCode Full C/C++ translation unit text.
 * @param oneBasedLine 1-based cursor line number.
 * @param zeroBasedCol 0-based cursor character column.
 * @param timeoutMs Maximum JSON-RPC wait budget in milliseconds.
 * @return Resolved `LspLocation` with 1-based `line` and 0-based `column`.
 */
LspLocation ClangdLspClient::findDefinition(const QString& sourceCode, int oneBasedLine, int zeroBasedCol, int timeoutMs) {
    const QString tempPath = materializeInlineBufferFile(QStringLiteral("gridlock_inline_def.cpp"), sourceCode);
    return findDefinition(tempPath, sourceCode, std::max(0, oneBasedLine - 1), zeroBasedCol, timeoutMs);
}

/**
 * @brief Finds cross-file symbol references via clangd `textDocument/references` (`includeDeclaration: true`).
 * @param filePath Canonical or absolute path of the active translation unit on disk.
 * @param sourceText Current in-memory editor text for `filePath`.
 * @param zeroBasedLine 0-based cursor line index.
 * @param zeroBasedCol 0-based cursor character column index.
 * @param timeoutMs Maximum JSON-RPC wait budget in milliseconds.
 * @return Vector of `LspLocation` occurrences across all opened/indexed workspace translation units.
 */
QVector<LspLocation> ClangdLspClient::findReferences(const QString& filePath, const QString& sourceText,
                                                     int zeroBasedLine, int zeroBasedCol, int timeoutMs) {
    const QString normPath = normalizeLocalPath(filePath);
    const QString rootDir = normPath.isEmpty() ? QDir::currentPath() : QFileInfo(normPath).absolutePath();

    // Query live `clangd` `textDocument/references` with `includeDeclaration: true` when daemon is available.
    if (!normPath.isEmpty() && timeoutMs > 0 && ensureServerInitialized(rootDir)) {
        syncDocument(normPath, sourceText);
        const QString docUri = QUrl::fromLocalFile(normPath).toString();

        // Helper lambda dispatching a single `textDocument/references` JSON-RPC request.
        auto runRefsQuery = [&](int budgetMs) -> QVector<LspLocation> {
            const int reqId = m_nextRequestId++;
            sendJsonRpc(QJsonObject{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), reqId},
                {QStringLiteral("method"), QStringLiteral("textDocument/references")},
                {QStringLiteral("params"), QJsonObject{
                    {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), docUri}}},
                    {QStringLiteral("position"), QJsonObject{
                        {QStringLiteral("line"), std::max(0, zeroBasedLine)},
                        {QStringLiteral("character"), std::max(0, zeroBasedCol)}
                    }},
                    {QStringLiteral("context"), QJsonObject{{QStringLiteral("includeDeclaration"), true}}}
                }}
            });
            const QJsonObject resp = readResponseForId(reqId, budgetMs);
            const QJsonArray arr = resp.value(QStringLiteral("result")).toArray();
            QVector<LspLocation> locs;
            locs.reserve(arr.size());
            // Decode each LSP `Location` entry into canonical local path and 1-based line coordinates.
            for (const QJsonValue v : arr) {
                const QJsonObject o = v.toObject();
                const QJsonObject r = o.value(QStringLiteral("range")).toObject();
                const QJsonObject s = r.value(QStringLiteral("start")).toObject();
                const QJsonObject e = r.value(QStringLiteral("end")).toObject();
                locs.append(LspLocation{
                    normalizeLocalPath(o.value(QStringLiteral("uri")).toString()),
                    s.value(QStringLiteral("line")).toInt(0) + 1,
                    s.value(QStringLiteral("character")).toInt(0),
                    e.value(QStringLiteral("character")).toInt(0)});
            }
            return locs;
        };

        QElapsedTimer timer;
        timer.start();
        QVector<LspLocation> refs = runRefsQuery(timeoutMs);
        // Allow clangd's dynamic AST/background index a brief settle window when multiple TUs were just synced.
        while (refs.size() < 2 && m_openDocumentVersions.size() > 1 && timer.elapsed() + 80 < timeoutMs) {
            QThread::msleep(50);
            drainIncomingMessages();
            refs = runRefsQuery(std::max(150, timeoutMs - static_cast<int>(timer.elapsed())));
        }
        return refs;
    }

    // Fallback to lexical code-only scan strictly when `clangd` binary is missing on host.
    QVector<LspLocation> fallback;
    const QStringList lines = sourceText.split(QLatin1Char('\n'));
    if (zeroBasedLine < 0 || zeroBasedLine >= lines.size()) return fallback;
    const QString sym = extractSymbolAtColumn(lines.at(zeroBasedLine), zeroBasedCol);
    for (const auto& m : scanCodeIdentifierMatches(sourceText, sym)) {
        fallback.append({normPath, m.lineOneBased, m.colZeroBased, m.colZeroBased + static_cast<int>(sym.size())});
    }
    return fallback;
}

/**
 * @brief Finds all reference locations of the symbol in an inline source buffer (1-based line).
 * @param sourceCode Full C/C++ translation unit text.
 * @param oneBasedLine 1-based cursor line number.
 * @param zeroBasedCol 0-based cursor character column.
 * @param timeoutMs Maximum JSON-RPC wait budget in milliseconds.
 * @return Vector of `LspLocation` occurrences within the inline buffer.
 */
QVector<LspLocation> ClangdLspClient::findReferences(const QString& sourceCode, int oneBasedLine, int zeroBasedCol, int timeoutMs) {
    const QString tempPath = materializeInlineBufferFile(QStringLiteral("gridlock_inline_refs.cpp"), sourceCode);
    return findReferences(tempPath, sourceCode, std::max(0, oneBasedLine - 1), zeroBasedCol, timeoutMs);
}

/**
 * @brief Executes cross-file AST rename via `textDocument/rename`, applying `WorkspaceEdit` bottom-up.
 *
 * Parses both `WorkspaceEdit.changes` (`uri -> TextEdit[]`) and `WorkspaceEdit.documentChanges`
 * (`TextDocumentEdit[]`), applying range replacements in reverse offset order so string literals,
 * comments, and unrelated overloads remain completely untouched.
 *
 * @param filePath Canonical or absolute path of the active translation unit on disk.
 * @param sourceText Current in-memory editor text for `filePath`.
 * @param zeroBasedLine 0-based cursor line index.
 * @param zeroBasedCol 0-based cursor character column index.
 * @param newName Replacement C++ identifier string.
 * @param timeoutMs Maximum JSON-RPC wait budget in milliseconds.
 * @return Map of canonical file paths to updated full document strings.
 */
QMap<QString, QString> ClangdLspClient::renameSymbol(const QString& filePath, const QString& sourceText,
                                                    int zeroBasedLine, int zeroBasedCol,
                                                    const QString& newName, int timeoutMs) {
    QMap<QString, QString> updatedFiles;
    if (newName.trimmed().isEmpty()) {
        return updatedFiles;
    }
    const QString normPath = normalizeLocalPath(filePath);
    const QString rootDir = normPath.isEmpty() ? QDir::currentPath() : QFileInfo(normPath).absolutePath();

    // Dispatch `textDocument/rename` to clangd when daemon initialization succeeds.
    if (!normPath.isEmpty() && timeoutMs > 0 && ensureServerInitialized(rootDir)) {
        syncDocument(normPath, sourceText);
        const QString docUri = QUrl::fromLocalFile(normPath).toString();

        // Helper lambda issuing `textDocument/rename` and collecting per-file `LspTextEditSpan` lists.
        auto requestWorkspaceEdit = [&](int budgetMs) -> QMap<QString, QVector<LspTextEditSpan>> {
            const int reqId = m_nextRequestId++;
            sendJsonRpc(QJsonObject{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), reqId},
                {QStringLiteral("method"), QStringLiteral("textDocument/rename")},
                {QStringLiteral("params"), QJsonObject{
                    {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), docUri}}},
                    {QStringLiteral("position"), QJsonObject{
                        {QStringLiteral("line"), std::max(0, zeroBasedLine)},
                        {QStringLiteral("character"), std::max(0, zeroBasedCol)}
                    }},
                    {QStringLiteral("newName"), newName.trimmed()}
                }}
            });
            const QJsonObject resp = readResponseForId(reqId, budgetMs);
            const QJsonObject editRoot = resp.value(QStringLiteral("result")).toObject();
            QMap<QString, QVector<LspTextEditSpan>> fileEdits;

            // Helper converting a JSON `TextEdit[]` array for `uri` into `LspTextEditSpan` items.
            auto appendJsonEdits = [&](const QString& uri, const QJsonArray& arr) {
                const QString targetPath = normalizeLocalPath(uri);
                if (targetPath.isEmpty()) return;
                auto& vec = fileEdits[targetPath];
                for (const QJsonValue ev : arr) {
                    const QJsonObject eo = ev.toObject();
                    const QJsonObject r = eo.value(QStringLiteral("range")).toObject();
                    const QJsonObject s = r.value(QStringLiteral("start")).toObject();
                    const QJsonObject e = r.value(QStringLiteral("end")).toObject();
                    vec.append({s.value(QStringLiteral("line")).toInt(0),
                                s.value(QStringLiteral("character")).toInt(0),
                                e.value(QStringLiteral("line")).toInt(0),
                                e.value(QStringLiteral("character")).toInt(0),
                                eo.value(QStringLiteral("newText")).toString()});
                }
            };

            // Parse `WorkspaceEdit.changes` map (`uri -> TextEdit[]`).
            const QJsonObject changesObj = editRoot.value(QStringLiteral("changes")).toObject();
            for (auto it = changesObj.begin(); it != changesObj.end(); ++it) {
                appendJsonEdits(it.key(), it.value().toArray());
            }
            // Parse `WorkspaceEdit.documentChanges` array (`TextDocumentEdit[]`).
            const QJsonArray docChanges = editRoot.value(QStringLiteral("documentChanges")).toArray();
            for (const QJsonValue dcv : docChanges) {
                const QJsonObject dco = dcv.toObject();
                const QString uri = dco.value(QStringLiteral("textDocument")).toObject().value(QStringLiteral("uri")).toString();
                if (!uri.isEmpty()) {
                    appendJsonEdits(uri, dco.value(QStringLiteral("edits")).toArray());
                }
            }
            return fileEdits;
        };

        QElapsedTimer timer;
        timer.start();
        auto fileEdits = requestWorkspaceEdit(timeoutMs);
        // If cross-file index is still warming right after `didOpen`, retry briefly within budget.
        const bool hasLocalInclude = sourceText.contains(QStringLiteral("#include \""));
        while ((fileEdits.isEmpty() || (hasLocalInclude && fileEdits.size() < 2)) &&
               timer.elapsed() + 80 < timeoutMs) {
            QThread::msleep(50);
            drainIncomingMessages();
            const auto retryEdits = requestWorkspaceEdit(std::max(150, timeoutMs - static_cast<int>(timer.elapsed())));
            if (retryEdits.size() >= fileEdits.size() && !retryEdits.isEmpty()) {
                fileEdits = retryEdits;
            }
            if (!hasLocalInclude && !fileEdits.isEmpty()) break;
        }

        // Apply bottom-up range edits to each affected workspace buffer or on-disk file.
        for (auto it = fileEdits.constBegin(); it != fileEdits.constEnd(); ++it) {
            QString baseText;
            if (it.key() == normPath && !sourceText.isEmpty()) {
                baseText = sourceText;
            } else if (m_openDocumentTexts.contains(it.key())) {
                baseText = m_openDocumentTexts.value(it.key());
            } else {
                QFile f(it.key());
                if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    baseText = QString::fromUtf8(f.readAll()).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
                }
            }
            if (!baseText.isEmpty()) {
                updatedFiles.insert(it.key(), applyTextEditsBottomUp(baseText, it.value()));
            }
        }
        return updatedFiles;
    }

    // Fallback to lexer-aware code-only token replacement strictly when `clangd` is absent on host.
    const QStringList lines = sourceText.split(QLatin1Char('\n'));
    if (zeroBasedLine >= 0 && zeroBasedLine < lines.size()) {
        const QString sym = extractSymbolAtColumn(lines.at(zeroBasedLine), zeroBasedCol);
        const auto matches = scanCodeIdentifierMatches(sourceText, sym);
        QString out = sourceText;
        for (int i = static_cast<int>(matches.size()) - 1; i >= 0; --i) {
            out.replace(matches.at(i).offset, static_cast<int>(sym.size()), newName.trimmed());
        }
        updatedFiles.insert(normPath, out);
    }
    return updatedFiles;
}

/**
 * @brief Performs clangd AST `WorkspaceEdit` symbol renaming on a single source buffer (1-based line).
 * @param sourceCode Full C/C++ translation unit text.
 * @param oneBasedLine 1-based cursor line number.
 * @param zeroBasedCol 0-based cursor character column.
 * @param newName Replacement C++ identifier string.
 * @param timeoutMs Maximum JSON-RPC wait budget in milliseconds.
 * @return Updated document text with AST-resolved identifier occurrences renamed.
 */
QString ClangdLspClient::renameSymbolInSource(const QString& sourceCode, int oneBasedLine, int zeroBasedCol,
                                              const QString& newName, int timeoutMs) {
    if (sourceCode.isEmpty() || newName.trimmed().isEmpty()) {
        return sourceCode;
    }
    const QString tempPath = materializeInlineBufferFile(QStringLiteral("gridlock_inline_rename.cpp"), sourceCode);
    const auto edits = renameSymbol(tempPath, sourceCode, std::max(0, oneBasedLine - 1), zeroBasedCol, newName, timeoutMs);
    return edits.value(tempPath, sourceCode);
}

}  // namespace gridlock::core

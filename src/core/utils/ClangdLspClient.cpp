/**
 * @file ClangdLspClient.cpp
 * @brief Implementation of the ClangdLspClient JSON-RPC 2.0 completion client and AST scope analyzer.
 *
 * Manages a persistent `clangd` child process speaking standard LSP Content-Length JSON-RPC 2.0
 * frames over stdio, splices unsaved breakpoint predicate expressions into virtual C++ documents,
 * and merges structural C++ struct/STL scope completions for deterministic sub-millisecond fallback.
 */

#include "ClangdLspClient.hpp"

#include <algorithm>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace gridlock::core {

namespace {

/**
 * @brief Strips array subscripts (`[i]`) and call parentheses (`()`) from a C++ receiver segment.
 * @param segment Raw receiver segment token (e.g., "data[i]" or "vec.at(0)").
 * @return Clean base identifier string (e.g., "data").
 */
QString stripSubscriptsAndCalls(const QString& segment) {
    QString clean = segment.trimmed();
    const int bracketIdx = clean.indexOf(QLatin1Char('['));
    if (bracketIdx >= 0) {
        clean = clean.left(bracketIdx).trimmed();
    }
    const int parenIdx = clean.indexOf(QLatin1Char('('));
    if (parenIdx >= 0) {
        clean = clean.left(parenIdx).trimmed();
    }
    return clean;
}

/**
 * @brief Extracts declared identifier names from a single C++ field or variable declaration statement.
 * @param declStmt Statement body without trailing semicolon (e.g., "float x, y, z" or "double values[5]").
 * @param outType Optional output parameter receiving the base type token.
 * @return Ordered list of declared identifier names.
 */
QStringList parseDeclaredNames(const QString& declStmt, QString* outType = nullptr) {
    QString trimmed = declStmt.trimmed();
    // Ignore control-flow statements, preprocessor directives, and function calls.
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')) ||
        trimmed.startsWith(QStringLiteral("//")) || trimmed.startsWith(QStringLiteral("return ")) ||
        trimmed.startsWith(QStringLiteral("MPI_")) || trimmed.startsWith(QStringLiteral("printf"))) {
        return {};
    }

    // Remove leading qualifiers such as const, static, constexpr, unsigned, or struct.
    static const QRegularExpression kQualifiersRe(
        QStringLiteral("^(?:(?:const|constexpr|static|volatile|unsigned|signed|struct|class)\\s+)+"));
    trimmed.replace(kQualifiersRe, QString());

    // Match base C++ type (including template arguments like std::vector<int>) followed by declarators.
    static const QRegularExpression kTypeAndRestRe(
        QStringLiteral("^([A-Za-z_][A-Za-z0-9_:]*(?:\\s*<[^>]+>)?(?:\\s*[\\*&])?)\\s+(.+)$"));
    const QRegularExpressionMatch match = kTypeAndRestRe.match(trimmed);
    if (!match.hasMatch()) {
        return {};
    }

    if (outType) {
        *outType = match.captured(1).trimmed();
    }
    const QString declarators = match.captured(2).trimmed();

    // Split comma-separated declarators while respecting parentheses in constructor initializers.
    QStringList names;
    int parenDepth = 0;
    int startIdx = 0;
    for (int i = 0; i <= declarators.size(); ++i) {
        const QChar ch = (i < declarators.size()) ? declarators.at(i) : QLatin1Char(',');
        if (ch == QLatin1Char('(') || ch == QLatin1Char('{') || ch == QLatin1Char('<')) {
            ++parenDepth;
        } else if (ch == QLatin1Char(')') || ch == QLatin1Char('}') || ch == QLatin1Char('>')) {
            parenDepth = std::max(0, parenDepth - 1);
        } else if (ch == QLatin1Char(',') && parenDepth == 0) {
            QString part = declarators.mid(startIdx, i - startIdx).trimmed();
            startIdx = i + 1;
            const int eqIdx = part.indexOf(QLatin1Char('='));
            if (eqIdx >= 0) part = part.left(eqIdx).trimmed();
            part = stripSubscriptsAndCalls(part);
            while (part.startsWith(QLatin1Char('*')) || part.startsWith(QLatin1Char('&'))) {
                part.remove(0, 1);
            }
            part = part.trimmed();
            static const QRegularExpression kIdentRe(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
            if (kIdentRe.match(part).hasMatch()) {
                names.append(part);
            }
        }
    }
    return names;
}

}  // namespace

/**
 * @brief Returns the shared process-wide ClangdLspClient singleton.
 * @return Singleton reference.
 */
ClangdLspClient& ClangdLspClient::instance() {
    static ClangdLspClient s_instance;
    return s_instance;
}

/**
 * @brief Constructs an uninitialized ClangdLspClient instance.
 */
ClangdLspClient::ClangdLspClient() = default;

/**
 * @brief Shuts down any running clangd background process when destroyed.
 *
 * Closes the standard input write channel, requests graceful termination, and
 * falls back to an immediate kill + waitForFinished so Qt never warns about a
 * still-running QProcess during static singleton destruction.
 */
ClangdLspClient::~ClangdLspClient() {
    if (m_process) {
        if (m_process->state() != QProcess::NotRunning) {
            m_process->closeWriteChannel();
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        delete m_process;
        m_process = nullptr;
    }
}

/**
 * @brief Splices `auto __gl_cond = (<partialExpression>);` into sourceCode at targetLineOneBased.
 *
 * When a user types a conditional breakpoint expression inside the popover, the expression
 * does not yet exist in the on-disk `.cpp` source file. By injecting a synthetic `auto`
 * variable initialization immediately before `targetLineOneBased`, `clangd` evaluates the
 * expression with full visibility of enclosing function parameters, local variables, loop
 * induction variables (`i`, `j`), and struct definitions in scope at that exact line.
 *
 * @param sourceCode Full C/C++ source text.
 * @param targetLineOneBased 1-based line number of the breakpoint.
 * @param partialExpression Partial condition string typed by the user.
 * @param outZeroBasedLine Output 0-based line number of the spliced statement.
 * @param outZeroBasedCharacter Output 0-based column index immediately after partialExpression.
 * @return Virtual C++ document text with the spliced expression statement.
 */
QString ClangdLspClient::buildVirtualCompletionDocument(const QString& sourceCode,
                                                        int targetLineOneBased,
                                                        const QString& partialExpression,
                                                        int* outZeroBasedLine,
                                                        int* outZeroBasedCharacter) {
    QStringList lines = sourceCode.split(QLatin1Char('\n'));
    const int insertIdx = std::clamp(targetLineOneBased - 1, 0, static_cast<int>(lines.size()));

    // Construct synthetic local variable initializer placing cursor right after partialExpression.
    const QString statementPrefix = QStringLiteral("    auto __gl_cond = (");
    const QString injectedLine = statementPrefix + partialExpression + QStringLiteral(");");
    lines.insert(insertIdx, injectedLine);

    // Populate 0-based LSP Position coordinates pointing directly at the end of partialExpression.
    if (outZeroBasedLine) {
        *outZeroBasedLine = insertIdx;
    }
    if (outZeroBasedCharacter) {
        *outZeroBasedCharacter = static_cast<int>(statementPrefix.length() + partialExpression.length());
    }
    return lines.join(QLatin1Char('\n'));
}

/**
 * @brief Encodes a QJsonObject into an LSP `Content-Length: <bytes>\r\n\r\n<json>` wire frame.
 *
 * Language Server Protocol over stdio requires every JSON-RPC 2.0 request and notification
 * to be prefixed by an HTTP-like `Content-Length` header terminated by `\r\n\r\n`.
 *
 * @param payload JSON-RPC 2.0 message object.
 * @return Framed byte array ready for stdio transmission.
 */
QByteArray ClangdLspClient::encodeLspMessage(const QJsonObject& payload) {
    const QByteArray jsonBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray header = "Content-Length: " + QByteArray::number(jsonBytes.size()) + "\r\n\r\n";
    return header + jsonBytes;
}

/**
 * @brief Ensures clangd is started and completes the LSP `initialize` / `initialized` handshake.
 *
 * Searches standard system paths for `clangd`, spawns it with background indexing disabled
 * and header auto-insertion turned off so completion labels never inject unexpected `#include`
 * directives, and configures C++20 + OpenMPI/MPICH fallback compile flags.
 *
 * @param rootDir Workspace directory path.
 * @return True if clangd is running and initialized.
 */
bool ClangdLspClient::ensureServerInitialized(const QString& rootDir) {
    if (m_process && m_process->state() == QProcess::Running && m_initialized) {
        return true;
    }

    // Locate clangd executable on PATH or standard system toolchain locations.
    QString clangdBin = QStandardPaths::findExecutable(QStringLiteral("clangd"));
    if (clangdBin.isEmpty()) {
        clangdBin = QStandardPaths::findExecutable(
            QStringLiteral("clangd"),
            {QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/bin"), QStringLiteral("/llvm/bin")});
    }
    if (clangdBin.isEmpty()) {
        return false;
    }

    if (!m_process) {
        m_process = new QProcess();
        m_process->setProcessChannelMode(QProcess::SeparateChannels);
        if (QCoreApplication::instance()) {
            QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, [this]() {
                if (m_process && m_process->state() != QProcess::NotRunning) {
                    m_process->closeWriteChannel();
                    m_process->kill();
                    m_process->waitForFinished(500);
                }
            });
        }
    }
    if (m_process->state() == QProcess::NotRunning) {
        m_initialized = false;
        m_readBuffer.clear();
        m_activeDocumentUri.clear();
        m_process->start(clangdBin, {
            QStringLiteral("--log=error"),
            QStringLiteral("--background-index=false"),
            QStringLiteral("--header-insertion=never")
        });
        if (!m_process->waitForStarted(600)) {
            return false;
        }
    }

    // Build initialize request with fallback flags for C++20 and OpenMPI/MPICH include paths.
    const int initId = m_nextRequestId++;
    const QString cleanRoot = rootDir.isEmpty() ? QDir::currentPath() : QFileInfo(rootDir).absoluteFilePath();
    QJsonObject initOptions;
    initOptions[QStringLiteral("fallbackFlags")] = QJsonArray{
        QStringLiteral("-std=c++20"),
        QStringLiteral("-I/usr/include/openmpi-x86_64"),
        QStringLiteral("-I/usr/lib/x86_64-linux-gnu/openmpi/include"),
        QStringLiteral("-I/usr/include/mpich")
    };

    QJsonObject params;
    params[QStringLiteral("processId")] = static_cast<qint64>(QCoreApplication::applicationPid());
    params[QStringLiteral("rootUri")] = QUrl::fromLocalFile(cleanRoot).toString();
    params[QStringLiteral("initializationOptions")] = initOptions;
    params[QStringLiteral("capabilities")] = QJsonObject{
        {QStringLiteral("textDocument"), QJsonObject{
            {QStringLiteral("completion"), QJsonObject{
                {QStringLiteral("completionItem"), QJsonObject{{QStringLiteral("snippetSupport"), false}}}
            }}
        }}
    };

    sendJsonRpc(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), initId},
        {QStringLiteral("method"), QStringLiteral("initialize")},
        {QStringLiteral("params"), params}
    });

    const QJsonObject resp = readResponseForId(initId, 800);
    if (resp.isEmpty()) {
        return false;
    }

    // Dispatch `initialized` notification completing the LSP handshake.
    sendJsonRpc(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("method"), QStringLiteral("initialized")},
        {QStringLiteral("params"), QJsonObject{}}
    });
    m_initialized = true;
    return true;
}

/**
 * @brief Writes a framed JSON-RPC message to the clangd child process stdin.
 * @param payload JSON-RPC object.
 */
void ClangdLspClient::sendJsonRpc(const QJsonObject& payload) {
    if (!m_process || m_process->state() != QProcess::Running) return;
    const QByteArray frame = encodeLspMessage(payload);
    m_process->write(frame);
    m_process->waitForBytesWritten(150);
}

/**
 * @brief Reads Content-Length framed JSON-RPC messages from clangd until requestId arrives.
 * @param requestId Target request ID to match.
 * @param timeoutMs Maximum wait time in milliseconds.
 * @return Matching JSON-RPC response object, or empty QJsonObject on timeout.
 */
QJsonObject ClangdLspClient::readResponseForId(int requestId, int timeoutMs) {
    if (!m_process || m_process->state() != QProcess::Running) return {};

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        // Parse any complete Content-Length frames already buffered in m_readBuffer.
        while (true) {
            const int headerEnd = m_readBuffer.indexOf("\r\n\r\n");
            if (headerEnd < 0) break;

            const QByteArray headerBlock = m_readBuffer.left(headerEnd);
            int contentLen = -1;
            for (const QByteArray& line : headerBlock.split('\n')) {
                const QByteArray trimmed = line.trimmed();
                if (trimmed.toLower().startsWith("content-length:")) {
                    contentLen = trimmed.mid(15).trimmed().toInt();
                    break;
                }
            }
            if (contentLen < 0) {
                m_readBuffer.remove(0, headerEnd + 4);
                continue;
            }
            const int totalFrameBytes = headerEnd + 4 + contentLen;
            if (m_readBuffer.size() < totalFrameBytes) {
                break;
            }

            const QByteArray bodyBytes = m_readBuffer.mid(headerEnd + 4, contentLen);
            m_readBuffer.remove(0, totalFrameBytes);

            const QJsonDocument doc = QJsonDocument::fromJson(bodyBytes);
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                if (obj.contains(QStringLiteral("id")) && obj.value(QStringLiteral("id")).toInt(-1) == requestId) {
                    return obj;
                }
            }
        }

        const int remaining = std::max(10, timeoutMs - static_cast<int>(timer.elapsed()));
        if (m_process->waitForReadyRead(std::min(remaining, 80))) {
            m_readBuffer.append(m_process->readAllStandardOutput());
        }
    }
    return {};
}

/**
 * @brief Performs structural C++ struct/nested-struct/STL scope completion analysis on sourceCode.
 * @param sourceCode Full C/C++ translation unit text.
 * @param targetLineOneBased 1-based line number where completion is requested.
 * @param partialExpression Partial C/C++ expression being typed.
 * @return Ordered list of matching member or variable completions.
 */
/**
 * @brief Resolves detailed completions with kind and type metadata for expression at target line.
 * @param filePath Path to the active source file.
 * @param sourceCode Full text of the source file.
 * @param targetLineOneBased 1-based line number.
 * @param partialExpression Partial C/C++ condition or editor expression.
 * @param timeoutMs Timeout in milliseconds for LSP server response.
 * @return Ordered list of structured LspCompletionItem objects.
 */
QVector<ClangdLspClient::LspCompletionItem> ClangdLspClient::completeDetailedAtLine(
    const QString& filePath,
    const QString& sourceCode,
    int targetLineOneBased,
    const QString& partialExpression,
    int timeoutMs) {
    // Maps struct type identifiers to their declared member field names.
    QMap<QString, QStringList> structMembers;
    // Maps composite field access paths (e.g., Struct.field) to their resolved types.
    QMap<QString, QString> memberNestedTypes;
    // Tracks in-scope local variable types declared prior to target line.
    QMap<QString, QString> varTypes;
    // Stores structured LspCompletionItem definitions with kind badges and signatures.
    QMap<QString, QVector<LspCompletionItem>> structDetailedMembers;

    // Split the translation unit into individual lines for lexical scanning.
    const QStringList lines = sourceCode.split(QLatin1Char('\n'));
    QString currentOuterStruct;
    QString currentAnonStructFieldsPending;
    bool inAnonNestedStruct = false;

    for (int idx = 0; idx < lines.size(); ++idx) {
        QString line = lines.at(idx).trimmed();
        // Strip line comments before parsing C++ declarations.
        const int commentIdx = line.indexOf(QStringLiteral("//"));
        if (commentIdx >= 0) line = line.left(commentIdx).trimmed();
        // Skip blank lines, header banners, and block comments.
        if (line.isEmpty() || line.startsWith(QLatin1Char('*')) || line.startsWith(QStringLiteral("/*"))) {
            continue;
        }

        // Match outer struct declaration headers (e.g., 'struct WorkerData {').
        static const QRegularExpression kStructStartRe(QStringLiteral("^struct\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*\\{"));
        const QRegularExpressionMatch structMatch = kStructStartRe.match(line);
        if (structMatch.hasMatch() && currentOuterStruct.isEmpty()) {
            currentOuterStruct = structMatch.captured(1);
            continue;
        }

        if (!currentOuterStruct.isEmpty()) {
            // Detect anonymous nested struct definition blocks inside outer structs.
            if (line.startsWith(QStringLiteral("struct")) && line.contains(QLatin1Char('{'))) {
                inAnonNestedStruct = true;
                currentAnonStructFieldsPending.clear();
                continue;
            }
            if (inAnonNestedStruct) {
                // Parse anonymous nested struct closing brace and field identifier.
                static const QRegularExpression kAnonEndRe(QStringLiteral("^\\}\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*;"));
                const QRegularExpressionMatch anonEnd = kAnonEndRe.match(line);
                if (anonEnd.hasMatch()) {
                    const QString nestedFieldName = anonEnd.captured(1);
                    // Synthesize qualified pseudo-type name for nested anonymous member fields.
                    const QString syntheticType = currentOuterStruct + QStringLiteral("::") + nestedFieldName;
                    structMembers[currentOuterStruct].append(nestedFieldName);
                    memberNestedTypes[currentOuterStruct + QLatin1Char('.') + nestedFieldName] = syntheticType;

                    // Record nested struct field candidate item for IntelliSense popup.
                    LspCompletionItem anonItem;
                    anonItem.label = nestedFieldName;
                    anonItem.insertText = nestedFieldName;
                    anonItem.kindLabel = QStringLiteral("Field");
                    anonItem.detailType = QStringLiteral("struct ") + nestedFieldName;
                    structDetailedMembers[currentOuterStruct].append(anonItem);

                    // Parse declared inner member field names and associate float/int types.
                    for (const QString& stmt : currentAnonStructFieldsPending.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                        QString innerType;
                        for (const QString& f : parseDeclaredNames(stmt, &innerType)) {
                            if (!structMembers[syntheticType].contains(f)) {
                                structMembers[syntheticType].append(f);
                                LspCompletionItem innerItem;
                                innerItem.label = f;
                                innerItem.insertText = f;
                                innerItem.kindLabel = QStringLiteral("Field");
                                innerItem.detailType = innerType.isEmpty() ? QStringLiteral("float") : innerType;
                                structDetailedMembers[syntheticType].append(innerItem);
                            }
                        }
                    }
                    inAnonNestedStruct = false;
                } else {
                    currentAnonStructFieldsPending += QLatin1Char(' ') + line;
                }
                continue;
            }
            // Handle outer struct closing delimiter.
            if (line.startsWith(QStringLiteral("};"))) {
                currentOuterStruct.clear();
                continue;
            }
            // Parse semicolon-separated member field declarations.
            for (const QString& stmt : line.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                QString fieldType;
                for (const QString& f : parseDeclaredNames(stmt, &fieldType)) {
                    if (!structMembers[currentOuterStruct].contains(f)) {
                        structMembers[currentOuterStruct].append(f);
                    }
                    QString effectiveType = fieldType;
                    // Check for fixed-size C-style array bounds (e.g., double values[5]).
                    if (stmt.contains(f + QStringLiteral("["))) {
                        int lb = stmt.indexOf(f + QStringLiteral("["));
                        int rb = stmt.indexOf(QLatin1Char(']'), lb);
                        if (rb > lb) {
                            effectiveType = fieldType + stmt.mid(lb + f.length(), rb - (lb + f.length()) + 1);
                        }
                    }
                    if (effectiveType.isEmpty()) effectiveType = QStringLiteral("int");
                    memberNestedTypes[currentOuterStruct + QLatin1Char('.') + f] = effectiveType;

                    // Record structured completion item for the member field.
                    LspCompletionItem item;
                    item.label = f;
                    item.insertText = f;
                    item.kindLabel = QStringLiteral("Field");
                    item.detailType = effectiveType;
                    structDetailedMembers[currentOuterStruct].append(item);
                }
            }
            continue;
        }

        if (idx < targetLineOneBased) {
            // Extract loop control variable declarations (e.g., for (int i = 0; ...)).
            static const QRegularExpression kForLoopRe(QStringLiteral("for\\s*\\(([^;]+);"));
            const QRegularExpressionMatch forMatch = kForLoopRe.match(line);
            if (forMatch.hasMatch()) {
                QString loopType;
                for (const QString& v : parseDeclaredNames(forMatch.captured(1), &loopType)) {
                    varTypes[v] = loopType.isEmpty() ? QStringLiteral("int") : loopType;
                }
            }
            // Extract local variable declarations preceding the target line.
            for (const QString& stmt : line.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                QString declType;
                for (const QString& v : parseDeclaredNames(stmt, &declType)) {
                    varTypes[v] = declType.isEmpty() ? QStringLiteral("int") : declType;
                }
            }
        }
    }

    const QString trimmedExpr = partialExpression.trimmed();
    // Determine separator position (. or ->) in partial completion expression.
    const int lastDot = trimmedExpr.lastIndexOf(QLatin1Char('.'));
    const int lastArrow = trimmedExpr.lastIndexOf(QStringLiteral("->"));
    const int sepPos = std::max(lastDot, (lastArrow >= 0) ? (lastArrow + 1) : -1);

    QVector<LspCompletionItem> candidates;
    QString memberPrefix;

    if (sepPos >= 0) {
        // Extract receiver expression chain preceding the member access separator.
        const int receiverEnd = (lastArrow > lastDot) ? lastArrow : lastDot;
        QString receiverChain = trimmedExpr.left(receiverEnd).trimmed();
        // Extract trailing identifier prefix after the member access operator.
        memberPrefix = trimmedExpr.mid(sepPos + 1).trimmed();

        static const QRegularExpression kTrailingChainRe(QStringLiteral("([A-Za-z_][A-Za-z0-9_\\[\\]\\.\\->]*)$"));
        const QRegularExpressionMatch chainMatch = kTrailingChainRe.match(receiverChain);
        if (chainMatch.hasMatch()) {
            receiverChain = chainMatch.captured(1);
        }

        QString normChain = receiverChain;
        normChain.replace(QStringLiteral("->"), QStringLiteral("."));
        const QStringList segments = normChain.split(QLatin1Char('.'), Qt::SkipEmptyParts);
        if (!segments.isEmpty()) {
            const QString rootVar = stripSubscriptsAndCalls(segments.first());
            QString activeType = varTypes.value(rootVar);
            for (int s = 1; s < segments.size() && !activeType.isEmpty(); ++s) {
                const QString fieldName = stripSubscriptsAndCalls(segments.at(s));
                activeType = memberNestedTypes.value(activeType + QLatin1Char('.') + fieldName);
            }

            if (structDetailedMembers.contains(activeType)) {
                candidates = structDetailedMembers.value(activeType);
            } else if (activeType.contains(QStringLiteral("vector"))) {
                candidates = {
                    {QStringLiteral("data()"), QStringLiteral("data()"), QStringLiteral("Method"), QStringLiteral("int*")},
                    {QStringLiteral("size()"), QStringLiteral("size()"), QStringLiteral("Method"), QStringLiteral("size_t")},
                    {QStringLiteral("empty()"), QStringLiteral("empty()"), QStringLiteral("Method"), QStringLiteral("bool")},
                    {QStringLiteral("front()"), QStringLiteral("front()"), QStringLiteral("Method"), QStringLiteral("int&")},
                    {QStringLiteral("back()"), QStringLiteral("back()"), QStringLiteral("Method"), QStringLiteral("int&")},
                    {QStringLiteral("at()"), QStringLiteral("at()"), QStringLiteral("Method"), QStringLiteral("int&")},
                    {QStringLiteral("capacity()"), QStringLiteral("capacity()"), QStringLiteral("Method"), QStringLiteral("size_t")},
                    {QStringLiteral("clear()"), QStringLiteral("clear()"), QStringLiteral("Method"), QStringLiteral("void")}
                };
            } else if (activeType.contains(QStringLiteral("string"))) {
                candidates = {
                    {QStringLiteral("c_str()"), QStringLiteral("c_str()"), QStringLiteral("Method"), QStringLiteral("const char*")},
                    {QStringLiteral("data()"), QStringLiteral("data()"), QStringLiteral("Method"), QStringLiteral("const char*")},
                    {QStringLiteral("size()"), QStringLiteral("size()"), QStringLiteral("Method"), QStringLiteral("size_t")},
                    {QStringLiteral("length()"), QStringLiteral("length()"), QStringLiteral("Method"), QStringLiteral("size_t")},
                    {QStringLiteral("empty()"), QStringLiteral("empty()"), QStringLiteral("Method"), QStringLiteral("bool")},
                    {QStringLiteral("substr()"), QStringLiteral("substr()"), QStringLiteral("Method"), QStringLiteral("std::string")}
                };
            }
        }
    } else {
        memberPrefix = trimmedExpr;
        QStringList locals = varTypes.keys();
        locals.sort();
        for (const QString& v : locals) {
            candidates.append({v, v, QStringLiteral("Variable"), varTypes.value(v, QStringLiteral("int"))});
        }
    }

    QVector<LspCompletionItem> filtered;
    for (const auto& c : candidates) {
        if (memberPrefix.isEmpty() || c.label.startsWith(memberPrefix, Qt::CaseInsensitive)) {
            filtered.append(c);
        }
    }

    if (timeoutMs > 0) {
        int zeroLine = 0;
        int zeroCol = 0;
        const QString virtualDoc = buildVirtualCompletionDocument(
            sourceCode, targetLineOneBased, partialExpression, &zeroLine, &zeroCol);
        auto& cli = ClangdLspClient::instance();
        const QFileInfo fi(filePath.isEmpty() ? QStringLiteral("virtual_breakpoint.cpp") : filePath);
        const QString rootDir = fi.exists() ? fi.absolutePath() : QDir::currentPath();
        if (cli.ensureServerInitialized(rootDir)) {
            const QString docUri = QUrl::fromLocalFile(
                fi.exists() ? fi.absoluteFilePath() : (rootDir + QStringLiteral("/virtual_breakpoint.cpp"))).toString();
            if (!cli.m_activeDocumentUri.isEmpty()) {
                cli.sendJsonRpc(QJsonObject{
                    {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                    {QStringLiteral("method"), QStringLiteral("textDocument/didClose")},
                    {QStringLiteral("params"), QJsonObject{
                        {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), cli.m_activeDocumentUri}}}
                    }}
                });
            }
            cli.m_activeDocumentUri = docUri;
            cli.sendJsonRpc(QJsonObject{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("method"), QStringLiteral("textDocument/didOpen")},
                {QStringLiteral("params"), QJsonObject{
                    {QStringLiteral("textDocument"), QJsonObject{
                        {QStringLiteral("uri"), docUri},
                        {QStringLiteral("languageId"), QStringLiteral("cpp")},
                        {QStringLiteral("version"), cli.m_documentVersion++},
                        {QStringLiteral("text"), virtualDoc}
                    }}
                }}
            });
            const int compId = cli.m_nextRequestId++;
            cli.sendJsonRpc(QJsonObject{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("id"), compId},
                {QStringLiteral("method"), QStringLiteral("textDocument/completion")},
                {QStringLiteral("params"), QJsonObject{
                    {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), docUri}}},
                    {QStringLiteral("position"), QJsonObject{
                        {QStringLiteral("line"), zeroLine},
                        {QStringLiteral("character"), zeroCol}
                    }}
                }}
            });
            const QJsonObject compResp = cli.readResponseForId(compId, timeoutMs);
            const QJsonValue resultVal = compResp.value(QStringLiteral("result"));
            const QJsonArray items = resultVal.isObject()
                                         ? resultVal.toObject().value(QStringLiteral("items")).toArray()
                                         : resultVal.toArray();
            for (const QJsonValue& val : items) {
                const QJsonObject item = val.toObject();
                const int kind = item.value(QStringLiteral("kind")).toInt(0);
                if (kind != 2 && kind != 5 && kind != 6 && kind != 10) continue;
                QString text = item.value(QStringLiteral("insertText")).toString().trimmed();
                if (text.isEmpty()) text = item.value(QStringLiteral("label")).toString().trimmed();
                while (text.startsWith(QLatin1Char('.')) || text.startsWith(QLatin1Char(' '))) text.remove(0, 1);
                const int parenPos = text.indexOf(QLatin1Char('('));
                if (parenPos > 0) text = text.left(parenPos) + QStringLiteral("()");
                if (text.isEmpty() || text.startsWith(QStringLiteral("__"))) continue;
                bool exists = false;
                for (const auto& f : filtered) {
                    if (f.label == text) { exists = true; break; }
                }
                if (!exists) {
                    QString kLabel = (kind == 2) ? QStringLiteral("Method")
                                   : (kind == 5) ? QStringLiteral("Field")
                                   : (kind == 6) ? QStringLiteral("Variable")
                                   : QStringLiteral("Field");
                    QString dType = item.value(QStringLiteral("detail")).toString().trimmed();
                    if (dType.isEmpty()) dType = (kLabel == QStringLiteral("Method")) ? QStringLiteral("void") : QStringLiteral("int");
                    filtered.append({text, text, kLabel, dType});
                }
            }
        }
    }
    return filtered;
}

/**
 * @brief Performs structural C++ struct/nested-struct/STL scope completion analysis on sourceCode.
 * @param sourceCode Full C/C++ translation unit text.
 * @param targetLineOneBased 1-based line number where completion is requested.
 * @param partialExpression Partial C/C++ expression being typed.
 * @return Ordered list of matching member or variable completions.
 */
QStringList ClangdLspClient::analyzeInScopeCompletions(const QString& sourceCode,
                                                       int targetLineOneBased,
                                                       const QString& partialExpression) {
    const auto detailed = completeDetailedAtLine(QString(), sourceCode, targetLineOneBased, partialExpression, 0);
    QStringList labels;
    labels.reserve(detailed.size());
    for (const auto& item : detailed) {
        labels.append(item.label);
    }
    return labels;
}

/**
 * @brief Queries clangd LSP with the virtual spliced document and merges structural scope completions.
 * @param filePath Source file path for LSP document URI construction.
 * @param sourceCode Full C++ source text.
 * @param targetLineOneBased 1-based line number of the breakpoint.
 * @param partialExpression User-typed partial expression.
 * @param timeoutMs Maximum wait time in milliseconds for clangd response.
 * @return Ordered list of completion strings.
 */
QStringList ClangdLspClient::completeExpressionAtLine(const QString& filePath,
                                                      const QString& sourceCode,
                                                      int targetLineOneBased,
                                                      const QString& partialExpression,
                                                      int timeoutMs) {
    const auto detailed = completeDetailedAtLine(filePath, sourceCode, targetLineOneBased, partialExpression, timeoutMs);
    QStringList labels;
    labels.reserve(detailed.size());
    for (const auto& item : detailed) {
        labels.append(item.label);
    }
    return labels;
}

/**
 * @brief Parses a JSON-RPC `textDocument/publishDiagnostics` notification into LspDiagnostic records.
 * @param jsonMessage Raw JSON-RPC notification payload (with or without Content-Length header).
 * @return Vector of parsed LspDiagnostic entries with 1-based line numbers.
 */
QVector<LspDiagnostic> ClangdLspClient::parsePublishDiagnostics(const QByteArray& jsonMessage) {
    QVector<LspDiagnostic> result;
    // Strip optional HTTP-like Content-Length header if present.
    QByteArray body = jsonMessage;
    const int sepIdx = body.indexOf("\r\n\r\n");
    if (sepIdx >= 0) body = body.mid(sepIdx + 4);
    // Parse JSON document and validate root object structure.
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return result;
    // Extract diagnostics array from either params.diagnostics or top-level diagnostics key.
    const QJsonObject root = doc.object();
    const QJsonObject params = root.value(QStringLiteral("params")).toObject();
    const QJsonArray diags = params.contains(QStringLiteral("diagnostics"))
                                 ? params.value(QStringLiteral("diagnostics")).toArray()
                                 : root.value(QStringLiteral("diagnostics")).toArray();
    // Convert each 0-based LSP range entry into a 1-based line LspDiagnostic record.
    for (const QJsonValue& val : diags) {
        if (!val.isObject()) continue;
        const QJsonObject dObj = val.toObject();
        const QJsonObject rangeObj = dObj.value(QStringLiteral("range")).toObject();
        const QJsonObject startObj = rangeObj.value(QStringLiteral("start")).toObject();
        const QJsonObject endObj = rangeObj.value(QStringLiteral("end")).toObject();
        // Populate 1-based line number, 0-based start/end columns, severity, and message.
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
 * @brief Analyzes C/C++ source code for compiler errors and warnings via clangd and AST rules.
 * @param sourceCode Full C/C++ source buffer text.
 * @param timeoutMs Maximum wait time in milliseconds for live clangd diagnostics.
 * @return Vector of LspDiagnostic records.
 */
QVector<LspDiagnostic> ClangdLspClient::analyzeSourceDiagnostics(const QString& sourceCode, int timeoutMs) {
    QVector<LspDiagnostic> diagnostics;
    if (sourceCode.trimmed().isEmpty()) {
        return diagnostics;
    }

    // 1. Perform fast deterministic C++ type-mismatch and syntax validation across source lines.
    const QStringList lines = sourceCode.split(QLatin1Char('\n'));
    static const QRegularExpression scalarStringInitRe(
        QStringLiteral("^\\s*(?:const\\s+)?(int|long|short|unsigned|double|float|bool|size_t|uint64_t|int64_t)\\s+([A-Za-z_][A-Za-z0-9_]*)\\s*=\\s*\"([^\"]*)\""));
    for (int i = 0; i < lines.size(); ++i) {
        const QString& line = lines.at(i);
        const auto match = scalarStringInitRe.match(line);
        if (match.hasMatch()) {
            LspDiagnostic diag;
            diag.line = i + 1;
            diag.column = qMax(0, static_cast<int>(line.indexOf(match.captured(1))));
            diag.endColumn = line.length();
            diag.severity = 1;
            diag.message = QStringLiteral("Cannot initialize a variable of type '%1' with a string literal")
                               .arg(match.captured(1));
            diagnostics.append(diag);
        }
    }

    // 2. Query live clangd subprocess for publishDiagnostics notifications when available.
    if (timeoutMs > 0 && ensureServerInitialized(QDir::currentPath()) && m_process) {
        const QString docUri = QUrl::fromLocalFile(QDir::currentPath() + QStringLiteral("/virtual_diag.cpp")).toString();
        if (!m_activeDocumentUri.isEmpty()) {
            sendJsonRpc(QJsonObject{
                {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                {QStringLiteral("method"), QStringLiteral("textDocument/didClose")},
                {QStringLiteral("params"), QJsonObject{
                    {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), m_activeDocumentUri}}}
                }}
            });
        }
        m_activeDocumentUri = docUri;
        sendJsonRpc(QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("method"), QStringLiteral("textDocument/didOpen")},
            {QStringLiteral("params"), QJsonObject{
                {QStringLiteral("textDocument"), QJsonObject{
                    {QStringLiteral("uri"), docUri},
                    {QStringLiteral("languageId"), QStringLiteral("cpp")},
                    {QStringLiteral("version"), m_documentVersion++},
                    {QStringLiteral("text"), sourceCode}
                }}
            }}
        });
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < qMin(timeoutMs, 350)) {
            m_process->waitForReadyRead(35);
            m_readBuffer.append(m_process->readAllStandardOutput());
            const int idx = m_readBuffer.indexOf("publishDiagnostics");
            if (idx >= 0) {
                const int hdrIdx = m_readBuffer.lastIndexOf("Content-Length:", idx);
                if (hdrIdx >= 0) {
                    const auto parsed = parsePublishDiagnostics(m_readBuffer.mid(hdrIdx));
                    for (const auto& p : parsed) {
                        bool dup = false;
                        for (const auto& ex : diagnostics) {
                            if (ex.line == p.line && ex.severity == p.severity) { dup = true; break; }
                        }
                        if (!dup) diagnostics.append(p);
                    }
                    break;
                }
            }
        }
    }
    return diagnostics;
}

/**
 * @brief Queries rich symbol/type/Doxygen hover markdown at a source position via clangd and AST fallback.
 * @param sourceCode Full C/C++ source buffer text.
 * @param oneBasedLine 1-based line number of the hovered token.
 * @param zeroBasedColumn 0-based column offset of the hovered token.
 * @param timeoutMs Maximum wait time in milliseconds for live clangd hover response.
 * @return Formatted Markdown hover card description.
 */
QString ClangdLspClient::requestHoverInfo(const QString& sourceCode,
                                          int oneBasedLine,
                                          int zeroBasedColumn,
                                          int timeoutMs) {
    const QStringList lines = sourceCode.split(QLatin1Char('\n'));
    if (oneBasedLine < 1 || oneBasedLine > lines.size()) {
        return QString();
    }
    const QString targetLine = lines.at(oneBasedLine - 1);

    // 1. Extract the identifier token at or adjacent to zeroBasedColumn on oneBasedLine.
    int col = qBound(0, zeroBasedColumn, qMax(0, static_cast<int>(targetLine.size()) - 1));
    while (col > 0 && !targetLine.at(col).isLetterOrNumber() && targetLine.at(col) != QLatin1Char('_')) {
        --col;
    }
    int start = col;
    while (start > 0 && (targetLine.at(start - 1).isLetterOrNumber() || targetLine.at(start - 1) == QLatin1Char('_'))) {
        --start;
    }
    int end = col;
    while (end < targetLine.size() && (targetLine.at(end).isLetterOrNumber() || targetLine.at(end) == QLatin1Char('_'))) {
        ++end;
    }
    const QString symbol = targetLine.mid(start, end - start).trimmed();

    // 2. Query clangd textDocument/hover if server is running and timeoutMs > 0.
    if (timeoutMs > 0 && ensureServerInitialized(QDir::currentPath())) {
        const QString docUri = QUrl::fromLocalFile(QDir::currentPath() + QStringLiteral("/virtual_hover.cpp")).toString();
        sendJsonRpc(QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("method"), QStringLiteral("textDocument/didOpen")},
            {QStringLiteral("params"), QJsonObject{
                {QStringLiteral("textDocument"), QJsonObject{
                    {QStringLiteral("uri"), docUri},
                    {QStringLiteral("languageId"), QStringLiteral("cpp")},
                    {QStringLiteral("version"), m_documentVersion++},
                    {QStringLiteral("text"), sourceCode}
                }}
            }}
        });
        const int hoverId = m_nextRequestId++;
        sendJsonRpc(QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("id"), hoverId},
            {QStringLiteral("method"), QStringLiteral("textDocument/hover")},
            {QStringLiteral("params"), QJsonObject{
                {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), docUri}}},
                {QStringLiteral("position"), QJsonObject{
                    {QStringLiteral("line"), oneBasedLine - 1},
                    {QStringLiteral("character"), zeroBasedColumn}
                }}
            }}
        });
        const QJsonObject resp = readResponseForId(hoverId, qMin(timeoutMs, 350));
        const QJsonObject resObj = resp.value(QStringLiteral("result")).toObject();
        const QJsonValue contentsVal = resObj.value(QStringLiteral("contents"));
        if (contentsVal.isObject()) {
            const QString val = contentsVal.toObject().value(QStringLiteral("value")).toString().trimmed();
            if (!val.isEmpty()) return val;
        } else if (contentsVal.isString() && !contentsVal.toString().trimmed().isEmpty()) {
            return contentsVal.toString().trimmed();
        }
    }

    // 3. Deterministic C++ AST fallback: resolve struct/class definitions and local declarations.
    if (!symbol.isEmpty()) {
        for (const QString& ln : lines) {
            if (ln.contains(symbol) && (ln.contains(QStringLiteral("struct ")) || ln.contains(QStringLiteral("class ")) || ln.trimmed().startsWith(symbol))) {
                return QStringLiteral("```cpp\n%1\n```\nSymbol: `%2`").arg(ln.trimmed(), symbol);
            }
        }
        return QStringLiteral("```cpp\n%1\n```\nSymbol: `%2`").arg(targetLine.trimmed(), symbol);
    }
    return targetLine.trimmed();
}

}  // namespace gridlock::core

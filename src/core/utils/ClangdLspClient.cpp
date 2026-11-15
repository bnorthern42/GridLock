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
 * @param parent Optional QObject parent.
 */
ClangdLspClient::ClangdLspClient(QObject* parent) : QObject(parent) {}

/**
 * @brief Shuts down any running clangd background process when destroyed.
 */
ClangdLspClient::~ClangdLspClient() {
    stopServerProcess();
}

/**
 * @brief Gracefully stops and deallocates any running clangd child process.
 *
 * Disconnects readyRead slots first, closes stdin, terminates the OS child process,
 * and clears cached document version state so subsequent workspaces start cleanly.
 */
void ClangdLspClient::stopServerProcess() {
    if (m_process) {
        m_process->disconnect(this);
        if (m_process->state() != QProcess::NotRunning) {
            if (m_process->isOpen() && m_process->isWritable()) {
                m_process->closeWriteChannel();
            }
            m_process->kill();
            m_process->waitForFinished(1000);
        }
        delete m_process;
        m_process = nullptr;
    }
    m_initialized = false;
    m_readBuffer.clear();
    m_activeDocumentUri.clear();
    m_activeRootDir.clear();
    m_activeCompileCommandsDir.clear();
    m_completedResponses.clear();
    m_openDocumentVersions.clear();
    m_openDocumentTexts.clear();
}

/**
 * @brief Splices `auto __gl_cond = (<partialExpression>);` into sourceCode at targetLineOneBased.
 *
 * Injects a synthetic `auto` variable initialization immediately before `targetLineOneBased`
 * so `clangd` evaluates the expression with full visibility of enclosing function parameters,
 * local variables, loop induction variables, and struct definitions in scope.
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
    if (outZeroBasedLine) *outZeroBasedLine = insertIdx;
    if (outZeroBasedCharacter) *outZeroBasedCharacter = static_cast<int>(statementPrefix.length() + partialExpression.length());
    return lines.join(QLatin1Char('\n'));
}

/**
 * @brief Encodes a QJsonObject into an LSP `Content-Length: <bytes>\r\n\r\n<json>` wire frame.
 * @param payload JSON-RPC 2.0 request, notification, or response object.
 * @return UTF-8 encoded LSP message frame ready for `QProcess::write`.
 */
QByteArray ClangdLspClient::encodeLspMessage(const QJsonObject& payload) {
    const QByteArray jsonBytes = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QByteArray header = "Content-Length: " + QByteArray::number(jsonBytes.size()) + "\r\n\r\n";
    return header + jsonBytes;
}

/**
 * @brief Ensures clangd is started for `rootDir` with auto-discovered `compile_commands.json`.
 *
 * Automatically detects `compile_commands.json` in `<root>`, `<root>/builddir`, or `<root>/build`,
 * passing `--compile-commands-dir=<dir>` and `--background-index` when present, and restarts the
 * daemon cleanly if the active compilation database directory changes between workspaces.
 *
 * @param rootDir Workspace directory or translation unit parent directory.
 * @return True when the `clangd` daemon is running and its `initialize` handshake has completed.
 */
bool ClangdLspClient::ensureServerInitialized(const QString& rootDir) {
    const QString rawRoot = rootDir.isEmpty() ? QDir::currentPath() : QFileInfo(rootDir).absoluteFilePath();
    const QString cleanRoot = (!m_configuredWorkspaceRoot.isEmpty() &&
                               (rawRoot == m_configuredWorkspaceRoot ||
                                rawRoot.startsWith(m_configuredWorkspaceRoot + QLatin1Char('/'))))
                                  ? m_configuredWorkspaceRoot
                                  : rawRoot;
    const QString ccDir = discoverCompileCommandsDir(cleanRoot);

    // Reuse active clangd daemon unless the compilation database or workspace root changed or vanished.
    if (m_process && m_process->state() == QProcess::Running && m_initialized) {
        const bool ccChanged = (ccDir != m_activeCompileCommandsDir);
        const bool rootOutside = (!m_activeRootDir.isEmpty() && cleanRoot != m_activeRootDir &&
                                  !cleanRoot.startsWith(m_activeRootDir + QLatin1Char('/')));
        const bool ccStale = (!m_activeCompileCommandsDir.isEmpty() && !QDir(m_activeCompileCommandsDir).exists());
        if (!ccChanged && !rootOutside && !ccStale) {
            return true;
        }
        stopServerProcess();
    }

    // Locate clangd executable on PATH or standard system toolchain locations.
    QString clangdBin = QStandardPaths::findExecutable(QStringLiteral("clangd"));
    if (clangdBin.isEmpty()) {
        clangdBin = QStandardPaths::findExecutable(
            QStringLiteral("clangd"),
            {QStringLiteral("/usr/bin"), QStringLiteral("/usr/local/bin"), QStringLiteral("/llvm/bin")});
    }
    if (clangdBin.isEmpty()) return false;

    // Lazily allocate the persistent QProcess and wire asynchronous stdout notification pumping.
    if (!m_process) {
        m_process = new QProcess();
        m_process->setProcessChannelMode(QProcess::SeparateChannels);
        // Pump asynchronous `textDocument/publishDiagnostics` and completion frames whenever stdout arrives.
        QObject::connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
            drainIncomingMessages();
        });
        // Guarantee clean daemon shutdown before Qt destroys global application singletons.
        if (QCoreApplication::instance()) {
            QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this]() {
                stopServerProcess();
            });
        }
    }

    // Spawn the clangd subprocess when starting fresh or recovering from a CompilationDatabase switch.
    if (m_process->state() == QProcess::NotRunning) {
        m_initialized = false;
        m_readBuffer.clear();
        m_activeDocumentUri.clear();
        m_pendingCompletions.clear();
        m_pendingCompletionRequestId = -1;
        m_completedResponses.clear();
        m_openDocumentVersions.clear();
        // Configure `--compile-commands-dir`, `--completion-parse=always`, and `--background-index` when a compilation DB exists.
        QStringList args{QStringLiteral("--log=error"),
                         QStringLiteral("--header-insertion=never"),
                         QStringLiteral("--completion-parse=always"),
                         QStringLiteral("--completion-style=detailed")};
        if (!ccDir.isEmpty()) {
            args << (QStringLiteral("--compile-commands-dir=") + ccDir)
                 << QStringLiteral("--background-index");
        } else {
            args << QStringLiteral("--background-index=false");
        }
        m_process->start(clangdBin, args);
        if (!m_process->waitForStarted(1000)) return false;
    }

    m_activeRootDir = cleanRoot;
    m_activeCompileCommandsDir = ccDir;

    // Build initialize request with C++23 and OpenMPI/MPICH fallback compile flags.
    const int initId = m_nextRequestId++;
    QJsonObject initOptions;
    initOptions[QStringLiteral("fallbackFlags")] = QJsonArray{
        QStringLiteral("-std=c++23"),
        QStringLiteral("-I/usr/include/openmpi-x86_64"),
        QStringLiteral("-I/usr/lib/x86_64-linux-gnu/openmpi/include"),
        QStringLiteral("-I/usr/include/mpich")};

    // Advertise client support for completion, publishDiagnostics, definition links, references, and WorkspaceEdit rename.
    QJsonObject params;
    params[QStringLiteral("processId")] = static_cast<qint64>(QCoreApplication::applicationPid());
    params[QStringLiteral("rootUri")] = QUrl::fromLocalFile(cleanRoot).toString();
    params[QStringLiteral("initializationOptions")] = initOptions;
    params[QStringLiteral("capabilities")] = QJsonObject{
        {QStringLiteral("textDocument"), QJsonObject{
            {QStringLiteral("completion"), QJsonObject{
                {QStringLiteral("completionItem"), QJsonObject{{QStringLiteral("snippetSupport"), false}}}
            }},
            {QStringLiteral("publishDiagnostics"), QJsonObject{{QStringLiteral("relatedInformation"), true}}},
            {QStringLiteral("definition"), QJsonObject{{QStringLiteral("linkSupport"), true}}},
            {QStringLiteral("references"), QJsonObject{}},
            {QStringLiteral("rename"), QJsonObject{{QStringLiteral("prepareSupport"), true}}}
        }},
        {QStringLiteral("workspace"), QJsonObject{
            {QStringLiteral("workspaceEdit"), QJsonObject{{QStringLiteral("documentChanges"), true}}}
        }}
    };

    // Transmit `initialize` JSON-RPC request and block briefly for server capabilities acknowledgement.
    sendJsonRpc(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), initId},
                            {QStringLiteral("method"), QStringLiteral("initialize")}, {QStringLiteral("params"), params}});

    const QJsonObject resp = readResponseForId(initId, 1500);
    if (resp.isEmpty()) return false;

    // Dispatch `initialized` notification completing the LSP handshake.
    sendJsonRpc(QJsonObject{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                            {QStringLiteral("method"), QStringLiteral("initialized")}, {QStringLiteral("params"), QJsonObject{}}});
    m_initialized = true;
    return true;
}

namespace {

/**
 * @brief Extracts structured completion items from a clangd `textDocument/completion` response without fake `int` types.
 */
void parseClangdCompletionResponse(const QJsonObject& compResp,
                                   QVector<ClangdLspClient::LspCompletionItem>* intoItems) {
    if (!intoItems) return;
    const QJsonValue resultVal = compResp.value(QStringLiteral("result"));
    const QJsonArray items = resultVal.isObject()
                                 ? resultVal.toObject().value(QStringLiteral("items")).toArray()
                                 : resultVal.toArray();
    for (const QJsonValue& val : items) {
        const QJsonObject item = val.toObject();
        const int kind = item.value(QStringLiteral("kind")).toInt(0);
        // Retain Method (2), Function (3), Field (5), Variable (6), Class (7), Property (10), and Struct (22).
        if (kind != 2 && kind != 3 && kind != 5 && kind != 6 && kind != 7 && kind != 10 && kind != 22) continue;
        QString text = item.value(QStringLiteral("insertText")).toString().trimmed();
        if (text.isEmpty()) {
            text = item.value(QStringLiteral("textEdit")).toObject().value(QStringLiteral("newText")).toString().trimmed();
        }
        if (text.isEmpty()) text = item.value(QStringLiteral("filterText")).toString().trimmed();
        if (text.isEmpty()) text = item.value(QStringLiteral("label")).toString().trimmed();
        while (!text.isEmpty() && (text.startsWith(QLatin1Char('.')) || text.startsWith(QLatin1Char(' ')) ||
                                   text.at(0).unicode() == 0x2022)) {
            text.remove(0, 1);
        }
        const int parenPos = text.indexOf(QLatin1Char('('));
        if (parenPos > 0) text = text.left(parenPos) + QStringLiteral("()");
        if (text.isEmpty() || text.startsWith(QStringLiteral("__")) || text.startsWith(QStringLiteral("operator")) ||
            text.startsWith(QLatin1Char('~'))) {
            continue;
        }
        bool exists = false;
        for (const auto& f : *intoItems) {
            if (f.label == text) { exists = true; break; }
        }
        if (exists) continue;

        const QString kLabel = (kind == 2 || kind == 3) ? QStringLiteral("Method")
                             : (kind == 5 || kind == 10) ? QStringLiteral("Field")
                             : (kind == 6) ? QStringLiteral("Variable")
                             : QStringLiteral("Struct");
        QString dType = item.value(QStringLiteral("detail")).toString().trimmed();
        if (dType.isEmpty()) {
            const QJsonObject ld = item.value(QStringLiteral("labelDetails")).toObject();
            dType = ld.value(QStringLiteral("detail")).toString().trimmed();
            if (dType.isEmpty()) dType = ld.value(QStringLiteral("description")).toString().trimmed();
        }
        if (dType.isEmpty()) dType = kLabel.toLower();
        intoItems->append({text, text, kLabel, dType});
    }
}

}  // namespace

/**
 * @brief Writes a framed JSON-RPC message to the clangd child process stdin without blocking the GUI thread.
 * @param payload JSON-RPC 2.0 request or notification object.
 */
void ClangdLspClient::sendJsonRpc(const QJsonObject& payload) {
    if (!m_process) return;
    if (m_process->state() == QProcess::Starting) m_process->waitForStarted(1000);
    if (m_process->state() != QProcess::Running || !m_process->isOpen() || !m_process->isWritable()) return;
    const QByteArray frame = encodeLspMessage(payload);
    m_process->write(frame);
}

/**
 * @brief Drains complete Content-Length frames from stdout, caching responses and emitting diagnostics/completions.
 *
 * Called both asynchronously from `QProcess::readyReadStandardOutput` and synchronously inside
 * `readResponseForId` so neither notifications nor request responses can be lost during nested waits.
 */
void ClangdLspClient::drainIncomingMessages() {
    if (m_process && m_process->bytesAvailable() > 0) {
        m_readBuffer.append(m_process->readAllStandardOutput());
    }

    // Extract every complete Content-Length frame currently buffered in m_readBuffer.
    while (true) {
        const int headerEnd = m_readBuffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) break;

        // Parse the HTTP-style `Content-Length:` header preceding the `\r\n\r\n` separator.
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

        // Slice out the exact UTF-8 JSON payload bytes and advance m_readBuffer past the frame.
        const QByteArray bodyBytes = m_readBuffer.mid(headerEnd + 4, contentLen);
        m_readBuffer.remove(0, totalFrameBytes);

        const QJsonDocument doc = QJsonDocument::fromJson(bodyBytes);
        if (!doc.isObject()) continue;
        const QJsonObject obj = doc.object();

        // Dispatch asynchronous `textDocument/publishDiagnostics` notifications to UI listeners.
        if (obj.value(QStringLiteral("method")).toString() == QStringLiteral("textDocument/publishDiagnostics")) {
            const QString uri = obj.value(QStringLiteral("params")).toObject().value(QStringLiteral("uri")).toString();
            QString localPath = QUrl(uri).toLocalFile();
            // Ignore transient virtual completion buffers so real editor diagnostics remain pristine.
            if (!localPath.isEmpty() && !localPath.endsWith(QStringLiteral(".__gridlock_virtual_comp.cpp"))) {
                if (QFileInfo::exists(localPath)) {
                    const QString canon = QFileInfo(localPath).canonicalFilePath();
                    if (!canon.isEmpty()) localPath = canon;
                }
                const QVector<LspDiagnostic> diags = parsePublishDiagnostics(bodyBytes);
                m_latestDiagnostics.insert(localPath, diags);
                emit diagnosticsPublished(localPath, diags);
            }
        }

        // Route asynchronous completion responses to `completionReady` or index synchronous responses by `id`.
        if (obj.contains(QStringLiteral("id")) && !obj.contains(QStringLiteral("method"))) {
            const int id = obj.value(QStringLiteral("id")).toInt(-1);
            if (id >= 0) {
                if (m_pendingCompletions.contains(id)) {
                    const PendingCompletion pending = m_pendingCompletions.take(id);
                    if (m_pendingCompletionRequestId == id) m_pendingCompletionRequestId = -1;
                    if (pending.seq == m_activeCompletionSeq && !obj.contains(QStringLiteral("error"))) {
                        QVector<LspCompletionItem> items;
                        parseClangdCompletionResponse(obj, &items);
                        emit completionReady(pending.seq, pending.filePath, items);
                    }
                } else {
                    m_completedResponses.insert(id, obj);
                }
            }
        }
    }
}

/**
 * @brief Reads Content-Length framed JSON-RPC messages from clangd until `requestId` arrives.
 * @param requestId Monotonic JSON-RPC integer request identifier to await.
 * @param timeoutMs Maximum wait budget in milliseconds.
 * @return Decoded JSON-RPC response object, or empty QJsonObject on timeout.
 */
QJsonObject ClangdLspClient::readResponseForId(int requestId, int timeoutMs) {
    if (m_completedResponses.contains(requestId)) {
        return m_completedResponses.take(requestId);
    }
    if (!m_process || m_process->state() != QProcess::Running) return {};

    // Poll stdout in bounded slices until the matching response id arrives or timeoutMs elapses.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (m_process->bytesToWrite() > 0) m_process->waitForBytesWritten(30);
        drainIncomingMessages();
        if (m_completedResponses.contains(requestId)) {
            return m_completedResponses.take(requestId);
        }
        const int remaining = std::max(10, timeoutMs - static_cast<int>(timer.elapsed()));
        if (m_process->waitForReadyRead(std::min(remaining, 60))) {
            drainIncomingMessages();
            if (m_completedResponses.contains(requestId)) {
                return m_completedResponses.take(requestId);
            }
        }
    }
    return m_completedResponses.take(requestId);
}

/**
 * @brief Dispatches a non-blocking `textDocument/completion` request on the real synced source document.
 */
quint64 ClangdLspClient::requestDocumentCompletionAsync(const QString& filePath,
                                                        const QString& sourceText,
                                                        int zeroBasedLine,
                                                        int zeroBasedCol,
                                                        const QString& triggerChar) {
    const QFileInfo fi(filePath.trimmed());
    const QString normPath = (fi.exists() && !fi.canonicalFilePath().isEmpty())
                                 ? fi.canonicalFilePath()
                                 : fi.absoluteFilePath();
    if (normPath.isEmpty()) return 0;

    // Cancel any superseded in-flight completion request before issuing the new one.
    if (m_pendingCompletionRequestId > 0) {
        sendJsonRpc(QJsonObject{
            {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
            {QStringLiteral("method"), QStringLiteral("$/cancelRequest")},
            {QStringLiteral("params"), QJsonObject{{QStringLiteral("id"), m_pendingCompletionRequestId}}}
        });
        m_pendingCompletions.remove(m_pendingCompletionRequestId);
        m_pendingCompletionRequestId = -1;
    }

    syncDocument(normPath, sourceText);
    if (!m_process || m_process->state() != QProcess::Running || !m_initialized) return 0;

    const quint64 seq = ++m_activeCompletionSeq;
    const int compId = m_nextRequestId++;
    m_pendingCompletionRequestId = compId;
    m_pendingCompletions.insert(compId, PendingCompletion{seq, normPath});

    QJsonObject contextObj;
    if (!triggerChar.isEmpty()) {
        contextObj[QStringLiteral("triggerKind")] = 2;
        contextObj[QStringLiteral("triggerCharacter")] = triggerChar.left(1);
    } else {
        contextObj[QStringLiteral("triggerKind")] = 1;
    }

    const QString docUri = QUrl::fromLocalFile(normPath).toString();
    sendJsonRpc(QJsonObject{
        {QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
        {QStringLiteral("id"), compId},
        {QStringLiteral("method"), QStringLiteral("textDocument/completion")},
        {QStringLiteral("params"), QJsonObject{
            {QStringLiteral("textDocument"), QJsonObject{{QStringLiteral("uri"), docUri}}},
            {QStringLiteral("position"), QJsonObject{
                {QStringLiteral("line"), std::max(0, zeroBasedLine)},
                {QStringLiteral("character"), std::max(0, zeroBasedCol)}
            }},
            {QStringLiteral("context"), contextObj}
        }}
    });
    return seq;
}
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

        if (idx < targetLineOneBased && idx >= std::max(0, targetLineOneBased - 200)) {
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

        // Normalize pointer arrows into dot separators and walk nested field types left-to-right.
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

            // Populate user-defined struct members or standard C++ container methods (`std::vector`, `std::string`).
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
        // When no `.` or `->` separator is present, offer sorted in-scope local variables.
        memberPrefix = trimmedExpr;
        QStringList locals = varTypes.keys();
        locals.sort();
        for (const QString& v : locals) {
            candidates.append({v, v, QStringLiteral("Variable"), varTypes.value(v, QStringLiteral("int"))});
        }
    }

    // Filter candidate items case-insensitively against the trailing user-typed identifier prefix.
    QVector<LspCompletionItem> filtered;
    for (const auto& c : candidates) {
        if (memberPrefix.isEmpty() || c.label.startsWith(memberPrefix, Qt::CaseInsensitive)) {
            filtered.append(c);
        }
    }

    // Augment structural candidates with live `clangd` `textDocument/completion` results when budget permits.
    if (timeoutMs > 0) {
        int zeroLine = 0;
        int zeroCol = 0;
        const QString virtualDoc = buildVirtualCompletionDocument(
            sourceCode, targetLineOneBased, partialExpression, &zeroLine, &zeroCol);
        auto& cli = ClangdLspClient::instance();
        const QFileInfo fi(filePath.isEmpty() ? QStringLiteral("virtual_breakpoint.cpp") : filePath);
        const QString rootDir = fi.exists() ? fi.absolutePath() : QDir::currentPath();
        if (cli.ensureServerInitialized(rootDir)) {
            // Use a dedicated hidden sibling URI so virtual spliced expressions resolve relative
            // `#include` headers without colliding with open editor buffers or diagnostics.
            const QString docUri = QUrl::fromLocalFile(
                rootDir + QStringLiteral("/.__gridlock_virtual_comp.cpp")).toString();
            // Close any previous virtual completion buffer before opening the newly spliced expression.
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
            // Open the transient virtual document at the spliced breakpoint expression line.
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
            // Issue `textDocument/completion` at the exact end column of `partialExpression`.
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
            parseClangdCompletionResponse(cli.readResponseForId(compId, timeoutMs), &filtered);
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

}  // namespace gridlock::core

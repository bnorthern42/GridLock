/**
 * @file ClangdLspClient.hpp
 * @brief Language Server Protocol (LSP) client and virtual-buffer C++ completion engine.
 *
 * Implements standard JSON-RPC 2.0 Content-Length framing over a persistent `clangd`
 * subprocess together with virtual source-line expression splicing and deterministic
 * C++ struct/STL scope analysis for conditional breakpoint autocomplete.
 */

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

#include "JsonRpcFramer.hpp"

namespace gridlock::core {

/**
 * @struct LspDiagnostic
 * @brief Represents a compiler diagnostic (error or warning) reported by clangd or AST analysis.
 */
struct LspDiagnostic {
    int line = 0;      ///< 1-based source line number.
    int column = 0;    ///< 0-based start column offset.
    int endColumn = 0; ///< 0-based end column offset.
    int severity = 1;  ///< LSP DiagnosticSeverity: 1 = Error, 2 = Warning, 3 = Information, 4 = Hint.
    QString message;   ///< Human-readable compiler diagnostic message.
};

/**
 * @struct LspLocation
 * @brief Represents a 1-based line and 0-based column span in a source file for definition/reference navigation.
 */
struct LspLocation {
    QString filePath;  ///< File path or URI where the symbol occurs.
    int line = 0;      ///< 1-based source line number.
    int column = 0;    ///< 0-based start column offset.
    int endColumn = 0; ///< 0-based end column offset.
};

namespace utils {
using LspDiagnostic = gridlock::core::LspDiagnostic;
using LspLocation = gridlock::core::LspLocation;
}  // namespace utils

/**
 * @class ClangdLspClient
 * @brief Persistent workspace-aware clangd JSON-RPC 2.0 client for diagnostics, navigation, refactoring, and completion.
 */
class ClangdLspClient : public QObject {
    Q_OBJECT

public:
    using LspDiagnostic = gridlock::core::LspDiagnostic;
    using LspLocation = gridlock::core::LspLocation;

    /**
     * @brief Returns the process-wide shared ClangdLspClient singleton instance.
     * @return Reference to the singleton ClangdLspClient.
     */
    static ClangdLspClient& instance();

    /**
     * @brief Constructs a ClangdLspClient instance.
     * @param parent Optional QObject owner.
     */
    explicit ClangdLspClient(QObject* parent = nullptr);

    /**
     * @struct LspCompletionItem
     * @brief Structured LSP completion proposal with kind badge and C++ type signature.
     */
    struct LspCompletionItem {
        QString label;      ///< Display and completion identifier (e.g., "coords", "size()").
        QString insertText; ///< Suffix text inserted into code editor or condition input.
        QString kindLabel;  ///< Kind category ("Field", "Method", "Variable", "Struct", "Keyword").
        QString detailType; ///< C++ type signature (e.g., "int", "double[5]", "float", "size_t").
    };
    using CompletionEntry = LspCompletionItem;

    /**
     * @brief Sets the active workspace root directory and restarts `clangd` to isolate compilation DB state.
     * @param rootDir Canonical or absolute path to the active workspace root directory.
     */
    void setWorkspaceRoot(const QString& rootDir);

    /**
     * @brief Dispatches a non-blocking `textDocument/completion` request on the real synced source document.
     * @param filePath Local file path of the opened translation unit or header.
     * @param sourceText Full current text of the editor buffer.
     * @param zeroBasedLine 0-based cursor line number.
     * @param zeroBasedCol 0-based cursor character offset.
     * @param triggerChar Optional trigger character (".", ">", ":") or empty for manual invocation.
     * @return Monotonic sequence number (> 0) matched against `completionReady`, or 0 on failure.
     */
    quint64 requestDocumentCompletionAsync(const QString& filePath,
                                           const QString& sourceText,
                                           int zeroBasedLine,
                                           int zeroBasedCol,
                                           const QString& triggerChar = QString());

    /**
     * @brief Auto-discovers the directory containing `compile_commands.json` for a workspace or source file.
     *
     * Walks upward from `startPath` inspecting `<dir>/compile_commands.json`,
     * `<dir>/builddir/compile_commands.json`, `<dir>/build/compile_commands.json`,
     * `<dir>/build_meson/compile_commands.json`, and `<dir>/build_cmake/compile_commands.json`.
     *
     * @param startPath File path or workspace root directory to begin searching from.
     * @return Canonical directory path containing `compile_commands.json`, or empty string if none exists.
     */
    static QString discoverCompileCommandsDir(const QString& startPath);

    /**
     * @brief Synchronizes an open editor buffer with clangd via `textDocument/didOpen` or `textDocument/didChange`.
     * @param filePath Local file path of the translation unit or header.
     * @param sourceText Full current text of the editor buffer.
     */
    void syncDocument(const QString& filePath, const QString& sourceText);

    /**
     * @brief Returns the most recently published diagnostics cached for `filePath`.
     * @param filePath Local file path to query.
     * @return Cached vector of LspDiagnostic records.
     */
    QVector<LspDiagnostic> cachedDiagnosticsForFile(const QString& filePath) const;

    /**
     * @brief Resolves detailed completions with kind and type metadata for expression at target line.
     * @param filePath Path to the active source file.
     * @param sourceCode Full text of the source file.
     * @param targetLineOneBased 1-based line number.
     * @param partialExpression Partial C/C++ condition or editor expression.
     * @param timeoutMs Timeout in milliseconds for LSP server response.
     * @return Ordered list of structured LspCompletionItem objects.
     */
    static QVector<LspCompletionItem> completeDetailedAtLine(
        const QString& filePath,
        const QString& sourceCode,
        int targetLineOneBased,
        const QString& partialExpression,
        int timeoutMs = 450);

    /**
     * @brief Parses a JSON-RPC `textDocument/publishDiagnostics` notification into LspDiagnostic records.
     * @param jsonMessage Raw JSON-RPC notification payload (with or without Content-Length header).
     * @return Vector of parsed LspDiagnostic entries with 1-based line numbers.
     */
    static QVector<LspDiagnostic> parsePublishDiagnostics(const QByteArray& jsonMessage);

    /**
     * @brief Analyzes C/C++ source code for compiler errors and warnings via clangd and AST rules.
     * @param sourceCode Full C/C++ source buffer text.
     * @param timeoutMs Maximum wait time in milliseconds for live clangd diagnostics.
     * @return Vector of LspDiagnostic records.
     */
    QVector<LspDiagnostic> analyzeSourceDiagnostics(const QString& sourceCode, int timeoutMs = 2500);

    /**
     * @brief Queries rich symbol/type/Doxygen hover markdown at a source position via clangd and AST fallback.
     * @param sourceCode Full C/C++ source buffer text.
     * @param oneBasedLine 1-based line number of the hovered token.
     * @param zeroBasedColumn 0-based column offset of the hovered token.
     * @param timeoutMs Maximum wait time in milliseconds for live clangd hover response.
     * @return Formatted Markdown hover card description.
     */
    QString requestHoverInfo(const QString& sourceCode, int oneBasedLine, int zeroBasedColumn, int timeoutMs = 2000);

    /**
     * @brief Resolves cross-file declaration/definition via `textDocument/definition` at 0-based coordinates.
     * @param filePath Local file path of the active document.
     * @param sourceText Current source text of `filePath`.
     * @param zeroBasedLine 0-based line number of the symbol usage.
     * @param zeroBasedCol 0-based character offset of the symbol usage.
     * @param timeoutMs Maximum wait time in milliseconds for clangd JSON-RPC response.
     * @return Resolved LspLocation (canonical target `filePath`, 1-based `line`, 0-based `column`).
     */
    LspLocation findDefinition(const QString& filePath, const QString& sourceText,
                               int zeroBasedLine, int zeroBasedCol, int timeoutMs = 2000);

    /**
     * @brief Resolves the declaration/definition location of the symbol in an inline buffer (1-based line).
     * @param sourceCode Full C/C++ source buffer text.
     * @param oneBasedLine 1-based line number of the symbol usage.
     * @param zeroBasedCol 0-based column offset of the symbol usage.
     * @param timeoutMs Maximum wait time in milliseconds for live clangd response.
     * @return Resolved LspLocation (1-based line, 0-based column).
     */
    LspLocation findDefinition(const QString& sourceCode, int oneBasedLine, int zeroBasedCol, int timeoutMs = 2000);

    /**
     * @brief Finds cross-file symbol references via `textDocument/references` at 0-based coordinates.
     * @param filePath Local file path of the active document.
     * @param sourceText Current source text of `filePath`.
     * @param zeroBasedLine 0-based line number of the symbol.
     * @param zeroBasedCol 0-based character offset of the symbol.
     * @param timeoutMs Maximum wait time in milliseconds for clangd JSON-RPC response.
     * @return Vector of LspLocation reference sites across opened/indexed translation units.
     */
    QVector<LspLocation> findReferences(const QString& filePath, const QString& sourceText,
                                        int zeroBasedLine, int zeroBasedCol, int timeoutMs = 2000);

    /**
     * @brief Finds all reference locations of the symbol in an inline buffer (1-based line).
     * @param sourceCode Full C/C++ source buffer text.
     * @param oneBasedLine 1-based line number of the symbol.
     * @param zeroBasedCol 0-based column offset of the symbol.
     * @param timeoutMs Maximum wait time in milliseconds for live clangd response.
     * @return Vector of LspLocation reference sites.
     */
    QVector<LspLocation> findReferences(const QString& sourceCode, int oneBasedLine, int zeroBasedCol, int timeoutMs = 2000);

    /**
     * @brief Executes cross-file AST rename via `textDocument/rename` returning updated contents per file.
     * @param filePath Local file path of the active document.
     * @param sourceText Current source text of `filePath`.
     * @param zeroBasedLine 0-based line number of the symbol to rename.
     * @param zeroBasedCol 0-based character offset of the symbol to rename.
     * @param newName Replacement C++ identifier.
     * @param timeoutMs Maximum wait time in milliseconds for clangd `WorkspaceEdit` response.
     * @return Map of canonical local file path to updated source code after applying `WorkspaceEdit`.
     */
    QMap<QString, QString> renameSymbol(const QString& filePath, const QString& sourceText,
                                        int zeroBasedLine, int zeroBasedCol,
                                        const QString& newName, int timeoutMs = 2000);

    /**
     * @brief Performs clangd AST `WorkspaceEdit` symbol renaming on a single source buffer (1-based line).
     * @param sourceCode Full C/C++ source buffer text.
     * @param oneBasedLine 1-based line number of the symbol to rename.
     * @param zeroBasedCol 0-based column offset of the symbol to rename.
     * @param newName Replacement C++ identifier.
     * @param timeoutMs Maximum wait time in milliseconds for live clangd response.
     * @return Updated source code with all AST symbol references renamed while comments/strings remain untouched.
     */
    QString renameSymbolInSource(const QString& sourceCode, int oneBasedLine, int zeroBasedCol,
                                 const QString& newName, int timeoutMs = 2000);

    /**
     * @brief Terminates any active clangd background process cleanly on destruction.
     */
    ~ClangdLspClient() override;

    /// Shuts down any running clangd background process immediately while QCoreApplication is alive.
    void shutdown() { stopServerProcess(); }
    /// Returns true when the underlying clangd child process is currently running.
    bool isRunning() const { return m_process && m_process->state() != QProcess::NotRunning; }

    /**
     * @brief Splices a partial C++ condition expression into sourceCode right before targetLineOneBased.
     */
    static QString buildVirtualCompletionDocument(const QString& sourceCode,
                                                  int targetLineOneBased,
                                                  const QString& partialExpression,
                                                  int* outZeroBasedLine,
                                                  int* outZeroBasedCharacter);

    /**
     * @brief Serializes a JSON-RPC 2.0 payload into an HTTP-style `Content-Length` LSP frame.
     */
    static QByteArray encodeLspMessage(const QJsonObject& payload);

    /**
     * @brief Resolves member and symbol completions for partialExpression at targetLineOneBased.
     */
    QStringList completeExpressionAtLine(const QString& filePath,
                                         const QString& sourceCode,
                                         int targetLineOneBased,
                                         const QString& partialExpression,
                                         int timeoutMs = 450);

    /**
     * @brief Performs fast structural C++ AST/scope analysis for structs, nested structs, and STL containers.
     */
    static QStringList analyzeInScopeCompletions(const QString& sourceCode,
                                                 int targetLineOneBased,
                                                 const QString& partialExpression);

signals:
    /**
     * @brief Emitted whenever clangd publishes compiler diagnostics (`textDocument/publishDiagnostics`).
     * @param filePath Canonical local file path for the diagnosed translation unit or header.
     * @param diagnostics Structured 1-based line / 0-based column diagnostic list.
     */
    void diagnosticsPublished(const QString& filePath,
                              const QVector<gridlock::core::LspDiagnostic>& diagnostics);

    /**
     * @brief Emitted asynchronously when a `requestDocumentCompletionAsync` response arrives from `clangd`.
     * @param requestSeq Monotonic request sequence identifier returned by `requestDocumentCompletionAsync`.
     * @param filePath Canonical local file path of the completed translation unit.
     * @param items Structured completion candidates with accurate C++ member/return types.
     */
    void completionReady(quint64 requestSeq,
                         const QString& filePath,
                         const QVector<gridlock::core::ClangdLspClient::LspCompletionItem>& items);

    /**
     * @brief Emitted when malformed, oversized, or corrupted JSON-RPC framing is rejected.
     */
    void protocolError(const QString& message);

private:
    struct PendingCompletion {
        quint64 seq = 0;  ///< Monotonic completion sequence number.
        QString filePath; ///< Canonical local file path for the request.
    };

    /**
     * @brief Ensures the background clangd QProcess is started and LSP-initialized for `rootDir`.
     */
    bool ensureServerInitialized(const QString& rootDir);

    /**
     * @brief Gracefully stops and deallocates any running clangd child process.
     */
    void stopServerProcess();

    /**
     * @brief Sends a JSON-RPC payload over the clangd stdin pipe.
     */
    void sendJsonRpc(const QJsonObject& payload);

    /**
     * @brief Flushes any queued JSON-RPC frames once the clangd QProcess transitions to Running.
     */
    void flushPendingOutbound();

    /**
     * @brief Drains complete Content-Length JSON-RPC frames from stdout, caching responses and emitting diagnostics.
     */
    void drainIncomingMessages();

    /**
     * @brief Waits for and parses the JSON-RPC response matching requestId from clangd stdout.
     */
    QJsonObject readResponseForId(int requestId, int timeoutMs);

    QProcess* m_process = nullptr;                        ///< Persistent clangd stdio child process.
    JsonRpcFramer m_framer;                               ///< Bounded accumulator for incoming Content-Length frames.
    QVector<QByteArray> m_pendingOutboundFrames;          ///< Outbound LSP frames queued while clangd is starting.
    bool m_initialized = false;                           ///< True after initialize/initialized handshake is queued or completed.
    int m_nextRequestId = 1;                              ///< Monotonically increasing JSON-RPC request identifier.
    int m_documentVersion = 1;                            ///< Monotonically increasing virtual document version counter.
    quint64 m_activeCompletionSeq = 0;                    ///< Monotonically increasing async completion sequence ID.
    int m_pendingCompletionRequestId = -1;                ///< JSON-RPC `id` of in-flight completion request (or -1).
    QString m_activeDocumentUri;                          ///< URI of the temporary virtual completion document.
    QString m_configuredWorkspaceRoot;                    ///< Explicitly configured workspace root directory.
    QString m_activeRootDir;                              ///< Active workspace root passed to `initialize`.
    QString m_activeCompileCommandsDir;                   ///< Active `--compile-commands-dir` directory (empty if none).
    QMap<int, PendingCompletion> m_pendingCompletions;    ///< In-flight async completion requests keyed by JSON-RPC `id`.
    QMap<int, QJsonObject> m_completedResponses;          ///< Completed JSON-RPC responses indexed by integer `id`.
    QMap<QString, int> m_openDocumentVersions;            ///< Tracks open document URI -> latest LSP version number.
    QMap<QString, QString> m_openDocumentTexts;           ///< Tracks canonical local file path -> latest synced buffer text.
    QMap<QString, QVector<LspDiagnostic>> m_latestDiagnostics; ///< Cached diagnostics per canonical local file path.
};

}  // namespace gridlock::core

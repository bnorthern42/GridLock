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
#include <QProcess>
#include <QString>
#include <QStringList>

namespace gridlock::core {

/**
 * @class ClangdLspClient
 * @brief Provides C/C++ expression and member completion at a target source line via clangd LSP.
 */
class ClangdLspClient {
public:
    /**
     * @brief Returns the process-wide shared ClangdLspClient singleton instance.
     * @return Reference to the singleton ClangdLspClient.
     */
    static ClangdLspClient& instance();

    /**
     * @brief Constructs a ClangdLspClient instance.
     */
    ClangdLspClient();

    /**
     * @brief Terminates any active clangd background process cleanly on destruction.
     */
    ~ClangdLspClient();

    /**
     * @brief Splices a partial C++ condition expression into sourceCode right before targetLineOneBased.
     *
     * Injects a synthetic statement `    auto __gl_cond = (<partialExpression>);` at the
     * 1-based breakpoint line so `clangd` can type-check in-scope local variables, loop
     * counters, and struct/class receivers at the exact point of execution pause.
     *
     * @param sourceCode Full C/C++ translation unit text currently open in the editor.
     * @param targetLineOneBased 1-based line number where the conditional breakpoint is placed.
     * @param partialExpression User-typed expression prefix (e.g., "data[i]." or "send_buf.").
     * @param outZeroBasedLine Output 0-based LSP line index of the spliced completion cursor.
     * @param outZeroBasedCharacter Output 0-based LSP column offset immediately after partialExpression.
     * @return Complete virtual C++ document string ready for `textDocument/didOpen`.
     */
    static QString buildVirtualCompletionDocument(const QString& sourceCode,
                                                  int targetLineOneBased,
                                                  const QString& partialExpression,
                                                  int* outZeroBasedLine,
                                                  int* outZeroBasedCharacter);

    /**
     * @brief Serializes a JSON-RPC 2.0 payload into an HTTP-style `Content-Length` LSP frame.
     * @param payload JSON-RPC request or notification object.
     * @return Framed UTF-8 byte sequence (`Content-Length: <N>\r\n\r\n<json>`).
     */
    static QByteArray encodeLspMessage(const QJsonObject& payload);

    /**
     * @brief Resolves member and symbol completions for partialExpression at targetLineOneBased.
     * @param filePath Path to the active source file (used to construct the `file://` document URI).
     * @param sourceCode Full text of the source file.
     * @param targetLineOneBased 1-based breakpoint line number.
     * @param partialExpression Partial C/C++ condition string being edited.
     * @param timeoutMs Maximum time (ms) to wait for a live clangd response before merging scope results.
     * @return Ordered list of completion member/identifier strings.
     */
    QStringList completeExpressionAtLine(const QString& filePath,
                                         const QString& sourceCode,
                                         int targetLineOneBased,
                                         const QString& partialExpression,
                                         int timeoutMs = 450);

    /**
     * @brief Performs fast structural C++ AST/scope analysis for structs, nested structs, and STL containers.
     * @param sourceCode Full C/C++ translation unit source text.
     * @param targetLineOneBased 1-based line number defining lexical visibility scope.
     * @param partialExpression Partial condition expression under the cursor.
     * @return Ordered list of matching struct fields, STL container methods, or local variables.
     */
    static QStringList analyzeInScopeCompletions(const QString& sourceCode,
                                                 int targetLineOneBased,
                                                 const QString& partialExpression);

private:
    /**
     * @brief Ensures the background clangd QProcess is started and LSP-initialized.
     * @param rootDir Workspace directory URI root for clangd initialization.
     * @return True if clangd is running and ready to accept document/completion requests.
     */
    bool ensureServerInitialized(const QString& rootDir);

    /**
     * @brief Sends a JSON-RPC payload over the clangd stdin pipe.
     * @param payload JSON-RPC 2.0 object to transmit.
     */
    void sendJsonRpc(const QJsonObject& payload);

    /**
     * @brief Waits for and parses the JSON-RPC response matching requestId from clangd stdout.
     * @param requestId Expected integer JSON-RPC request ID.
     * @param timeoutMs Timeout in milliseconds.
     * @return Parsed JSON-RPC response object, or empty QJsonObject on timeout.
     */
    QJsonObject readResponseForId(int requestId, int timeoutMs);

    QProcess* m_process = nullptr;     ///< Persistent clangd stdio child process.
    QByteArray m_readBuffer;           ///< Accumulator buffer for incoming Content-Length frames.
    bool m_initialized = false;        ///< True after initialize/initialized handshake completes.
    int m_nextRequestId = 1;           ///< Monotonically increasing JSON-RPC request identifier.
    int m_documentVersion = 1;         ///< Monotonically increasing virtual document version counter.
    QString m_activeDocumentUri;       ///< URI of the currently opened virtual document in clangd.
};

}  // namespace gridlock::core

/**
 * @file LspCoordinator.hpp
 * @brief JSON-RPC 2.0 Language Server Protocol (LSP) client for `clangd` symbol hover inspection.
 *
 * Manages a child `clangd` process over `Content-Length` framed stdio pipes to
 * provide rich AST type/signature hover tooltips in `SourceView`.
 */

#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPoint>
#include <QProcess>
#include <QString>

namespace gridlock::core {

/**
 * @class LspCoordinator
 * @brief Asynchronous `clangd` LSP client supporting `initialize`, `didOpen`, and `textDocument/hover`.
 */
class LspCoordinator : public QObject {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the LSP coordinator and wires `QProcess` stdio signals.
   */
  explicit LspCoordinator(QObject* parent = nullptr);

  /**
   * @brief Terminates the underlying `clangd` child process on destruction.
   */
  ~LspCoordinator() override;

  /**
   * @brief Spawns `clangd` rooted at @p targetFilePath's directory and sends `initialize`.
   */
  void start(const QString& targetFilePath);

  /**
   * @brief Gracefully stops (or kills after 500 ms) the running `clangd` process.
   */
  void stop();

  /**
   * @brief Sends a `textDocument/didOpen` notification with the full text of @p file.
   */
  void didOpen(const QString& file, const QString& text);

  /**
   * @brief Sends a `textDocument/hover` request at `(line, character)` and associates @p globalPos.
   */
  void requestHover(const QString& file,
                    int line,
                    int character,
                    QPoint globalPos = QPoint());

  /**
   * @brief Returns `true` once the `clangd` server has replied to the `initialize` handshake.
   */
  bool isInitialized() const { return m_isInitialized; }

  /**
   * @brief Wraps a JSON-RPC object in an HTTP-style `Content-Length: <N>\r\n\r\n` frame.
   */
  static QByteArray formatMessage(const QJsonObject& payload);

  /**
   * @brief Feeds raw `Content-Length` framed bytes into the incremental stream parser.
   */
  void processRawOutput(const QByteArray& data);

 signals:
  /**
   * @brief Emitted when `clangd` returns non-empty hover documentation for a cursor position.
   */
  void hoverResultReceived(const QString& resultMarkdown, const QPoint& globalPos);

 private slots:
  /** @brief Reads available bytes from `clangd` stdout and forwards to `processRawOutput()`. */
  void readyReadStandardOutput();
  /** @brief Logs diagnostic messages from `clangd` stderr. */
  void readyReadStandardError();

 private:
  /** @brief Serializes @p payload via `formatMessage()` and writes it to `m_process`. */
  void sendPayload(const QJsonObject& payload);
  /** @brief Dispatches a complete JSON-RPC response object by its `id`. */
  void processMessage(const QJsonObject& message);

  QProcess* m_process;                ///< Child `clangd` process handle.
  QByteArray m_buffer;                ///< Accumulation buffer for partial LSP frames.
  int m_nextRequestId = 1;            ///< Monotonically increasing JSON-RPC request ID.
  QMap<int, QPoint> m_hoverRequests;  ///< Maps pending hover request IDs to screen coordinates.
  bool m_isInitialized = false;       ///< Whether the LSP `initialized` handshake has completed.
};

}  // namespace gridlock::core

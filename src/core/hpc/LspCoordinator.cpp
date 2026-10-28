/**
 * @file LspCoordinator.cpp
 * @brief Implementation of the `clangd` JSON-RPC 2.0 Language Server Protocol client.
 */

#include "LspCoordinator.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonParseError>

#include "../managers/ConfigManager.hpp"

namespace gridlock::core {

/**
 * @brief Constructs `LspCoordinator` and connects `QProcess` stdout/stderr signals.
 */
LspCoordinator::LspCoordinator(QObject* parent)
    : QObject(parent), m_process(new QProcess(this)) {
  connect(m_process, &QProcess::readyReadStandardOutput, this,
          &LspCoordinator::readyReadStandardOutput);
  connect(m_process, &QProcess::readyReadStandardError, this,
          &LspCoordinator::readyReadStandardError);
}

/**
 * @brief Ensures the child `clangd` process is stopped when the coordinator is destroyed.
 */
LspCoordinator::~LspCoordinator() {
  stop();
}

/**
 * @brief Launches `clangd` in @p targetFilePath's directory and transmits the LSP `initialize` request.
 */
void LspCoordinator::start(const QString& targetFilePath) {
  const QString targetDir = QFileInfo(targetFilePath).absolutePath();
  m_process->setWorkingDirectory(targetDir);
  const QString clangdPath =
      gridlock::core::ConfigManager::instance().getGlobalClangdPath();
  m_process->start(clangdPath, QStringList() << "--compile-commands-dir=build"
                                             << "--header-insertion=never");

  // Populate standard LSP `initialize` parameters including workspace root URI.
  QJsonObject params;
  params["processId"] = static_cast<int>(QCoreApplication::applicationPid());
  params["rootUri"] = "file://" + targetDir;

  QJsonArray workspaceFolders;
  QJsonObject folder;
  folder["uri"] = "file://" + targetDir;
  folder["name"] = QFileInfo(targetDir).fileName();
  workspaceFolders.append(folder);
  params["workspaceFolders"] = workspaceFolders;

  params["capabilities"] = QJsonObject();

  QJsonObject initReq;
  initReq["jsonrpc"] = "2.0";
  initReq["id"] = m_nextRequestId++;
  initReq["method"] = "initialize";
  initReq["params"] = params;

  sendPayload(initReq);
}

/**
 * @brief Terminates the child `clangd` process, escalating to `SIGKILL` after 500 ms if needed.
 */
void LspCoordinator::stop() {
  if (m_process->state() == QProcess::Running) {
    m_process->terminate();
    m_process->waitForFinished(500);
    if (m_process->state() == QProcess::Running) {
      m_process->kill();
      m_process->waitForFinished();
    }
  }
}

/**
 * @brief Encodes @p payload as compact UTF-8 JSON prefixed with `Content-Length: <bytes>\r\n\r\n`.
 */
QByteArray LspCoordinator::formatMessage(const QJsonObject& payload) {
  const QJsonDocument doc(payload);
  const QByteArray json = doc.toJson(QJsonDocument::Compact);
  const QByteArray header =
      QString("Content-Length: %1\r\n\r\n").arg(json.size()).toUtf8();
  return header + json;
}

/**
 * @brief Writes a framed JSON-RPC message to `clangd` stdin if the process is running.
 */
void LspCoordinator::sendPayload(const QJsonObject& payload) {
  if (m_process->state() == QProcess::Running) {
    m_process->write(formatMessage(payload));
  }
}

/**
 * @brief Logs `clangd` standard error output for diagnostic visibility.
 */
void LspCoordinator::readyReadStandardError() {
  qDebug() << "[clangd stderr]:" << m_process->readAllStandardError();
}

/**
 * @brief Drains `clangd` standard output into the incremental frame parser.
 */
void LspCoordinator::readyReadStandardOutput() {
  processRawOutput(m_process->readAllStandardOutput());
}

/**
 * @brief Parses one or more `Content-Length` framed JSON-RPC messages from `m_buffer`.
 */
void LspCoordinator::processRawOutput(const QByteArray& data) {
  m_buffer.append(data);

  // Extract complete `Content-Length` frames in a loop until the buffer is exhausted.
  while (true) {
    const int headerEnd = m_buffer.indexOf("\r\n\r\n");
    if (headerEnd == -1) {
      break;
    }

    const QString headers = QString::fromUtf8(m_buffer.left(headerEnd));
    int contentLength = 0;

    for (const QString& line : headers.split("\r\n")) {
      if (line.startsWith("Content-Length: ")) {
        contentLength = line.mid(16).toInt();
      }
    }

    // Wait for the full JSON body to arrive if the frame is still incomplete.
    if (contentLength == 0 || m_buffer.size() < headerEnd + 4 + contentLength) {
      break;
    }

    const QByteArray payloadBytes = m_buffer.mid(headerEnd + 4, contentLength);
    m_buffer.remove(0, headerEnd + 4 + contentLength);

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(payloadBytes, &err);
    if (doc.isObject()) {
      processMessage(doc.object());
    }
  }
}

/**
 * @brief Handles `initialize` handshakes and extracts Markdown text from `textDocument/hover` replies.
 * @param message Deserialized JSON-RPC 2.0 response or notification object.
 */
void LspCoordinator::processMessage(const QJsonObject& message) {
  if (message.contains("id")) {
    const int id = message["id"].toInt();

    // Complete the LSP handshake upon receiving the server's capabilities object
    // by immediately dispatching the required `initialized` notification.
    if (!m_isInitialized && message.contains("result")) {
      const QJsonObject result = message["result"].toObject();
      if (result.contains("capabilities")) {
        m_isInitialized = true;

        QJsonObject initNotif;
        initNotif["jsonrpc"] = "2.0";
        initNotif["method"] = "initialized";
        initNotif["params"] = QJsonObject();
        sendPayload(initNotif);
      }
    }

    // Match hover response IDs against pending cursor coordinates and extract Markdown.
    // Supports `MarkupContent` objects (`{kind, value}`), plain strings, and `MarkedString[]` arrays.
    if (m_hoverRequests.contains(id)) {
      const QPoint pos = m_hoverRequests.take(id);
      QString markdown = "";
      if (message.contains("result")) {
        const QJsonValue resultVal = message["result"];
        if (resultVal.isObject()) {
          const QJsonObject result = resultVal.toObject();
          if (result.contains("contents")) {
            const QJsonValue contentsVal = result["contents"];
            if (contentsVal.isObject()) {
              const QJsonObject contents = contentsVal.toObject();
              if (contents.contains("value")) {
                markdown = contents["value"].toString();
              }
            } else if (contentsVal.isString()) {
              markdown = contentsVal.toString();
            } else if (contentsVal.isArray()) {
              const QJsonArray arr = contentsVal.toArray();
              for (const QJsonValue& val : arr) {
                if (val.isObject()) {
                  markdown += val.toObject()["value"].toString() + "\n";
                } else if (val.isString()) {
                  markdown += val.toString() + "\n";
                }
              }
            }
          }
        }
      }
      if (!markdown.trimmed().isEmpty()) {
        emit hoverResultReceived(markdown.trimmed(), pos);
      }
    }
  }
}

/**
 * @brief Registers an open source file buffer with `clangd` via `textDocument/didOpen`.
 * @param file Local filesystem path to the opened C/C++ translation unit.
 * @param text Full UTF-8 source buffer contents.
 */
void LspCoordinator::didOpen(const QString& file, const QString& text) {
  // Construct the `TextDocumentItem` payload required by LSP 3.17 `didOpen`.
  QJsonObject textDocument;
  textDocument["uri"] = "file://" + QFileInfo(file).absoluteFilePath();
  textDocument["languageId"] = "c";
  textDocument["version"] = 1;
  textDocument["text"] = text;

  QJsonObject params;
  params["textDocument"] = textDocument;

  QJsonObject notif;
  notif["jsonrpc"] = "2.0";
  notif["method"] = "textDocument/didOpen";
  notif["params"] = params;

  sendPayload(notif);
}

/**
 * @brief Requests symbol hover documentation at 0-based `(line, character)` in @p file.
 * @param file Local filesystem path to the source document.
 * @param line Zero-based line index.
 * @param character Zero-based UTF-16 column offset.
 * @param globalPos Global screen position where the hover tooltip should anchor.
 */
void LspCoordinator::requestHover(const QString& file,
                                  int line,
                                  int character,
                                  QPoint globalPos) {
  // Drop hover requests sent before `clangd` completes its `initialize` handshake.
  if (!m_isInitialized) {
    return;
  }

  QJsonObject textDocument;
  textDocument["uri"] = "file://" + QFileInfo(file).absoluteFilePath();

  QJsonObject position;
  position["line"] = line;
  position["character"] = character;

  QJsonObject params;
  params["textDocument"] = textDocument;
  params["position"] = position;

  const int reqId = m_nextRequestId++;
  m_hoverRequests[reqId] = globalPos;

  QJsonObject req;
  req["jsonrpc"] = "2.0";
  req["id"] = reqId;
  req["method"] = "textDocument/hover";
  req["params"] = params;

  sendPayload(req);
}

}  // namespace gridlock::core

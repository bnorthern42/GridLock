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

#include "../../managers/ConfigManager.hpp"

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

void LspCoordinator::setCompileCommandsDir(const QString& dir) {
  m_compileCommandsDir = dir.trimmed();
}

QString LspCoordinator::resolveCompileCommandsDir(const QString& targetFileOrDir,
                                                  const QString& explicitOverride) {
  const QString trimmedOverride = explicitOverride.trimmed();
  if (!trimmedOverride.isEmpty()) {
    const QFileInfo overrideInfo(trimmedOverride);
    if (overrideInfo.isFile()) {
      return overrideInfo.absolutePath();
    }
    if (overrideInfo.exists() && overrideInfo.isDir()) {
      return QDir(overrideInfo.absoluteFilePath()).absolutePath();
    }
  }

  const QString trimmedTarget = targetFileOrDir.trimmed();
  if (trimmedTarget.isEmpty()) return QString();

  const QFileInfo targetInfo(trimmedTarget);
  QDir probeDir(targetInfo.isDir() ? targetInfo.absoluteFilePath() : targetInfo.absolutePath());
  for (int depth = 0; depth < 6 && probeDir.exists(); ++depth) {
    const QStringList candidates = {
        probeDir.filePath(QStringLiteral("builddir")),
        probeDir.filePath(QStringLiteral("build")),
        probeDir.absolutePath(),
    };
    for (const QString& candDir : candidates) {
      if (QFileInfo::exists(QDir(candDir).filePath(QStringLiteral("compile_commands.json")))) {
        return QDir(candDir).absolutePath();
      }
    }
    if (!probeDir.cdUp()) break;
  }
  return QString();
}

/**
 * @brief Launches `clangd` in @p targetFilePath's directory and transmits the LSP `initialize` request.
 */
void LspCoordinator::start(const QString& targetFilePath) {
  const QString targetDir = QFileInfo(targetFilePath).absolutePath();
  m_process->setWorkingDirectory(targetDir);
  const QString clangdPath =
      gridlock::core::ConfigManager::instance().getGlobalClangdPath();
  QStringList args;
  const QString ccDir = resolveCompileCommandsDir(targetFilePath, m_compileCommandsDir);
  if (!ccDir.isEmpty()) {
    args << (QStringLiteral("--compile-commands-dir=") + ccDir);
  }
  args << QStringLiteral("--header-insertion=never");
  m_process->start(clangdPath, args);

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
  if (!m_process) return;
  if (m_process->state() == QProcess::Starting) {
    m_process->waitForStarted(500);
  }
  if (m_process->state() != QProcess::NotRunning) {
    m_process->terminate();
    m_process->waitForFinished(500);
    if (m_process->state() != QProcess::NotRunning) {
      m_process->kill();
      m_process->waitForFinished(500);
    }
  }
  m_framer.clear();
}

/**
 * @brief Encodes @p payload as compact UTF-8 JSON prefixed with `Content-Length: <bytes>\r\n\r\n`.
 */
QByteArray LspCoordinator::formatMessage(const QJsonObject& payload) {
  return JsonRpcFramer::encode(payload);
}

/**
 * @brief Writes a framed JSON-RPC message to `clangd` stdin if the process is running and writable.
 */
void LspCoordinator::sendPayload(const QJsonObject& payload) {
  if (!m_process) return;
  if (m_process->state() == QProcess::Starting) {
    m_process->waitForStarted(1000);
  }
  if (m_process->state() == QProcess::Running && m_process->isOpen() &&
      m_process->isWritable()) {
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
 * @brief Parses one or more `Content-Length` framed JSON-RPC messages via the bounded framer.
 */
void LspCoordinator::processRawOutput(const QByteArray& data) {
  const auto framed = m_framer.feed(data);
  for (const QString& err : framed.errors) {
    qWarning() << "[LSP]" << err;
    emit protocolError(err);
  }
  for (const QByteArray& payloadBytes : framed.payloads) {
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(payloadBytes, &err);
    if (err.error != QJsonParseError::NoError) {
      const QString msg = QStringLiteral("Failed to parse LSP JSON payload: ") + err.errorString();
      qWarning() << "[LSP]" << msg;
      emit protocolError(msg);
      continue;
    }
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
              for (const QJsonValue val : arr) {
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

#include "LspCoordinator.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonParseError>
#include <QDebug>

namespace gridlock::core {

LspCoordinator::LspCoordinator(QObject *parent) : QObject(parent), m_process(new QProcess(this)) {
    connect(m_process, &QProcess::readyReadStandardOutput, this, &LspCoordinator::readyReadStandardOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, &LspCoordinator::readyReadStandardError);
}

LspCoordinator::~LspCoordinator() {
    stop();
}

void LspCoordinator::start(const QString& targetFilePath) {
    QString targetDir = QFileInfo(targetFilePath).absolutePath();
    m_process->setWorkingDirectory(targetDir);
    m_process->start("clangd", QStringList() << "--compile-commands-dir=build" << "--header-insertion=never");
    
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

QByteArray LspCoordinator::formatMessage(const QJsonObject& payload) {
    QJsonDocument doc(payload);
    QByteArray json = doc.toJson(QJsonDocument::Compact);
    QByteArray header = QString("Content-Length: %1\r\n\r\n").arg(json.size()).toUtf8();
    return header + json;
}

void LspCoordinator::sendPayload(const QJsonObject& payload) {
    if (m_process->state() == QProcess::Running) {
        m_process->write(formatMessage(payload));
    }
}

void LspCoordinator::readyReadStandardError() {
    qDebug() << "[clangd stderr]:" << m_process->readAllStandardError();
}

void LspCoordinator::readyReadStandardOutput() {
    processRawOutput(m_process->readAllStandardOutput());
}

void LspCoordinator::processRawOutput(const QByteArray& data) {
    m_buffer.append(data);

    while (true) {
        int headerEnd = m_buffer.indexOf("\r\n\r\n");
        if (headerEnd == -1) break;

        QString headers = QString::fromUtf8(m_buffer.left(headerEnd));
        int contentLength = 0;
        
        for (const QString& line : headers.split("\r\n")) {
            if (line.startsWith("Content-Length: ")) {
                contentLength = line.mid(16).toInt();
            }
        }

        if (contentLength == 0 || m_buffer.size() < headerEnd + 4 + contentLength) {
            break; // Wait for more data
        }

        QByteArray payloadBytes = m_buffer.mid(headerEnd + 4, contentLength);
        m_buffer.remove(0, headerEnd + 4 + contentLength);

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(payloadBytes, &err);
        if (doc.isObject()) {
            processMessage(doc.object());
        }
    }
}

void LspCoordinator::processMessage(const QJsonObject& message) {
    if (message.contains("id")) {
        int id = message["id"].toInt();
        
        if (!m_isInitialized && message.contains("result")) {
            QJsonObject result = message["result"].toObject();
            if (result.contains("capabilities")) {
                m_isInitialized = true;
            }
        }
        
        if (m_hoverRequests.contains(id)) {
            QPoint pos = m_hoverRequests.take(id);
            QString markdown = "";
            if (message.contains("result")) {
                QJsonObject result = message["result"].toObject();
                if (result.contains("contents")) {
                    QJsonObject contents = result["contents"].toObject();
                    markdown = contents["value"].toString();
                }
            }
            if (!markdown.isEmpty()) {
                emit hoverResultReceived(markdown, pos);
            }
        }
    }
}

void LspCoordinator::didOpen(const QString& file, const QString& text) {
    QJsonObject textDocument;
    textDocument["uri"] = "file://" + QFileInfo(file).absoluteFilePath();
    textDocument["languageId"] = "c"; // or cpp based on extension, c for mpi_mm.c
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

void LspCoordinator::requestHover(const QString& file, int line, int character, QPoint globalPos) {
    QJsonObject textDocument;
    textDocument["uri"] = "file://" + QFileInfo(file).absoluteFilePath();

    QJsonObject position;
    position["line"] = line;
    position["character"] = character;

    QJsonObject params;
    params["textDocument"] = textDocument;
    params["position"] = position;

    int reqId = m_nextRequestId++;
    m_hoverRequests[reqId] = globalPos;

    QJsonObject req;
    req["jsonrpc"] = "2.0";
    req["id"] = reqId;
    req["method"] = "textDocument/hover";
    req["params"] = params;

    sendPayload(req);
}

} // namespace gridlock::core

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

void LspCoordinator::start() {
    m_process->start("clangd", QStringList() << "--compile-commands-dir=build" << "--header-insertion=never");
    
    QJsonObject params;
    params["processId"] = static_cast<int>(QCoreApplication::applicationPid());
    params["rootUri"] = "file://" + QDir::currentPath();
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

void LspCoordinator::sendPayload(const QJsonObject& payload) {
    QJsonDocument doc(payload);
    QByteArray json = doc.toJson(QJsonDocument::Compact);
    QByteArray header = QString("Content-Length: %1\r\n\r\n").arg(json.size()).toUtf8();
    
    m_process->write(header);
    m_process->write(json);
}

void LspCoordinator::readyReadStandardError() {
    qDebug() << "[clangd stderr]:" << m_process->readAllStandardError();
}

void LspCoordinator::readyReadStandardOutput() {
    m_buffer.append(m_process->readAllStandardOutput());

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

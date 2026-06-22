#include "DapCoordinator.hpp"
#include <QDebug>
#include <QStringList>

DapCoordinator::DapCoordinator(QObject* parent)
    : QObject(parent), m_process(new QProcess(this)) {
    
    connect(m_process, &QProcess::readyReadStandardOutput, this, &DapCoordinator::readyReadStandardOutput);
    connect(m_process, &QProcess::errorOccurred, this, &DapCoordinator::handleProcessError);
    connect(m_process, &QProcess::finished, this, &DapCoordinator::handleProcessFinished);
    connect(m_process, &QProcess::started, this, &DapCoordinator::adapterStarted);
}

DapCoordinator::~DapCoordinator() {
    stopAdapter();
}

void DapCoordinator::startAdapter(const QString& program) {
    if (m_process->state() != QProcess::NotRunning) {
        qWarning() << "DAP Adapter is already running.";
        return;
    }
    m_process->start(program, QStringList());
}

void DapCoordinator::stopAdapter() {
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
        }
    }
}

void DapCoordinator::sendRequest(const QJsonObject& request) {
    if (m_process->state() != QProcess::Running) {
        qWarning() << "Cannot send request: DAP adapter is not running.";
        return;
    }
    
    QJsonDocument doc(request);
    QByteArray jsonBytes = doc.toJson(QJsonDocument::Compact);
    
    QByteArray message;
    message.append("Content-Length: ");
    message.append(QByteArray::number(jsonBytes.size()));
    message.append("\r\n\r\n");
    message.append(jsonBytes);
    
    m_process->write(message);
}

void DapCoordinator::readyReadStandardOutput() {
    m_buffer.append(m_process->readAllStandardOutput());
    
    while (true) {
        int headerEndIndex = m_buffer.indexOf("\r\n\r\n");
        if (headerEndIndex == -1) {
            // Header not complete yet
            break;
        }
        
        // Extract header
        QByteArray headerData = m_buffer.left(headerEndIndex);
        QString headerString = QString::fromUtf8(headerData);
        
        int contentLength = 0;
        
        // Parse headers (only looking for Content-Length)
        QStringList headers = headerString.split("\r\n");
        for (const QString& header : headers) {
            if (header.startsWith("Content-Length:", Qt::CaseInsensitive)) {
                contentLength = header.mid(15).trimmed().toInt();
                break;
            }
        }
        
        if (contentLength <= 0) {
            qWarning() << "Invalid or missing Content-Length in DAP header.";
            // Recover by consuming the invalid header
            m_buffer.remove(0, headerEndIndex + 4);
            continue;
        }
        
        // Check if we have the full payload
        int totalMessageLength = headerEndIndex + 4 + contentLength;
        if (m_buffer.size() < totalMessageLength) {
            // Payload not fully received yet
            break;
        }
        
        // Extract payload
        QByteArray payloadData = m_buffer.mid(headerEndIndex + 4, contentLength);
        m_buffer.remove(0, totalMessageLength);
        
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(payloadData, &parseError);
        
        if (parseError.error != QJsonParseError::NoError) {
            qWarning() << "Failed to parse DAP JSON payload:" << parseError.errorString();
            continue;
        }
        
        if (doc.isObject()) {
            emit messageReceived(doc.object());
        }
    }
}

void DapCoordinator::handleProcessError(QProcess::ProcessError error) {
    Q_UNUSED(error);
    emit errorOccurred(m_process->errorString());
}

void DapCoordinator::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    emit adapterExited(exitCode, exitStatus);
}

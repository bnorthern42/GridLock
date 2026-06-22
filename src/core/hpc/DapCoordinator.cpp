#include "DapCoordinator.hpp"
#include <QDebug>
#include <QStringList>
#include <QJsonArray>
#include "core/managers/ConfigManager.hpp"

DapCoordinator::DapCoordinator(QObject* parent)
    : IBackendCoordinator(parent), m_process(new QProcess(this)) {
    
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

void DapCoordinator::initializeAdapter() {
    QJsonObject args;
    args["clientID"] = "gridlock";
    args["clientName"] = "GridLock IDE";
    args["adapterID"] = "lldb";
    args["linesStartAt1"] = true;
    args["columnsStartAt1"] = true;
    sendRequest("initialize", args);
}

void DapCoordinator::sendRequest(const QString& command, const QJsonObject& arguments) {
    QJsonObject request;
    request["type"] = "request";
    request["seq"] = m_sequenceNumber++;
    request["command"] = command;
    if (!arguments.isEmpty()) {
        request["arguments"] = arguments;
    }
    sendRawMessage(request);
}

void DapCoordinator::sendRawMessage(const QJsonObject& messageObj) {
    if (!isAdapterRunning()) {
        qWarning() << "Cannot send message: DAP adapter is not running.";
        return;
    }
    
    QJsonDocument doc(messageObj);
    QByteArray jsonBytes = doc.toJson(QJsonDocument::Compact);
    
    QByteArray message;
    message.append("Content-Length: ");
    message.append(QByteArray::number(jsonBytes.size()));
    message.append("\r\n\r\n");
    message.append(jsonBytes);
    
    writeToAdapter(message);
}

bool DapCoordinator::isAdapterRunning() const {
    return m_process->state() == QProcess::Running;
}

void DapCoordinator::writeToAdapter(const QByteArray& data) {
    m_process->write(data);
}

void DapCoordinator::stepOver(int threadId) {
    QJsonObject args;
    args["threadId"] = threadId;
    sendRequest("next", args);
}

void DapCoordinator::stepInto(int threadId) {
    QJsonObject args;
    args["threadId"] = threadId;
    sendRequest("stepIn", args);
}

void DapCoordinator::continueExecution(int threadId) {
    QJsonObject args;
    args["threadId"] = threadId;
    sendRequest("continue", args);
}

void DapCoordinator::pauseExecution(int threadId) {
    QJsonObject args;
    args["threadId"] = threadId;
    sendRequest("pause", args);
}

void DapCoordinator::toggleBreakpoint(const QString& file, int line) {
    if (m_breakpoints[file].contains(line)) {
        m_breakpoints[file].removeOne(line);
    } else {
        m_breakpoints[file].append(line);
    }
    
    QJsonObject args;
    QJsonObject source;
    source["path"] = file;
    args["source"] = source;
    
    QJsonArray breakpointsArray;
    for (int l : m_breakpoints[file]) {
        QJsonObject bp;
        bp["line"] = l;
        breakpointsArray.append(bp);
    }
    args["breakpoints"] = breakpointsArray;
    
    sendRequest("setBreakpoints", args);
}

void DapCoordinator::requestStackTrace(int rankId) {
    QJsonObject args;
    args["threadId"] = rankId + 1;
    args["levels"] = 1;
    m_stackTraceRequests[m_sequenceNumber] = rankId;
    sendRequest("stackTrace", args);
}

void DapCoordinator::readyReadStandardOutput() {
    processRawData(m_process->readAllStandardOutput());
}

void DapCoordinator::processRawData(const QByteArray& data) {
    m_buffer.append(data);
    
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
            handleMessage(doc.object());
        }
    }
}

void DapCoordinator::handleMessage(const QJsonObject& message) {
    QString type = message["type"].toString();
    
    if (type == "response") {
        QString command = message["command"].toString();
        if (command == "initialize" && message["success"].toBool(true)) {
            auto settings = gridlock::core::ConfigManager::instance().loadProjectSettings();
            QJsonObject launchArgs;
            launchArgs["program"] = QString::fromStdString(settings.targetBinary);
            
            QString argsStr = QString::fromStdString(settings.binaryArguments);
            QJsonArray argsArray;
            for (const QString& arg : argsStr.split(" ", Qt::SkipEmptyParts)) {
                argsArray.append(arg);
            }
            launchArgs["args"] = argsArray;
            launchArgs["cwd"] = QString::fromStdString(settings.workingDirectory);
            
            sendRequest("launch", launchArgs);
        } else if (command == "stackTrace" && message["success"].toBool()) {
            int seq = message["request_seq"].toInt();
            if (m_stackTraceRequests.contains(seq)) {
                int rankId = m_stackTraceRequests.take(seq);
                QJsonObject body = message["body"].toObject();
                QJsonArray stackFrames = body["stackFrames"].toArray();
                if (!stackFrames.isEmpty()) {
                    QJsonObject topFrame = stackFrames[0].toObject();
                    if (topFrame.contains("source")) {
                        QJsonObject source = topFrame["source"].toObject();
                        QString path = source["path"].toString();
                        int line = topFrame["line"].toInt();
                        emit locationChanged(rankId, path, line);
                    }
                }
            }
        }
    } else if (type == "event") {
        QString event = message["event"].toString();
        if (event == "initialized") {
            sendRequest("configurationDone");
        } else if (event == "stopped") {
            QJsonObject body = message["body"].toObject();
            QString reason = body["reason"].toString();
            int threadId = body["threadId"].toInt();
            int rankId = threadId - 1;
            emit executionStopped(rankId, reason);
            requestStackTrace(rankId);
        }
    }
    
    emit messageReceived(message);
}

void DapCoordinator::handleProcessError(QProcess::ProcessError error) {
    Q_UNUSED(error);
    emit errorOccurred(m_process->errorString());
}

void DapCoordinator::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    emit adapterExited(exitCode, exitStatus);
}

#include "DapCoordinator.hpp"
#include <QDebug>
#include <QStringList>
#include <QJsonArray>
#include <QRegularExpression>
#include "core/managers/ConfigManager.hpp"
#include "NativeMemoryReader.hpp"

DapCoordinator::DapCoordinator(QObject* parent)
    : IBackendCoordinator(parent), m_process(new QProcess(this)) {
    
    connect(m_process, &QProcess::readyReadStandardOutput, this, &DapCoordinator::readyReadStandardOutput);
    connect(m_process, &QProcess::errorOccurred, this, &DapCoordinator::handleProcessError);
    connect(m_process, &QProcess::finished, this, &DapCoordinator::handleProcessFinished);
    connect(m_process, &QProcess::started, this, &DapCoordinator::adapterStarted);
    connect(m_process, &QProcess::started, this, &DapCoordinator::initializeAdapter);
}

DapCoordinator::~DapCoordinator() {
    stopAdapter();
}

void DapCoordinator::startAdapter(const QString& program) {
    if (m_state != SessionState::Disconnected) {
        qWarning() << "DAP Adapter is already active or launching.";
        return;
    }
    
    if (m_process->state() != QProcess::NotRunning) {
        qWarning() << "DAP Adapter process is already running.";
        return;
    }
    
    m_state = SessionState::Launching;
    emit stateChanged(m_state);
    
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

int DapCoordinator::sendRequest(const QString& command, const QJsonObject& arguments) {
    int seq = m_sequenceNumber++;
    QJsonObject request;
    request["type"] = "request";
    request["seq"] = seq;
    request["command"] = command;
    if (!arguments.isEmpty()) {
        request["arguments"] = arguments;
    }
    sendRawMessage(request);
    return seq;
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

void DapCoordinator::launchParallelSession(const QString& binaryPath, int ranks) {
    Q_UNUSED(binaryPath);
    Q_UNUSED(ranks);
    qDebug() << "[DAP] Spawning DAP Adapter process (lldb-dap)...";
    // Arch Linux LLVM packages use 'lldb-dap'. Fallback to 'lldb-vscode' if needed.
    startAdapter("lldb-dap"); 
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

void DapCoordinator::requestScopes(int frameId, int rankId) {
    QJsonObject args;
    args["frameId"] = frameId;
    m_scopesRequests[m_sequenceNumber] = rankId;
    sendRequest("scopes", args);
}

void DapCoordinator::requestVariables(int rankId, int variablesReference) {
    QJsonObject args;
    args["variablesReference"] = variablesReference;
    m_variablesRequests[m_sequenceNumber] = rankId;
    sendRequest("variables", args);
}

void DapCoordinator::evaluateExpression(int rankId, const QString& expression) {
    QJsonObject args;
    args["expression"] = expression;
    args["context"] = "watch";
    if (m_activeFrameIds.contains(rankId)) {
        args["frameId"] = m_activeFrameIds[rankId];
    }
    m_evaluateRequests[m_sequenceNumber] = qMakePair(rankId, expression);
    sendRequest("evaluate", args);
}

void DapCoordinator::requestHeatmapRender(int rankId, const QString& expression, int rows, int cols) {
    QJsonObject args;
    args["expression"] = expression;
    args["context"] = "watch";
    if (m_activeFrameIds.contains(rankId)) {
        args["frameId"] = m_activeFrameIds[rankId];
    }
    int seq = sendRequest("evaluate", args);
    m_heatmapRequests[seq] = {rankId, rows, cols};
    qDebug() << "[Heatmap] Sent DAP evaluate request, seq:" << seq << "for" << expression;
}

void DapCoordinator::readMemory(int rankId, const QString& memoryReference, int count) {
    if (count > 1024 && m_rankToPid.contains(rankId)) {
        bool ok = false;
        uintptr_t baseAddress = 0;
        if (memoryReference.startsWith("0x", Qt::CaseInsensitive)) {
            baseAddress = memoryReference.mid(2).toULongLong(&ok, 16);
        } else {
            baseAddress = memoryReference.toULongLong(&ok, 10);
        }
        
        if (ok) {
            try {
                pid_t pid = m_rankToPid[rankId];
                std::vector<double> doubles = NativeMemoryReader::readDoubles(pid, baseAddress, count);
                QByteArray data(reinterpret_cast<const char*>(doubles.data()), doubles.size() * sizeof(double));
                emit memoryRead(rankId, memoryReference, data);
                return;
            } catch (const std::exception& e) {
                qWarning() << "Native memory read failed, falling back to DAP:" << e.what();
            }
        }
    }

    QJsonObject args;
    args["memoryReference"] = memoryReference;
    args["count"] = count;
    m_memoryRequests[m_sequenceNumber] = rankId;
    sendRequest("readMemory", args);
}

void DapCoordinator::terminateSession() {
    if (m_slurmJobId != -1) {
        qDebug() << "Executing scancel for job" << m_slurmJobId << "...";
        QProcess::execute("scancel", {QString::number(m_slurmJobId)});
    }

    QJsonObject args;
    args["terminateDebuggee"] = true;
    sendRequest("disconnect", args);
    
    if (m_process && m_process->state() == QProcess::Running) {
        if (!m_process->waitForFinished(1000)) {
            m_process->terminate();
            if (!m_process->waitForFinished(1000)) {
                m_process->kill();
            }
        }
    }

    m_slurmJobId = -1;
    m_state = SessionState::Disconnected;
    emit stateChanged(m_state);
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
    if (message["type"].toString() == "response" && message["command"].toString() == "evaluate") {
        qDebug() << "[DAP Sniffer] Evaluate Response:" << QJsonDocument(message).toJson(QJsonDocument::Compact);
    }

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
                    int frameId = topFrame["id"].toInt();
                    m_activeFrameIds[rankId] = frameId;
                    requestScopes(frameId, rankId);
                }
            }
        } else if (command == "scopes" && message["success"].toBool()) {
            int seq = message["request_seq"].toInt();
            if (m_scopesRequests.contains(seq)) {
                int rankId = m_scopesRequests.take(seq);
                QJsonArray scopes = message["body"].toObject()["scopes"].toArray();
                for (const QJsonValue& val : std::as_const(scopes)) {
                    QJsonObject scope = val.toObject();
                    if (scope["name"].toString() == "Locals") {
                        int varRef = scope["variablesReference"].toInt();
                        requestVariables(rankId, varRef);
                    } else if (scope["name"].toString() == "Registers") {
                        int varRef = scope["variablesReference"].toInt();
                        QJsonObject args;
                        args["variablesReference"] = varRef;
                        m_registersRequests[m_sequenceNumber] = rankId;
                        sendRequest("variables", args);
                    }
                }
            }
        } else if (command == "variables" && message["success"].toBool()) {
            int seq = message["request_seq"].toInt();
            if (m_variablesRequests.contains(seq)) {
                int rankId = m_variablesRequests.take(seq);
                QJsonArray variables = message["body"].toObject()["variables"].toArray();
                emit localsUpdated(rankId, variables);
            } else if (m_registersRequests.contains(seq)) {
                int rankId = m_registersRequests.take(seq);
                QJsonArray registers = message["body"].toObject()["variables"].toArray();
                emit registersUpdated(rankId, registers);
            }
        } else if (command == "readMemory" && message["success"].toBool()) {
            int seq = message["request_seq"].toInt();
            if (m_memoryRequests.contains(seq)) {
                int rankId = m_memoryRequests.take(seq);
                QJsonObject body = message["body"].toObject();
                QString address = body["address"].toString();
                QString base64Data = body["data"].toString();
                QByteArray data = QByteArray::fromBase64(base64Data.toUtf8());
                emit memoryRead(rankId, address, data);
            }
        } else if (command == "evaluate" && message["success"].toBool()) {
            int seq = message["request_seq"].toInt();
            if (m_heatmapRequests.contains(seq)) {
                auto req = m_heatmapRequests.take(seq);
                QString result = message["body"].toObject()["result"].toString();
                QRegularExpression re("(0x[0-9a-fA-F]+)");
                QRegularExpressionMatch match = re.match(result);
                if (match.hasMatch() && m_rankToPid.contains(req.rankId)) {
                    bool ok;
                    uintptr_t baseAddress = match.captured(1).toULongLong(&ok, 16);
                    if (ok) {
                        try {
                            int count = req.rows * req.cols;
                            qDebug() << "[Heatmap] LLDB Raw Evaluate Response:" << result;
                            qDebug() << "[Heatmap] Parsed Target Hex Address:" << match.captured(1);
                            std::vector<double> doubles = NativeMemoryReader::readDoubles(m_rankToPid[req.rankId], baseAddress, count);
                            emit heatmapDataReady(doubles, req.rows, req.cols);
                        } catch (const std::exception& e) {
                            qWarning() << "Heatmap memory read failed:" << e.what();
                        }
                    }
                }
            } else if (m_evaluateRequests.contains(seq)) {
                auto pair = m_evaluateRequests.take(seq);
                int rankId = pair.first;
                QString expr = pair.second;
                QString result = message["body"].toObject()["result"].toString();
                emit expressionEvaluated(rankId, expr, result);
            }
        }
    } else if (type == "event") {
        QString event = message["event"].toString();
        if (event == "initialized") {
            sendRequest("configurationDone");
            m_state = SessionState::Running;
            emit stateChanged(m_state);
        } else if (event == "stopped") {
            QJsonObject body = message["body"].toObject();
            QString reason = body["reason"].toString();
            int threadId = body["threadId"].toInt();
            int rankId = threadId - 1;
            
            m_state = SessionState::Paused;
            emit stateChanged(m_state);
            
            emit executionStopped(rankId, reason);
            requestStackTrace(rankId);
        } else if (event == "output") {
            QJsonObject body = message["body"].toObject();
            QString category = body["category"].toString();
            QString output = body["output"].toString();
            
            QRegularExpression re("(?:Submitted batch job |Granted job allocation )(\\d+)");
            QRegularExpressionMatch match = re.match(output);
            if (match.hasMatch()) {
                m_slurmJobId = match.captured(1).toInt();
                m_state = SessionState::Queued;
                emit stateChanged(m_state);
            }
            
            emit targetOutputReceived(category, output);
        } else if (event == "process") {
            QJsonObject body = message["body"].toObject();
            if (body.contains("systemProcessId")) {
                pid_t pid = body["systemProcessId"].toInt();
                m_rankToPid[m_rankToPid.size()] = pid;
            }
        }
    }
    
    emit messageReceived(message);
}

void DapCoordinator::handleProcessError(QProcess::ProcessError error) {
    qDebug() << "[DAP] FATAL: Adapter process error:" << error << "-" << m_process->errorString();
    emit errorOccurred(m_process->errorString());
}

void DapCoordinator::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    emit adapterExited(exitCode, exitStatus);
}

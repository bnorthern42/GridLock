/**
 * @file DapCoordinator.cpp
 * @brief Implementation of the Debug Adapter Protocol (DAP) coordinator for `lldb-dap`.
 *
 * Manages asynchronous `Content-Length` framed JSON-RPC messaging, thread-pool
 * JSON parsing, breakpoint synchronization, and hybrid `NativeMemoryReader`
 * cross-process memory transfers.
 */

#include "DapCoordinator.hpp"

#include <QDebug>
#include <QFuture>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QThreadPool>
#include <QtConcurrent>
#include <QtConcurrent/QtConcurrent>

#include "NativeMemoryReader.hpp"
#include "core/managers/ConfigManager.hpp"

/**
 * @brief Constructs `DapCoordinator` and connects `QProcess` stdout, error, and lifecycle signals.
 */
DapCoordinator::DapCoordinator(QObject* parent)
    : IBackendCoordinator(parent), m_process(new QProcess(this)) {
  connect(m_process, &QProcess::readyReadStandardOutput, this,
          &DapCoordinator::readyReadStandardOutput);
  connect(m_process, &QProcess::errorOccurred, this,
          &DapCoordinator::handleProcessError);
  connect(m_process, &QProcess::finished, this,
          &DapCoordinator::handleProcessFinished);
  connect(m_process, &QProcess::started, this, [this]() {
    emit adapterStarted();
    qDebug() << "[DAP] Adapter process successfully started.";
    m_state = SessionState::Running;
    emit stateChanged(m_state);
    initializeAdapter();
  });
}

/**
 * @brief Disconnects `QProcess` signals and halts the adapter process on destruction.
 */
DapCoordinator::~DapCoordinator() {
  if (m_process) {
    m_process->disconnect();
  }
  stopAdapter();
}

/**
 * @brief Spawns the DAP adapter executable if no session is currently active.
 */
void DapCoordinator::startAdapter(const QString& program) {
  if (m_state != SessionState::Disconnected) {
    qWarning() << "DAP Adapter is already active or launching.";
    return;
  }

  m_state = SessionState::Launching;
  emit stateChanged(m_state);

  qDebug() << "[DAP] Executing:" << program;
  m_process->start(program, QStringList());
}

/**
 * @brief Gracefully terminates the adapter process, escalating to `SIGKILL` after 3 seconds.
 */
void DapCoordinator::stopAdapter() {
  if (m_process->state() != QProcess::NotRunning) {
    m_process->terminate();
    if (!m_process->waitForFinished(3000)) {
      m_process->kill();
    }
  }
}

/**
 * @brief Transmits the DAP `initialize` request advertising 1-based line and column indexing.
 */
void DapCoordinator::initializeAdapter() {
  QJsonObject args;
  args["clientID"] = "gridlock";
  args["clientName"] = "GridLock IDE";
  args["adapterID"] = "lldb";
  args["linesStartAt1"] = true;
  args["columnsStartAt1"] = true;
  sendRequest("initialize", args);
}

/**
 * @brief Assigns a monotonic sequence number and sends a DAP `request` envelope.
 */
int DapCoordinator::sendRequest(const QString& command,
                                const QJsonObject& arguments) {
  // Reject non-disconnect requests once session teardown has started.
  if ((m_state == SessionState::Terminating ||
       m_state == SessionState::Disconnected) &&
      command != "disconnect") {
    return -1;
  }
  const int seq = m_sequenceNumber++;
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

/**
 * @brief Serializes @p messageObj with a `Content-Length` HTTP header and writes to the adapter.
 */
void DapCoordinator::sendRawMessage(const QJsonObject& messageObj) {
  if (m_state != SessionState::Running && m_state != SessionState::Paused &&
      m_state != SessionState::Terminating) {
    return;
  }

  qDebug() << "[DAP Sniffer] TX:"
           << QJsonDocument(messageObj).toJson(QJsonDocument::Compact);

  const QJsonDocument doc(messageObj);
  const QByteArray jsonBytes = doc.toJson(QJsonDocument::Compact);

  QByteArray message;
  message.append("Content-Length: ");
  message.append(QByteArray::number(jsonBytes.size()));
  message.append("\r\n\r\n");
  message.append(jsonBytes);

  writeToAdapter(message);
}

/**
 * @brief Returns `true` when the child `QProcess` is in `QProcess::Running` state.
 */
bool DapCoordinator::isAdapterRunning() const {
  return m_process->state() == QProcess::Running;
}

/**
 * @brief Writes raw framed bytes to the child adapter's standard input.
 */
void DapCoordinator::writeToAdapter(const QByteArray& data) {
  m_process->write(data);
}

/**
 * @brief Steps over the current source line on @p threadId via DAP `next`.
 */
void DapCoordinator::stepOver(int threadId) {
  QJsonObject args;
  args["threadId"] = threadId;
  sendRequest("next", args);
}

/**
 * @brief Steps into the target function call on @p threadId via DAP `stepIn`.
 */
void DapCoordinator::stepInto(int threadId) {
  QJsonObject args;
  args["threadId"] = threadId;
  sendRequest("stepIn", args);
}

/**
 * @brief Resumes execution on @p threadId via DAP `continue`.
 */
void DapCoordinator::continueExecution(int threadId) {
  QJsonObject args;
  args["threadId"] = threadId;
  sendRequest("continue", args);
}

/**
 * @brief Pauses execution on @p threadId via DAP `pause`.
 */
void DapCoordinator::pauseExecution(int threadId) {
  QJsonObject args;
  args["threadId"] = threadId;
  sendRequest("pause", args);
}

/**
 * @brief Resolves `lldb-dap` (or fallback `lldb-vscode`) and starts a parallel debugging session.
 */
void DapCoordinator::launchParallelSession(const QString& binaryPath,
                                           int ranks) {
  m_processCount = ranks;
  m_currentBinaryPath = binaryPath;
  qDebug() << "[DAP] Spawning DAP Adapter process for binary:" << binaryPath;

  QString adapterProgram = QStandardPaths::findExecutable("lldb-dap");
  if (adapterProgram.isEmpty()) {
    adapterProgram = QStandardPaths::findExecutable("lldb-vscode");
  }

  if (adapterProgram.isEmpty()) {
    qDebug() << "[DAP] FATAL: Could not find lldb-dap or lldb-vscode in PATH!";
    return;
  }

  startAdapter(adapterProgram);
}

/**
 * @brief Toggles a line breakpoint in @p file at @p line and sends the updated `setBreakpoints` list.
 */
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

/**
 * @brief Requests the top stack frame (`levels = 1`) for `threadId = rankId + 1`.
 */
void DapCoordinator::requestStackTrace(int rankId) {
  QJsonObject args;
  args["threadId"] = rankId + 1;
  args["levels"] = 1;
  m_stackTraceRequests[m_sequenceNumber] = rankId;
  sendRequest("stackTrace", args);
}

/**
 * @brief Requests lexical scopes (`Locals`, `Registers`, etc.) for @p frameId on @p rankId.
 */
void DapCoordinator::requestScopes(int frameId, int rankId) {
  QJsonObject args;
  args["frameId"] = frameId;
  m_scopesRequests[m_sequenceNumber] = rankId;
  sendRequest("scopes", args);
}

/**
 * @brief Requests child variables belonging to @p variablesReference on @p rankId.
 */
void DapCoordinator::requestVariables(int rankId, int variablesReference) {
  QJsonObject args;
  args["variablesReference"] = variablesReference;
  m_variablesRequests[m_sequenceNumber] =
      qMakePair(rankId, variablesReference);
  sendRequest("variables", args);
}

/**
 * @brief Evaluates @p expression in the `"watch"` context of @p rankId's active stack frame.
 */
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

/**
 * @brief Evaluates pointer @p expression to a base address and triggers asynchronous heatmap loading.
 */
void DapCoordinator::requestHeatmapRender(int rankId,
                                          const QString& expression,
                                          int rows,
                                          int cols) {
  if (!isAdapterRunning()) {
    qWarning() << "[Heatmap] Aborting request: DAP adapter is not running.";
    return;
  }

  QJsonObject args;
  args["expression"] = expression;
  args["context"] = "watch";
  if (m_activeFrameIds.contains(rankId)) {
    args["frameId"] = m_activeFrameIds[rankId];
  }
  const int seq = sendRequest("evaluate", args);
  m_heatmapRequests[seq] = {rankId, rows, cols};
  qDebug() << "[Heatmap] Sent DAP evaluate request, seq:" << seq << "for"
           << expression;
}

/**
 * @brief Reads @p count elements/bytes at @p memoryReference on @p rankId.
 *
 * Uses `NativeMemoryReader::readDoubles` via `process_vm_readv` when `count > 1024`
 * and a local PID mapping is available, falling back to DAP `readMemory` otherwise.
 */
void DapCoordinator::readMemory(int rankId,
                                const QString& memoryReference,
                                int count) {
  // Fast path: bypass base64 JSON serialization for large local memory reads.
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
        const pid_t pid = m_rankToPid[rankId];
        const std::vector<double> doubles =
            NativeMemoryReader::readDoubles(pid, baseAddress, count);
        const QByteArray data(reinterpret_cast<const char*>(doubles.data()),
                              static_cast<qsizetype>(doubles.size() * sizeof(double)));
        emit memoryRead(rankId, memoryReference, data);
        return;
      } catch (const std::exception& e) {
        qWarning() << "Native memory read failed, falling back to DAP:"
                   << e.what();
      }
    }
  }

  // Standard DAP fallback path using `readMemory`.
  QJsonObject args;
  args["memoryReference"] = memoryReference;
  args["count"] = count;
  m_memoryRequests[m_sequenceNumber] = rankId;
  sendRequest("readMemory", args);
}

/**
 * @brief Cancels any active SLURM allocation, sends DAP `disconnect`, and stops the adapter.
 */
void DapCoordinator::terminateSession() {
  m_state = SessionState::Terminating;
  emit stateChanged(m_state);

  // Cancel the remote SLURM batch job if one was captured from session stdout.
  if (m_slurmJobId != -1) {
    qDebug() << "Executing scancel for job" << m_slurmJobId << "...";
    QProcess::execute("scancel", {QString::number(m_slurmJobId)});
  }

  QJsonObject args;
  args["terminateDebuggee"] = true;
  sendRequest("disconnect", args);

  // Wait briefly for clean exit before sending SIGTERM / SIGKILL.
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

/**
 * @brief Drains available stdout bytes from the adapter into `processRawData()`.
 */
void DapCoordinator::readyReadStandardOutput() {
  processRawData(m_process->readAllStandardOutput());
}

/**
 * @brief Incrementally extracts `Content-Length` framed JSON payloads and parses them on `QThreadPool`.
 */
void DapCoordinator::processRawData(const QByteArray& data) {
  m_buffer.append(data);

  while (true) {
    const int headerEndIndex = m_buffer.indexOf("\r\n\r\n");
    if (headerEndIndex == -1) {
      // Header delimiter `\r\n\r\n` has not arrived yet.
      break;
    }

    // Extract the ASCII header block preceding `\r\n\r\n`.
    const QByteArray headerData = m_buffer.left(headerEndIndex);
    const QString headerString = QString::fromUtf8(headerData);

    int contentLength = 0;

    // Parse the `Content-Length` header line (case-insensitive).
    const QStringList headers = headerString.split("\r\n");
    for (const QString& header : headers) {
      if (header.startsWith("Content-Length:", Qt::CaseInsensitive)) {
        contentLength = header.mid(15).trimmed().toInt();
        break;
      }
    }

    if (contentLength <= 0) {
      qWarning() << "Invalid or missing Content-Length in DAP header.";
      // Recover stream synchronization by discarding the malformed header block.
      m_buffer.remove(0, headerEndIndex + 4);
      continue;
    }

    // Verify that the entire JSON body has arrived in `m_buffer`.
    const int totalMessageLength = headerEndIndex + 4 + contentLength;
    if (m_buffer.size() < totalMessageLength) {
      break;
    }

    const QByteArray payloadData =
        m_buffer.mid(headerEndIndex + 4, contentLength);
    m_buffer.remove(0, totalMessageLength);

    // Offload JSON deserialization to the global thread pool and marshal back to the UI thread.
    QThreadPool::globalInstance()->start([this, payloadData]() {
      QJsonParseError parseError;
      const QJsonDocument doc =
          QJsonDocument::fromJson(payloadData, &parseError);

      if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Failed to parse DAP JSON payload:"
                   << parseError.errorString();
        return;
      }

      if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        QMetaObject::invokeMethod(
            this, [this, obj]() { handleMessage(obj); },
            Qt::QueuedConnection);
      }
    });
  }
}

/**
 * @brief Routes incoming DAP `response` and `event` objects to the corresponding state handlers.
 * @param message Deserialized DAP JSON envelope (`type`: `"response"` or `"event"`).
 */
void DapCoordinator::handleMessage(const QJsonObject& message) {
  // Log incoming DAP traffic for protocol debugging and evaluate inspection.
  if (message["type"].toString() == "response" &&
      message["command"].toString() == "evaluate") {
    qDebug() << "[DAP Sniffer] Evaluate Response:"
             << QJsonDocument(message).toJson(QJsonDocument::Compact);
  } else {
    qDebug() << "[DAP Sniffer] RX:"
             << QJsonDocument(message).toJson(QJsonDocument::Compact);
  }

  const QString type = message["type"].toString();

  if (type == "response") {
    const QString command = message["command"].toString();
    if (command == "initialize" && message["success"].toBool(true)) {
      // Upon `initialize` response, send `launch` configured from `ProjectSettings`.
      // Fall back to `settings.targetBinary` if no explicit binary override was passed.
      const auto settings =
          gridlock::core::ConfigManager::instance().loadProjectSettings();
      QJsonObject launchArgs;
      QString programToLaunch = m_currentBinaryPath;
      if (programToLaunch.isEmpty()) {
        programToLaunch = QString::fromStdString(settings.targetBinary);
      }
      launchArgs["program"] = programToLaunch;
      launchArgs["stopOnEntry"] = false;

      // Tokenize space-delimited target arguments into a JSON array.
      const QString argsStr = QString::fromStdString(settings.programArguments);
      QJsonArray argsArray;
      for (const QString& arg : argsStr.split(" ", Qt::SkipEmptyParts)) {
        argsArray.append(arg);
      }
      launchArgs["args"] = argsArray;

      // Tokenize space-delimited environment variable assignments (`KEY=VALUE`).
      const QString envStr =
          QString::fromStdString(settings.environmentVariables);
      QJsonArray envArray;
      for (const QString& envVar : envStr.split(" ", Qt::SkipEmptyParts)) {
        envArray.append(envVar);
      }
      if (!envArray.isEmpty()) {
        launchArgs["env"] = envArray;
      }

      launchArgs["cwd"] = QString::fromStdString(settings.workingDirectory);

      sendRequest("launch", launchArgs);
    } else if (command == "stackTrace" && message["success"].toBool()) {
      // Extract the active frame ID, source path, and line number for the stopped rank.
      // Only frame 0 (the innermost executing frame) is cached for subsequent scope queries.
      const int seq = message["request_seq"].toInt();
      if (m_stackTraceRequests.contains(seq)) {
        const int rankId = m_stackTraceRequests.take(seq);
        const QJsonObject body = message["body"].toObject();
        const QJsonArray stackFrames = body["stackFrames"].toArray();
        if (!stackFrames.isEmpty()) {
          const QJsonObject topFrame = stackFrames[0].toObject();
          const int frameId = topFrame["id"].toInt();
          m_activeFrameIds[rankId] = frameId;

          // Resolve source file path and 1-based line coordinate if symbols are available.
          QString path = "";
          int line = 0;
          if (topFrame.contains("source")) {
            const QJsonObject source = topFrame["source"].toObject();
            path = source["path"].toString();
          }
          if (topFrame.contains("line")) {
            line = topFrame["line"].toInt();
          }
          emit locationChanged(rankId, path, line);
        }
      }
    } else if (command == "scopes" && message["success"].toBool()) {
      // Match the response `request_seq` to the requesting rank and emit its scope descriptors
      // (typically "Locals", "Arguments", and "Registers").
      const int seq = message["request_seq"].toInt();
      if (m_scopesRequests.contains(seq)) {
        const int rankId = m_scopesRequests.take(seq);
        const QJsonArray scopes =
            message["body"].toObject()["scopes"].toArray();
        emit scopesUpdated(rankId, scopes);
      }
    } else if (command == "variables" && message["success"].toBool()) {
      // Route the returned `variables` array to either the Local Variables tree model
      // or the CPU Register inspector depending on which request map owns `request_seq`.
      const int seq = message["request_seq"].toInt();
      if (m_variablesRequests.contains(seq)) {
        const auto pair = m_variablesRequests.take(seq);
        const int rankId = pair.first;
        const int varRef = pair.second;
        const QJsonArray variables =
            message["body"].toObject()["variables"].toArray();
        emit localsUpdated(rankId, varRef, variables);
      } else if (m_registersRequests.contains(seq)) {
        const int rankId = m_registersRequests.take(seq);
        const QJsonArray registers =
            message["body"].toObject()["variables"].toArray();
        emit registersUpdated(rankId, registers);
      }
    } else if (command == "readMemory" && message["success"].toBool()) {
      // Decode base64-encoded memory bytes from the DAP `readMemory` response
      // and broadcast the raw byte buffer to `MemView`.
      const int seq = message["request_seq"].toInt();
      if (m_memoryRequests.contains(seq)) {
        const int rankId = m_memoryRequests.take(seq);
        const QJsonObject body = message["body"].toObject();
        const QString address = body["address"].toString();
        const QString base64Data = body["data"].toString();
        const QByteArray data = QByteArray::fromBase64(base64Data.toUtf8());
        emit memoryRead(rankId, address, data);
      }
    } else if (command == "evaluate" && message["success"].toBool()) {
      // Distinguish between background 2D heatmap pointer evaluations and interactive watch expressions.
      const int seq = message["request_seq"].toInt();
      if (m_heatmapRequests.contains(seq)) {
        // Parse the evaluated pointer hex address and trigger a background `process_vm_readv` copy
        // so large double-precision matrices never block the Qt GUI thread or saturate the JSON pipe.
        const auto req = m_heatmapRequests.take(seq);
        const QString result = message["body"].toObject()["result"].toString();
        static const QRegularExpression re("(0x[0-9a-fA-F]+)");
        const QRegularExpressionMatch match = re.match(result);
        if (match.hasMatch() && m_rankToPid.contains(req.rankId)) {
          bool ok = false;
          const uintptr_t baseAddress =
              match.captured(1).toULongLong(&ok, 16);
          if (ok) {
            try {
              (void)QtConcurrent::run([this, pid = m_rankToPid[req.rankId],
                                       baseAddress,
                                       count = req.rows * req.cols,
                                       rows = req.rows, cols = req.cols]() {
                try {
                  const std::vector<double> doubles =
                      NativeMemoryReader::readDoubles(pid, baseAddress, count);
                  emit heatmapDataReady(doubles, rows, cols);
                } catch (...) {
                  // Silently drop transient ptrace/process_vm_readv read failures on exiting ranks.
                }
              });
            } catch (const std::exception& e) {
              qWarning() << "Heatmap memory read failed:" << e.what();
            }
          }
        }
      } else if (m_evaluateRequests.contains(seq)) {
        // Forward interactive watch expression results to the UI console/evaluator.
        const auto pair = m_evaluateRequests.take(seq);
        const int rankId = pair.first;
        const QString expr = pair.second;
        const QString result = message["body"].toObject()["result"].toString();
        emit expressionEvaluated(rankId, expr, result);
      }
    }
  } else if (type == "event") {
    // Handle asynchronous adapter notifications (`initialized`, `stopped`, `output`, `process`).
    const QString event = message["event"].toString();
    if (event == "initialized") {
      // Flush all registered breakpoints followed by `configurationDone` to release execution.
      for (auto it = m_breakpoints.constBegin();
           it != m_breakpoints.constEnd(); ++it) {
        if (!it.value().isEmpty()) {
          QJsonObject args;
          QJsonObject source;
          source["path"] = it.key();
          args["source"] = source;

          QJsonArray breakpointsArray;
          for (int l : *it) {
            QJsonObject bp;
            bp["line"] = l;
            breakpointsArray.append(bp);
          }
          args["breakpoints"] = breakpointsArray;
          sendRequest("setBreakpoints", args);
        }
      }
      sendRequest("configurationDone");
      m_state = SessionState::Running;
      emit stateChanged(m_state);
    } else if (event == "stopped") {
      // Map 1-based DAP `threadId` back to 0-based MPI `rankId` and request top stack frame.
      const QJsonObject body = message["body"].toObject();
      const QString reason = body["reason"].toString();
      const int threadId = body["threadId"].toInt();
      const int rankId = threadId - 1;

      m_state = SessionState::Paused;
      emit stateChanged(m_state);

      emit executionStopped(rankId, reason);
      requestStackTrace(rankId);
    } else if (event == "output") {
      // Inspect target output for SLURM job allocation banners before forwarding to UI.
      // When `salloc`/`sbatch` emits a job ID, transition the session to `SessionState::Queued`.
      const QJsonObject body = message["body"].toObject();
      const QString category = body["category"].toString();
      const QString output = body["output"].toString();

      static const QRegularExpression re(
          "(?:Submitted batch job |Granted job allocation )(\\d+)");
      const QRegularExpressionMatch match = re.match(output);
      if (match.hasMatch()) {
        m_slurmJobId = match.captured(1).toInt();
        m_state = SessionState::Queued;
        emit stateChanged(m_state);
      }

      emit targetOutputReceived(category, output);
    } else if (event == "process") {
      // Record the OS `systemProcessId` so `NativeMemoryReader` can attach via `process_vm_readv`.
      const QJsonObject body = message["body"].toObject();
      if (body.contains("systemProcessId")) {
        const pid_t pid = body["systemProcessId"].toInt();
        m_rankToPid[m_rankToPid.size()] = pid;
      }
    }
  }

  // Always emit the raw JSON envelope for diagnostic logging and protocol unit tests.
  emit messageReceived(message);
}

/**
 * @brief Logs `QProcess` transport errors and transitions the session to `Disconnected`.
 * @param error The `QProcess::ProcessError` code reported by the OS.
 */
void DapCoordinator::handleProcessError(QProcess::ProcessError error) {
  qDebug() << "[DAP] FATAL: Adapter process error:" << error << "-"
           << m_process->errorString();
  emit errorOccurred(m_process->errorString());
  m_state = SessionState::Disconnected;
  emit stateChanged(m_state);
}

/**
 * @brief Emits `adapterExited` and resets session state when the adapter process terminates.
 * @param exitCode Process exit status code.
 * @param exitStatus Normal vs. crash termination status.
 */
void DapCoordinator::handleProcessFinished(int exitCode,
                                           QProcess::ExitStatus exitStatus) {
  emit adapterExited(exitCode, exitStatus);
  m_state = SessionState::Disconnected;
  emit stateChanged(m_state);
}


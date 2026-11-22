/**
 * @file DapCoordinator.cpp
 * @brief DAP adapter process lifecycle, `Content-Length` framing, and response/event dispatch.
 *
 * Manages the `lldb-dap` child process, incremental `Content-Length` JSON-RPC
 * stream parsing, session shutdown/teardown, and incoming `response`/`event`
 * state transitions.
 */

#include "DapCoordinator.hpp"

#include <csignal>

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStringList>
#include <QtConcurrent/QtConcurrent>

#include "../memory/NativeMemoryReader.hpp"
#include "core/managers/ConfigManager.hpp"

/**
 * @brief Constructs `DapCoordinator` and connects `QProcess` stdout, error, and lifecycle signals.
 */
DapCoordinator::DapCoordinator(QObject* parent)
    : IBackendCoordinator(parent), m_process(new QProcess(this)) {
  bindProcessSignals();
}

void DapCoordinator::bindProcessSignals() {
  if (!m_process) return;
  m_process->disconnect();
  connect(m_process, &QProcess::readyReadStandardOutput, this, &DapCoordinator::readyReadStandardOutput);
  connect(m_process, &QProcess::errorOccurred, this, &DapCoordinator::handleProcessError);
  connect(m_process, &QProcess::finished, this, &DapCoordinator::handleProcessFinished);
  connect(m_process, &QProcess::started, this, [this]() {
    if (m_shuttingDown) return;
    emit adapterStarted();
    m_state = SessionState::Running;
    emit stateChanged(m_state);
    initializeAdapter();
  });
}

/** @brief Disconnects `QProcess` signals and halts the adapter process on destruction without teardown signals. */
DapCoordinator::~DapCoordinator() {
  m_shuttingDown = true;
  QSignalBlocker blocker(this);
  stopSession(true);
}

/** @brief Spawns the DAP adapter executable if no session is currently active. */
void DapCoordinator::startAdapter(const QString& program) {
  if (m_state != SessionState::Disconnected) return;
  m_shuttingDown = false;
  bindProcessSignals();
  m_state = SessionState::Launching;
  emit stateChanged(m_state);
  const QString cwd = !workingDirectory().trimmed().isEmpty()
                          ? workingDirectory().trimmed()
                          : QString::fromStdString(gridlock::core::ConfigManager::instance().loadProjectSettings().workingDirectory).trimmed();
  if (!cwd.isEmpty() && QFileInfo(cwd).isDir()) m_process->setWorkingDirectory(cwd);
  m_process->start(program, QStringList());
}

/** @brief Gracefully terminates the adapter process with bounded 250ms steps, escalating to `SIGKILL`. */
void DapCoordinator::stopAdapter() {
  if (!m_process) return;
  m_process->disconnect();
  if (m_process->state() == QProcess::Starting) m_process->waitForStarted(250);
  if (m_process->state() != QProcess::NotRunning && !m_process->waitForFinished(250)) {
    m_process->terminate();
    if (!m_process->waitForFinished(250)) { m_process->kill(); m_process->waitForFinished(250); }
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
  return m_process && m_process->state() == QProcess::Running &&
         m_process->isOpen() && m_process->isWritable();
}

/**
 * @brief Writes raw framed bytes to the child adapter's standard input if open and writable.
 */
void DapCoordinator::writeToAdapter(const QByteArray& data) {
  if (!m_process) return;
  if (m_process->state() == QProcess::Starting) {
    m_process->waitForStarted(1000);
  }
  if (m_process->state() != QProcess::Running || !m_process->isOpen() ||
      !m_process->isWritable()) {
    return;
  }
  m_process->write(data);
}

/** @brief Cancels any active SLURM allocation, sends DAP `disconnect`, and stops the adapter. */
void DapCoordinator::terminateSession() { stopSession(m_shuttingDown); }

void DapCoordinator::stopSession(bool silentShutdown) {
  m_shuttingDown = true;
  m_state = SessionState::Terminating;
  if (!silentShutdown) emit stateChanged(m_state);
  if (m_slurmJobId != -1) QProcess::execute("scancel", {QString::number(m_slurmJobId)});
  const bool hadRunningAdapter = m_process && m_process->state() != QProcess::NotRunning;
  if (m_process) m_process->disconnect();
  if (isAdapterRunning()) sendRequest("disconnect", QJsonObject{{"terminateDebuggee", true}});
  if (m_process) {
    if (m_process->state() == QProcess::Starting) m_process->waitForStarted(250);
    if (m_process->state() != QProcess::NotRunning && !m_process->waitForFinished(250)) {
      m_process->terminate();
      if (!m_process->waitForFinished(250)) { m_process->kill(); m_process->waitForFinished(250); }
    }
  }
  if (hadRunningAdapter && m_pendingAttachPid == 0) {
    const pid_t selfPid = static_cast<pid_t>(QCoreApplication::applicationPid());
    for (auto it = m_rankToPid.constBegin(); it != m_rankToPid.constEnd(); ++it)
      if (it.value() > 0 && it.value() != selfPid && ::kill(it.value(), 0) == 0) ::kill(it.value(), SIGTERM);
  }
  m_slurmJobId = -1; m_processCount = 0; m_pendingAttachPid = 0; m_buffer.clear();
  m_rankToPid.clear(); m_activeFrameIds.clear(); m_activeThreadIds.clear();
  m_rankFrames.clear(); m_rankThreads.clear(); m_state = SessionState::Disconnected;
  if (!silentShutdown) {
    m_shuttingDown = false;
    bindProcessSignals();
    emit stackTraceUpdated(0, {});
    emit threadsUpdated(0, {});
    emit stateChanged(m_state);
  }
}

/** @brief Drains available stdout bytes from the adapter into `processRawData()`. */
void DapCoordinator::readyReadStandardOutput() {
  if (!m_shuttingDown && m_process) processRawData(m_process->readAllStandardOutput());
}

/** @brief Incrementally extracts `Content-Length` framed JSON payloads and dispatches them in strict FIFO order. */
void DapCoordinator::processRawData(const QByteArray& data) {
  if (m_shuttingDown) return;
  // Append newly arrived adapter bytes into the rolling stream buffer.
  m_buffer.append(data);
  while (!m_shuttingDown) {
    // Locate the HTTP-style `\r\n\r\n` separator bounding the DAP header block.
    const int headerEndIndex = m_buffer.indexOf("\r\n\r\n");
    if (headerEndIndex == -1) break;

    // Parse the `Content-Length` header line (case-insensitive) to determine body byte length.
    const QString headerString = QString::fromUtf8(m_buffer.left(headerEndIndex));
    int contentLength = 0;
    for (const QString& header : headerString.split("\r\n")) {
      if (header.startsWith("Content-Length:", Qt::CaseInsensitive)) {
        contentLength = header.mid(15).trimmed().toInt();
        break;
      }
    }
    if (contentLength <= 0) {
      // Recover stream synchronization by discarding malformed header blocks.
      qWarning() << "Invalid or missing Content-Length in DAP header.";
      m_buffer.remove(0, headerEndIndex + 4);
      continue;
    }

    // Defer parsing until the complete UTF-8 JSON body is buffered in memory.
    const int totalMessageLength = headerEndIndex + 4 + contentLength;
    if (m_buffer.size() < totalMessageLength) break;
    const QByteArray payloadData = m_buffer.mid(headerEndIndex + 4, contentLength);
    m_buffer.remove(0, totalMessageLength);

    // Parse and dispatch each frame sequentially so Frame k+1 can never overtake Frame k.
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(payloadData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
      qWarning() << "Failed to parse DAP JSON payload:" << parseError.errorString();
      continue;
    }
    if (doc.isObject()) {
      handleMessage(doc.object());
    }
  }
}

/**
 * @brief Routes incoming DAP `response` and `event` objects to the corresponding state handlers.
 * @param message Deserialized DAP JSON envelope (`type`: `"response"` or `"event"`).
 */
void DapCoordinator::handleMessage(const QJsonObject& message) {
  // Log incoming DAP traffic for protocol debugging and REPL/watch evaluation inspection.
  if (message["type"].toString() == "response" && message["command"].toString() == "evaluate") {
    qDebug() << "[DAP Sniffer] Evaluate Response:" << QJsonDocument(message).toJson(QJsonDocument::Compact);
  } else {
    qDebug() << "[DAP Sniffer] RX:" << QJsonDocument(message).toJson(QJsonDocument::Compact);
  }

  const QString type = message["type"].toString();
  if (type == "response") {
    // Dispatch adapter command responses (`initialize`, `stackTrace`, `threads`, `scopes`, `variables`, `evaluate`, `dataBreakpointInfo`).
    const QString command = message["command"].toString();
    if ((command == "attach" || command == "launch") && !message["success"].toBool(true)) {
      const QString errMsg = message["message"].toString(QStringLiteral("DAP %1 failed").arg(command));
      terminateSession(); emit sessionErrorOccurred(errMsg); emit messageReceived(message); return;
    }
    if (command == "initialize" && message["success"].toBool(true)) {
      // Dispatch live PID `attach` when `attachToProcess(pid)` initiated this session.
      if (m_pendingAttachPid > 0) {
        const qint64 attachPid = m_pendingAttachPid;
        m_pendingAttachPid = 0;
        sendRequest("attach", QJsonObject{{"pid", attachPid}, {"stopOnEntry", true}});
        emit messageReceived(message);
        return;
      }
      // Upon `initialize` response, send `launch` configured from execution context / `ProjectSettings`.
      const auto settings = gridlock::core::ConfigManager::instance().loadProjectSettings();
      QJsonObject launchArgs;
      const QString programToLaunch = m_currentBinaryPath.isEmpty() ? QString::fromStdString(settings.targetBinary) : m_currentBinaryPath;
      launchArgs["program"] = programToLaunch;
      launchArgs["stopOnEntry"] = false;

      // Tokenize target CLI arguments (preserving quoted strings) and environment assignments.
      QString rawArgs = targetArguments().trimmed();
      if (rawArgs.isEmpty()) rawArgs = !settings.targetArguments.isEmpty() ? settings.targetArguments.trimmed() : QString::fromStdString(settings.programArguments).trimmed();
      QJsonArray argsArray;
      for (const QString& arg : QProcess::splitCommand(rawArgs)) argsArray.append(arg);
      launchArgs["args"] = argsArray;
      QJsonArray envArray;
      for (const QString& envVar : QString::fromStdString(settings.environmentVariables).split(" ", Qt::SkipEmptyParts)) envArray.append(envVar);
      if (!envArray.isEmpty()) launchArgs["env"] = envArray;
      QString cwd = workingDirectory().trimmed();
      if (cwd.isEmpty()) cwd = QString::fromStdString(settings.workingDirectory).trimmed();
      if (cwd.isEmpty()) cwd = gridlock::core::ConfigManager::instance().workspacePath().trimmed();
      if (cwd.isEmpty()) cwd = QFileInfo(programToLaunch).absolutePath();
      launchArgs["cwd"] = cwd;
      sendRequest("launch", launchArgs);
    } else if (command == "stackTrace" && message["success"].toBool()) {
      // Parse up to 64 unwound stack frames, cache frame descriptors, and broadcast `stackTraceUpdated`.
      const int seq = message["request_seq"].toInt();
      if (m_stackTraceRequests.contains(seq)) {
        const int rankId = m_stackTraceRequests.take(seq);
        const QJsonArray stackFrames = message["body"].toObject()["stackFrames"].toArray();
        QVector<StackFrameEntry> frames; frames.reserve(stackFrames.size());
        // Convert each DAP `StackFrame` object into a `StackFrameEntry` with DWARF path and RIP address.
        for (int idx = 0; idx < stackFrames.size(); ++idx) {
          const QJsonObject fObj = stackFrames.at(idx).toObject();
          StackFrameEntry entry; entry.level = idx; entry.frameId = fObj["id"].toInt();
          entry.function = fObj["name"].toString(QStringLiteral("??"));
          if (fObj.contains("source")) entry.file = fObj["source"].toObject()["path"].toString();
          entry.line = fObj["line"].toInt(0);
          const QString ipRef = fObj["instructionPointerReference"].toString();
          if (ipRef.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) { bool ok = false; entry.address = ipRef.mid(2).toULongLong(&ok, 16); }
          frames.append(entry);
        }
        // Cache frames and publish `stackTraceUpdated` + top-frame `locationChanged`.
        m_rankFrames[rankId] = frames;
        emit stackTraceUpdated(rankId, frames);
        if (!frames.isEmpty()) { m_activeFrameIds[rankId] = frames.front().frameId; emit locationChanged(rankId, frames.front().file, frames.front().line); }
      }
    } else if (command == "threads" && message["success"].toBool()) {
      // Parse live OS/OpenMP threads from DAP `threads` response and emit `threadsUpdated`.
      const QJsonArray rawThreads = message["body"].toObject()["threads"].toArray();
      const int curThread = m_activeThreadIds.value(0, 1);
      QVector<ThreadEntry> threads; threads.reserve(rawThreads.size());
      for (const QJsonValue& tv : rawThreads) {
        const QJsonObject tObj = tv.toObject();
        ThreadEntry th; th.threadId = tObj["id"].toInt(1); th.name = tObj["name"].toString(QStringLiteral("main"));
        th.state = QStringLiteral("stopped"); th.isCurrent = (th.threadId == curThread);
        threads.append(th);
      }
      m_rankThreads[0] = threads;
      emit threadsUpdated(0, threads);
    } else if (command == "scopes" && message["success"].toBool()) {
      // Match the response `request_seq` to the requesting rank and emit its scope descriptors.
      const int seq = message["request_seq"].toInt();
      if (m_scopesRequests.contains(seq)) {
        const int rankId = m_scopesRequests.take(seq);
        const QJsonArray scopes = message["body"].toObject()["scopes"].toArray();
        emit scopesUpdated(rankId, scopes);
        // Automatically query the "Registers" scope so RegisterView receives live CPU registers.
        for (const QJsonValue& scVal : scopes) {
          const QJsonObject scObj = scVal.toObject();
          if (scObj["name"].toString().contains("Register", Qt::CaseInsensitive)) {
            const int regRef = scObj["variablesReference"].toInt();
            if (regRef > 0) { m_registersRequests[m_sequenceNumber] = rankId; sendRequest("variables", QJsonObject{{"variablesReference", regRef}}); }
          }
        }
      }
    } else if (command == "variables" && message["success"].toBool()) {
      // Route the returned `variables` array to VariableTreeModel children, Locals, or CPU Registers.
      const int seq = message["request_seq"].toInt(); const QJsonArray vars = message["body"].toObject()["variables"].toArray();
      if (m_varChildRequests.contains(seq)) {
        const auto cr = m_varChildRequests.take(seq); QVariantList children;
        for (const QJsonValue& v : vars) { const QJsonObject o = v.toObject(); const int vr = o["variablesReference"].toInt(0); const QString val = o["value"].toString();
          children.append(QVariantMap{{"name", o["name"].toString()}, {"value", val}, {"type", o["type"].toString()}, {"hasChildren", vr > 0 || val.trimmed().startsWith('{')}, {"variablesReference", vr}}); }
        emit variableChildrenUpdated(cr.first, cr.second, children);
      }
      if (m_variablesRequests.contains(seq)) { const auto pair = m_variablesRequests.take(seq); emit localsUpdated(pair.first, pair.second, vars); }
      else if (m_registersRequests.contains(seq)) emit registersUpdated(m_registersRequests.take(seq), vars);
    } else if (command == "readMemory" && message["success"].toBool()) {
      // Decode base64-encoded memory bytes from the DAP `readMemory` response for `MemView`.
      const int seq = message["request_seq"].toInt();
      if (m_memoryRequests.contains(seq)) {
        const int rankId = m_memoryRequests.take(seq);
        const QJsonObject body = message["body"].toObject();
        emit memoryRead(rankId, body["address"].toString(), QByteArray::fromBase64(body["data"].toString().toUtf8()));
      }
    } else if (command == "evaluate") {
      // Route interactive REPL console queries, heatmap pointer reads, and watch expressions.
      const int seq = message["request_seq"].toInt();
      const bool okResp = message["success"].toBool();
      const QString result = message["body"].toObject()["result"].toString();
      if (m_replRequests.contains(seq)) {
        // Forward REPL output or adapter error message directly to `GdbConsoleWidget`.
        const int rankId = m_replRequests.take(seq);
        emit debuggerConsoleOutput(rankId, okResp ? result : message["message"].toString(QStringLiteral("Evaluation error")), !okResp);
      } else if (okResp && m_heatmapRequests.contains(seq)) {
        // Extract base hex pointer from expression result and stream doubles via `process_vm_readv`.
        const auto req = m_heatmapRequests.take(seq);
        static const QRegularExpression re("(0x[0-9a-fA-F]+)");
        const QRegularExpressionMatch match = re.match(result);
        if (match.hasMatch() && m_rankToPid.contains(req.rankId)) {
          bool ok = false;
          const uintptr_t baseAddress = match.captured(1).toULongLong(&ok, 16);
          if (ok) {
            try {
              (void)QtConcurrent::run([this, pid = m_rankToPid[req.rankId], baseAddress, count = req.rows * req.cols, rows = req.rows, cols = req.cols]() {
                try { emit heatmapDataReady(NativeMemoryReader::readDoubles(pid, baseAddress, count), rows, cols); } catch (...) {}
              });
            } catch (const std::exception& e) { qWarning() << "Heatmap memory read failed:" << e.what(); }
          }
        }
      } else if (okResp && m_evaluateRequests.contains(seq)) {
        const auto pair = m_evaluateRequests.take(seq);
        emit expressionEvaluated(pair.first, pair.second, result);
      }
    } else if (command == "dataBreakpointInfo" && message["success"].toBool()) {
      // Promote resolved `dataId` from `dataBreakpointInfo` into an active `setDataBreakpoints` request.
      const int seq = message["request_seq"].toInt();
      if (m_dataBpInfoRequests.contains(seq)) {
        const auto spec = m_dataBpInfoRequests.take(seq);
        const QString dataId = message["body"].toObject()["dataId"].toString();
        if (!dataId.isEmpty()) { m_activeDataBreakpoints[spec.first] = qMakePair(dataId, spec.second); syncDataBreakpoints(); }
      }
    }
  } else if (type == "event") {
    // Handle asynchronous adapter notifications (`initialized`, `stopped`, `output`, `process`, `terminated`).
    const QString event = message["event"].toString();
    if (event == "initialized") {
      // Flush all registered source breakpoints followed by `configurationDone` to release execution.
      for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it) {
        if (!it.value().isEmpty()) {
          QJsonArray breakpointsArray;
          for (int l : *it) {
            QJsonObject bp{{"line", l}};
            const QString cond = m_breakpointConditions.value(it.key()).value(l);
            if (!cond.isEmpty()) bp["condition"] = cond;
            breakpointsArray.append(bp);
          }
          sendRequest("setBreakpoints", QJsonObject{{"source", QJsonObject{{"path", it.key()}}}, {"breakpoints", breakpointsArray}});
        }
      }
      sendRequest("configurationDone");
      m_state = SessionState::Running;
      emit stateChanged(m_state);
    } else if (event == "stopped") {
      // Map 1-based DAP `threadId` back to 0-based `rankId` and query live threads + stack frames.
      const QJsonObject body = message["body"].toObject();
      const QString reason = body["reason"].toString();
      const int threadId = body["threadId"].toInt(1);
      const int rankId = std::max(0, threadId - 1);
      m_activeThreadIds[rankId] = threadId;

      // Notify UI toolbars, hardware watchpoint listeners, and rack tiles that target execution is halted.
      m_state = SessionState::Paused;
      emit stateChanged(m_state);
      if (reason.contains(QStringLiteral("data"), Qt::CaseInsensitive) || reason.contains(QStringLiteral("watchpoint"), Qt::CaseInsensitive)) {
        const QString expr = m_activeDataBreakpoints.isEmpty() ? QStringLiteral("watchpoint") : m_activeDataBreakpoints.firstKey();
        emit watchpointHit(rankId, expr, QString(), body["description"].toString(body["text"].toString()));
      }
      emit executionStopped(rankId, reason);
      if (m_processCount > 0) sendRequest("threads");
      requestStackTrace(rankId);
    } else if (event == "output") {
      // Inspect target output for SLURM job allocation banners before forwarding to UI logs.
      const QJsonObject body = message["body"].toObject();
      const QString output = body["output"].toString();
      static const QRegularExpression re("(?:Submitted batch job |Granted job allocation )(\\d+)");
      const QRegularExpressionMatch match = re.match(output);
      if (match.hasMatch()) { m_slurmJobId = match.captured(1).toInt(); m_state = SessionState::Queued; emit stateChanged(m_state); }
      const QString category = body["category"].toString();
      emit targetOutputReceived(category, output);
      if (category == QLatin1String("stdout") || category == QLatin1String("stderr")) {
        emit programOutputReceived(0, output.trimmed(), category == QLatin1String("stderr"));
      }
    } else if (event == "process") {
      // Record the OS `systemProcessId` so `NativeMemoryReader` can attach via `process_vm_readv`.
      const qint64 pid = message["body"].toObject()["systemProcessId"].toVariant().toLongLong();
      if (pid > 0) m_rankToPid[m_rankToPid.isEmpty() ? 0 : m_rankToPid.size()] = static_cast<pid_t>(pid);
    } else if (event == "terminated" || event == "exited") {
      // Clear per-rank PIDs, frames, and threads when the debuggee process terminates.
      m_rankToPid.clear(); m_rankFrames.clear(); m_rankThreads.clear(); m_activeThreadIds.clear();
      m_state = SessionState::Disconnected;
      emit stateChanged(m_state);
    }
  }
  emit messageReceived(message);
}

/**
 * @brief Logs `QProcess` transport errors and transitions the session to `Disconnected`.
 * @param error Underlying `QProcess::ProcessError` code reported by Qt.
 */
void DapCoordinator::handleProcessError(QProcess::ProcessError error) {
  if (m_shuttingDown) return;
  qDebug() << "[DAP] FATAL: Adapter process error:" << error << "-" << m_process->errorString();
  emit errorOccurred(m_process->errorString());
  m_processCount = 0; m_state = SessionState::Disconnected; emit stateChanged(m_state);
}

/** @brief Emits `adapterExited` and resets session state when the adapter process terminates. */
void DapCoordinator::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
  if (m_shuttingDown) return;
  m_processCount = 0;
  m_rankToPid.clear(); m_rankFrames.clear(); m_rankThreads.clear(); m_activeThreadIds.clear();
  emit adapterExited(exitCode, exitStatus);
  m_state = SessionState::Disconnected; emit stateChanged(m_state);
}

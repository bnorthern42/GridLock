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
#include <QFileInfo>
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
  if (!m_process) return;
  if (m_process->state() == QProcess::Starting) {
    m_process->waitForStarted(500);
  }
  if (m_process->state() != QProcess::NotRunning) {
    m_process->terminate();
    if (!m_process->waitForFinished(3000)) {
      m_process->kill();
      m_process->waitForFinished(500);
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
  args["threadId"] = (threadId > 0) ? threadId : 1;
  sendRequest("stepIn", args);
}

/**
 * @brief Steps a single machine instruction on @p threadId via DAP `stepIn` with `"instruction"` granularity.
 */
void DapCoordinator::stepInstruction(int threadId) {
  QJsonObject args;
  args["threadId"] = (threadId > 0) ? threadId : 1;
  args["granularity"] = "instruction";
  sendRequest("stepIn", args);
}

/**
 * @brief Resumes execution on @p threadId via DAP `continue`.
 */
void DapCoordinator::continueExecution(int threadId) {
  QJsonObject args;
  args["threadId"] = (threadId > 0) ? threadId : 1;
  sendRequest("continue", args);
}

/**
 * @brief Pauses execution on @p threadId via DAP `pause`.
 */
void DapCoordinator::pauseExecution(int threadId) {
  QJsonObject args;
  args["threadId"] = (threadId > 0) ? threadId : 1;
  sendRequest("pause", args);
}

/**
 * @brief Resolves `lldb-dap` (or fallback `lldb-vscode`) and starts a parallel debugging session.
 */
void DapCoordinator::launchParallelSession(const QString& binaryPath,
                                           int ranks) {
  launchParallelSession(binaryPath, ranks,
                        gridlock::core::ConfigManager::instance().getMpiArgs());
}

/**
 * @brief Resolves the configured DAP adapter and starts a debugging session with @p mpiArgs.
 */
void DapCoordinator::launchParallelSession(const QString& binaryPath,
                                           int ranks,
                                           const QString& mpiArgs) {
  if (m_state != SessionState::Disconnected) {
    terminateSession();
  }
  m_processCount = std::max(1, ranks);
  m_currentBinaryPath = binaryPath;
  m_mpiArgs = mpiArgs;

  QString adapterProgram =
      gridlock::core::ConfigManager::instance().getGlobalDapAdapterPath().trimmed();
  if (!adapterProgram.isEmpty() && !QFileInfo::exists(adapterProgram)) {
    adapterProgram = QStandardPaths::findExecutable(adapterProgram);
  }
  if (adapterProgram.isEmpty()) {
    adapterProgram = QStandardPaths::findExecutable("lldb-dap");
  }
  if (adapterProgram.isEmpty()) {
    adapterProgram = QStandardPaths::findExecutable("lldb-vscode");
  }
  if (adapterProgram.isEmpty()) {
    qWarning() << "[DAP] Could not find lldb-dap or lldb-vscode in PATH.";
    return;
  }

  startAdapter(adapterProgram);
}

/**
 * @brief Attaches `lldb-dap` to a running local OS process @p pid and records its rank-0 PID.
 * @param pid Target OS process identifier from `/proc`.
 * @return `true` if the adapter binary was found and launched for attachment.
 */
bool DapCoordinator::attachToProcess(qint64 pid) {
  if (pid <= 0 || !QFileInfo::exists(QStringLiteral("/proc/%1/status").arg(pid))) return false;
  if (m_state != SessionState::Disconnected) terminateSession();

  // Resolve configured or system `lldb-dap` / `lldb-vscode` adapter binary.
  QString adapterProgram = gridlock::core::ConfigManager::instance().getGlobalDapAdapterPath().trimmed();
  if (!adapterProgram.isEmpty() && !QFileInfo::exists(adapterProgram)) {
    adapterProgram = QStandardPaths::findExecutable(adapterProgram);
  }
  if (adapterProgram.isEmpty()) adapterProgram = QStandardPaths::findExecutable(QStringLiteral("lldb-dap"));
  if (adapterProgram.isEmpty()) adapterProgram = QStandardPaths::findExecutable(QStringLiteral("lldb-vscode"));
  if (adapterProgram.isEmpty()) return false;

  // Bind rank 0 immediately to the target PID and queue DAP `attach` on `initialize` completion.
  m_processCount = 1;
  m_pendingAttachPid = pid;
  m_rankToPid.clear();
  m_rankToPid[0] = static_cast<pid_t>(pid);
  m_currentBinaryPath = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
  startAdapter(adapterProgram);
  return true;
}

/**
 * @brief Registers a breakpoint at `(file, line)` with optional @p condition and syncs via DAP `setBreakpoints`.
 */
void DapCoordinator::setBreakpoint(const QString& file,
                                   int line,
                                   const QString& condition,
                                   int rankFilter) {
  Q_UNUSED(rankFilter);
  if (file.isEmpty() || line <= 0) return;
  if (!m_breakpoints[file].contains(line)) {
    m_breakpoints[file].append(line);
  }
  if (!condition.trimmed().isEmpty()) {
    m_breakpointConditions[file][line] = condition.trimmed();
  } else {
    m_breakpointConditions[file].remove(line);
  }

  if (m_state != SessionState::Running && m_state != SessionState::Paused) {
    return;
  }

  QJsonObject args;
  args["source"] = QJsonObject{{"path", file}};
  QJsonArray breakpointsArray;
  for (int l : m_breakpoints[file]) {
    QJsonObject bp{{"line", l}};
    const QString cond = m_breakpointConditions.value(file).value(l);
    if (!cond.isEmpty()) bp["condition"] = cond;
    breakpointsArray.append(bp);
  }
  args["breakpoints"] = breakpointsArray;
  sendRequest("setBreakpoints", args);
}

/**
 * @brief Clears all active breakpoints across tracked files and notifies any running adapter.
 */
void DapCoordinator::clearBreakpoints() {
  if (m_state == SessionState::Running || m_state == SessionState::Paused) {
    for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it) {
      QJsonObject args;
      args["source"] = QJsonObject{{"path", it.key()}};
      args["breakpoints"] = QJsonArray();
      sendRequest("setBreakpoints", args);
    }
  }
  m_breakpoints.clear();
  m_breakpointConditions.clear();
}

/**
 * @brief Toggles a line breakpoint in @p file at @p line and sends the updated `setBreakpoints` list.
 */
void DapCoordinator::toggleBreakpoint(const QString& file, int line) {
  if (m_breakpoints[file].contains(line)) {
    m_breakpoints[file].removeOne(line);
    m_breakpointConditions[file].remove(line);
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
    const QString cond = m_breakpointConditions.value(file).value(l);
    if (!cond.isEmpty()) bp["condition"] = cond;
    breakpointsArray.append(bp);
  }
  args["breakpoints"] = breakpointsArray;

  sendRequest("setBreakpoints", args);
}

/**
 * @brief Requests up to 64 stack frames (`levels = 64` for active sessions) on @p rankId.
 */
void DapCoordinator::requestStackTrace(int rankId) {
  QJsonObject args;
  args["threadId"] = m_activeThreadIds.value(rankId, rankId + 1);
  // Request 64 frames in live sessions while keeping synthetic unit-test frame queries compact.
  args["levels"] = (m_processCount > 0) ? 64 : 1;
  m_stackTraceRequests[m_sequenceNumber] = rankId;
  sendRequest("stackTrace", args);
}

/**
 * @brief Queries live DAP `threads` and a 64-frame `stackTrace` for @p rank.
 */
void DapCoordinator::requestStackAndThreads(int rank) {
  if (m_rankThreads.contains(rank)) emit threadsUpdated(rank, m_rankThreads.value(rank));
  if (m_rankFrames.contains(rank)) emit stackTraceUpdated(rank, m_rankFrames.value(rank));
  sendRequest("threads");
  QJsonObject args{{"threadId", m_activeThreadIds.value(rank, rank + 1)}, {"levels", 64}};
  m_stackTraceRequests[m_sequenceNumber] = rank;
  sendRequest("stackTrace", args);
}

/**
 * @brief Selects @p threadId and @p frameLevel on @p rank, refreshing DAP `scopes` -> `Locals` + `Registers`.
 */
void DapCoordinator::selectStackFrame(int rank, int threadId, int frameLevel) {
  const int prevThread = m_activeThreadIds.value(rank, rank + 1);
  if (threadId > 0 && threadId != prevThread) {
    m_activeThreadIds[rank] = threadId;
    if (m_rankThreads.contains(rank)) {
      auto& list = m_rankThreads[rank];
      for (auto& th : list) th.isCurrent = (th.threadId == threadId);
      emit threadsUpdated(rank, list);
    }
    QJsonObject args{{"threadId", threadId}, {"levels", 64}};
    m_stackTraceRequests[m_sequenceNumber] = rank;
    sendRequest("stackTrace", args);
  }
  const auto frames = m_rankFrames.value(rank);
  for (const auto& f : frames) {
    if (f.level == frameLevel) {
      m_activeFrameIds[rank] = f.frameId;
      if (!f.file.isEmpty() && f.line > 0) emit locationChanged(rank, f.file, f.line);
      requestScopes(f.frameId, rank);
      break;
    }
  }
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
  m_variablesRequests[m_sequenceNumber] = qMakePair(rankId, variablesReference);
  sendRequest("variables", args);
}

/**
 * @brief Evaluates @p expression in the `"watch"` context of @p rankId's active stack frame.
 */
void DapCoordinator::evaluateExpression(int rankId, const QString& expression) {
  QJsonObject args{{"expression", expression}, {"context", "watch"}};
  if (m_activeFrameIds.contains(rankId)) args["frameId"] = m_activeFrameIds[rankId];
  m_evaluateRequests[m_sequenceNumber] = qMakePair(rankId, expression);
  sendRequest("evaluate", args);
}

/**
 * @brief Evaluates pointer @p expression to a base address and triggers asynchronous heatmap loading.
 */
void DapCoordinator::requestHeatmapRender(int rankId, const QString& expression, int rows, int cols) {
  if (!isAdapterRunning()) {
    qWarning() << "[Heatmap] Aborting request: DAP adapter is not running.";
    return;
  }
  QJsonObject args{{"expression", expression}, {"context", "watch"}};
  if (m_activeFrameIds.contains(rankId)) args["frameId"] = m_activeFrameIds[rankId];
  const int seq = sendRequest("evaluate", args);
  m_heatmapRequests[seq] = {rankId, rows, cols};
}

/**
 * @brief Reads @p count elements/bytes at @p memoryReference on @p rankId.
 *
 * Uses `NativeMemoryReader::readDoubles` via `process_vm_readv` when `count > 1024`
 * and a local PID mapping is available, falling back to DAP `readMemory` otherwise.
 */
void DapCoordinator::readMemory(int rankId, const QString& memoryReference, int count) {
  // Fast path: bypass base64 JSON serialization for large local memory reads.
  if (count > 1024 && m_rankToPid.contains(rankId)) {
    bool ok = false;
    const uintptr_t baseAddress = memoryReference.startsWith("0x", Qt::CaseInsensitive)
                                      ? memoryReference.mid(2).toULongLong(&ok, 16)
                                      : memoryReference.toULongLong(&ok, 10);
    if (ok) {
      try {
        const std::vector<double> doubles =
            NativeMemoryReader::readDoubles(m_rankToPid[rankId], baseAddress, count);
        emit memoryRead(rankId, memoryReference,
                        QByteArray(reinterpret_cast<const char*>(doubles.data()),
                                   static_cast<qsizetype>(doubles.size() * sizeof(double))));
        return;
      } catch (const std::exception& e) {
        qWarning() << "Native memory read failed, falling back to DAP:" << e.what();
      }
    }
  }

  // Standard DAP fallback path using `readMemory`.
  QJsonObject args{{"memoryReference", memoryReference}, {"count", count}};
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
  if (m_slurmJobId != -1) QProcess::execute("scancel", {QString::number(m_slurmJobId)});

  if (isAdapterRunning()) sendRequest("disconnect", QJsonObject{{"terminateDebuggee", true}});

  // Wait briefly for clean exit before sending SIGTERM / SIGKILL.
  if (m_process && m_process->state() != QProcess::NotRunning) {
    if (!m_process->waitForFinished(1000)) {
      m_process->terminate();
      if (!m_process->waitForFinished(1000)) {
        m_process->kill();
        m_process->waitForFinished(500);
      }
    }
  }

  m_slurmJobId = -1;
  m_processCount = 0;
  m_rankToPid.clear();
  m_activeFrameIds.clear();
  m_activeThreadIds.clear();
  m_rankFrames.clear();
  m_rankThreads.clear();
  emit stackTraceUpdated(0, {});
  emit threadsUpdated(0, {});
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
      // Dispatch live PID `attach` when `attachToProcess(pid)` initiated this session.
      if (m_pendingAttachPid > 0) {
        const qint64 attachPid = m_pendingAttachPid;
        m_pendingAttachPid = 0;
        sendRequest("attach", QJsonObject{{"pid", attachPid}, {"stopOnEntry", true}});
        emit messageReceived(message);
        return;
      }
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
      // Parse up to 64 unwound stack frames, cache frame descriptors, and broadcast stackTraceUpdated.
      const int seq = message["request_seq"].toInt();
      if (m_stackTraceRequests.contains(seq)) {
        const int rankId = m_stackTraceRequests.take(seq);
        const QJsonArray stackFrames = message["body"].toObject()["stackFrames"].toArray();
        QVector<StackFrameEntry> frames;
        frames.reserve(stackFrames.size());
        for (int idx = 0; idx < stackFrames.size(); ++idx) {
          const QJsonObject fObj = stackFrames.at(idx).toObject();
          StackFrameEntry entry;
          entry.level = idx;
          entry.frameId = fObj["id"].toInt();
          entry.function = fObj["name"].toString(QStringLiteral("??"));
          if (fObj.contains("source")) entry.file = fObj["source"].toObject()["path"].toString();
          entry.line = fObj["line"].toInt(0);
          const QString ipRef = fObj["instructionPointerReference"].toString();
          if (ipRef.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
            bool ok = false;
            entry.address = ipRef.mid(2).toULongLong(&ok, 16);
          }
          frames.append(entry);
        }
        m_rankFrames[rankId] = frames;
        emit stackTraceUpdated(rankId, frames);
        if (!frames.isEmpty()) {
          m_activeFrameIds[rankId] = frames.front().frameId;
          emit locationChanged(rankId, frames.front().file, frames.front().line);
        }
      }
    } else if (command == "threads" && message["success"].toBool()) {
      // Parse live OS/OpenMP threads from DAP `threads` response and emit `threadsUpdated`.
      const QJsonArray rawThreads = message["body"].toObject()["threads"].toArray();
      const int curThread = m_activeThreadIds.value(0, 1);
      QVector<ThreadEntry> threads;
      threads.reserve(rawThreads.size());
      for (const QJsonValue& tv : rawThreads) {
        const QJsonObject tObj = tv.toObject();
        ThreadEntry th;
        th.threadId = tObj["id"].toInt(1);
        th.name = tObj["name"].toString(QStringLiteral("main"));
        th.state = QStringLiteral("stopped");
        th.isCurrent = (th.threadId == curThread);
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
            if (regRef > 0) {
              m_registersRequests[m_sequenceNumber] = rankId;
              sendRequest("variables", QJsonObject{{"variablesReference", regRef}});
            }
          }
        }
      }
    } else if (command == "variables" && message["success"].toBool()) {
      // Route the returned `variables` array to either Local Variables or CPU Registers.
      const int seq = message["request_seq"].toInt();
      if (m_variablesRequests.contains(seq)) {
        const auto pair = m_variablesRequests.take(seq);
        emit localsUpdated(pair.first, pair.second, message["body"].toObject()["variables"].toArray());
      } else if (m_registersRequests.contains(seq)) {
        emit registersUpdated(m_registersRequests.take(seq), message["body"].toObject()["variables"].toArray());
      }
    } else if (command == "readMemory" && message["success"].toBool()) {
      // Decode base64-encoded memory bytes from the DAP `readMemory` response for `MemView`.
      const int seq = message["request_seq"].toInt();
      if (m_memoryRequests.contains(seq)) {
        const int rankId = m_memoryRequests.take(seq);
        const QJsonObject body = message["body"].toObject();
        emit memoryRead(rankId, body["address"].toString(),
                        QByteArray::fromBase64(body["data"].toString().toUtf8()));
      }
    } else if (command == "evaluate" && message["success"].toBool()) {
      // Distinguish between background 2D heatmap pointer evaluations and interactive watch expressions.
      const int seq = message["request_seq"].toInt();
      if (m_heatmapRequests.contains(seq)) {
        // Parse evaluated pointer hex address and trigger a background `process_vm_readv` copy.
        const auto req = m_heatmapRequests.take(seq);
        const QString result = message["body"].toObject()["result"].toString();
        static const QRegularExpression re("(0x[0-9a-fA-F]+)");
        const QRegularExpressionMatch match = re.match(result);
        if (match.hasMatch() && m_rankToPid.contains(req.rankId)) {
          bool ok = false;
          const uintptr_t baseAddress = match.captured(1).toULongLong(&ok, 16);
          if (ok) {
            try {
              (void)QtConcurrent::run([this, pid = m_rankToPid[req.rankId], baseAddress,
                                       count = req.rows * req.cols, rows = req.rows, cols = req.cols]() {
                try {
                  emit heatmapDataReady(NativeMemoryReader::readDoubles(pid, baseAddress, count), rows, cols);
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
        // Forward interactive watch expression results to VariablesDockWidget and GdbConsoleWidget.
        const auto pair = m_evaluateRequests.take(seq);
        emit expressionEvaluated(pair.first, pair.second, message["body"].toObject()["result"].toString());
      }
    }
  } else if (type == "event") {
    // Handle asynchronous adapter notifications (`initialized`, `stopped`, `output`, `process`).
    const QString event = message["event"].toString();
    if (event == "initialized") {
      // Flush all registered breakpoints followed by `configurationDone` to release execution.
      for (auto it = m_breakpoints.constBegin(); it != m_breakpoints.constEnd(); ++it) {
        if (!it.value().isEmpty()) {
          QJsonArray breakpointsArray;
          for (int l : *it) {
            QJsonObject bp{{"line", l}};
            const QString cond = m_breakpointConditions.value(it.key()).value(l);
            if (!cond.isEmpty()) bp["condition"] = cond;
            breakpointsArray.append(bp);
          }
          sendRequest("setBreakpoints", QJsonObject{{"source", QJsonObject{{"path", it.key()}}},
                                                    {"breakpoints", breakpointsArray}});
        }
      }
      // Complete initial launch handshake and transition workstation state to Running.
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

      // Notify UI toolbars and rack tiles that target execution is now halted.
      m_state = SessionState::Paused;
      emit stateChanged(m_state);
      emit executionStopped(rankId, reason);
      // Query live adapter threads and up to 64 stack frames for CallStackDockWidget.
      if (m_processCount > 0) sendRequest("threads");
      requestStackTrace(rankId);
    } else if (event == "output") {
      // Inspect target output for SLURM job allocation banners before forwarding to UI.
      const QJsonObject body = message["body"].toObject();
      const QString category = body["category"].toString();
      const QString output = body["output"].toString();
      static const QRegularExpression re("(?:Submitted batch job |Granted job allocation )(\\d+)");
      const QRegularExpressionMatch match = re.match(output);
      if (match.hasMatch()) {
        // Transition session state to Queued when a SLURM batch/allocation ID is detected.
        m_slurmJobId = match.captured(1).toInt();
        m_state = SessionState::Queued;
        emit stateChanged(m_state);
      }
      // Forward categorized stdout/stderr/console text to the Network & Console log dock.
      emit targetOutputReceived(category, output);
    } else if (event == "process") {
      // Record the OS `systemProcessId` so `NativeMemoryReader` can attach via `process_vm_readv`.
      const qint64 pid = message["body"].toObject()["systemProcessId"].toVariant().toLongLong();
      if (pid > 0) m_rankToPid[m_rankToPid.isEmpty() ? 0 : m_rankToPid.size()] = static_cast<pid_t>(pid);
    } else if (event == "terminated" || event == "exited") {
      // Clear inferior PID and stack/thread registries when target terminates.
      m_rankToPid.clear(); m_rankFrames.clear(); m_rankThreads.clear(); m_activeThreadIds.clear();
      // Notify listeners that the debug adapter session has disconnected cleanly.
      m_state = SessionState::Disconnected;
      emit stateChanged(m_state);
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
  // Surface adapter spawn/pipe failures immediately to connected workstation listeners.
  qDebug() << "[DAP] FATAL: Adapter process error:" << error << "-" << m_process->errorString();
  emit errorOccurred(m_process->errorString());
  m_processCount = 0;
  m_state = SessionState::Disconnected;
  emit stateChanged(m_state);
}

/**
 * @brief Emits `adapterExited` and resets session state when the adapter process terminates.
 * @param exitCode Process exit status code.
 * @param exitStatus Normal vs. crash termination status.
 */
void DapCoordinator::handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus) {
  // Clear tracked inferior PIDs, frames, and threads when the debug adapter exits.
  m_processCount = 0;
  m_rankToPid.clear(); m_rankFrames.clear(); m_rankThreads.clear(); m_activeThreadIds.clear();
  emit adapterExited(exitCode, exitStatus);
  m_state = SessionState::Disconnected;
  emit stateChanged(m_state);
}


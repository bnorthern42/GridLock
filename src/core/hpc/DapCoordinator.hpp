/**
 * @file DapCoordinator.hpp
 * @brief Debug Adapter Protocol (DAP) coordinator for `lldb-dap` / `lldb-vscode` sessions.
 *
 * Implements `IBackendCoordinator` over `Content-Length` framed JSON messages,
 * supporting parallel thread-to-rank mapping, asynchronous stack/scope/variable
 * inspection, and zero-copy `NativeMemoryReader` fast paths for large heatmaps.
 */

#pragma once

#include <sys/types.h>

#include <atomic>
#include <vector>

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QProcess>
#include <QString>

#include "IBackendCoordinator.hpp"
#include "ProcessScanner.hpp"

/**
 * @enum SessionState
 * @brief Lifecycle state machine of a `DapCoordinator` debugging session.
 */
enum class SessionState {
  Disconnected,  ///< No DAP adapter process is running.
  Launching,     ///< `lldb-dap` process has been spawned and is initializing.
  Queued,        ///< Remote job is queued inside a SLURM batch allocation.
  Running,       ///< Target program is actively executing under the adapter.
  Paused,        ///< One or more target threads/ranks have hit a stop event.
  Terminating    ///< Session teardown (`disconnect` / `scancel`) is in progress.
};

/**
 * @class DapCoordinator
 * @brief Concrete `IBackendCoordinator` driving an `lldb-dap` child process.
 */
class DapCoordinator : public IBackendCoordinator {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the DAP coordinator and wires `QProcess` lifecycle signals.
   */
  explicit DapCoordinator(QObject* parent = nullptr);

  /**
   * @brief Disconnects signals and terminates the DAP adapter process.
   */
  ~DapCoordinator() override;

  /** @brief Spawns the DAP adapter binary at @p program. */
  void startAdapter(const QString& program);

  /** @brief Terminates or kills the running DAP adapter process. */
  void stopAdapter();

  /** @brief Sends the initial DAP `initialize` handshake request. */
  void initializeAdapter();

  /**
   * @brief Sends a DAP request for @p command with @p arguments and returns its sequence number.
   */
  int sendRequest(const QString& command,
                  const QJsonObject& arguments = QJsonObject());

  /** @brief Appends raw stdout bytes from the adapter and parses complete `Content-Length` frames. */
  void processRawData(const QByteArray& data);

  /** @brief Cancels any active SLURM job, sends `disconnect`, and stops the adapter. */
  void terminateSession() override;

  /** @brief Toggles a source-line breakpoint in @p file at @p line and syncs via `setBreakpoints`. */
  void toggleBreakpoint(const QString& file, int line);

  /** @brief Requests the top stack frame (`stackTrace`) for @p rankId. */
  void requestStackTrace(int rankId);

  /** @brief Requests variable scopes (`scopes`) for @p frameId on @p rankId. */
  void requestScopes(int frameId, int rankId);

  /** @brief Requests child variables (`variables`) for @p variablesReference on @p rankId. */
  void requestVariables(int rankId, int variablesReference);

  /** @brief Routes child inspection for @p parentExpression through DAP `variables` via its varRef. */
  void requestVariableChildren(int rank, const QString& parentExpression) override;

  /** @brief Evaluates @p expression in the active stack frame of @p rankId. */
  void evaluateExpression(int rankId, const QString& expression) override;

  /** @brief Reads @p count bytes at @p memoryReference on @p rankId (using `NativeMemoryReader` when > 1 KB). */
  void readMemory(int rankId, const QString& memoryReference, int count) override;

  /** @brief Evaluates pointer @p expression and reads a `rows x cols` double matrix for heatmap rendering. */
  void requestHeatmapRender(int rankId,
                            const QString& expression,
                            int rows,
                            int cols);

  /** @brief Returns the cached top stack frame ID for @p rankId, or `-1` if unknown. */
  int getActiveFrameId(int rankId) const {
    return m_activeFrameIds.value(rankId, -1);
  }

  /** @brief Sends a DAP `next` (step-over) request for @p threadId. */
  void stepOver(int threadId) override;

  /** @brief Sends a DAP `stepIn` request for @p threadId. */
  void stepInto(int threadId) override;

  /** @brief Sends a machine-instruction granularity `stepIn` request for @p threadId. */
  void stepInstruction(int threadId) override;

  /** @brief Sends a DAP `continue` request for @p threadId. */
  void continueExecution(int threadId) override;

  /** @brief Sends a DAP `pause` request for @p threadId. */
  void pauseExecution(int threadId) override;

  /** @brief Pauses 0-based MPI rank @p rankId (`threadId = rankId + 1`). */
  void pauseRank(int rankId) override { pauseExecution(rankId + 1); }

  /** @brief Sends a DAP `reverseContinue` request for 0-based MPI @p rank. */
  void reverseContinue(int rank) override {
    sendRequest(QStringLiteral("reverseContinue"), QJsonObject{{QStringLiteral("threadId"), qMax(0, rank) + 1}});
  }
  /** @brief Sends a statement-granularity DAP `stepBack` request for 0-based MPI @p rank. */
  void reverseStepOver(int rank) override {
    sendRequest(QStringLiteral("stepBack"), QJsonObject{{QStringLiteral("threadId"), qMax(0, rank) + 1}, {QStringLiteral("granularity"), QStringLiteral("statement")}});
  }
  /** @brief Sends a statement-granularity DAP `stepBack` request for 0-based MPI @p rank. */
  void reverseStepInto(int rank) override { reverseStepOver(rank); }
  /** @brief Sends an instruction-granularity DAP `stepBack` request for 0-based MPI @p rank. */
  void reverseStepInstruction(int rank) override {
    sendRequest(QStringLiteral("stepBack"), QJsonObject{{QStringLiteral("threadId"), qMax(0, rank) + 1}, {QStringLiteral("granularity"), QStringLiteral("instruction")}});
  }

  /** @brief Locates `lldb-dap` / `lldb-vscode` in `PATH` and launches a session for @p binaryPath. */
  void launchParallelSession(const QString& binaryPath, int ranks) override;

  /** @brief Launches a DAP session with explicit MPI launcher arguments @p mpiArgs. */
  void launchParallelSession(const QString& binaryPath,
                             int ranks,
                             const QString& mpiArgs) override;

  /** @brief Sets a source-line breakpoint (with optional @p condition) and syncs via `setBreakpoints`. */
  void setBreakpoint(const QString& file,
                     int line,
                     const QString& condition = QString(),
                     int rankFilter = -1) override;

  /** @brief Clears all active breakpoints across tracked source files. */
  void clearBreakpoints() override;

  /** @brief Returns `"lldb-dap"` as the active debugger engine identifier. */
  QString activeEngineName() const override {
    return QStringLiteral("lldb-dap");
  }

  /** @brief Returns the OS PID mapped to @p rank, or `0` if unmapped. */
  qint64 getPidForRank(int rank) const override {
    return static_cast<qint64>(m_rankToPid.value(rank, 0));
  }

  /** @brief Attaches an `lldb-dap` session to a running local OS process @p pid. */
  bool attachToProcess(qint64 pid) override;

  /** @brief Attaches an `lldb-dap` session to one or more running `ProcessEntry` targets. */
  bool attachToProcesses(const QList<gridlock::ProcessEntry>& targets);

  /** @brief Returns the path to the target binary for the active session. */
  QString getCurrentBinaryPath() const override { return m_currentBinaryPath; }

  /** @brief Returns the number of MPI ranks configured for the session. */
  int getProcessCount() const override { return m_processCount; }

  /** @brief Queries DAP `threads` and 64-frame `stackTrace` for @p rank. */
  void requestStackAndThreads(int rank) override;

  /** @brief Switches active DAP thread/frame on @p rank and refreshes `scopes` -> `variables` + `Registers`. */
  void selectStackFrame(int rank, int threadId, int frameLevel) override;

  /** @brief Scrubs @p command via `MiSanitizer` and evaluates it in DAP `"repl"` context. */
  void sendDebuggerCommand(int rank, const QString& command) override;

  /** @brief Queries `dataBreakpointInfo` and arms a DAP hardware data breakpoint on @p expression. */
  void setDataWatchpoint(const QString& expression,
                         WatchpointAccessMode mode,
                         int rankFilter = -1) override;

  /** @brief Removes an active DAP hardware data breakpoint matching @p expression. */
  void removeDataWatchpoint(const QString& expression) override;

 signals:
  /** @brief Emitted when @p rankId stops due to @p reason (e.g., `"breakpoint"`, `"step"`). */
  void executionStopped(int rankId, const QString& reason);

  /** @brief Emitted for every parsed JSON message received from the adapter. */
  void messageReceived(const QJsonObject& message);

  /** @brief Emitted when `scopes` arrive for @p rankId. */
  void scopesUpdated(int rankId, const QJsonArray& scopes);

  /** @brief Emitted when `variables` arrive for @p parentVarRef on @p rankId. */
  void localsUpdated(int rankId, int parentVarRef, const QJsonArray& variables);

  /** @brief Emitted when a 2D `double` matrix has been transferred for the heatmap widget. */
  void heatmapDataReady(const std::vector<double>& data, int rows, int cols);

  /** @brief Emitted once the child adapter process has started. */
  void adapterStarted();

  /** @brief Emitted when the child adapter process exits. */
  void adapterExited(int exitCode, QProcess::ExitStatus exitStatus);

  /** @brief Emitted when `QProcess` encounters a launch or transport error. */
  void errorOccurred(const QString& errorString);

  /** @brief Emitted whenever `m_state` transitions to @p newState. */
  void stateChanged(SessionState newState);

 private slots:
  /** @brief Reads available bytes from `m_process` stdout and forwards to `processRawData()`. */
  void readyReadStandardOutput();
  /** @brief Handles `QProcess::errorOccurred` from the adapter process. */
  void handleProcessError(QProcess::ProcessError error);
  /** @brief Handles `QProcess::finished` from the adapter process. */
  void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);

 protected slots:
  /** @brief Dispatches a parsed DAP `response` or `event` JSON object on the main thread. */
  void handleMessage(const QJsonObject& message);

 protected:
  /** @brief Frames @p message with `Content-Length` and writes it to the adapter. */
  virtual void sendRawMessage(const QJsonObject& message);
  /** @brief Returns `true` if the adapter process is currently running. */
  virtual bool isAdapterRunning() const;
  /** @brief Writes raw framed bytes to the adapter's stdin pipe. */
  virtual void writeToAdapter(const QByteArray& data);

  SessionState m_state = SessionState::Disconnected; ///< Current session lifecycle state.
  int m_processCount = 0;                            ///< Number of MPI ranks in the session.
  QMap<int, pid_t> m_rankToPid;                      ///< Maps zero-based rank ID to OS `pid_t`.

 private:
  /** @brief Transmits the consolidated `setDataBreakpoints` array to the active DAP adapter. */
  void syncDataBreakpoints();

  QProcess* m_process = nullptr;                     ///< Underlying `lldb-dap` child process.
  QByteArray m_buffer;                               ///< Receive buffer for partial DAP frames.
  std::atomic<int> m_sequenceNumber{1};              ///< Monotonic DAP `seq` counter.
  QMap<QString, QList<int>> m_breakpoints;           ///< Active breakpoints keyed by source file path.
  QMap<int, int> m_stackTraceRequests;               ///< Maps `request_seq` to `rankId`.
  QMap<int, int> m_scopesRequests;                   ///< Maps `request_seq` to `rankId`.
  QMap<int, QPair<int, int>> m_variablesRequests;    ///< Maps `request_seq` to `(rankId, varRef)`.
  QMap<int, QPair<int, QString>> m_varChildRequests; ///< Maps `request_seq` to `(rankId, parentExpr)`.
  QMap<int, int> m_registersRequests;                ///< Maps `request_seq` to `rankId`.
  QMap<int, int> m_memoryRequests;                   ///< Maps `request_seq` to `rankId`.
  QMap<int, int> m_activeFrameIds;                   ///< Maps `rankId` to active stack `frameId`.
  QMap<int, int> m_activeThreadIds;                  ///< Maps `rankId` to active 1-based DAP `threadId`.
  QMap<int, QVector<StackFrameEntry>> m_rankFrames;  ///< Cached 64-frame call stack per rank.
  QMap<int, QVector<ThreadEntry>> m_rankThreads;     ///< Cached thread descriptors per rank.
  QMap<int, QPair<int, QString>> m_evaluateRequests; ///< Maps `request_seq` to `(rankId, expr)`.
  QMap<int, int> m_replRequests;                     ///< Maps `request_seq` to REPL target `rankId`.
  QMap<int, QPair<QString, QString>> m_dataBpInfoRequests; ///< Maps `request_seq` to `(expr, accessType)`.
  QMap<QString, QPair<QString, QString>> m_activeDataBreakpoints; ///< Maps `expr` to `(dataId, accessType)`.

  /**
   * @struct HeatmapRequest
   * @brief Tracks dimensions for an in-flight heatmap pointer evaluation request.
   */
  struct HeatmapRequest {
    int rankId = 0; ///< Target MPI rank index.
    int rows = 0;   ///< Matrix row count.
    int cols = 0;   ///< Matrix column count.
  };
  QMap<int, HeatmapRequest> m_heatmapRequests; ///< Maps `request_seq` to `HeatmapRequest`.

  int m_slurmJobId = -1;       ///< Active SLURM job ID parsed from target output, or `-1`.
  qint64 m_pendingAttachPid = 0; ///< Target local OS PID queued for DAP `attach` after `initialize`.
  QString m_currentBinaryPath; ///< Filesystem path of the target binary being debugged.
  QString m_mpiArgs;           ///< Optional MPI launcher arguments passed to `launchParallelSession`.
  QMap<QString, QMap<int, QString>> m_breakpointConditions; ///< Optional conditional expressions per `(file, line)`.
};

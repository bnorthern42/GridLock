/**
 * @file GdbRankCoordinator.hpp
 * @brief Multi-rank GDB/MI3 session coordinator managing `mpiexec` + per-rank `gdbserver` instances.
 *
 * Launches an MPMD `mpiexec` job binding each MPI rank to `localhost:(2000 + rankId)`
 * under `gdbserver`, attaches a dedicated `gdb --interpreter=mi3` process per rank,
 * and synchronizes breakpoints, watch variables, register dumps, and disassembly.
 */

#pragma once

#include <memory>
#include <vector>

#include <QMap>
#include <QObject>
#include <QPoint>
#include <QProcess>
#include <QString>

#include "../../RankState.hpp"
#include "IBackendCoordinator.hpp"

namespace gridlock {

/**
 * @class GdbRankCoordinator
 * @brief Orchestrates parallel GDB/MI3 processes attached to MPI ranks via `gdbserver`.
 */
class GdbRankCoordinator : public IBackendCoordinator {
  Q_OBJECT

 public:
  /**
   * @brief Constructs an idle `GdbRankCoordinator`.
   */
  explicit GdbRankCoordinator(QObject* parent = nullptr);

  /**
   * @brief Terminates all attached GDB and `mpiexec` child processes on destruction.
   */
  ~GdbRankCoordinator() override;

  /** @brief Convenience wrapper delegating to `launchParallelSession(executable, rankCount)`. */
  void startDebugSession(int rankCount, const QString& executable);

  /** @brief Spawns `mpiexec` with per-rank `gdbserver` slots and connects `rankCount` GDB frontends. */
  void launchParallelSession(const QString& executable, int rankCount) override;

  /** @brief Returns the OS PID for @p rankId (`0` when managed via remote TCP `gdbserver`). */
  pid_t getPidForRank([[maybe_unused]] int rankId) const override { return 0; }

  /** @brief Returns the filesystem path of the target executable. */
  QString getCurrentBinaryPath() const override { return m_currentBinaryPath; }

  /** @brief Broadcasts `-break-insert -f <location>` across all active rank GDB processes. */
  void insertBreakpoint(const QString& location);

  /**
   * @brief Inserts or deletes a source-line breakpoint (with optional @p condition) across all ranks.
   */
  void broadcastBreakpoint(const QString& file,
                           int line,
                           bool isSet,
                           const QString& condition = QString());

  /** @brief Sends a raw GDB/MI command string to every rank in the session. */
  void broadcastCommand(const QString& cmd);

  /** @brief Cleanly exits all per-rank GDB processes and kills `m_mpirunProcess`. */
  void terminateAllSessions();

  /** @brief Requests `-data-disassemble` around the stopped source line or `$pc` on @p rankId. */
  void requestDisassemblyFallback(int rankId);

  /** @brief Pushes persisted workspace breakpoints to @p rankId during initial attach sync. */
  void flushCachedBreakpoints(int rankId);

  /** @brief Writes a raw MI command line to @p rankId's GDB stdin pipe. */
  void writeCmd(int rankId, const QString& cmd);

  /** @brief Parses one or more newline-delimited GDB/MI records from @p rankId. */
  void processGdbOutput(int rankId, const QString& output);

  /** @brief Initializes `rankCount` synthetic `RankProcess` slots for unit testing without `QProcess`. */
  void initializeMockSession(int rankCount, bool simulateInitialSync = false);

  /** @brief Issues `-data-read-memory-bytes <address> <length>` on @p rankId. */
  void readMemory(int rankId, const QString& address, int length) override;

  /** @brief Evaluates @p expression on @p rankId via `101-data-evaluate-expression`. */
  void evaluateExpression(int rankId, const QString& expression) override;

  /** @brief Returns the number of MPI rank slots currently managed. */
  int getProcessCount() const override {
    return static_cast<int>(m_processes.size());
  }

  /**
   * @brief Returns a snapshot of `RankState` for @p rankId, or an `"offline"` state if out of range.
   */
  RankState getRankState(int rankId) const {
    if (rankId >= 0 && rankId < static_cast<int>(m_processes.size()) &&
        m_processes[rankId]) {
      return m_processes[rankId]->state;
    }
    RankState st{};
    st.currentState = "offline";
    return st;
  }

 signals:
  /** @brief Emitted whenever @p rankId's execution state, location, registers, or watches change. */
  void rankStateChanged(int rankId, const RankState& state);

  /** @brief Emitted for every raw GDB/MI line received from @p rankId. */
  void gdbOutputReceived(int rankId, const QString& output);

  /** @brief Emitted whenever a command string is written to @p rankId's GDB stdin. */
  void commandSentToGdb(int rankId, const QString& cmd);

  /** @brief Emitted when a source editor hover expression (`100-data-evaluate-expression`) resolves. */
  void hoverEvaluationComplete(QString varName, QString result, QPoint globalPos);

  /** @brief Emitted when `-data-read-memory-bytes` returns hex contents starting at @p beginAddress. */
  void memoryDataReady(int rankId, qint64 beginAddress, const QString& hexContents);

  /** @brief Emitted when `101-data-evaluate-expression` completes on @p rankId. */
  void expressionEvaluated(int rankId, const QString& expr, const QString& result);

 public slots:
  /** @brief Broadcasts `-exec-next` across all MPI ranks. */
  virtual void stepAll();
  /** @brief Broadcasts `-exec-continue` across all MPI ranks. */
  virtual void continueAll();
  /** @brief Broadcasts `-exec-run` across all MPI ranks. */
  virtual void runAll();
  /** @brief Broadcasts `-exec-interrupt` across all MPI ranks. */
  virtual void haltAll();
  /** @brief Sends `-exec-interrupt` to a single focused rank @p rankId. */
  virtual void pauseFocusedRank(int rankId);

  /** @brief Implements `IBackendCoordinator::terminateSession()` via `terminateAllSessions()`. */
  void terminateSession() override;

  /** @brief `IBackendCoordinator` step-over shim routing to `stepAll()`. */
  void stepOver(int threadId) override {
    Q_UNUSED(threadId);
    stepAll();
  }

  /** @brief `IBackendCoordinator` step-into shim routing to `stepAll()`. */
  void stepInto(int threadId) override {
    Q_UNUSED(threadId);
    stepAll();
  }

  /** @brief `IBackendCoordinator` continue shim routing to `continueAll()`. */
  void continueExecution(int threadId) override {
    Q_UNUSED(threadId);
    continueAll();
  }

  /** @brief `IBackendCoordinator` pause shim routing to `pauseFocusedRank(threadId - 1)`. */
  void pauseExecution(int threadId) override { pauseFocusedRank(threadId - 1); }

  /** @brief Registers @p varName as a tracked GDB/MI variable object (`-var-create`) across all ranks. */
  void registerWatchVariable(const QString& varName);

  /** @brief Sends a user or console command to @p rankId (or all ranks when `rankId == -1`). */
  void sendCommand(int rankId, const QString& cmd);

  /** @brief Slot triggered by `QProcess::readyReadStandardOutput` for @p rankId. */
  void handleGdbOutput(int rankId);

  /** @brief Evaluates @p varName for an editor hover tooltip at screen coordinate @p globalPos. */
  void evaluateHoverVariable(int rankId, const QString& varName, QPoint globalPos);

 private:
  /**
   * @struct RankProcess
   * @brief Per-rank GDB process handle, line accumulation buffer, and telemetry state.
   */
  struct RankProcess {
    int id;                            ///< Zero-based MPI rank index.
    std::unique_ptr<QProcess> process; ///< Dedicated `gdb --interpreter=mi3` child process.
    RankState state;                   ///< Live execution, location, register, and watch telemetry.
    QString buffer;                    ///< Partial line accumulation buffer for GDB stdout.
    QString lastHoverVarName;          ///< Variable name for pending token `100` hover evaluation.
    QPoint lastHoverPos;               ///< Screen position for pending token `100` hover tooltip.
    QString lastEvalExpression;        ///< Expression string for pending token `101` evaluation.
  };

  std::unique_ptr<QProcess> m_mpirunProcess;             ///< Master `mpiexec` launcher process.
  std::vector<std::unique_ptr<RankProcess>> m_processes; ///< Per-rank GDB frontend slots.
  QMap<int, QMap<QString, QString>> m_varNameMap;        ///< `rankId -> (varName -> gdbVarId)`.
  QMap<int, QMap<QString, QString>> m_varNameRevMap;     ///< `rankId -> (gdbVarId -> varName)`.
  std::vector<QString> m_watchVariables;                 ///< Ordered list of watched variable expressions.
  QString m_currentBinaryPath;                           ///< Target binary path for the active session.
};

}  // namespace gridlock
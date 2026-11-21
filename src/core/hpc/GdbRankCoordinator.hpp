/**
 * @file GdbRankCoordinator.hpp
 * @brief Multi-rank GDB/MI3 session coordinator managing `mpiexec` + per-rank `gdbserver` instances.
 *
 * Launches an MPMD `mpiexec` job binding each MPI rank to `localhost:(2000 + rankId)`
 * under `gdbserver`, attaches a dedicated `gdb --interpreter=mi3` process per rank,
 * and synchronizes breakpoints, watch variables, register dumps, and disassembly.
 */

#pragma once

#include <algorithm>
#include <memory>
#include <vector>

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QMap>
#include <QObject>
#include <QPoint>
#include <QProcess>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QString>

#include "../../RankState.hpp"
#include "IBackendCoordinator.hpp"
#include "ProcessScanner.hpp"

namespace gridlock {

/**
 * @class GdbRankCoordinator
 * @brief Orchestrates parallel GDB/MI3 processes attached to MPI ranks via FIFO pipes or `gdbserver`.
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

  /** @brief Spawns `mpiexec`/`mpirun` across @p rankCount GDB/MI3 instances. */
  void launchParallelSession(const QString& executable, int rankCount) override;

  /** @brief Spawns `mpiexec`/`mpirun` across @p rankCount GDB/MI3 instances with @p mpiArgs. */
  void launchParallelSession(const QString& executable,
                             int rankCount,
                             const QString& mpiArgs) override;

  /** @brief Returns the live inferior OS PID for @p rank, or `0` if not running. */
  qint64 getPidForRank(int rank) const override;

  /** @brief Attaches a live `gdb --interpreter=mi3` session to a running OS process. */
  bool attachToProcess(qint64 pid) override;

  /** @brief Attaches one GDB/MI3 rank session per target OS PID via `-target-attach <pid>`. */
  bool attachToPids(const QList<qint64>& pids);

  /** @brief Attaches one GDB/MI3 rank session per target `ProcessEntry` (ordered by `mpiRank`). */
  bool attachToProcesses(const QList<ProcessEntry>& targets);

  /** @brief Connects one GDB/MI3 rank session per remote `gdbserver` / `qemu-user -g` endpoint. */
  bool attachToRemoteTargets(const QList<RemoteTargetSpec>& targets);

  /** @brief Returns `"gdb"` as the canonical backend engine identifier. */
  QString activeEngineName() const override { return QStringLiteral("gdb"); }

  /** @brief Pre-validates configured `gdb` and `mpirun`/`mpiexec` binaries before starting UI sessions. */
  bool validateLaunchEnvironment(int ranks, QString* outError = nullptr) const override;

  /** @brief Writes raw bytes into @p rankId's live inferior address space via `-data-write-memory-bytes`. */
  void writeMemory(int rankId, uint64_t address, const QByteArray& data) override {
    if (!data.isEmpty())
      queueOrWriteCmd(rankId, QStringLiteral("-data-write-memory-bytes 0x%1 \"%2\"\n")
                                  .arg(address, 0, 16)
                                  .arg(QString::fromLatin1(data.toHex())));
  }

  /** @brief Registers an active source-line breakpoint (with optional @p condition and @p rankFilter). */
  void setBreakpoint(const QString& file,
                     int line,
                     const QString& condition = QString(),
                     int rankFilter = -1) override;
  /** @brief Configures hit ignore count (`ignoreCount > 0`) and Logpoint trace message for `file:line`. */
  void configureBreakpointOptions(const QString& file, int line, int ignoreCount, const QString& logMessage) override;

  /** @brief Formats a GDB/MI `-break-commands <id> "cmd1" ...` command line (empty @p commands clears commands). */
  static QString formatBreakCommandsMi(int bkptId, const QStringList& commands) {
    QString cmd = QStringLiteral("-break-commands %1").arg(bkptId);
    for (const QString& raw : commands) {
      const QString t = raw.trimmed();
      if (t.isEmpty() || t.contains(QLatin1Char('\n')) || t.contains(QLatin1Char('\r')) || t.contains(QChar('\0'))) continue;
      QString esc = t;
      esc.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
      cmd += QStringLiteral(" \"%1\"").arg(esc);
    }
    return cmd + QLatin1Char('\n');
  }

  /** @brief Non-destructively enables or disables an existing breakpoint (`-break-enable` / `-break-disable`). */
  void setBreakpointEnabled(const QString& filePath, int line, bool enabled) override {
    if (filePath.trimmed().isEmpty() || line <= 0) return;
    IBackendCoordinator::setBreakpointEnabled(filePath, line, enabled);
    const QString locFull = QStringLiteral("%1:%2").arg(filePath).arg(line);
    const QString locBase = QStringLiteral("%1:%2").arg(QFileInfo(filePath).fileName()).arg(line);
    for (auto& bp : m_pendingBreakpoints)
      if (bp.line == line && (bp.file == filePath || QFileInfo(bp.file).fileName() == QFileInfo(filePath).fileName())) bp.enabled = enabled;
    const QString verb = enabled ? QStringLiteral("-break-enable") : QStringLiteral("-break-disable");
    for (size_t i = 0; i < m_processes.size(); ++i) {
      if (!m_processes[i]) continue;
      for (auto it = m_processes[i]->state.breakpoints.constBegin(); it != m_processes[i]->state.breakpoints.constEnd(); ++it) {
        if (it.value() == locFull || it.value() == locBase || QFileInfo(it.value().section(QLatin1Char(':'), 0, -2)).fileName() + QLatin1Char(':') + it.value().section(QLatin1Char(':'), -1) == locBase)
          queueOrWriteCmd(static_cast<int>(i), QStringLiteral("%1 %2\n").arg(verb).arg(it.key()));
      }
    }
  }

  /** @brief Inserts a one-shot temporary breakpoint (`-break-insert -t`) at `filePath:line`. */
  void insertTemporaryBreakpoint(const QString& filePath, int line, const QString& condition = QString(), int targetRank = -1) override {
    if (filePath.trimmed().isEmpty() || line <= 0) return;
    m_breakpointsCleared = false;
    IBackendCoordinator::insertTemporaryBreakpoint(filePath, line, condition, targetRank);
    const QString cleanCond = condition.trimmed();
    m_pendingBreakpoints.erase(std::remove_if(m_pendingBreakpoints.begin(), m_pendingBreakpoints.end(), [&](const PendingBreakpoint& bp) { return bp.file == filePath && bp.line == line; }), m_pendingBreakpoints.end());
    m_pendingBreakpoints.push_back({filePath, line, cleanCond, targetRank, 0, QString(), true, true, {}});
    QString escCond = cleanCond;
    escCond.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    const QString loc = QStringLiteral("%1:%2").arg(filePath).arg(line);
    const QString cmd = cleanCond.isEmpty() ? QStringLiteral("-break-insert -t -f %1\n").arg(loc) : QStringLiteral("-break-insert -t -c \"%1\" %2\n").arg(escCond, loc);
    for (size_t i = 0; i < m_processes.size(); ++i) {
      if (m_processes[i] && (targetRank < 0 || targetRank == static_cast<int>(i))) queueOrWriteCmd(static_cast<int>(i), cmd);
    }
  }

  /** @brief Installs function-symbol regex breakpoints via `-interpreter-exec console "rbreak <pattern>"`. */
  void insertRegexBreakpoint(const QString& regexPattern) override {
    const QString pat = regexPattern.trimmed();
    if (pat.isEmpty() || pat.contains(QLatin1Char('\n')) || pat.contains(QLatin1Char('\r')) || pat.contains(QChar('\0'))) return;
    if (!m_pendingRegexBreakpoints.contains(pat)) m_pendingRegexBreakpoints.append(pat);
    QString esc = pat;
    esc.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
    const QString cmd = QStringLiteral("-interpreter-exec console \"rbreak %1\"\n").arg(esc);
    for (size_t i = 0; i < m_processes.size(); ++i) if (m_processes[i]) queueOrWriteCmd(static_cast<int>(i), cmd);
  }

  /** @brief Attaches or clears GDB hit commands (`-break-commands <id> ...`) on `filePath:line`. */
  void setBreakpointCommands(const QString& filePath, int line, const QStringList& commands) override {
    if (filePath.trimmed().isEmpty() || line <= 0) return;
    QStringList cleaned;
    for (const QString& ln : commands) if (const QString t = ln.trimmed(); !t.isEmpty() && !t.contains(QLatin1Char('\n')) && !t.contains(QLatin1Char('\r'))) cleaned.append(t);
    IBackendCoordinator::setBreakpointCommands(filePath, line, cleaned);
    const QString locFull = QStringLiteral("%1:%2").arg(filePath).arg(line);
    const QString locBase = QStringLiteral("%1:%2").arg(QFileInfo(filePath).fileName()).arg(line);
    for (auto& bp : m_pendingBreakpoints)
      if (bp.line == line && (bp.file == filePath || QFileInfo(bp.file).fileName() == QFileInfo(filePath).fileName())) bp.commands = cleaned;
    for (size_t i = 0; i < m_processes.size(); ++i) {
      if (!m_processes[i]) continue;
      for (auto it = m_processes[i]->state.breakpoints.constBegin(); it != m_processes[i]->state.breakpoints.constEnd(); ++it) {
        if (it.value() == locFull || it.value() == locBase || QFileInfo(it.value().section(QLatin1Char(':'), 0, -2)).fileName() + QLatin1Char(':') + it.value().section(QLatin1Char(':'), -1) == locBase)
          queueOrWriteCmd(static_cast<int>(i), formatBreakCommandsMi(it.key(), cleaned));
      }
    }
  }

  /** @brief Returns breakpoint records created dynamically via `rbreak` or `=breakpoint-created`. */
  QVector<BreakpointSpec> regexCreatedBreakpoints() const { return m_regexCreatedBreakpoints; }

  /** @brief Clears all pending and live breakpoints across all rank sessions. */
  void clearBreakpoints() override;

  /** @brief Enables or disables GDB Process Record (`record full`) on @p rank. */
  void setReverseRecordingEnabled(int rank, bool enabled) override;
  /** @brief Returns true when GDB Process Record is active on @p rank. */
  bool isReverseRecordingEnabled(int rank) const override;
  /** @brief Executes `-exec-continue --reverse` on @p rank. */
  void reverseContinue(int rank) override;
  /** @brief Executes `-exec-next --reverse` on @p rank. */
  void reverseStepOver(int rank) override;
  /** @brief Executes `-exec-step --reverse` on @p rank. */
  void reverseStepInto(int rank) override;
  /** @brief Executes `-exec-step-instruction --reverse` on @p rank. */
  void reverseStepInstruction(int rank) override;

  /** @brief Returns the filesystem path of the target executable. */
  QString getCurrentBinaryPath() const override { return m_currentBinaryPath; }

  /** @brief Returns the human-readable launch CWD + command summary for the active session. */
  QString lastLaunchCommandSummary() const override { return m_lastLaunchCommandSummary; }

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

  /** @brief Writes a raw MI command line to @p rankId's GDB stdin pipe or FIFO. */
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

  /** @brief Queries `-stack-list-frames 0 63` and `-thread-info` on @p rank when paused. */
  void requestStackAndThreads(int rank) override;

  /**
   * @brief Switches @p rank to @p threadId and @p frameLevel, refreshing locals, registers, and source location.
   */
  void selectStackFrame(int rank, int threadId, int frameLevel) override;

  /**
   * @brief Queries child fields/elements for composite expression @p parentExpression on @p rank via MI varobjs.
   */
  void requestVariableChildren(int rank, const QString& parentExpression) override;

  /**
   * @brief Scrubs @p command via `MiSanitizer` and dispatches CLI/MI input to @p rank (`< 0` = all ranks).
   */
  void sendDebuggerCommand(int rank, const QString& command) override;

  /**
   * @brief Arms a hardware data watchpoint (`-break-watch` / `-r` / `-a`) on @p expression across @p rankFilter.
   */
  void setDataWatchpoint(const QString& expression,
                         WatchpointAccessMode mode,
                         int rankFilter = -1) override;

  /** @brief Deletes any active hardware data watchpoint matching @p expression via `-break-delete`. */
  void removeDataWatchpoint(const QString& expression) override;

  /** @brief Configures `handle <SIG> <stop|nostop> <print|noprint> <pass|nopass>` across active ranks. */
  void setSignalDisposition(const QString& signalName, bool stop, bool print, bool pass) override;
  /** @brief Queries `-interpreter-exec console "info handle"` and emits `signalDispositionsReady`. */
  void requestSignalDispositions() override;
  /** @brief Installs a C++ exception, Linux syscall, or shared-library catchpoint across active ranks. */
  void insertCatchpoint(const CatchpointSpec& spec) override;
  /** @brief Removes an installed catchpoint by its assigned ID @p gdbId across active ranks. */
  void removeCatchpoint(int gdbId) override;
  /** @brief Queries `-interpreter-exec console "complete <prefix>"` and emits `consoleCompletionsReady`. */
  void requestConsoleCompletions(const QString& prefix) override;
  /** @brief Returns the list of currently installed catchpoints tracked by this coordinator. */
  QVector<CatchpointSpec> activeCatchpoints() const { return m_activeCatchpoints; }

  /** @brief Creates a copy-on-write process checkpoint (`checkpoint`) on @p rank with optional @p note. */
  void createCheckpoint(int rank, const QString& note = QString()) override {
    const int r = qMax(0, rank);
    if (r >= static_cast<int>(m_processes.size()) || !m_processes[r]) return;
    m_pendingCheckpointNote[r] = note.trimmed();
    m_pendingCheckpointCreate[r] = true;
    m_checkpointCreateBuffer[r].clear();
    queueOrWriteCmd(r, QStringLiteral("212-interpreter-exec console \"checkpoint\"\n"));
  }

  /** @brief Restores checkpoint @p checkpointId (`restart <id>`) on @p rank and refreshes stack/locals/registers. */
  void restoreCheckpoint(int rank, int checkpointId) override {
    const int r = qMax(0, rank);
    if (checkpointId < 0 || r >= static_cast<int>(m_processes.size()) || !m_processes[r]) return;
    queueOrWriteCmd(r, QStringLiteral("-interpreter-exec console \"restart %1\"\n").arg(checkpointId));
    requestStackAndThreads(r);
    queueOrWriteCmd(r, QStringLiteral("201-stack-list-variables --all-values\n"));
    queueOrWriteCmd(r, QStringLiteral("-data-list-register-values x\n"));
  }

  /** @brief Deletes checkpoint @p checkpointId (`delete checkpoint <id>`) on @p rank. */
  void deleteCheckpoint(int rank, int checkpointId) override {
    const int r = qMax(0, rank);
    if (checkpointId <= 0 || r >= static_cast<int>(m_processes.size()) || !m_processes[r]) return;
    auto& list = m_rankCheckpoints[r];
    for (int i = list.size() - 1; i >= 0; --i) if (list[i].id == checkpointId) list.removeAt(i);
    queueOrWriteCmd(r, QStringLiteral("-interpreter-exec console \"delete checkpoint %1\"\n").arg(checkpointId));
    emit checkpointsReady(r, list);
  }

  /** @brief Queries active copy-on-write checkpoints (`info checkpoints`) for @p rank. */
  void requestCheckpoints(int rank) override {
    const int r = qMax(0, rank);
    if (r >= static_cast<int>(m_processes.size()) || !m_processes[r]) {
      emit checkpointsReady(r, m_rankCheckpoints.value(r));
      return;
    }
    m_pendingInfoCheckpoints[r] = true;
    m_infoCheckpointsBuffer[r].clear();
    queueOrWriteCmd(r, QStringLiteral("213-interpreter-exec console \"info checkpoints\"\n"));
  }

  /** @brief Returns cached copy-on-write checkpoints tracked for @p rank. */
  QVector<ProcessCheckpoint> checkpointsForRank(int rank) const { return m_rankCheckpoints.value(qMax(0, rank)); }

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

  /** @brief Emitted when stack frame local variables (`201-stack-list-variables`) arrive for @p rankId. */
  void localsUpdated(int rankId, const QJsonArray& locals);

  /** @brief Emitted for every raw GDB/MI line received from @p rankId. */
  void gdbOutputReceived(int rankId, const QString& output);

  /** @brief Emitted whenever a command string is written to @p rankId's GDB stdin. */
  void commandSentToGdb(int rankId, const QString& cmd);

  /** @brief Emitted when a source editor hover expression (`100-data-evaluate-expression`) resolves. */
  void hoverEvaluationComplete(const QString& varName, const QString& result, QPoint globalPos);

  /** @brief Emitted when `-data-read-memory-bytes` returns hex contents starting at @p beginAddress. */
  void memoryDataReady(int rankId, qint64 beginAddress, const QString& hexContents);

 public slots:
  /** @brief Broadcasts `-exec-next` across stopped MPI ranks. */
  virtual void stepAll();
  /** @brief Broadcasts `-exec-continue` across stopped MPI ranks. */
  virtual void continueAll();
  /** @brief Broadcasts `-exec-run` across all MPI ranks. */
  virtual void runAll();
  /** @brief Broadcasts `-exec-interrupt` across all MPI ranks. */
  void haltAll() override;
  /** @brief Sends `SIGINT` / `-exec-interrupt` to a single focused rank @p rankId. */
  virtual void pauseFocusedRank(int rankId);

  /** @brief Implements `IBackendCoordinator::terminateSession()` via `terminateAllSessions()`. */
  void terminateSession() override;

  /** @brief Disconnects signals and stops all rank GDB/FIFO/MPI processes (`silentShutdown` suppresses UI reset signals). */
  void stopAll(bool silentShutdown = false);

  /** @brief Steps over one source line (`threadId <= 0` = all stopped ranks, `> 0` = rank `threadId - 1`). */
  void stepOver(int threadId) override {
    if (threadId <= 0 || m_processes.empty()) stepAll();
    else dispatchStoppedExecCmd(threadId - 1, QStringLiteral("-exec-next\n"));
  }

  /** @brief Steps into callees (`-exec-step`) on @p threadId (`<= 0` = all stopped ranks, `> 0` = `rankId + 1`). */
  void stepInto(int threadId) override;

  /** @brief Finishes the current stack frame (`-exec-finish`) on @p threadId (`<= 0` = all stopped ranks, `> 0` = `rankId + 1`). */
  void stepOut(int threadId) override;

  /** @brief Steps one machine instruction (`-exec-step-instruction`) on @p threadId (`rankId + 1`). */
  void stepInstruction(int threadId) override;

  /** @brief Steps over one machine instruction (`-exec-next-instruction`) on @p rank (`< 0` = all stopped ranks). */
  void nextInstruction(int rank = -1) override;

  /** @brief Executes until `filePath:line` is reached (`-exec-until "<path>:<line>"`) on @p rank. */
  void runToCursor(const QString& filePath, int line, int rank = -1) override;

  /** @brief Executes until instruction address `*0x<hex>` is reached (`-exec-until *0x<hex>`) on @p rank. */
  void runToAddress(uint64_t address, int rank = -1) override;

  /** @brief Relocates execution PC to `filePath:line` (`-exec-jump "<path>:<line>"`) on @p rank. */
  void jumpToLocation(const QString& filePath, int line, int rank = -1) override;

  /** @brief Relocates execution PC to machine address `*0x<hex>` (`-exec-jump *0x<hex>`) on @p rank. */
  void jumpToAddress(uint64_t address, int rank = -1) override;

  /** @brief Forces an immediate return from the current stack frame (`-exec-return [expr]`) on @p rank. */
  void forceEarlyReturn(const QString& returnExpr = QString(), int rank = -1) override;

  /** @brief Resumes execution (`threadId > 0` = rank `threadId - 1`, `<= 0` honors `gl_scoped_continue_rank` or resumes all). */
  void continueExecution(int threadId) override;

  /** @brief Interrupts execution (`threadId <= 0` = all ranks, `> 0` = rank `threadId - 1`). */
  void pauseExecution(int threadId) override {
    if (threadId <= 0) haltAll(); else pauseFocusedRank(threadId - 1);
  }

  /** @brief Interrupts 0-based MPI rank @p rankId (`< 0` = all ranks). */
  void pauseRank(int rankId) override {
    if (rankId < 0) haltAll(); else pauseFocusedRank(rankId);
  }

  /** @brief Registers @p varName as a tracked GDB/MI variable object (`-var-create`) across all ranks. */
  void registerWatchVariable(const QString& varName) override;

  /** @brief Unregisters @p varName and deletes any active GDB/MI `-var-delete` objects across ranks. */
  void unregisterWatchVariable(const QString& varName) override {
    m_watchVariables.erase(std::remove(m_watchVariables.begin(), m_watchVariables.end(), varName), m_watchVariables.end());
    for (auto it = m_varNameMap.begin(); it != m_varNameMap.end(); ++it) {
      if (it.value().contains(varName)) {
        const QString vo = it.value().take(varName);
        if (!vo.isEmpty()) writeCmd(it.key(), QStringLiteral("-var-delete %1\n").arg(vo));
      }
    }
  }

  /** @brief Returns currently registered cross-rank watch variable expressions. */
  const std::vector<QString>& registeredWatchVariables() const { return m_watchVariables; }

  /** @brief Sends a user or console command to @p rankId (or all ranks when `rankId == -1`). */
  void sendCommand(int rankId, const QString& cmd);

  /** @brief Slot triggered by `QProcess::readyReadStandardOutput` or FIFO notifier for @p rankId. */
  void handleGdbOutput(int rankId);

  /** @brief Evaluates @p varName for an editor hover tooltip at screen coordinate @p globalPos. */
  void evaluateHoverVariable(int rankId, const QString& varName, QPoint globalPos);

 private:
  /** @brief Writes @p cmd immediately if stopped, or queues it behind a transient SIGINT if running. */
  void queueOrWriteCmd(int rankId, const QString& cmd);

  /** @brief Writes an execution control MI command to @p rank (or all stopped ranks when `rank < 0`). */
  void dispatchStoppedExecCmd(int rank, const QString& cmd);

  /** @brief Parses MI `202..206` stack, thread, console REPL, and watchpoint records for @p rankId. */
  bool handleMiStackOrThreadRecord(int rankId, const QString& line);

  /** @brief Parses `*stopped,reason=".*watchpoint-trigger"` fields and emits `watchpointHit`. */
  void handleWatchpointStopRecord(int rankId, const QString& line);

  /**
   * @struct PendingBreakpoint
   * @brief Cached breakpoint specification synchronized to GDB rank slots on launch or live edit.
   */
  struct PendingBreakpoint {
    QString file;       ///< Source file path.
    int line = 0;       ///< One-based source line number.
    QString condition;  ///< Optional conditional expression (`-c`).
    int rankFilter = -1; ///< Target zero-based rank index, or `-1` for all ranks.
    int ignoreCount = 0; ///< Number of hits to ignore before stopping (0 = stop immediately).
    QString logMessage; ///< Non-empty trace expression message for non-suspending Logpoints.
    bool enabled = true; ///< False when breakpoint is non-destructively disabled.
    bool temporary = false; ///< True for one-shot temporary (`tbreak`) breakpoints.
    QStringList commands; ///< Automated GDB commands executed on hit (`-break-commands`).
  };

  /**
   * @struct RankProcess
   * @brief Per-rank GDB process/FIFO handle, line accumulation buffer, and telemetry state.
   */
  struct RankProcess {
    int id = 0;                          ///< Zero-based MPI rank index.
    std::unique_ptr<QProcess> process;   ///< Optional standalone `gdb` child process.
    int fifoInFd = -1;                   ///< Non-blocking FIFO write descriptor to rank GDB stdin.
    int fifoOutFd = -1;                  ///< Non-blocking FIFO read descriptor from rank GDB stdout.
    QSocketNotifier* fifoNotifier = nullptr; ///< Event-loop read notifier on `fifoOutFd`.
    pid_t inferiorPid = 0;               ///< Live target rank OS PID reported by `=thread-group-started`.
    RankState state;                     ///< Live execution, location, register, and watch telemetry.
    QString buffer;                      ///< Partial line accumulation buffer for GDB stdout.
    QString lastHoverVarName;            ///< Variable name for pending token `100` hover evaluation.
    QPoint lastHoverPos;                 ///< Screen position for pending token `100` hover tooltip.
    QString lastEvalExpression;          ///< Expression string for pending token `101` evaluation.
    QStringList pendingAfterStopCmds;    ///< MI commands queued while running to execute upon transient SIGINT stop.
    bool autoContinueAfterPending = false; ///< True when rank was interrupted solely to apply pending breakpoints.
    bool pendingAttach = false;            ///< True while `-target-attach` or `-target-select` awaits confirmation.
    bool isAttached = false;               ///< True when attached to an external OS/remote target (detach, do not kill).
    int pendingInterruptSignals = 0;       ///< Count of in-flight synthetic SIGINTs sent for transient interrupts.
    QString pendingLogpointMessage;        ///< Active Logpoint template awaiting `209-stack-list-variables`.
    QMap<QString, QString> pendingLogpointArgs; ///< Frame args captured from `*stopped` for Logpoint expansion.
  };

  /**
   * @struct PendingWatchpoint
   * @brief Cached hardware data watchpoint specification synchronized across matching rank sessions.
   */
  struct PendingWatchpoint {
    QString expression;                                      ///< Watched symbol or address expression.
    WatchpointAccessMode mode = WatchpointAccessMode::Write; ///< Hardware watchpoint access trigger mode.
    int rankFilter = -1;                                     ///< Target rank index (`-1` = all ranks).
  };

  /**
   * @struct PendingVarChildQuery
   * @brief Tracks an in-flight temporary GDB varobj child listing that unwraps access pseudo-nodes.
   */
  struct PendingVarChildQuery {
    int rankId = 0;            ///< Target MPI rank index.
    QString parentExpression;  ///< Original expression requested by VariableTreeModel.
    QString rootVarObj;        ///< Temporary GDB varobj deleted once all children finish listing.
    int inFlightLists = 0;     ///< Outstanding `-var-list-children` commands (including unwrapped scopes).
    QVariantList resolved;     ///< Collected real member descriptors (`name`, `value`, `type`, `hasChildren`).
  };

  /** @brief Parses `^done,bkpt={...}`, `=breakpoint-created`, and `=breakpoint-deleted` records for @p rankId. */
  bool handleBreakpointMiRecord(int rankId, const QString& line) {
    auto& rp = m_processes[rankId];
    if (line.startsWith("^done,bkpt={") || line.startsWith("=breakpoint-created,bkpt={")) {
      static const QRegularExpression idRe("number=\"(\\d+)\""), fileRe("(?:fullname|file)=\"([^\"]+)\""), lnRe("line=\"(\\d+)\""), fnRe("func=\"([^\"]+)\"");
      if (const auto idM = idRe.match(line); idM.hasMatch()) {
        const int bkptId = idM.captured(1).toInt();
        const auto fM = fileRe.match(line); const auto lM = lnRe.match(line);
        const QString fPath = fM.hasMatch() ? fM.captured(1) : QString();
        const int bkptLn = lM.hasMatch() ? lM.captured(1).toInt() : 0;
        const bool isTemp = line.contains(QStringLiteral("disp=\"del\"")), isCreated = line.startsWith("=breakpoint-created");
        if (!fPath.isEmpty() && bkptLn > 0) rp->state.breakpoints[bkptId] = QString("%1:%2").arg(fPath).arg(bkptLn);
        else if (const auto fnM = fnRe.match(line); fnM.hasMatch()) rp->state.breakpoints[bkptId] = fnM.captured(1);
        if (isCreated) {
          BreakpointSpec sp; sp.file = fPath; sp.line = bkptLn; sp.temporary = isTemp; sp.gdbId = bkptId;
          sp.enabled = !line.contains(QStringLiteral("enabled=\"n\""));
          if (const auto fnM = fnRe.match(line); fnM.hasMatch()) sp.regexPattern = fnM.captured(1);
          m_regexCreatedBreakpoints.append(sp);
        }
        emit breakpointRecordCreated(rankId, fPath, bkptLn, bkptId, isTemp);
        const QString bkptBase = QFileInfo(fPath).fileName();
        for (const auto& bp : m_pendingBreakpoints) if (bkptLn > 0 && bp.line == bkptLn && QFileInfo(bp.file).fileName() == bkptBase) {
          if (bp.ignoreCount > 0) writeCmd(rankId, QString("-break-after %1 %2\n").arg(bkptId).arg(bp.ignoreCount));
          if (!bp.enabled) writeCmd(rankId, QString("-break-disable %1\n").arg(bkptId));
          if (!bp.commands.isEmpty()) writeCmd(rankId, formatBreakCommandsMi(bkptId, bp.commands));
        }
        if (rp->state.currentState == "sync_flushing") { if (--rp->state.pendingBreakpoints <= 0) { writeCmd(rankId, "-exec-continue\n"); rp->state.currentState = "running"; } }
        else if (rp->state.currentState != "stopped") emit rankStateChanged(rankId, rp->state);
      }
      return true;
    }
    if (line.startsWith("=breakpoint-deleted,id=")) {
      static const QRegularExpression delRe("id=\"(\\d+)\"");
      if (const auto dm = delRe.match(line); dm.hasMatch()) {
        const QString loc = rp->state.breakpoints.take(dm.captured(1).toInt());
        if (!loc.isEmpty()) {
          const QString f = loc.section(QLatin1Char(':'), 0, -2);
          const int ln = loc.section(QLatin1Char(':'), -1).toInt();
          for (auto it = m_pendingBreakpoints.begin(); it != m_pendingBreakpoints.end(); ++it)
            if (it->temporary && it->line == ln && QFileInfo(it->file).fileName() == QFileInfo(f).fileName()) {
              emit temporaryBreakpointHit(rankId, it->file, it->line);
              m_pendingBreakpoints.erase(it);
              break;
            }
        }
      }
      return true;
    }
    return false;
  }

  /** @brief Parses MI tokens `212` (`checkpoint`) and `213` (`info checkpoints`) plus `checkpoint <id>: fork returned pid <pid>.` console records. */
  bool handleCheckpointMiRecord(int rankId, const QString& line) {
    auto unquoteConsole = [](const QString& raw) {
      QString p = raw.mid(1).trimmed();
      if (p.startsWith(QLatin1Char('"')) && p.endsWith(QLatin1Char('"')) && p.size() >= 2) p = p.mid(1, p.size() - 2);
      p.replace(QStringLiteral("\\n"), QStringLiteral("\n")).replace(QStringLiteral("\\t"), QStringLiteral("\t")).replace(QStringLiteral("\\\""), QStringLiteral("\"")).replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
      return p;
    };
    auto upsertCheckpoint = [&](int r, const ProcessCheckpoint& entry) {
      auto& list = m_rankCheckpoints[r];
      for (auto& existing : list) {
        if (existing.id == entry.id) {
          existing.forkPid = entry.forkPid;
          if (!entry.location.isEmpty()) existing.location = entry.location;
          if (!entry.note.isEmpty()) existing.note = entry.note;
          if (existing.timestamp.isEmpty()) existing.timestamp = entry.timestamp;
          return;
        }
      }
      list.append(entry);
    };
    if (m_pendingCheckpointCreate.value(rankId, false) && (line.startsWith(QLatin1Char('~')) || line.startsWith(QLatin1Char('&')))) {
      m_checkpointCreateBuffer[rankId] += unquoteConsole(line);
      return true;
    }
    if (m_pendingCheckpointCreate.value(rankId, false) && (line.startsWith(QLatin1String("212^done")) || line.startsWith(QLatin1String("212^error")) || line == QLatin1String("^done"))) {
      m_pendingCheckpointCreate[rankId] = false;
      const QString buf = m_checkpointCreateBuffer.take(rankId);
      const QString note = m_pendingCheckpointNote.take(rankId);
      if (!line.contains(QLatin1String("^error"))) {
        static const QRegularExpression cpRe(QStringLiteral("checkpoint\\s+(\\d+)\\s*:\\s*fork returned pid\\s+(\\d+)"), QRegularExpression::CaseInsensitiveOption);
        if (const auto m = cpRe.match(buf); m.hasMatch()) {
          ProcessCheckpoint cp;
          cp.id = m.captured(1).toInt();
          cp.rank = rankId;
          cp.forkPid = m.captured(2).toLongLong();
          cp.timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
          cp.note = note;
          if (rankId >= 0 && rankId < static_cast<int>(m_processes.size()) && m_processes[rankId]) {
            const auto& st = m_processes[rankId]->state;
            const QString fn = st.currentFunction.isEmpty() ? QStringLiteral("main") : st.currentFunction;
            cp.location = (!st.currentFile.isEmpty() && st.currentLine > 0) ? QStringLiteral("%1 (%2:%3)").arg(fn, QFileInfo(st.currentFile).fileName()).arg(st.currentLine) : fn;
          }
          upsertCheckpoint(rankId, cp);
          emit checkpointsReady(rankId, m_rankCheckpoints.value(rankId));
          requestCheckpoints(rankId);
        }
      }
      return true;
    }
    if (m_pendingInfoCheckpoints.value(rankId, false) && (line.startsWith(QLatin1Char('~')) || line.startsWith(QLatin1Char('&')))) {
      m_infoCheckpointsBuffer[rankId] += unquoteConsole(line);
      return true;
    }
    if (line.startsWith(QLatin1String("213^done")) || line.startsWith(QLatin1String("213^error"))) {
      m_pendingInfoCheckpoints[rankId] = false;
      const QString buf = m_infoCheckpointsBuffer.take(rankId);
      if (line.startsWith(QLatin1String("213^done"))) {
        QMap<int, ProcessCheckpoint> prevById;
        for (const auto& existing : m_rankCheckpoints.value(rankId)) prevById.insert(existing.id, existing);
        static const QRegularExpression rowRe(QStringLiteral("^\\s*\\*?\\s*(\\d+)\\s+process\\s+(\\d+)\\s*(.*)$"), QRegularExpression::CaseInsensitiveOption);
        QVector<ProcessCheckpoint> parsed;
        for (const QString& rawLn : buf.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
          if (const auto m = rowRe.match(rawLn); m.hasMatch()) {
            const int cid = m.captured(1).toInt();
            const ProcessCheckpoint prev = prevById.value(cid);
            ProcessCheckpoint cp;
            cp.id = cid;
            cp.rank = rankId;
            cp.forkPid = m.captured(2).toLongLong();
            cp.location = m.captured(3).trimmed();
            if (cp.location.startsWith(QStringLiteral("at "), Qt::CaseInsensitive)) cp.location = cp.location.mid(3).trimmed();
            cp.timestamp = prev.timestamp.isEmpty() ? QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")) : prev.timestamp;
            cp.note = prev.note;
            parsed.append(cp);
          }
        }
        if (!parsed.isEmpty()) m_rankCheckpoints[rankId] = parsed;
        emit checkpointsReady(rankId, m_rankCheckpoints.value(rankId));
      }
      return true;
    }
    return false;
  }

  std::unique_ptr<QProcess> m_mpirunProcess;             ///< Master `mpirun`/`mpiexec` launcher process.
  std::vector<std::unique_ptr<RankProcess>> m_processes; ///< Per-rank GDB frontend slots.
  std::vector<PendingBreakpoint> m_pendingBreakpoints;   ///< Configured breakpoints synced across launches.
  std::vector<PendingWatchpoint> m_pendingWatchpoints;   ///< Configured hardware watchpoints.
  QString m_fifoTempDir;                                 ///< Temporary directory owning per-rank MI FIFOs.
  QMap<int, QMap<QString, QString>> m_varNameMap;        ///< `rankId -> (varName -> gdbVarId)`.
  QMap<int, QMap<QString, QString>> m_varNameRevMap;     ///< `rankId -> (gdbVarId -> varName)`.
  QMap<int, qint64> m_rankPids;                          ///< Live target OS PID per MPI rank index.
  QMap<int, QVector<StackFrameEntry>> m_rankFrames;      ///< Cached 64-frame call stack per MPI rank.
  QMap<int, QVector<ThreadEntry>> m_rankThreads;         ///< Cached OS/OpenMP threads per MPI rank.
  QMap<int, int> m_activeThreadIds;                      ///< Active 1-based GDB thread ID per MPI rank.
  QMap<int, int> m_pendingFrameSelect;                   ///< Deferred frame level after cross-thread switch.
  QMap<int, int> m_pendingConsoleCmdCount;               ///< Count of in-flight `204` CLI commands per rank.
  QMap<int, QString> m_consoleReplyBuffer;               ///< Buffered `~`/`&` console lines per rank.
  QMap<int, QMap<QString, int>> m_watchpointIds;         ///< `rankId -> (expression -> gdbBkptNum)`.
  QMap<int, PendingVarChildQuery> m_varChildQueries;     ///< Active composite child queries by query ID.
  QMap<int, int> m_tokenToVarCreateQuery;                ///< MI token -> query ID for `-var-create`.
  QMap<int, int> m_tokenToVarListQuery;                  ///< MI token -> query ID for `-var-list-children`.
  int m_nextVarQueryId = 1;                              ///< Monotonic ID generator for `m_varChildQueries`.
  int m_nextVarQueryToken = 4000;                        ///< Monotonic MI command token (>= 4000) to avoid watch prefix collisions.
  std::vector<QString> m_watchVariables;                 ///< Ordered list of watched variable expressions.
  QString m_currentBinaryPath;                           ///< Target binary path for the active session.
  QString m_lastLaunchCommandSummary;                    ///< Human-readable CWD and launch command summary.
  bool m_breakpointsCleared = false;                     ///< True when clearBreakpoints() explicitly emptied session breakpoints.
  QMap<QString, SignalDisposition> m_signalDispositions; ///< Configured `handle <SIG>` dispositions re-applied on launch.
  QVector<CatchpointSpec> m_activeCatchpoints;           ///< Active C++ exception, syscall, and dlopen/dlclose catchpoints.
  QMap<int, bool> m_pendingInfoHandle;                   ///< True while `210-interpreter-exec console "info handle"` is in flight.
  QMap<int, QString> m_infoHandleBuffer;                 ///< Accumulated `~` output lines for `210` `info handle` query.
  QMap<int, QString> m_pendingCompletePrefix;            ///< Active `211` `complete <prefix>` prefix per rank.
  QMap<int, QString> m_completeBuffer;                   ///< Accumulated `~` output lines for `211` `complete` query.
  int m_nextCatchpointId = 1;                            ///< Monotonic logical ID generator for installed catchpoints.
  QStringList m_pendingRegexBreakpoints;                 ///< Registered `rbreak` patterns replayed on launch.
  QVector<BreakpointSpec> m_regexCreatedBreakpoints;     ///< Breakpoint records created via `rbreak` / `=breakpoint-created`.
  QMap<int, QVector<ProcessCheckpoint>> m_rankCheckpoints; ///< Active copy-on-write checkpoints per MPI rank.
  QMap<int, bool> m_pendingCheckpointCreate;             ///< True while `212` `checkpoint` is in flight per rank.
  QMap<int, QString> m_checkpointCreateBuffer;           ///< Buffered `~` output for `212` `checkpoint`.
  QMap<int, QString> m_pendingCheckpointNote;            ///< Pending user note for in-flight `checkpoint` creation.
  QMap<int, bool> m_pendingInfoCheckpoints;              ///< True while `213` `info checkpoints` is in flight per rank.
  QMap<int, QString> m_infoCheckpointsBuffer;            ///< Buffered `~` output for `213` `info checkpoints`.
  bool m_shuttingDown = false;                           ///< Suppresses async MI/FIFO processing while stopping or destroying.
};

}  // namespace gridlock
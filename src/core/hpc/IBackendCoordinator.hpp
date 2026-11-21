/**
 * @file IBackendCoordinator.hpp
 * @brief Abstract base interface for parallel debugger backend coordinators (GDB/MI and DAP).
 *
 * Unifies execution control (`stepOver`, `stepInto`, `continueExecution`, `pauseExecution`),
 * expression evaluation, and memory reading across `GdbRankCoordinator` and `DapCoordinator`.
 */

#pragma once

#include <sys/types.h>

#include <cstdint>

#include <QByteArray>
#include <QJsonArray>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>

/**
 * @struct StackFrameEntry
 * @brief Represents a single unwound call-stack frame for an MPI rank thread.
 */
struct StackFrameEntry {
  int level = 0;        ///< Zero-based frame depth (`0` = innermost active frame).
  int frameId = 0;      ///< Backend-specific frame identifier (e.g., DAP `frameId` or `level`).
  quint64 address = 0;  ///< Instruction pointer or return-site virtual address.
  QString function;     ///< Demangled C/C++ function or symbol name.
  QString file;         ///< Resolved DWARF source file path, if available.
  int line = 0;         ///< One-based DWARF source line number, or `0` if unknown.
};

/**
 * @struct ThreadEntry
 * @brief Describes a live OS or OpenMP thread inside an MPI rank process.
 */
struct ThreadEntry {
  int threadId = 1;         ///< One-based debugger thread identifier.
  QString name;             ///< Thread name or OS target descriptor (e.g., `"main"`).
  QString state;            ///< Operational state string (`"stopped"`, `"running"`, etc.).
  QString topFrameSummary;  ///< Summary of the thread's top frame (`func @ file:line`).
  bool isCurrent = false;   ///< True when this thread is currently selected in the debugger.
};

/**
 * @struct SignalDisposition
 * @brief Represents the debugger's stop/print/pass disposition for a POSIX signal (`handle <SIG>`).
 */
struct SignalDisposition {
  QString signal;       ///< Canonical signal identifier (e.g., `"SIGFPE"`, `"SIGUSR1"`).
  bool stop = true;     ///< True if the debugger suspends execution when the signal arrives.
  bool print = true;    ///< True if the debugger logs a message when the signal arrives.
  bool pass = false;    ///< True if the signal is forwarded to the target application.
  QString description;  ///< Human-readable description of the signal.
};

/**
 * @struct CatchpointSpec
 * @brief Describes an active C++ exception, Linux syscall, or shared-library (`dlopen`/`dlclose`) catchpoint.
 */
struct CatchpointSpec {
  enum class Kind {
    CppThrow,    ///< Break on C++ `throw` (`-catch-throw [-r <regex>]`).
    CppRethrow,  ///< Break on C++ `rethrow` (`-catch-rethrow [-r <regex>]`).
    CppCatch,    ///< Break on C++ `catch` (`-catch-catch [-r <regex>]`).
    Syscall,     ///< Break on Linux syscall entry/exit (`catch syscall <filter>`).
    LoadLib,     ///< Break on shared-library load / `dlopen` (`-catch-load [-r <regex>]`).
    UnloadLib    ///< Break on shared-library unload / `dlclose` (`-catch-unload [-r <regex>]`).
  };
  Kind kind = Kind::CppThrow;  ///< Catchpoint event category.
  QString filter;              ///< Optional exception/library regex or space/comma-separated syscall names.
  int gdbId = -1;              ///< Assigned debugger breakpoint/catchpoint ID (`> 0` once installed).
  bool enabled = true;         ///< True when the catchpoint is active.
};

/**
 * @struct ProcessCheckpoint
 * @brief Snapshot descriptor for a GDB copy-on-write process checkpoint (`checkpoint` / `info checkpoints`).
 */
struct ProcessCheckpoint {
  int id = 0;         ///< GDB checkpoint identifier (0 = main process, >= 1 = forked snapshot).
  int rank = 0;       ///< Zero-based MPI rank index owning this checkpoint.
  qint64 forkPid = 0; ///< OS PID of the copy-on-write forked snapshot process.
  QString location;   ///< Function + `file:line` or address where the snapshot was taken.
  QString timestamp;  ///< Creation timestamp string (`HH:mm:ss`).
  QString note;       ///< Optional user-supplied checkpoint description.
};

Q_DECLARE_METATYPE(StackFrameEntry)
Q_DECLARE_METATYPE(QVector<StackFrameEntry>)
Q_DECLARE_METATYPE(ThreadEntry)
Q_DECLARE_METATYPE(QVector<ThreadEntry>)
Q_DECLARE_METATYPE(SignalDisposition)
Q_DECLARE_METATYPE(QVector<SignalDisposition>)
Q_DECLARE_METATYPE(CatchpointSpec)
Q_DECLARE_METATYPE(QVector<CatchpointSpec>)
Q_DECLARE_METATYPE(ProcessCheckpoint)
Q_DECLARE_METATYPE(QVector<ProcessCheckpoint>)

/**
 * @class IBackendCoordinator
 * @brief Polymorphic Qt interface for multi-rank debugger session engines.
 */
class IBackendCoordinator : public QObject {
  Q_OBJECT

 public:
  /**
   * @brief Constructs the coordinator with an optional parent QObject.
   */
  explicit IBackendCoordinator(QObject* parent = nullptr) : QObject(parent) {}

  /**
   * @brief Virtual destructor for safe polymorphic cleanup.
   */
  ~IBackendCoordinator() override = default;

  /**
   * @brief Steps over the current source line on the specified rank/thread context.
   *
   * In DAP mode, `threadId` maps MPI Rank `r` to DAP Thread `r + 1`.
   */
  virtual void stepOver(int threadId) = 0;

  /** @brief Steps into the function call on the current line for @p threadId. */
  virtual void stepInto(int threadId) = 0;

  /** @brief Steps out of the current stack frame (`-exec-finish` / DAP `stepOut`) for @p threadId. */
  virtual void stepOut(int threadId) { Q_UNUSED(threadId); }

  /** @brief Steps a single machine instruction on @p threadId (`rankId + 1`). */
  virtual void stepInstruction(int threadId) = 0;

  /** @brief Steps over a single machine instruction (`-exec-next-instruction`) on @p rank (`< 0` = all stopped ranks). */
  virtual void nextInstruction(int rank = -1) { Q_UNUSED(rank); }

  /** @brief Executes until source location `filePath:line` is reached (`-exec-until`) on @p rank. */
  virtual void runToCursor(const QString& filePath, int line, int rank = -1) {
    Q_UNUSED(filePath); Q_UNUSED(line); Q_UNUSED(rank);
  }

  /** @brief Executes until machine instruction @p address is reached (`-exec-until *0x...`) on @p rank. */
  virtual void runToAddress(uint64_t address, int rank = -1) {
    Q_UNUSED(address); Q_UNUSED(rank);
  }

  /** @brief Relocates the program counter to `filePath:line` (`-exec-jump`) on @p rank. */
  virtual void jumpToLocation(const QString& filePath, int line, int rank = -1) {
    Q_UNUSED(filePath); Q_UNUSED(line); Q_UNUSED(rank);
  }

  /** @brief Relocates the program counter to machine @p address (`-exec-jump *0x...`) on @p rank. */
  virtual void jumpToAddress(uint64_t address, int rank = -1) {
    Q_UNUSED(address); Q_UNUSED(rank);
  }

  /** @brief Forces an immediate return from the active stack frame (`-exec-return [expr]`) on @p rank. */
  virtual void forceEarlyReturn(const QString& returnExpr = QString(), int rank = -1) {
    Q_UNUSED(returnExpr); Q_UNUSED(rank);
  }

  /** @brief Resumes execution of @p threadId until the next breakpoint or signal. */
  virtual void continueExecution(int threadId) = 0;

  /** @brief Interrupts/pauses execution of 1-based @p threadId (`rankId + 1`). */
  virtual void pauseExecution(int threadId) = 0;

  /** @brief Interrupts/pauses execution of 0-based MPI rank @p rankId. */
  virtual void pauseRank(int rankId) { pauseExecution(rankId + 1); }

  /** @brief Enables or disables GDB Process Record / time-travel recording on @p rank. */
  virtual void setReverseRecordingEnabled(int rank, bool enabled) {
    if (enabled) m_reverseRecordingRanks.insert(qMax(0, rank));
    else m_reverseRecordingRanks.remove(qMax(0, rank));
  }

  /** @brief Returns true when time-travel process recording is enabled on @p rank. */
  virtual bool isReverseRecordingEnabled(int rank) const {
    return m_reverseRecordingRanks.contains(qMax(0, rank));
  }

  /** @brief Runs backward in recorded execution history until the previous breakpoint or boundary. */
  virtual void reverseContinue(int rank) { Q_UNUSED(rank); }

  /** @brief Steps backward over the previous source statement on @p rank. */
  virtual void reverseStepOver(int rank) { Q_UNUSED(rank); }

  /** @brief Steps backward into the previous function call on @p rank. */
  virtual void reverseStepInto(int rank) { Q_UNUSED(rank); }

  /** @brief Steps backward by a single machine instruction on @p rank. */
  virtual void reverseStepInstruction(int rank) { Q_UNUSED(rank); }

  /** @brief Evaluates @p expression in the current stack frame of @p rankId. */
  virtual void evaluateExpression(int rankId, const QString& expression) = 0;

  /** @brief Terminates all attached debugger processes and MPI ranks. */
  virtual void terminateSession() = 0;

  /** @brief Reads @p count bytes starting at @p memoryReference from @p rankId. */
  virtual void readMemory(int rankId, const QString& memoryReference, int count) = 0;

  /** @brief Spawns a parallel debugging session for @p binaryPath across @p ranks processes. */
  virtual void launchParallelSession(const QString& binaryPath, int ranks) = 0;

  /**
   * @brief Spawns a parallel debugging session with explicit MPI launcher arguments @p mpiArgs.
   */
  virtual void launchParallelSession(const QString& binaryPath,
                                     int ranks,
                                     const QString& mpiArgs) {
    Q_UNUSED(mpiArgs);
    launchParallelSession(binaryPath, ranks);
  }

  /**
   * @brief Stores CLI arguments and inferior working directory for the next launched debug session.
   * @param targetArgs Program command-line arguments string (e.g. `"config.toml --threads 4"`).
   * @param workingDir Initial working directory path for the spawned target binary.
   */
  virtual void setTargetExecutionContext(const QString& targetArgs, const QString& workingDir) {
    setProperty("gridlock_target_args", targetArgs);
    setProperty("gridlock_working_dir", workingDir);
  }

  /** @brief Returns the configured inferior command-line arguments string. */
  QString targetArguments() const { return property("gridlock_target_args").toString(); }

  /** @brief Returns the configured inferior working directory path. */
  QString workingDirectory() const { return property("gridlock_working_dir").toString(); }

  /** @brief Extra per-breakpoint execution options (ignore count, logpoint, enablement, temporary one-shot, GDB commands). */
  struct BreakpointExtraOptions {
    int ignoreCount = 0;  ///< Number of hits to ignore before suspending (0 = stop immediately).
    QString logMessage;   ///< Non-empty trace message for non-suspending Logpoints.
    bool enabled = true;  ///< False when breakpoint is non-destructively disabled (`-break-disable`).
    bool temporary = false; ///< True for one-shot temporary breakpoints (`tbreak` / `-break-insert -t`).
    QStringList commands; ///< Automated GDB commands executed on hit (`-break-commands <id> ...`).
  };

  /** @brief Complete breakpoint lifecycle specification tracked across ranks and UI models. */
  struct BreakpointSpec {
    QString file;          ///< Source file path (empty for pure regex symbol breakpoints).
    int line = 0;          ///< 1-based source line number.
    QString condition;     ///< Optional C/C++ predicate expression.
    int rankFilter = -1;   ///< Target 0-based MPI rank (`-1` = all ranks).
    int ignoreCount = 0;   ///< Number of hits to ignore before suspending.
    QString logMessage;    ///< Optional non-suspending Logpoint trace message.
    bool enabled = true;   ///< True when active; false when disabled non-destructively.
    bool temporary = false;///< True for one-shot temporary (`tbreak`) breakpoints.
    QStringList commands;  ///< Multi-line GDB command list executed on hit (`commands ... end`).
    QString regexPattern;  ///< Regex pattern when created via `rbreak`.
    int gdbId = -1;        ///< Debugger-assigned breakpoint number (`> 0` once installed).
  };

  /**
   * @brief Registers an active breakpoint at `file:line` with optional @p condition and @p rankFilter (`-1` = all ranks).
   */
  virtual void setBreakpoint(const QString& file,
                             int line,
                             const QString& condition = QString(),
                             int rankFilter = -1) = 0;

  /** @brief Configures hit ignore count (`ignoreCount > 0`) and non-suspending Logpoint message for `file:line`. */
  virtual void configureBreakpointOptions(const QString& file, int line, int ignoreCount, const QString& logMessage) {
    auto& opts = m_breakpointExtras[QStringLiteral("%1:%2").arg(file).arg(line)];
    opts.ignoreCount = qMax(0, ignoreCount);
    opts.logMessage = logMessage.trimmed();
  }

  /** @brief Non-destructively enables or disables an existing breakpoint (`-break-enable` / `-break-disable`). */
  virtual void setBreakpointEnabled(const QString& filePath, int line, bool enabled) {
    m_breakpointExtras[QStringLiteral("%1:%2").arg(filePath).arg(line)].enabled = enabled;
  }

  /** @brief Inserts a one-shot temporary breakpoint (`tbreak` / `-break-insert -t`) at `filePath:line`. */
  virtual void insertTemporaryBreakpoint(const QString& filePath,
                                         int line,
                                         const QString& condition = QString(),
                                         int targetRank = -1) {
    Q_UNUSED(condition); Q_UNUSED(targetRank);
    m_breakpointExtras[QStringLiteral("%1:%2").arg(filePath).arg(line)].temporary = true;
  }

  /** @brief Installs function-symbol regex breakpoints via GDB `rbreak <regexPattern>`. */
  virtual void insertRegexBreakpoint(const QString& regexPattern) { Q_UNUSED(regexPattern); }

  /** @brief Attaches or clears automated GDB hit commands (`-break-commands <id> ...`) for `filePath:line`. */
  virtual void setBreakpointCommands(const QString& filePath, int line, const QStringList& commands) {
    m_breakpointExtras[QStringLiteral("%1:%2").arg(filePath).arg(line)].commands = commands;
  }

  /** @brief Clears all active breakpoints tracked by this coordinator. */
  virtual void clearBreakpoints() = 0;

  /** @brief Returns the canonical engine identifier (`"gdb"` or `"lldb-dap"`). */
  virtual QString activeEngineName() const = 0;

  /** @brief Returns the OS process ID of the target MPI rank @p rank, or `0` if not running. */
  virtual qint64 getPidForRank(int rank) const = 0;

  /**
   * @brief Attaches the debugger backend directly to a running local OS process @p pid.
   * @return `true` if the attach session started successfully, `false` otherwise.
   */
  virtual bool attachToProcess(qint64 pid) = 0;

  /** @brief Returns the filesystem path to the currently loaded target executable. */
  virtual QString getCurrentBinaryPath() const = 0;

  /** @brief Returns the number of MPI rank processes in the active session. */
  virtual int getProcessCount() const = 0;

  /** @brief Queries up to 64 call-stack frames and OS/OpenMP threads for @p rank. */
  virtual void requestStackAndThreads(int rank) { Q_UNUSED(rank); }

  /**
   * @brief Selects @p threadId and @p frameLevel on @p rank, refreshing locals, registers, and source location.
   */
  virtual void selectStackFrame(int rank, int threadId, int frameLevel) {
    Q_UNUSED(rank);
    Q_UNUSED(threadId);
    Q_UNUSED(frameLevel);
  }

  /**
   * @enum WatchpointAccessMode
   * @brief Hardware debug register access trigger mode for data watchpoints.
   */
  enum class WatchpointAccessMode {
    Write = 0,     ///< Break on memory write (`-break-watch <expr>` / DAP `"write"`).
    Read = 1,      ///< Break on memory read (`-break-watch -r <expr>` / DAP `"read"`).
    ReadWrite = 2  ///< Break on memory read or write (`-break-watch -a <expr>` / DAP `"readWrite"`).
  };

  /**
   * @brief Dispatches a sanitized CLI or MI debugger command to @p rank (`< 0` broadcasts to all active ranks).
   */
  Q_INVOKABLE virtual void sendDebuggerCommand(int rank, const QString& command) {
    Q_UNUSED(rank);
    Q_UNUSED(command);
  }

  /**
   * @brief Arms a hardware data watchpoint on @p expression across @p rankFilter (`-1` = all ranks).
   */
  virtual void setDataWatchpoint(const QString& expression,
                                 WatchpointAccessMode mode,
                                 int rankFilter = -1) {
    Q_UNUSED(expression);
    Q_UNUSED(mode);
    Q_UNUSED(rankFilter);
  }

  /**
   * @brief Qt meta-invokable overload forwarding integer @p mode (`0`=Write, `1`=Read, `2`=ReadWrite).
   */
  Q_INVOKABLE void setDataWatchpoint(const QString& expression,
                                     int mode,
                                     int rankFilter = -1) {
    const WatchpointAccessMode parsedMode =
        (mode == 1)   ? WatchpointAccessMode::Read
        : (mode == 2) ? WatchpointAccessMode::ReadWrite
                      : WatchpointAccessMode::Write;
    setDataWatchpoint(expression, parsedMode, rankFilter);
  }

  /** @brief Deletes any active hardware data watchpoint matching @p expression across ranks. */
  Q_INVOKABLE virtual void removeDataWatchpoint(const QString& expression) {
    Q_UNUSED(expression);
  }

  /**
   * @brief Queries child fields/elements for composite expression @p parentExpression on @p rank.
   */
  virtual void requestVariableChildren(int rank, const QString& parentExpression) {
    Q_UNUSED(rank);
    Q_UNUSED(parentExpression);
  }

  /** @brief Returns a human-readable summary of the active session's CWD and launch command. */
  virtual QString lastLaunchCommandSummary() const { return {}; }

  /**
   * @brief Configures the debugger's `Stop` / `Print` / `Pass` disposition for @p signalName across all ranks.
   */
  virtual void setSignalDisposition(const QString& signalName,
                                    bool stop,
                                    bool print,
                                    bool pass) {
    Q_UNUSED(signalName);
    Q_UNUSED(stop);
    Q_UNUSED(print);
    Q_UNUSED(pass);
  }

  /** @brief Requests the current signal disposition table (`info handle`) from the active debugger backend. */
  virtual void requestSignalDispositions() {}

  /** @brief Installs a C++ exception, Linux syscall, or shared-library catchpoint across all active ranks. */
  virtual void insertCatchpoint(const CatchpointSpec& spec) { Q_UNUSED(spec); }

  /** @brief Removes an installed catchpoint by its debugger breakpoint/catchpoint ID @p gdbId. */
  virtual void removeCatchpoint(int gdbId) { Q_UNUSED(gdbId); }

  /** @brief Queries GDB/debugger tab completions for @p prefix via `complete <prefix>`. */
  virtual void requestConsoleCompletions(const QString& prefix) { Q_UNUSED(prefix); }

  /** @brief Creates a copy-on-write process snapshot (`checkpoint`) on @p rank with optional @p note. */
  virtual void createCheckpoint(int rank, const QString& note = QString()) { Q_UNUSED(rank); Q_UNUSED(note); }

  /** @brief Restores a previously saved checkpoint (`restart <checkpointId>`) on @p rank. */
  virtual void restoreCheckpoint(int rank, int checkpointId) { Q_UNUSED(rank); Q_UNUSED(checkpointId); }

  /** @brief Deletes a saved checkpoint (`delete checkpoint <checkpointId>`) on @p rank. */
  virtual void deleteCheckpoint(int rank, int checkpointId) { Q_UNUSED(rank); Q_UNUSED(checkpointId); }

  /** @brief Requests the active checkpoint table (`info checkpoints`) for @p rank. */
  virtual void requestCheckpoints(int rank) { Q_UNUSED(rank); }

 signals:
  /** @brief Emitted when @p rankId stops at @p file and @p line. */
  void locationChanged(int rankId, const QString& file, int line);

  /** @brief Emitted when an evaluated expression on @p rankId produces @p result. */
  void expressionEvaluated(int rankId, const QString& expr, const QString& result);

  /** @brief Emitted when stdout/stderr or debugger console output arrives. */
  void targetOutputReceived(const QString& category, const QString& output);

  /** @brief Emitted when target program stdout/stderr arrives for @p rank (free of MI noise). */
  void programOutputReceived(int rank, const QString& text, bool isError);

  /** @brief Emitted when a raw memory read from @p rankId at @p address completes. */
  void memoryRead(int rankId, const QString& address, const QByteArray& data);

  /** @brief Emitted when CPU register values for @p rankId are refreshed. */
  void registersUpdated(int rankId, const QJsonArray& registers);

  /** @brief Emitted when up to 64 call-stack frames are parsed for @p rank. */
  void stackTraceUpdated(int rank, const QVector<StackFrameEntry>& frames);

  /** @brief Emitted when child entries for @p parentExpression on @p rank are resolved. */
  void variableChildrenUpdated(int rank,
                               const QString& parentExpression,
                               const QVariantList& children);

  /** @brief Emitted when OS/OpenMP thread descriptors are parsed for @p rank. */
  void threadsUpdated(int rank, const QVector<ThreadEntry>& threads);

  /** @brief Emitted when an interactive console/REPL command on @p rank produces @p output. */
  void debuggerConsoleOutput(int rank, const QString& output, bool isError);

  /** @brief Emitted when a hardware data watchpoint triggers on @p rank (`oldValue` -> `newValue`). */
  void watchpointHit(int rank,
                     const QString& expression,
                     const QString& oldValue,
                     const QString& newValue);

  /** @brief Emitted when the signal disposition table (`info handle`) is parsed for @p rank. */
  void signalDispositionsReady(int rank, const QVector<SignalDisposition>& dispositions);

  /** @brief Emitted when the active catchpoints list changes. */
  void catchpointsUpdated(const QVector<CatchpointSpec>& catchpoints);

  /** @brief Emitted when GDB `complete <prefix>` tab completions are resolved. */
  void consoleCompletionsReady(const QString& prefix, const QStringList& completions);

  /** @brief Emitted when a new breakpoint record (including `rbreak` matches) is parsed from the backend. */
  void breakpointRecordCreated(int rank, const QString& file, int line, int gdbId, bool temporary);

  /** @brief Emitted when a one-shot temporary (`tbreak`) breakpoint triggers and auto-deletes. */
  void temporaryBreakpointHit(int rank, const QString& file, int line);

  /** @brief Emitted when the active copy-on-write process checkpoints list for @p rank updates. */
  void checkpointsReady(int rank, const QVector<ProcessCheckpoint>& checkpoints);

 protected:
  QMap<QString, BreakpointExtraOptions> m_breakpointExtras;  ///< Extra ignoreCount/logMessage keyed by "file:line".
  QSet<int> m_reverseRecordingRanks;                         ///< Ranks with active time-travel process recording.
};

/// Top-level convenience alias for hardware data watchpoint access modes.
using WatchpointAccessMode = IBackendCoordinator::WatchpointAccessMode;

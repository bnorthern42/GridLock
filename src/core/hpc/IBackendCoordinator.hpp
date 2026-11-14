/**
 * @file IBackendCoordinator.hpp
 * @brief Abstract base interface for parallel debugger backend coordinators (GDB/MI and DAP).
 *
 * Unifies execution control (`stepOver`, `stepInto`, `continueExecution`, `pauseExecution`),
 * expression evaluation, and memory reading across `GdbRankCoordinator` and `DapCoordinator`.
 */

#pragma once

#include <sys/types.h>

#include <QByteArray>
#include <QJsonArray>
#include <QMetaType>
#include <QObject>
#include <QString>
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

Q_DECLARE_METATYPE(StackFrameEntry)
Q_DECLARE_METATYPE(QVector<StackFrameEntry>)
Q_DECLARE_METATYPE(ThreadEntry)
Q_DECLARE_METATYPE(QVector<ThreadEntry>)

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

  /** @brief Steps a single machine instruction on @p threadId (`rankId + 1`). */
  virtual void stepInstruction(int threadId) = 0;

  /** @brief Resumes execution of @p threadId until the next breakpoint or signal. */
  virtual void continueExecution(int threadId) = 0;

  /** @brief Interrupts/pauses execution of 1-based @p threadId (`rankId + 1`). */
  virtual void pauseExecution(int threadId) = 0;

  /** @brief Interrupts/pauses execution of 0-based MPI rank @p rankId. */
  virtual void pauseRank(int rankId) { pauseExecution(rankId + 1); }

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
   * @brief Registers an active breakpoint at `file:line` with optional @p condition and @p rankFilter (`-1` = all ranks).
   */
  virtual void setBreakpoint(const QString& file,
                             int line,
                             const QString& condition = QString(),
                             int rankFilter = -1) = 0;

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

 signals:
  /** @brief Emitted when @p rankId stops at @p file and @p line. */
  void locationChanged(int rankId, const QString& file, int line);

  /** @brief Emitted when an evaluated expression on @p rankId produces @p result. */
  void expressionEvaluated(int rankId, const QString& expr, const QString& result);

  /** @brief Emitted when stdout/stderr or debugger console output arrives. */
  void targetOutputReceived(const QString& category, const QString& output);

  /** @brief Emitted when a raw memory read from @p rankId at @p address completes. */
  void memoryRead(int rankId, const QString& address, const QByteArray& data);

  /** @brief Emitted when CPU register values for @p rankId are refreshed. */
  void registersUpdated(int rankId, const QJsonArray& registers);

  /** @brief Emitted when up to 64 call-stack frames are parsed for @p rank. */
  void stackTraceUpdated(int rank, const QVector<StackFrameEntry>& frames);

  /** @brief Emitted when OS/OpenMP thread descriptors are parsed for @p rank. */
  void threadsUpdated(int rank, const QVector<ThreadEntry>& threads);
};

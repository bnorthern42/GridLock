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
#include <QObject>
#include <QString>

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

  /** @brief Returns the OS process ID (`pid_t`) of the target rank @p rankId. */
  virtual pid_t getPidForRank(int rankId) const = 0;

  /** @brief Returns the filesystem path to the currently loaded target executable. */
  virtual QString getCurrentBinaryPath() const = 0;

  /** @brief Returns the number of MPI rank processes in the active session. */
  virtual int getProcessCount() const = 0;

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
};

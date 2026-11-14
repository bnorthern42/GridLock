/**
 * @file DeadlockAnalyzer.hpp
 * @brief Automatic MPI collective/point-to-point deadlock detector across stopped ranks.
 *
 * Inspects shallow stack frames (`-stack-list-frames 0 3`) whenever an MPI rank
 * enters the `stopped` state and flags ranks blocked inside synchronous MPI or
 * PMPI routines (`MPI_Barrier`, `MPI_Recv`, `MPI_Wait`, `MPI_Allreduce`, etc.).
 */

#pragma once

#include <QMap>
#include <QObject>
#include <QString>
#include <QVector>

#include "IBackendCoordinator.hpp"

namespace gridlock {
class GdbRankCoordinator;
struct RankState;
}  // namespace gridlock

namespace gridlock::core {

/**
 * @struct DeadlockInfo
 * @brief Describes an MPI rank stalled inside a blocking point-to-point or collective MPI call.
 */
struct DeadlockInfo {
  int rankId = 0;                    ///< Zero-based MPI rank index.
  QString blockedFunction;           ///< Blocking MPI/PMPI routine (`MPI_Recv`, `MPI_Bcast`, etc.).
  QString file;                      ///< Source file path of the caller frame.
  int line = 0;                      ///< 1-based source line number of the caller frame.
  QString reason;                    ///< Human-readable wait-cycle or collective divergence summary.
  bool isCollectiveMismatch = false; ///< True when paused ranks diverged across mismatched collectives.
};

/**
 * @class DeadlockAnalyzer
 * @brief Observes live rank stack traces and detects point-to-point wait cycles and collective divergence.
 */
class DeadlockAnalyzer : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString lastDiagnosticAlert READ lastDiagnosticAlert)

 public:
  /**
   * @brief Attaches the analyzer to a `GdbRankCoordinator` instance (preserving existing GDB hooks).
   */
  explicit DeadlockAnalyzer(gridlock::GdbRankCoordinator* coordinator,
                            QObject* parent = nullptr);

  /**
   * @brief Attaches the analyzer polymorphically to any `IBackendCoordinator` (`gdb` or `lldb-dap`).
   */
  explicit DeadlockAnalyzer(::IBackendCoordinator* coordinator,
                            QObject* parent = nullptr);

  /** @brief Returns the latest human-readable cycle or collective divergence diagnostic summary. */
  QString lastDiagnosticAlert() const { return m_lastDiagnosticAlert; }

  /** @brief Resets all cached per-rank MPI stall states and diagnostic summaries. */
  void reset();

 public slots:
  /**
   * @brief Evaluates a rank's live unwound `StackFrameEntry` backtrace for MPI cycles and collective divergence.
   */
  void updateRankBacktrace(int rankId, const QVector<StackFrameEntry>& frames);

 signals:
  /** @brief Emitted when a stopped rank is confirmed to be stalled in a blocking MPI routine or divergence. */
  void deadlockDetected(const gridlock::core::DeadlockInfo& info);

  /** @brief Emitted when @p rankId resumes execution or stops outside any blocking MPI call. */
  void rankCleared(int rankId);

  /** @brief Emitted when two or more paused ranks are stalled in mismatched MPI collective primitives. */
  void collectiveDivergenceDetected(const QString& summary);

 private slots:
  /** @brief Requests `-stack-list-frames 0 3` when a rank stops, or clears its alert when running. */
  void onRankStateChanged(int rankId, const gridlock::RankState& state);

  /** @brief Parses untokenized `-stack-list-frames` MI replies and checks blocking MPI functions. */
  void onGdbOutputReceived(int rankId, const QString& output);

 private:
  /** @brief Evaluates cross-rank `m_activeBlockedRanks` for collective divergence or point-to-point wait cycles. */
  void evaluateClusterStalls(int updatedRankId);

  ::IBackendCoordinator* m_backend = nullptr;            ///< Non-owning pointer to polymorphic backend coordinator.
  gridlock::GdbRankCoordinator* m_coordinator = nullptr; ///< Non-owning pointer when backed by `GdbRankCoordinator`.
  QMap<int, bool> m_pendingStackRequest;                 ///< Tracks ranks awaiting untokenized `-stack-list-frames`.
  QMap<int, DeadlockInfo> m_activeBlockedRanks;          ///< Currently paused ranks stalled inside blocking MPI calls.
  QString m_lastDiagnosticAlert;                         ///< Latest formatted cycle or collective mismatch summary.
};

}  // namespace gridlock::core
Q_DECLARE_METATYPE(gridlock::core::DeadlockInfo)

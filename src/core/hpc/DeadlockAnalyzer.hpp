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

namespace gridlock {
class GdbRankCoordinator;
struct RankState;
}  // namespace gridlock

namespace gridlock::core {

/**
 * @struct DeadlockInfo
 * @brief Describes an MPI rank stalled inside a blocking MPI call.
 */
struct DeadlockInfo {
  int rankId;               ///< Zero-based MPI rank index.
  QString blockedFunction;  ///< Name of the blocking MPI/PMPI routine (`MPI_Recv`, `MPI_Barrier`, etc.).
  QString file;             ///< Source file path of the caller frame.
  int line;                 ///< 1-based source line number of the caller frame.
};

/**
 * @class DeadlockAnalyzer
 * @brief Observes `GdbRankCoordinator` state transitions and identifies ranks stalled in MPI calls.
 */
class DeadlockAnalyzer : public QObject {
  Q_OBJECT

 public:
  /**
   * @brief Attaches the analyzer to @p coordinator's rank state and GDB/MI output signals.
   */
  explicit DeadlockAnalyzer(gridlock::GdbRankCoordinator* coordinator,
                            QObject* parent = nullptr);

 signals:
  /** @brief Emitted when a stopped rank is confirmed to be inside a blocking MPI routine. */
  void deadlockDetected(const gridlock::core::DeadlockInfo& info);

  /** @brief Emitted when @p rankId resumes execution or stops outside any blocking MPI call. */
  void rankCleared(int rankId);

 private slots:
  /** @brief Requests `-stack-list-frames 0 3` when a rank stops, or clears its alert when running. */
  void onRankStateChanged(int rankId, const gridlock::RankState& state);

  /** @brief Parses `-stack-list-frames` MI replies and checks against known blocking MPI functions. */
  void onGdbOutputReceived(int rankId, const QString& output);

 private:
  gridlock::GdbRankCoordinator* m_coordinator; ///< Non-owning pointer to the GDB rank coordinator.
  QMap<int, bool> m_pendingStackRequest;       ///< Tracks ranks awaiting a `-stack-list-frames` reply.
};

}  // namespace gridlock::core

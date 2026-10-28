/**
 * @file DeadlockAnalyzer.cpp
 * @brief Implementation of stack-frame and source-line MPI deadlock detection.
 *
 * Implements a two-pass heuristic that inspects GDB/MI `-stack-list-frames`
 * responses whenever an MPI rank pauses:
 * 1. Symbol-based pass over frames 0..3 for `MPI_*` / `PMPI_*` collective and
 *    point-to-point routines.
 * 2. Source-line fallback pass that reads the user source file at frame 0 when
 *    vendor MPI shared libraries are compiled without symbol tables.
 */

#include "DeadlockAnalyzer.hpp"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include "GdbRankCoordinator.hpp"

namespace gridlock::core {

/**
 * @brief Connects rank state transitions and GDB output signals from @p coordinator.
 * @param coordinator Non-owning pointer to the active `GdbRankCoordinator`.
 * @param parent Optional QObject parent.
 */
DeadlockAnalyzer::DeadlockAnalyzer(gridlock::GdbRankCoordinator* coordinator,
                                   QObject* parent)
    : QObject(parent), m_coordinator(coordinator) {
  if (m_coordinator) {
    connect(m_coordinator, &gridlock::GdbRankCoordinator::rankStateChanged,
            this, &DeadlockAnalyzer::onRankStateChanged);
    connect(m_coordinator, &gridlock::GdbRankCoordinator::gdbOutputReceived,
            this, &DeadlockAnalyzer::onGdbOutputReceived);
  }
}

/**
 * @brief Issues a shallow stack query when @p rankId stops, or clears deadlock status when running.
 * @param rankId Zero-based MPI rank index.
 * @param state Updated `RankState` snapshot from `GdbRankCoordinator`.
 */
void DeadlockAnalyzer::onRankStateChanged(int rankId,
                                          const gridlock::RankState& state) {
  if (state.currentState == "stopped") {
    // Request top 4 frames (0..3) so wrapper calls inside libmpi/libpmpi are visible.
    m_pendingStackRequest[rankId] = true;
    m_coordinator->writeCmd(rankId, "-stack-list-frames 0 3\n");
  } else if (state.currentState == "running") {
    // Clear any stale deadlock badge as soon as the rank resumes execution.
    m_pendingStackRequest[rankId] = false;
    emit rankCleared(rankId);
  }
}

/**
 * @brief Inspects `-stack-list-frames` output for blocking MPI/PMPI symbols or source lines.
 * @param rankId Zero-based MPI rank index.
 * @param output Raw GDB/MI response line.
 */
void DeadlockAnalyzer::onGdbOutputReceived(int rankId, const QString& output) {
  // Ignore unrelated GDB/MI records if no deadlock stack query is in flight for `rankId`.
  if (!m_pendingStackRequest.value(rankId, false)) {
    return;
  }

  if (output.startsWith("^done,stack=[")) {
    m_pendingStackRequest[rankId] = false;

    // Precompiled regular expressions for extracting MI frame attributes.
    static const QRegularExpression funcRegex("func=\"([^\"]+)\"");
    static const QRegularExpression fileRegex("file=\"([^\"]+)\"");
    static const QRegularExpression lineRegex("line=\"(\\d+)\"");

    // Canonical list of synchronous MPI and PMPI calls capable of stalling a rank.
    const QStringList blockingFuncs = {
        "MPI_Barrier",     "MPI_Wait",       "MPI_Waitall",   "MPI_Recv",
        "MPI_Send",        "MPI_Ssend",      "MPI_Bcast",     "MPI_Reduce",
        "MPI_Allreduce",   "MPI_Gather",     "MPI_Scatter",   "MPI_Allgather",
        "MPI_Alltoall",    "PMPI_Barrier",   "PMPI_Wait",     "PMPI_Waitall",
        "PMPI_Recv",       "PMPI_Send",      "PMPI_Ssend",    "PMPI_Bcast",
        "PMPI_Reduce",     "PMPI_Allreduce", "PMPI_Gather",   "PMPI_Scatter",
        "PMPI_Allgather",  "PMPI_Alltoall"};

    QRegularExpressionMatchIterator funcIt = funcRegex.globalMatch(output);
    QRegularExpressionMatchIterator fileIt = fileRegex.globalMatch(output);
    QRegularExpressionMatchIterator lineIt = lineRegex.globalMatch(output);

    QString detectedFunc;
    QString detectedFile;
    int detectedLine = 0;
    bool found = false;

    // Capture the top user frame's file and line number for attribution and source fallback.
    QString topFile;
    int topLine = 0;
    if (fileIt.hasNext()) {
      topFile = fileIt.next().captured(1);
    }
    if (lineIt.hasNext()) {
      topLine = lineIt.next().captured(1).toInt();
    }

    // Pass 1: Check whether any frame symbol in `0..3` matches a blocking MPI routine.
    while (funcIt.hasNext()) {
      const QRegularExpressionMatch match = funcIt.next();
      const QString func = match.captured(1);
      if (blockingFuncs.contains(func)) {
        detectedFunc = func;
        detectedFile = topFile;
        detectedLine = topLine;
        found = true;
        break;
      }
    }

    // Pass 2 (Fallback): When libmpi is stripped of symbols, inspect the source line of frame 0.
    if (!found && !topFile.isEmpty() && topLine > 0) {
      QFile file(topFile);
      if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        int currentLine = 1;
        while (!in.atEnd()) {
          const QString lineCode = in.readLine();
          if (currentLine == topLine) {
            for (const QString& bFunc : blockingFuncs) {
              if (lineCode.contains(bFunc)) {
                detectedFunc = bFunc;
                detectedFile = topFile;
                detectedLine = topLine;
                found = true;
                break;
              }
            }
            break;
          }
          currentLine++;
        }
      }
    }

    // Notify subscribers whether this rank is stalled in an MPI call or clear.
    if (found) {
      DeadlockInfo info;
      info.rankId = rankId;
      info.blockedFunction = detectedFunc;
      info.file = detectedFile;
      info.line = detectedLine;
      emit deadlockDetected(info);
    } else {
      emit rankCleared(rankId);
    }
  }
}

}  // namespace gridlock::core

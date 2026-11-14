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
#include <QSet>
#include <QStringList>
#include <QTextStream>

#include "DapCoordinator.hpp"
#include "GdbRankCoordinator.hpp"

namespace gridlock::core {
namespace {

/**
 * @brief Canonical list of synchronous MPI and PMPI primitives capable of blocking an MPI rank.
 */
const QStringList& blockingMpiPrimitives() {
  static const QStringList kList = {
      QStringLiteral("MPI_Barrier"),    QStringLiteral("MPI_Wait"),
      QStringLiteral("MPI_Waitall"),    QStringLiteral("MPI_Waitany"),
      QStringLiteral("MPI_Recv"),       QStringLiteral("MPI_Send"),
      QStringLiteral("MPI_Ssend"),      QStringLiteral("MPI_Sendrecv"),
      QStringLiteral("MPI_Bcast"),      QStringLiteral("MPI_Reduce"),
      QStringLiteral("MPI_Allreduce"),  QStringLiteral("MPI_Gather"),
      QStringLiteral("MPI_Scatter"),    QStringLiteral("MPI_Allgather"),
      QStringLiteral("MPI_Alltoall"),   QStringLiteral("MPI_Scan"),
      QStringLiteral("PMPI_Barrier"),   QStringLiteral("PMPI_Wait"),
      QStringLiteral("PMPI_Waitall"),   QStringLiteral("PMPI_Waitany"),
      QStringLiteral("PMPI_Recv"),      QStringLiteral("PMPI_Send"),
      QStringLiteral("PMPI_Ssend"),     QStringLiteral("PMPI_Sendrecv"),
      QStringLiteral("PMPI_Bcast"),     QStringLiteral("PMPI_Reduce"),
      QStringLiteral("PMPI_Allreduce"), QStringLiteral("PMPI_Gather"),
      QStringLiteral("PMPI_Scatter"),   QStringLiteral("PMPI_Allgather"),
      QStringLiteral("PMPI_Alltoall"),  QStringLiteral("PMPI_Scan")};
  return kList;
}

/**
 * @brief Normalizes `PMPI_*` profiling wrapper symbols to their canonical `MPI_*` primitive name.
 * @param sym Raw C/C++ symbol extracted from a paused rank's stack frame.
 * @return Canonical `MPI_*` routine identifier.
 */
QString canonicalMpiName(const QString& sym) {
  return sym.startsWith(QLatin1String("PMPI_")) ? sym.mid(1) : sym;
}

/**
 * @brief Returns true when @p sym is an MPI collective synchronization or reduction primitive.
 * @param sym Candidate MPI or PMPI routine name.
 * @return True if @p sym requires collective participation across communicator ranks.
 */
bool isCollectivePrimitive(const QString& sym) {
  const QString norm = canonicalMpiName(sym);
  static const QSet<QString> kCollectives = {
      QStringLiteral("MPI_Barrier"),   QStringLiteral("MPI_Bcast"),
      QStringLiteral("MPI_Reduce"),    QStringLiteral("MPI_Allreduce"),
      QStringLiteral("MPI_Gather"),    QStringLiteral("MPI_Scatter"),
      QStringLiteral("MPI_Allgather"), QStringLiteral("MPI_Alltoall"),
      QStringLiteral("MPI_Scan")};
  return kCollectives.contains(norm);
}

/**
 * @brief Matches @p rawSymbol against known blocking `MPI_*` / `PMPI_*` routines.
 * @param rawSymbol Demangled frame function string (with optional C++ parameter list).
 * @return Matched blocking MPI primitive name, or empty string if non-blocking.
 */
QString matchBlockingPrimitive(const QString& rawSymbol) {
  for (const QString& bFunc : blockingMpiPrimitives()) {
    if (rawSymbol == bFunc || rawSymbol.startsWith(bFunc + QLatin1Char('('))) {
      return bFunc;
    }
  }
  return QString();
}

}  // namespace

/**
 * @brief Connects rank state transitions, raw MI stack replies, and `stackTraceUpdated` on @p coordinator.
 * @param coordinator Pointer to the multi-rank GDB coordinator backend.
 * @param parent Optional QObject parent.
 */
DeadlockAnalyzer::DeadlockAnalyzer(gridlock::GdbRankCoordinator* coordinator,
                                   QObject* parent)
    : DeadlockAnalyzer(static_cast<::IBackendCoordinator*>(coordinator), parent) {}

/**
 * @brief Connects live `stackTraceUpdated` and engine-specific state lifecycle signals from @p coordinator.
 * @param coordinator Polymorphic `IBackendCoordinator` (`GdbRankCoordinator` or `DapCoordinator`).
 * @param parent Optional QObject parent.
 */
DeadlockAnalyzer::DeadlockAnalyzer(::IBackendCoordinator* coordinator,
                                   QObject* parent)
    : QObject(parent),
      m_backend(coordinator),
      m_coordinator(dynamic_cast<gridlock::GdbRankCoordinator*>(coordinator)) {
  if (!m_backend) return;

  // Ingest live 64-frame backtraces emitted whenever any GDB or LLDB-DAP rank stops.
  connect(m_backend, &::IBackendCoordinator::stackTraceUpdated, this,
          &DeadlockAnalyzer::updateRankBacktrace);

  // Subscribe to GDB per-rank state changes or DAP session state transitions.
  if (m_coordinator) {
    connect(m_coordinator, &gridlock::GdbRankCoordinator::rankStateChanged,
            this, &DeadlockAnalyzer::onRankStateChanged);
    connect(m_coordinator, &gridlock::GdbRankCoordinator::gdbOutputReceived,
            this, &DeadlockAnalyzer::onGdbOutputReceived);
  } else if (auto* dap = dynamic_cast<DapCoordinator*>(m_backend)) {
    // Clear cached rank stalls when a DAP session resumes execution or disconnects.
    connect(dap, &DapCoordinator::stateChanged, this, [this](SessionState st) {
      if (st == SessionState::Running || st == SessionState::Disconnected) reset();
    });
  }
}

/**
 * @brief Clears all tracked rank stall descriptors and resets diagnostic alerts.
 */
void DeadlockAnalyzer::reset() {
  const QList<int> prevRanks = m_activeBlockedRanks.keys();
  m_activeBlockedRanks.clear();
  m_pendingStackRequest.clear();
  m_lastDiagnosticAlert.clear();
  for (int r : prevRanks) emit rankCleared(r);
}

/**
 * @brief Issues a shallow stack query when @p rankId stops, or clears deadlock status when running.
 * @param rankId Zero-based MPI rank index.
 * @param state Updated operational state snapshot for @p rankId.
 */
void DeadlockAnalyzer::onRankStateChanged(int rankId,
                                          const gridlock::RankState& state) {
  if (state.currentState == QLatin1String("stopped")) {
    // Request top 4 frames (0..3) when no live `202` backtrace has cleared the pending flag.
    m_pendingStackRequest[rankId] = true;
    if (m_coordinator) m_coordinator->writeCmd(rankId, QStringLiteral("-stack-list-frames 0 3\n"));
  } else if (state.currentState == QLatin1String("running") ||
             state.currentState == QLatin1String("exited")) {
    // Remove resolved rank from active stall map and emit rankCleared.
    m_pendingStackRequest[rankId] = false;
    m_activeBlockedRanks.remove(rankId);
    emit rankCleared(rankId);
  }
}

/**
 * @brief Inspects live `StackFrameEntry` backtraces (frames `0..7`) for MPI stalls and collective divergence.
 * @param rankId Zero-based MPI rank index whose call stack was unwound.
 * @param frames Ordered stack frames (`0` = innermost active frame).
 */
void DeadlockAnalyzer::updateRankBacktrace(int rankId,
                                           const QVector<StackFrameEntry>& frames) {
  // Cancel any redundant untokenized `-stack-list-frames 0 3` parse once a full backtrace arrives.
  m_pendingStackRequest[rankId] = false;
  if (frames.isEmpty()) {
    m_activeBlockedRanks.remove(rankId);
    emit rankCleared(rankId);
    return;
  }

  QString detectedFunc;
  QString callerFile = frames.front().file;
  int callerLine = frames.front().line;

  // Pass 1: Scan innermost 8 frames for an active blocking MPI/PMPI symbol and user caller site.
  const int maxDepth = std::min<int>(8, frames.size());
  for (int i = 0; i < maxDepth; ++i) {
    const QString matched = matchBlockingPrimitive(frames.at(i).function);
    if (!matched.isEmpty() && detectedFunc.isEmpty()) {
      detectedFunc = matched;
    }
    // Prefer the first user-code caller frame outside libmpi so double-clicking jumps to user C++ source.
    if (!detectedFunc.isEmpty() && !frames.at(i).file.isEmpty() && frames.at(i).line > 0) {
      callerFile = frames.at(i).file;
      callerLine = frames.at(i).line;
      if (!frames.at(i).function.startsWith(QLatin1String("MPI_")) &&
          !frames.at(i).function.startsWith(QLatin1String("PMPI_"))) {
        break;
      }
    }
  }

  // Clear any prior stall alert if the rank paused outside all blocking MPI routines.
  if (detectedFunc.isEmpty()) {
    if (m_activeBlockedRanks.remove(rankId) > 0) emit rankCleared(rankId);
    return;
  }

  // Record this rank's active blocking MPI primitive and evaluate multi-rank cycle/divergence state.
  DeadlockInfo info;
  info.rankId = rankId;
  info.blockedFunction = detectedFunc;
  info.file = callerFile;
  info.line = callerLine;
  m_activeBlockedRanks[rankId] = info;
  evaluateClusterStalls(rankId);
}

/**
 * @brief Detects cross-rank collective call divergence (`MPI_Bcast` vs `MPI_Barrier`) and P2P wait cycles.
 * @param updatedRankId Most recently updated MPI rank index.
 */
void DeadlockAnalyzer::evaluateClusterStalls(int updatedRankId) {
  QSet<QString> distinctCollectives;
  QStringList rankSummaries;
  int p2pWaitCount = 0;

  // Inspect every currently paused rank stalled inside an MPI primitive.
  for (auto it = m_activeBlockedRanks.constBegin(); it != m_activeBlockedRanks.constEnd(); ++it) {
    const QString& fn = it.value().blockedFunction;
    rankSummaries.append(QStringLiteral("Rank %1 in %2").arg(it.key()).arg(fn));
    if (isCollectivePrimitive(fn)) {
      distinctCollectives.insert(canonicalMpiName(fn));
    } else {
      ++p2pWaitCount;
    }
  }

  const bool hasCollectiveMismatch =
      (distinctCollectives.size() >= 2) ||
      (distinctCollectives.size() == 1 && p2pWaitCount >= 1 && m_activeBlockedRanks.size() >= 2);
  const bool hasWaitCycle = (!hasCollectiveMismatch && p2pWaitCount >= 2);

  if (hasCollectiveMismatch) {
    m_lastDiagnosticAlert =
        QStringLiteral("Collective Divergence: %1").arg(rankSummaries.join(QStringLiteral(" vs ")));
    emit collectiveDivergenceDetected(m_lastDiagnosticAlert);
    // Re-broadcast updated mismatch metadata across all participating stalled ranks.
    for (auto it = m_activeBlockedRanks.begin(); it != m_activeBlockedRanks.end(); ++it) {
      it.value().isCollectiveMismatch = true;
      it.value().reason = m_lastDiagnosticAlert;
      emit deadlockDetected(it.value());
    }
    return;
  }

  if (hasWaitCycle) {
    m_lastDiagnosticAlert =
        QStringLiteral("Wait-For Cycle Stall: %1").arg(rankSummaries.join(QStringLiteral(", ")));
    for (auto it = m_activeBlockedRanks.begin(); it != m_activeBlockedRanks.end(); ++it) {
      it.value().isCollectiveMismatch = false;
      it.value().reason = m_lastDiagnosticAlert;
      emit deadlockDetected(it.value());
    }
    return;
  }

  // Single-rank blocking MPI call notification (preserving per-rank stall visibility).
  if (m_activeBlockedRanks.contains(updatedRankId)) {
    DeadlockInfo& single = m_activeBlockedRanks[updatedRankId];
    single.isCollectiveMismatch = false;
    single.reason = QStringLiteral("Blocked in %1").arg(single.blockedFunction);
    m_lastDiagnosticAlert = single.reason;
    emit deadlockDetected(single);
  }
}

/**
 * @brief Inspects untokenized `-stack-list-frames` MI output for blocking MPI/PMPI symbols or source lines.
 */
void DeadlockAnalyzer::onGdbOutputReceived(int rankId, const QString& output) {
  if (!m_pendingStackRequest.value(rankId, false) || !output.startsWith(QLatin1String("^done,stack=["))) {
    return;
  }
  m_pendingStackRequest[rankId] = false;

  // Precompiled regular expressions for extracting MI frame attributes.
  static const QRegularExpression funcRegex(QStringLiteral("func=\"([^\"]+)\""));
  static const QRegularExpression fileRegex(QStringLiteral("file=\"([^\"]+)\""));
  static const QRegularExpression lineRegex(QStringLiteral("line=\"(\\d+)\""));

  QRegularExpressionMatchIterator funcIt = funcRegex.globalMatch(output);
  QRegularExpressionMatchIterator fileIt = fileRegex.globalMatch(output);
  QRegularExpressionMatchIterator lineIt = lineRegex.globalMatch(output);

  const QString topFile = fileIt.hasNext() ? fileIt.next().captured(1) : QString();
  const int topLine = lineIt.hasNext() ? lineIt.next().captured(1).toInt() : 0;

  QString detectedFunc;
  // Pass 1: Match frame symbols against canonical blocking MPI/PMPI routines.
  while (funcIt.hasNext()) {
    const QString matched = matchBlockingPrimitive(funcIt.next().captured(1));
    if (!matched.isEmpty()) {
      detectedFunc = matched;
      break;
    }
  }

  // Pass 2 (Fallback): Inspect source line at frame 0 only when binary symbols are stripped.
  if (detectedFunc.isEmpty() && !topFile.isEmpty() && topLine > 0 && output.contains(QLatin1String("func=\"??\""))) {
    QFile file(topFile);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&file);
      for (int cur = 1; !in.atEnd(); ++cur) {
        const QString lineCode = in.readLine();
        if (cur == topLine) {
          for (const QString& bFunc : blockingMpiPrimitives()) {
            if (lineCode.contains(bFunc)) { detectedFunc = bFunc; break; }
          }
          break;
        }
      }
    }
  }

  if (!detectedFunc.isEmpty()) {
    DeadlockInfo info;
    info.rankId = rankId;
    info.blockedFunction = detectedFunc;
    info.file = topFile;
    info.line = topLine;
    m_activeBlockedRanks[rankId] = info;
    evaluateClusterStalls(rankId);
  } else {
    m_activeBlockedRanks.remove(rankId);
    emit rankCleared(rankId);
  }
}

}  // namespace gridlock::core

/**
 * @file RankState.hpp
 * @brief Definition of execution telemetry and state models for an individual MPI rank.
 *
 * Encapsulates the execution state, current source location, disassembly slice,
 * active register file, variable watch values, and timing telemetry for an
 * individual rank within an HPC parallel debug session.
 */

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMap>
#include <QString>
#include <vector>

namespace gridlock {

/**
 * @struct RankState
 * @brief Comprehensive snapshot of execution state for a single MPI rank.
 *
 * Maintained by GdbRankCoordinator and DapCoordinator to synchronize GUI
 * elements (ServerRackView, DisassemblyView, RegisterView, SourceCodeView).
 */
struct RankState {
    /// Zero-based identifier of the MPI process rank (0 to N-1).
    int rankId = 0;

    /// Execution state string: "running", "stopped", "diverged", or "exited".
    QString currentState;

    /// 1-based source line number corresponding to current program counter.
    int currentLine = 0;

    /// Absolute file system path to the active source file for this rank.
    QString currentFile;

    /// Demangled name of the currently executing function or subroutine.
    QString currentFunction;

    /// Disassembly string representation around the active program counter.
    QString disassemblyText;

    /// Table mapping watched expression tokens to evaluated value strings.
    QHash<QString, QString> variableWatches;

    /// Map of active breakpoint numbers to symbol or address location strings.
    QHash<int, QString> breakpoints;

    /// Hardware register map (register index to hexadecimal string value).
    QMap<int, QString> registers;

    /// Monotonic wall-clock timer tracking execution duration for this rank.
    QElapsedTimer executionTimer;

    /// Accumulated runtime execution duration in milliseconds.
    qint64 totalRuntimeMs = 0;

    /// ISO timestamp string recording when the last breakpoint or event fired.
    QString lastFiredTimestamp;

    /// Count of pending unacknowledged breakpoints queued for this rank.
    int pendingBreakpoints = 0;
};

}  // namespace gridlock

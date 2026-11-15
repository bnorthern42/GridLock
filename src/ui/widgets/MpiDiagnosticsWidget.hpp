#pragma once

/**
 * @file MpiDiagnosticsWidget.hpp
 * @brief MPI cluster diagnostic panel for deadlock detection and message logging.
 *
 * Implements MpiDiagnosticsWidget, housing tabs for automated deadlock alerts,
 * collective communication verification, and point-to-point network log tracking.
 */

#include <QElapsedTimer>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QWidget>

namespace gridlock::core {
struct DeadlockInfo;
}

namespace gridlock::ui {
class MpiNetworkLogWidget;

/**
 * @class MpiDiagnosticsWidget
 * @brief Tabbed container widget for MPI network logs and deadlock diagnosis.
 */
class MpiDiagnosticsWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new MpiDiagnosticsWidget.
     * @param parent Optional parent widget.
     */
    explicit MpiDiagnosticsWidget(QWidget* parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~MpiDiagnosticsWidget() override = default;

    /**
     * @brief Returns the number of visible rank rows in the Deadlock Analyzer wait-state table.
     * @return Number of populated rank wait-state rows.
     */
    int rankWaitTableRowCount() const;

    /**
     * @brief Reports whether the inner tab bar is hidden and view switching is driven by the single 26px header bar.
     * @return True when the inner QTabBar is hidden and inline header toggle buttons control active sub-views.
     */
    bool hasCompactSingleRowHeader() const;

signals:
    /**
     * @brief Emitted when double-clicking a deadlock node to seek that rank's frame.
     * @param rankId The MPI rank identifier.
     */
    void jumpToFrameRequested(int rankId);

public slots:
    /**
     * @brief Handles a newly detected MPI deadlock condition alert.
     * @param info Detailed deadlock state and cycle information.
     */
    void onDeadlockDetected(const gridlock::core::DeadlockInfo& info);

    /**
     * @brief Clears deadlock alerts for a specific rank after resolution.
     * @param rankId Resolved MPI rank index.
     */
    void onRankCleared(int rankId);

    /**
     * @brief Returns a pointer to the child MPI network log widget.
     * @return MpiNetworkLogWidget pointer.
     */
    MpiNetworkLogWidget* networkLogWidget() const { return m_networkLogWidget; }

    /**
     * @brief Clears static placeholder rows and prepares per-rank live telemetry rows.
     * @param rankCount Active number of MPI ranks in the newly launched or attached session.
     */
    void beginLiveSession(int rankCount);

    /**
     * @brief Updates a rank telemetry row with live OS PID, state, MPI primitive, and source location.
     * @param rankId Zero-based MPI rank index.
     * @param pid Live OS child PID (`<= 0` omits the `(PID ...)` suffix until spawned).
     * @param stateText Execution state string (`"RUNNING"`, `"PAUSED"`, `"EXITED"`, etc.).
     * @param primitive Active `MPI_*` primitive or function symbol in the rank's backtrace.
     * @param target Peer rank, communicator, or collective wait target label.
     * @param location Formatted `file:line (0xaddr)` or PC location string.
     * @param totalRanks Optional total rank count used to resize the table on demand.
     */
    void updateLiveRankRow(int rankId, qint64 pid, const QString& stateText,
                           const QString& primitive, const QString& target,
                           const QString& location, int totalRanks = -1);

    /**
     * @brief Dynamically updates or inserts a rank state row in the wait-state table.
     * @param rankId Zero-based MPI rank identifier.
     * @param stateText Execution state string (e.g. "PAUSED (BP)", "STEPPED", "RUNNING").
     * @param primitive Active MPI primitive or instruction phase.
     * @param target Peer rank or collective target description.
     * @param pcAddr Active 64-bit program counter address.
     * @param totalRanks Optional total cluster rank count to resize table rows.
     */
    void updateRankStateRow(int rankId, const QString& stateText, const QString& primitive,
                            const QString& target, uint64_t pcAddr, int totalRanks = -1);

    /**
     * @brief Clears all wait-state rows and deadlock alert entries when returning to idle.
     */
    void clear();

    /**
     * @brief Populates preview cluster rank wait-state telemetry rows into m_rankWaitTable.
     */
    void populateDefaultRankTelemetry();

private slots:
    /**
     * @brief Handles double-clicking deadlock alert items in the list.
     * @param item The clicked QListWidgetItem.
     */
    void onItemDoubleClicked(QListWidgetItem* item);

    /**
     * @brief Filters rows in the rank wait-state table by rank ID, primitive, or state.
     * @param filterText Substring filter query.
     */
    void filterRankTable(const QString& filterText);

private:

    /// Tab widget container (with hidden inner QTabBar for single-row header switching).
    QTabWidget* m_tabWidget = nullptr;

    /// Inline header button activating the Deadlock Analyzer wait-state table view.
    QPushButton* m_analyzerModeBtn = nullptr;

    /// Inline header button activating the Network Log packet/collective trace view.
    QPushButton* m_networkLogModeBtn = nullptr;

    /// Status badge label in the 26px control strip.
    QLabel* m_statusBadge = nullptr;

    /// Filter input box in the 26px control strip.
    QLineEdit* m_filterEdit = nullptr;

    /// Structured 6-column rank wait-state table inside the Deadlock Analyzer tab.
    QTableWidget* m_rankWaitTable = nullptr;

    /// Deadlock alert listing widget.
    QListWidget* m_deadlockList = nullptr;

    /// Integrated MPI network packet/call log widget.
    MpiNetworkLogWidget* m_networkLogWidget = nullptr;

    /// Formats elapsed session time as MM:SS.mmm for live network log events.
    QString formatSessionTimestamp() const;

    /// Elapsed timer started on beginLiveSession() for live network log timestamps.
    QElapsedTimer m_sessionTimer;

    /// Per-rank last logged event signature to avoid flooding identical polling rows.
    QHash<int, QString> m_lastLoggedRankEvent;
};

}  // namespace gridlock::ui


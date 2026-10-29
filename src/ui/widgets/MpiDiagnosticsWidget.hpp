#pragma once

/**
 * @file MpiDiagnosticsWidget.hpp
 * @brief MPI cluster diagnostic panel for deadlock detection and message logging.
 *
 * Implements MpiDiagnosticsWidget, housing tabs for automated deadlock alerts,
 * collective communication verification, and point-to-point network log tracking.
 */

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
    /**
     * @brief Populates default cluster rank wait-state telemetry rows into m_rankWaitTable.
     */
    void populateDefaultRankTelemetry();

    /// Tab widget container.
    QTabWidget* m_tabWidget = nullptr;

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
};

}  // namespace gridlock::ui


#pragma once

/**
 * @file MpiDiagnosticsWidget.hpp
 * @brief MPI cluster diagnostic panel for deadlock detection and message logging.
 *
 * Implements MpiDiagnosticsWidget, housing tabs for automated deadlock alerts,
 * collective communication verification, and point-to-point network log tracking.
 */

#include <QWidget>
#include <QTabWidget>
#include <QListWidget>

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

private slots:
    /**
     * @brief Handles double-clicking deadlock alert items in the list.
     * @param item The clicked QListWidgetItem.
     */
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    /// Tab widget container.
    QTabWidget* m_tabWidget = nullptr;

    /// Deadlock alert listing widget.
    QListWidget* m_deadlockList = nullptr;

    /// Integrated MPI network packet/call log widget.
    MpiNetworkLogWidget* m_networkLogWidget = nullptr;
};

}  // namespace gridlock::ui


/**
 * @file MpiDiagnosticsWidget.cpp
 * @brief MPI cluster diagnostic container widget implementation.
 *
 * Implements MpiDiagnosticsWidget, organizing deadlock alerts and the network packet
 * log into a tabbed layout, and dispatching jump-to-frame navigation requests when
 * deadlocked ranks are selected.
 */

#include "MpiDiagnosticsWidget.hpp"
#include "../../core/hpc/DeadlockAnalyzer.hpp"
#include "MpiNetworkLogWidget.hpp"
#include <QVBoxLayout>

namespace gridlock::ui {

/**
 * @brief Constructs a new MpiDiagnosticsWidget.
 *
 * Initializes the tab container, embeds the deadlock QListWidget and MpiNetworkLogWidget,
 * and sets up item double-click handlers.
 *
 * @param parent Optional parent widget.
 */
MpiDiagnosticsWidget::MpiDiagnosticsWidget(QWidget* parent)
    : QWidget(parent)
{
    // Configure zero-margin vertical layout
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Initialize tabs and embedded widgets
    m_tabWidget = new QTabWidget(this);
    m_deadlockList = new QListWidget(this);
    m_networkLogWidget = new MpiNetworkLogWidget("Network Log", this);

    m_tabWidget->addTab(m_deadlockList, "Deadlock Analyzer");
    m_tabWidget->addTab(m_networkLogWidget, "Network Log");
    layout->addWidget(m_tabWidget);

    // Connect item activation to frame seek dispatcher
    connect(m_deadlockList, &QListWidget::itemDoubleClicked,
            this, &MpiDiagnosticsWidget::onItemDoubleClicked);
}

/**
 * @brief Records a detected deadlock event and adds an alert entry to the list.
 * @param info Deadlock descriptor containing blocked rank and function.
 */
void MpiDiagnosticsWidget::onDeadlockDetected(const gridlock::core::DeadlockInfo& info) {
    // Clear any previous stale alert entry for this rank
    onRankCleared(info.rankId);
    
    // Create new alert item storing rank ID in UserRole
    QListWidgetItem* item = new QListWidgetItem();
    item->setText(QString("Rank %1: Blocked in %2").arg(info.rankId).arg(info.blockedFunction));
    item->setData(Qt::UserRole, info.rankId);
    
    m_deadlockList->addItem(item);
}

/**
 * @brief Removes deadlock alert entries for a specific rank once it resumes execution.
 * @param rankId The resolved MPI rank index.
 */
void MpiDiagnosticsWidget::onRankCleared(int rankId) {
    for (int i = 0; i < m_deadlockList->count(); ++i) {
        QListWidgetItem* item = m_deadlockList->item(i);
        if (item->data(Qt::UserRole).toInt() == rankId) {
            delete m_deadlockList->takeItem(i);
            break;
        }
    }
}

/**
 * @brief Handles double-clicking an alert row to request focusing the deadlocked rank.
 * @param item Pointer to the clicked list item.
 */
void MpiDiagnosticsWidget::onItemDoubleClicked(QListWidgetItem* item) {
    if (item) {
        int rankId = item->data(Qt::UserRole).toInt();
        emit jumpToFrameRequested(rankId);
    }
}

}  // namespace gridlock::ui


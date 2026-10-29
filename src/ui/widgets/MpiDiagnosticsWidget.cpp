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
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace gridlock::ui {

/**
 * @brief Constructs a new MpiDiagnosticsWidget.
 *
 * Initializes the 26px #21252b status header bar, embeds the borderless deadlock
 * QListWidget and MpiNetworkLogWidget inside a document-mode tab container,
 * and sets up item double-click handlers.
 *
 * @param parent Optional parent widget.
 */
MpiDiagnosticsWidget::MpiDiagnosticsWidget(QWidget* parent)
    : QWidget(parent)
{
    // Configure zero-margin vertical layout with zero inter-widget spacing.
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Unified 26px #21252b header strip displaying telemetry and deadlock diagnostics title.
    QWidget* headerBar = new QWidget(this);
    headerBar->setObjectName(QStringLiteral("mpiDiagnosticsHeaderBar"));
    headerBar->setFixedHeight(26);
    headerBar->setStyleSheet(QStringLiteral(
        "#mpiDiagnosticsHeaderBar { background: #21252b; border-bottom: 1px solid #2c313a; }"
        "#mpiDiagnosticsHeaderBar QLabel { color: #abb2bf; font-family: 'JetBrains Mono', monospace; "
        "font-size: 11px; font-weight: 600; }"));
    QHBoxLayout* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(6, 2, 6, 2);
    headerLayout->setSpacing(6);
    headerLayout->addWidget(new QLabel(QStringLiteral("MPI COLLECTIVE & WAIT-FOR GRAPH TELEMETRY"), headerBar));
    headerLayout->addStretch(1);

    // Initialize document-mode tabs and borderless embedded widgets.
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setContentsMargins(0, 0, 0, 0);

    m_deadlockList = new QListWidget(this);
    m_deadlockList->setObjectName(QStringLiteral("mpiDeadlockList"));
    m_deadlockList->setFrameShape(QFrame::NoFrame);
    m_deadlockList->setStyleSheet(QStringLiteral(
        "QListWidget#mpiDeadlockList { background: #16181d; color: #d7dae0; border: none; "
        "font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 4px 6px; }"));

    m_networkLogWidget = new MpiNetworkLogWidget("Network Log", this);

    m_tabWidget->addTab(m_deadlockList, "Deadlock Analyzer");
    m_tabWidget->addTab(m_networkLogWidget, "Network Log");
    layout->addWidget(headerBar);
    layout->addWidget(m_tabWidget, 1);

    // Connect item activation to frame seek dispatcher.
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


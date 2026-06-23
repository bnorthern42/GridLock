#include "DeadlockDockWidget.hpp"
#include "../../core/hpc/DeadlockAnalyzer.hpp"

namespace gridlock::ui {

DeadlockDockWidget::DeadlockDockWidget(QWidget* parent)
    : QDockWidget("Deadlock Detector", parent)
{
    m_listWidget = new QListWidget(this);
    setWidget(m_listWidget);

    connect(m_listWidget, &QListWidget::itemDoubleClicked,
            this, &DeadlockDockWidget::onItemDoubleClicked);
}

void DeadlockDockWidget::onDeadlockDetected(const gridlock::core::DeadlockInfo& info) {
    onRankCleared(info.rankId); // Clear existing entry if any
    
    QListWidgetItem* item = new QListWidgetItem();
    item->setText(QString("Rank %1: Blocked in %2").arg(info.rankId).arg(info.blockedFunction));
    item->setData(Qt::UserRole, info.rankId);
    
    m_listWidget->addItem(item);
}

void DeadlockDockWidget::onRankCleared(int rankId) {
    for (int i = 0; i < m_listWidget->count(); ++i) {
        QListWidgetItem* item = m_listWidget->item(i);
        if (item->data(Qt::UserRole).toInt() == rankId) {
            delete m_listWidget->takeItem(i);
            break;
        }
    }
}

void DeadlockDockWidget::onItemDoubleClicked(QListWidgetItem* item) {
    if (item) {
        int rankId = item->data(Qt::UserRole).toInt();
        emit jumpToFrameRequested(rankId);
    }
}

} // namespace gridlock::ui

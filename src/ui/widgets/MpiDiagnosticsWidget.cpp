#include "MpiDiagnosticsWidget.hpp"
#include "../../core/hpc/DeadlockAnalyzer.hpp"
#include <QVBoxLayout>

namespace gridlock::ui {

MpiDiagnosticsWidget::MpiDiagnosticsWidget(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_tabWidget = new QTabWidget(this);
    m_deadlockList = new QListWidget(this);

    m_tabWidget->addTab(m_deadlockList, "Deadlock Analyzer");
    layout->addWidget(m_tabWidget);

    connect(m_deadlockList, &QListWidget::itemDoubleClicked,
            this, &MpiDiagnosticsWidget::onItemDoubleClicked);
}

void MpiDiagnosticsWidget::onDeadlockDetected(const gridlock::core::DeadlockInfo& info) {
    onRankCleared(info.rankId); // Clear existing entry if any
    
    QListWidgetItem* item = new QListWidgetItem();
    item->setText(QString("Rank %1: Blocked in %2").arg(info.rankId).arg(info.blockedFunction));
    item->setData(Qt::UserRole, info.rankId);
    
    m_deadlockList->addItem(item);
}

void MpiDiagnosticsWidget::onRankCleared(int rankId) {
    for (int i = 0; i < m_deadlockList->count(); ++i) {
        QListWidgetItem* item = m_deadlockList->item(i);
        if (item->data(Qt::UserRole).toInt() == rankId) {
            delete m_deadlockList->takeItem(i);
            break;
        }
    }
}

void MpiDiagnosticsWidget::onItemDoubleClicked(QListWidgetItem* item) {
    if (item) {
        int rankId = item->data(Qt::UserRole).toInt();
        emit jumpToFrameRequested(rankId);
    }
}

} // namespace gridlock::ui

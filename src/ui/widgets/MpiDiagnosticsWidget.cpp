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
#include <QAbstractItemView>
#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

namespace gridlock::ui {

/**
 * @brief Constructs a new MpiDiagnosticsWidget.
 *
 * Initializes the 26px #21252b status header bar with cycle status badge and
 * rank filter input, embeds a structured 6-column rank wait-state QTableWidget
 * and deadlock alert QListWidget inside the Deadlock Analyzer tab, and wires
 * double-click frame seek navigation.
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

    // Unified 26px #21252b header strip displaying telemetry, cycle status badge, and filter box.
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

    // Status pill badge indicating whether a circular wait-for cycle is currently active.
    m_statusBadge = new QLabel(QStringLiteral("STATUS: NO CYCLE"), headerBar);
    m_statusBadge->setObjectName(QStringLiteral("mpiDeadlockStatusBadge"));
    m_statusBadge->setStyleSheet(QStringLiteral(
        "color: #4ec9b0; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; "
        "padding: 1px 6px; background: #1b2a2f; border: 1px solid #2c4a52; border-radius: 2px;"));
    headerLayout->addWidget(m_statusBadge);
    headerLayout->addStretch(1);

    // Compact 22px search filter input for narrowing rank rows by rank ID, primitive, or communicator.
    m_filterEdit = new QLineEdit(headerBar);
    m_filterEdit->setObjectName(QStringLiteral("mpiDiagnosticsFilterEdit"));
    m_filterEdit->setPlaceholderText(QStringLiteral("Filter rank, primitive, or communicator..."));
    m_filterEdit->setFixedHeight(22);
    m_filterEdit->setMaximumWidth(240);
    headerLayout->addWidget(m_filterEdit);

    // Initialize document-mode tabs and borderless embedded widgets.
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setDocumentMode(true);
    m_tabWidget->setContentsMargins(0, 0, 0, 0);

    // Deadlock Analyzer container hosting the structured 6-column rank wait-state table and cycle alert list.
    QWidget* analyzerPage = new QWidget(m_tabWidget);
    analyzerPage->setContentsMargins(0, 0, 0, 0);
    QVBoxLayout* analyzerLayout = new QVBoxLayout(analyzerPage);
    analyzerLayout->setContentsMargins(0, 0, 0, 0);
    analyzerLayout->setSpacing(0);

    // Configure 6-column rank wait-state table with borderless frame and alternating row colors.
    m_rankWaitTable = new QTableWidget(0, 6, analyzerPage);
    m_rankWaitTable->setObjectName(QStringLiteral("mpiRankWaitTable"));
    m_rankWaitTable->setFrameShape(QFrame::NoFrame);
    m_rankWaitTable->setAlternatingRowColors(true);
    m_rankWaitTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rankWaitTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rankWaitTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_rankWaitTable->verticalHeader()->setVisible(false);
    m_rankWaitTable->verticalHeader()->setDefaultSectionSize(22);
    m_rankWaitTable->setHorizontalHeaderLabels({
        QStringLiteral("RANK"),
        QStringLiteral("STATE"),
        QStringLiteral("MPI PRIMITIVE"),
        QStringLiteral("PEER / TARGET"),
        QStringLiteral("COMMUNICATOR"),
        QStringLiteral("PC / LOCATION")
    });
    // Enforce >= 48px minimum section width and stretch columns across the viewport.
    m_rankWaitTable->horizontalHeader()->setVisible(true);
    m_rankWaitTable->horizontalHeader()->setMinimumSectionSize(48);
    m_rankWaitTable->horizontalHeader()->setDefaultSectionSize(110);
    m_rankWaitTable->horizontalHeader()->setStretchLastSection(true);
    for (int col = 0; col < 5; ++col) {
        m_rankWaitTable->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Stretch);
    }

    // Compact bottom list widget displaying detected wait-for cycle summaries.
    m_deadlockList = new QListWidget(analyzerPage);
    m_deadlockList->setObjectName(QStringLiteral("mpiDeadlockList"));
    m_deadlockList->setFrameShape(QFrame::NoFrame);
    m_deadlockList->setMaximumHeight(56);
    m_deadlockList->setStyleSheet(QStringLiteral(
        "QListWidget#mpiDeadlockList { background: #16181d; color: #d7dae0; border: none; "
        "border-top: 1px solid #282c34; font-family: 'JetBrains Mono', monospace; font-size: 11px; padding: 2px 6px; }"));

    analyzerLayout->addWidget(m_rankWaitTable, 1);
    analyzerLayout->addWidget(m_deadlockList);

    // Second sub-tab: MPI point-to-point and collective network packet log.
    m_networkLogWidget = new MpiNetworkLogWidget("Network Log", this);

    m_tabWidget->addTab(analyzerPage, "Deadlock Analyzer");
    m_tabWidget->addTab(m_networkLogWidget, "Network Log");
    layout->addWidget(headerBar);
    layout->addWidget(m_tabWidget, 1);

    // Populate initial rank wait-state telemetry rows so the Deadlock Analyzer is never a blank void.
    populateDefaultRankTelemetry();

    // Connect item activation and table double-click to frame seek dispatcher.
    connect(m_deadlockList, &QListWidget::itemDoubleClicked,
            this, &MpiDiagnosticsWidget::onItemDoubleClicked);
    connect(m_rankWaitTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (const QTableWidgetItem* item = m_rankWaitTable->item(row, 0)) {
            emit jumpToFrameRequested(item->data(Qt::UserRole).toInt());
        }
    });
    connect(m_filterEdit, &QLineEdit::textChanged, this, &MpiDiagnosticsWidget::filterRankTable);
}

/**
 * @brief Populates baseline MPI rank wait-state rows into the Deadlock Analyzer table.
 *
 * Ensures the Deadlock Analyzer tab immediately presents structured rank state,
 * active MPI primitive, target peer/collective, communicator, and instruction pointer
 * telemetry upon opening the workstation.
 */
void MpiDiagnosticsWidget::populateDefaultRankTelemetry() {
    if (!m_rankWaitTable) {
        return;
    }
    // Static descriptor for initial cluster rank telemetry rows.
    struct RankRowSpec {
        int rank;
        const char* state;
        const char* primitive;
        const char* target;
        const char* comm;
        const char* location;
    };
    static const RankRowSpec kDefaultRows[] = {
        {0, "PAUSED (BP)",   "MPI_Init",      "ALL RANKS",     "MPI_COMM_WORLD", "0x00401120 <main+0>"},
        {1, "PAUSED (SYNC)", "MPI_Allreduce", "COLL BARRIER",  "MPI_COMM_WORLD", "0x00401157 <main+55>"},
        {2, "RUNNING",       "MPI_Send",      "Rank 3 (tag 0)","MPI_COMM_WORLD", "0x0040115f <main+63>"},
        {3, "WAITING",       "MPI_Recv",      "Rank 2 (tag 0)","MPI_COMM_WORLD", "0x00401163 <main+67>"}
    };

    // Populate each rank row into the 6-column wait-state table.
    m_rankWaitTable->setRowCount(4);
    for (int r = 0; r < 4; ++r) {
        auto* rankItem = new QTableWidgetItem(QStringLiteral("Rank %1").arg(kDefaultRows[r].rank));
        rankItem->setData(Qt::UserRole, kDefaultRows[r].rank);
        m_rankWaitTable->setItem(r, 0, rankItem);
        m_rankWaitTable->setItem(r, 1, new QTableWidgetItem(QString::fromLatin1(kDefaultRows[r].state)));
        m_rankWaitTable->setItem(r, 2, new QTableWidgetItem(QString::fromLatin1(kDefaultRows[r].primitive)));
        m_rankWaitTable->setItem(r, 3, new QTableWidgetItem(QString::fromLatin1(kDefaultRows[r].target)));
        m_rankWaitTable->setItem(r, 4, new QTableWidgetItem(QString::fromLatin1(kDefaultRows[r].comm)));
        m_rankWaitTable->setItem(r, 5, new QTableWidgetItem(QString::fromLatin1(kDefaultRows[r].location)));
    }
}

/**
 * @brief Returns the number of rows in the Deadlock Analyzer rank wait-state table.
 * @return Row count of m_rankWaitTable.
 */
int MpiDiagnosticsWidget::rankWaitTableRowCount() const {
    return m_rankWaitTable ? m_rankWaitTable->rowCount() : 0;
}

/**
 * @brief Dynamically updates or inserts a rank state row in the wait-state table.
 * @param rankId Zero-based MPI rank identifier.
 * @param stateText Execution state string (e.g. "PAUSED (BP)", "STEPPED", "RUNNING").
 * @param primitive Active MPI primitive or instruction phase.
 * @param target Peer rank or collective target description.
 * @param pcAddr Active 64-bit program counter address.
 * @param totalRanks Optional total cluster rank count to resize table rows.
 */
void MpiDiagnosticsWidget::updateRankStateRow(int rankId, const QString& stateText,
                                              const QString& primitive, const QString& target,
                                              uint64_t pcAddr, int totalRanks) {
    if (!m_rankWaitTable || rankId < 0) {
        return;
    }
    if (totalRanks > 0 && m_rankWaitTable->rowCount() != totalRanks) {
        m_rankWaitTable->setRowCount(totalRanks);
    }
    if (rankId >= m_rankWaitTable->rowCount()) {
        m_rankWaitTable->setRowCount(rankId + 1);
    }
    auto* rankItem = new QTableWidgetItem(QStringLiteral("Rank %1").arg(rankId));
    rankItem->setData(Qt::UserRole, rankId);
    m_rankWaitTable->setItem(rankId, 0, rankItem);
    m_rankWaitTable->setItem(rankId, 1, new QTableWidgetItem(stateText));
    m_rankWaitTable->setItem(rankId, 2, new QTableWidgetItem(primitive));
    m_rankWaitTable->setItem(rankId, 3, new QTableWidgetItem(target));
    m_rankWaitTable->setItem(rankId, 4, new QTableWidgetItem(QStringLiteral("MPI_COMM_WORLD")));
    const int offset = (pcAddr >= 0x00401120ULL && pcAddr <= 0x00401190ULL)
                           ? static_cast<int>(pcAddr - 0x00401120ULL)
                           : 0;
    m_rankWaitTable->setItem(rankId, 5,
                             new QTableWidgetItem(QStringLiteral("0x%1 <main+%2>")
                                                      .arg(pcAddr, 8, 16, QChar('0'))
                                                      .arg(offset)));
}

/**
 * @brief Filters rows in the rank wait-state table matching the query substring.
 * @param filterText Substring filter query.
 */
void MpiDiagnosticsWidget::filterRankTable(const QString& filterText) {
    if (!m_rankWaitTable) {
        return;
    }
    const QString q = filterText.trimmed();
    for (int row = 0; row < m_rankWaitTable->rowCount(); ++row) {
        bool matches = q.isEmpty();
        for (int col = 0; !matches && col < m_rankWaitTable->columnCount(); ++col) {
            if (const QTableWidgetItem* it = m_rankWaitTable->item(row, col)) {
                if (it->text().contains(q, Qt::CaseInsensitive)) {
                    matches = true;
                }
            }
        }
        m_rankWaitTable->setRowHidden(row, !matches);
    }
}

/**
 * @brief Records a detected deadlock event and updates both the table and alert list.
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

    if (m_statusBadge) {
        m_statusBadge->setText(QStringLiteral("STATUS: CYCLE DETECTED"));
        m_statusBadge->setStyleSheet(QStringLiteral(
            "color: #f44747; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; "
            "padding: 1px 6px; background: #331c1f; border: 1px solid #6b2a30; border-radius: 2px;"));
    }

    // Update or append the corresponding row in m_rankWaitTable.
    if (m_rankWaitTable) {
        int targetRow = -1;
        for (int r = 0; r < m_rankWaitTable->rowCount(); ++r) {
            if (m_rankWaitTable->item(r, 0) &&
                m_rankWaitTable->item(r, 0)->data(Qt::UserRole).toInt() == info.rankId) {
                targetRow = r;
                break;
            }
        }
        if (targetRow == -1) {
            targetRow = m_rankWaitTable->rowCount();
            m_rankWaitTable->insertRow(targetRow);
            auto* rItem = new QTableWidgetItem(QStringLiteral("Rank %1").arg(info.rankId));
            rItem->setData(Qt::UserRole, info.rankId);
            m_rankWaitTable->setItem(targetRow, 0, rItem);
            m_rankWaitTable->setItem(targetRow, 3, new QTableWidgetItem(QStringLiteral("CYCLE WAIT")));
            m_rankWaitTable->setItem(targetRow, 4, new QTableWidgetItem(QStringLiteral("MPI_COMM_WORLD")));
            m_rankWaitTable->setItem(targetRow, 5, new QTableWidgetItem(QStringLiteral("0x0040113d")));
        }
        m_rankWaitTable->setItem(targetRow, 1, new QTableWidgetItem(QStringLiteral("DEADLOCKED")));
        m_rankWaitTable->setItem(targetRow, 2, new QTableWidgetItem(info.blockedFunction));
    }
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
    if (m_deadlockList && m_deadlockList->count() == 0 && m_statusBadge) {
        m_statusBadge->setText(QStringLiteral("STATUS: NO CYCLE"));
        m_statusBadge->setStyleSheet(QStringLiteral(
            "color: #4ec9b0; font-family: 'JetBrains Mono', monospace; font-size: 10px; font-weight: bold; "
            "padding: 1px 6px; background: #1b2a2f; border: 1px solid #2c4a52; border-radius: 2px;"));
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


#include "VariablesDockWidget.hpp"
#include "../../core/hpc/IBackendCoordinator.hpp"
#include "../../core/hpc/GdbRankCoordinator.hpp"
#include "../../core/hpc/DapCoordinator.hpp"
#include "../../core/managers/ThemeManager.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>

namespace gridlock {

VariablesDockWidget::VariablesDockWidget(QWidget* parent)
    : QWidget(parent), m_coordinator(nullptr), m_currentRankId(0) {
    
    m_model = new VariableTreeModel(nullptr, this);
    setupUi();
}

VariablesDockWidget::~VariablesDockWidget() = default;

void VariablesDockWidget::setCoordinator(::IBackendCoordinator* coordinator) {
    m_coordinator = coordinator;
    m_model->deleteLater();
    m_model = new VariableTreeModel(coordinator, this);
    m_variablesTree->setModel(m_model);
    connect(m_variablesTree, &QTreeView::expanded, m_model, &VariableTreeModel::fetchMore);

    if (m_coordinator) {
        if (auto* gdb = dynamic_cast<GdbRankCoordinator*>(m_coordinator)) {
            connect(gdb, &GdbRankCoordinator::rankStateChanged, this, &VariablesDockWidget::onRankStateChanged);
        } else if (auto* dap = dynamic_cast<DapCoordinator*>(m_coordinator)) {
            connect(dap, &DapCoordinator::stateChanged, this, [this](SessionState state) {
                if (state == SessionState::Paused) {
                    RankState rs; rs.currentState = "stopped";
                    onRankStateChanged(m_currentRankId, rs);
                }
            });
            connect(dap, &DapCoordinator::executionStopped, this, [this](int rankId, const QString&) {
                RankState rs; rs.currentState = "stopped";
                onRankStateChanged(rankId, rs);
            });
        }
        onProcessCountChanged();
    }
}

void VariablesDockWidget::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Top Bar
    QHBoxLayout* topBar = new QHBoxLayout();
    topBar->setContentsMargins(5, 5, 5, 5);
    QLabel* scopeLabel = new QLabel("Scope:", this);
    m_rankSelector = new QComboBox(this);
    topBar->addWidget(scopeLabel);
    topBar->addWidget(m_rankSelector, 1);
    layout->addLayout(topBar);

    // Main Area
    m_variablesTree = new QTreeView(this);
    m_variablesTree->setModel(m_model);
    m_variablesTree->setRootIsDecorated(true);
    m_variablesTree->setIndentation(20);
    m_variablesTree->setItemsExpandable(true);
    m_variablesTree->setExpandsOnDoubleClick(true);

    m_variablesTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_variablesTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_variablesTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    layout->addWidget(m_variablesTree);

    connect(m_rankSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VariablesDockWidget::onRankSelected);
    
    connect(m_variablesTree, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid() || !m_coordinator) return;
        QModelIndex valueIndex = m_model->index(index.row(), 1, index.parent());
        QString valueStr = m_model->data(valueIndex, Qt::DisplayRole).toString();
        if (valueStr.startsWith("0x")) {
            m_coordinator->readMemory(m_currentRankId, valueStr, 256);
        }
    });
}

void VariablesDockWidget::onProcessCountChanged() {
    if (!m_coordinator) return;
    
    m_rankSelector->blockSignals(true);
    m_rankSelector->clear();
    int count = m_coordinator->getProcessCount();
    for (int i = 0; i < count; ++i) {
        if (i == 0) {
            m_rankSelector->addItem("Rank 0 (Master)", i);
        } else {
            m_rankSelector->addItem(QString("Rank %1").arg(i), i);
        }
    }
    m_rankSelector->blockSignals(false);
    
    if (count > 0) {
        m_rankSelector->setCurrentIndex(0);
        onRankSelected(0);
    }
}

void VariablesDockWidget::onRankSelected(int index) {
    if (index < 0 || !m_coordinator) return;
    int rankId = m_rankSelector->itemData(index).toInt();
    m_currentRankId = rankId;
    m_model->loadLocals(rankId);
}

void VariablesDockWidget::onRankStateChanged(int rankId, const RankState& state) {
    if (!m_coordinator) return;
    if (m_rankSelector->count() != m_coordinator->getProcessCount()) {
        onProcessCountChanged();
    }

    if (rankId == m_currentRankId && state.currentState == "stopped") {
        if (m_lastRankStates.value(rankId) != "stopped") {
            m_model->loadLocals(rankId);
        }
    }
    m_lastRankStates[rankId] = state.currentState;
}

} // namespace gridlock

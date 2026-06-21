#include "VariablesDockWidget.hpp"
#include "../GdbRankCoordinator.hpp"
#include "ThemeManager.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>

namespace gridlock {

VariablesDockWidget::VariablesDockWidget(GdbRankCoordinator* coordinator, QWidget* parent)
    : QDockWidget("Variables", parent), m_coordinator(coordinator), m_currentRankId(0) {
    
    m_model = new VariableTreeModel(coordinator, this);
    setupUi();

    connect(m_coordinator, &GdbRankCoordinator::rankStateChanged, this, &VariablesDockWidget::onRankStateChanged);

    onProcessCountChanged();
}

VariablesDockWidget::~VariablesDockWidget() = default;

void VariablesDockWidget::setupUi() {
    QWidget* container = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    // Top Bar
    QHBoxLayout* topBar = new QHBoxLayout();
    topBar->setContentsMargins(5, 5, 5, 5);
    QLabel* scopeLabel = new QLabel("Scope:", container);
    m_rankSelector = new QComboBox(container);
    topBar->addWidget(scopeLabel);
    topBar->addWidget(m_rankSelector, 1);
    layout->addLayout(topBar);

    // Main Area
    m_variablesTree = new QTreeView(container);
    m_variablesTree->setModel(m_model);
    m_variablesTree->setStyleSheet(R"(
        QTreeView {
            background-color: #181825;
            color: #cdd6f4;
            border: none;
        }
    )");
    m_variablesTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_variablesTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_variablesTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    layout->addWidget(m_variablesTree);
    setWidget(container);

    connect(m_rankSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VariablesDockWidget::onRankSelected);
    connect(m_variablesTree, &QTreeView::expanded, m_model, &VariableTreeModel::fetchMore);
}

void VariablesDockWidget::onProcessCountChanged() {
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
    if (index < 0) return;
    int rankId = m_rankSelector->itemData(index).toInt();
    m_currentRankId = rankId;
    m_model->loadLocals(rankId);
}

void VariablesDockWidget::onRankStateChanged(int rankId, const RankState& state) {
    if (m_rankSelector->count() != m_coordinator->getProcessCount()) {
        onProcessCountChanged();
    }

    if (rankId == m_currentRankId && state.currentState == "stopped") {
        m_model->loadLocals(rankId);
    }
}

} // namespace gridlock

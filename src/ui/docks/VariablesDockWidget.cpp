/**
 * @file VariablesDockWidget.cpp
 * @brief Implementation of hierarchical variable tree dock widget.
 *
 * Implements tree view configuration, asynchronous data fetching via
 * VariableTreeModel, rank selector updates, and memory inspector triggers.
 */

#include "VariablesDockWidget.hpp"
#include "../../core/hpc/DapCoordinator.hpp"
#include "../../core/hpc/GdbRankCoordinator.hpp"
#include "../../core/hpc/IBackendCoordinator.hpp"
#include "../../core/managers/ThemeManager.hpp"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QVBoxLayout>

namespace gridlock {

/**
 * @brief Constructs the variables widget and initializes default empty tree model.
 * @param parent Optional parent widget.
 */
VariablesDockWidget::VariablesDockWidget(QWidget* parent)
    : QWidget(parent), m_coordinator(nullptr), m_currentRankId(0) {
    // Instantiate initial unattached tree model.
    m_model = new VariableTreeModel(nullptr, this);
    setupUi();
}

VariablesDockWidget::~VariablesDockWidget() = default;

/**
 * @brief Binds a coordinator backend and re-initializes variable tree model.
 * @param coordinator Active IBackendCoordinator instance.
 */
void VariablesDockWidget::setCoordinator(::IBackendCoordinator* coordinator) {
    m_coordinator = coordinator;
    // Replace old tree model with newly bound instance.
    m_model->deleteLater();
    m_model = new VariableTreeModel(coordinator, this);
    m_variablesTree->setModel(m_model);
    // Fetch child members dynamically when user expands a struct or object node.
    connect(m_variablesTree, &QTreeView::expanded, m_model, &VariableTreeModel::fetchMore);

    if (m_coordinator) {
        // Wire GDB state change notifications to variable tree refreshes.
        if (auto* gdb = dynamic_cast<GdbRankCoordinator*>(m_coordinator)) {
            connect(gdb, &GdbRankCoordinator::rankStateChanged, this, &VariablesDockWidget::onRankStateChanged);
        } else if (auto* dap = dynamic_cast<DapCoordinator*>(m_coordinator)) {
            // Wire DAP state change and stop events.
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

/**
 * @brief Constructs the layout, 26px rank selection header bar, and hierarchical tree view.
 */
void VariablesDockWidget::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Unified 26px #21252b header strip hosting Scope label and rank selector combo box.
    QWidget* headerBar = new QWidget(this);
    headerBar->setObjectName(QStringLiteral("variablesHeaderBar"));
    headerBar->setFixedHeight(26);
    headerBar->setStyleSheet(QStringLiteral(
        "#variablesHeaderBar { background: #21252b; border-bottom: 1px solid #2c313a; }"
        "#variablesHeaderBar QLabel { color: #abb2bf; font-size: 11px; font-weight: 600; }"));

    QHBoxLayout* topBar = new QHBoxLayout(headerBar);
    topBar->setContentsMargins(6, 2, 6, 2);
    topBar->setSpacing(6);
    QLabel* scopeLabel = new QLabel(QStringLiteral("Scope:"), headerBar);
    m_rankSelector = new QComboBox(headerBar);
    m_rankSelector->setFixedHeight(22);
    m_rankSelector->addItem(QStringLiteral("Rank 0 (Master)"), 0);
    topBar->addWidget(scopeLabel);
    topBar->addWidget(m_rankSelector, 1);
    layout->addWidget(headerBar);

    // Main Area: Borderless tree view rendering variable names, values, and data types.
    m_variablesTree = new QTreeView(this);
    m_variablesTree->setFrameShape(QFrame::NoFrame);
    m_variablesTree->setAlternatingRowColors(true);
    m_variablesTree->setModel(m_model);
    m_variablesTree->setRootIsDecorated(true);
    m_variablesTree->setIndentation(16);
    m_variablesTree->setItemsExpandable(true);
    m_variablesTree->setExpandsOnDoubleClick(true);

    // Distribute tree column header widths across Name, Value, and Type with >= 48px minimum section size.
    m_variablesTree->header()->setVisible(true);
    m_variablesTree->header()->setMinimumSectionSize(48);
    m_variablesTree->header()->setDefaultSectionSize(96);
    m_variablesTree->header()->setStretchLastSection(true);
    m_variablesTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_variablesTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_variablesTree->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_variablesTree->setColumnWidth(2, 72);

    layout->addWidget(m_variablesTree, 1);

    // Re-query local variables when user changes active rank selection in combo box.
    connect(m_rankSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VariablesDockWidget::onRankSelected);
    
    // Clicking a pointer variable with hex address triggers a memory read in the hex view.
    connect(m_variablesTree, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid() || !m_coordinator) return;
        QModelIndex valueIndex = m_model->index(index.row(), 1, index.parent());
        QString valueStr = m_model->data(valueIndex, Qt::DisplayRole).toString();
        if (valueStr.startsWith("0x")) {
            m_coordinator->readMemory(m_currentRankId, valueStr, 256);
        }
    });
}

/**
 * @brief Rebuilds rank selector dropdown items to match active MPI cluster size.
 */
void VariablesDockWidget::onProcessCountChanged() {
    if (!m_coordinator) return;
    
    // Suppress signals during batch combobox repopulation.
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
    
    // Select master rank by default if processes exist.
    if (count > 0) {
        m_rankSelector->setCurrentIndex(0);
        onRankSelected(0);
    }
}

/**
 * @brief Updates focused rank ID and requests frame locals from tree model.
 * @param index Selected index in combobox.
 */
void VariablesDockWidget::onRankSelected(int index) {
    if (index < 0 || !m_coordinator) return;
    int rankId = m_rankSelector->itemData(index).toInt();
    m_currentRankId = rankId;
    m_model->loadLocals(rankId);
}

/**
 * @brief Handles state changes on MPI ranks to refresh variable trees upon suspension.
 * @param rankId Zero-based rank index.
 * @param state State snapshot.
 */
void VariablesDockWidget::onRankStateChanged(int rankId, const RankState& state) {
    if (!m_coordinator) return;
    // Re-sync process count if rank topology changed.
    if (m_rankSelector->count() != m_coordinator->getProcessCount()) {
        onProcessCountChanged();
    }

    // If focused rank has stopped, reload stack frame local variables.
    if (rankId == m_currentRankId && state.currentState == "stopped") {
        if (m_lastRankStates.value(rankId) != "stopped") {
            m_model->loadLocals(rankId);
        }
    }
    m_lastRankStates[rankId] = state.currentState;
}

}  // namespace gridlock

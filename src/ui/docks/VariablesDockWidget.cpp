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

#include <functional>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace gridlock {

namespace {

/**
 * @class VariableFilterProxyModel
 * @brief Recursive case-insensitive multi-column filter and sort proxy for variable trees.
 */
class VariableFilterProxyModel final : public QSortFilterProxyModel {
public:
    /** @brief Configures recursive case-insensitive filtering and dynamic column sorting. */
    explicit VariableFilterProxyModel(QObject* parent = nullptr)
        : QSortFilterProxyModel(parent) {
        // Keep matching leaf fields visible together with their parent struct chain.
        setRecursiveFilteringEnabled(true);
        setFilterCaseSensitivity(Qt::CaseInsensitive);
        setSortCaseSensitivity(Qt::CaseInsensitive);
        setDynamicSortFilter(true);
    }

protected:
    /** @brief Accepts a source row when Name, Value, or Type matches the active search pattern. */
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override {
        const QRegularExpression rx = filterRegularExpression();
        if (rx.pattern().isEmpty() || !sourceModel()) {
            return true;
        }
        // Match filter text against Name (0), Value (1), or Type (2) columns.
        for (int col = 0; col < 3; ++col) {
            const QModelIndex idx = sourceModel()->index(sourceRow, col, sourceParent);
            const QString text = sourceModel()->data(idx, Qt::DisplayRole).toString();
            if (text.contains(rx)) {
                return true;
            }
        }
        return false;
    }
};

}  // namespace

/**
 * @brief Constructs the variables widget and initializes default empty tree model.
 * @param parent Optional parent widget.
 */
VariablesDockWidget::VariablesDockWidget(QWidget* parent)
    : QWidget(parent), m_coordinator(nullptr), m_currentRankId(0) {
    // Instantiate initial unattached tree model (kept parented to widget for findChild lookups).
    m_model = new VariableTreeModel(nullptr, this);
    setupUi();
}

VariablesDockWidget::~VariablesDockWidget() = default;

/**
 * @brief Promotes a local variable name to the Watch Expressions differential grid.
 * @param variableName Local variable or expression string to watch across MPI ranks.
 * @return True if a non-empty variable name was promoted, false otherwise.
 */
bool VariablesDockWidget::promoteVariableToWatch(const QString& variableName) {
    const QString trimmed = variableName.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    emit watchExpressionRequested(trimmed);
    return true;
}

/**
 * @brief Binds a coordinator backend and re-initializes variable tree model.
 * @param coordinator Active IBackendCoordinator instance.
 */
void VariablesDockWidget::setCoordinator(::IBackendCoordinator* coordinator) {
    m_coordinator = coordinator;
    // Replace old tree model while preserving proxy filtering and tree sorting.
    m_model->deleteLater();
    m_model = new VariableTreeModel(coordinator, this);
    m_proxyModel->setSourceModel(m_model);

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
 * @brief Constructs layout, scope/search header bar, and sortable variable tree view.
 */
void VariablesDockWidget::setupUi() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Unified 26px header strip hosting Scope rank selector and live variable search bar.
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

    m_searchEdit = new QLineEdit(headerBar);
    m_searchEdit->setObjectName(QStringLiteral("variableSearchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("Filter variables by name, value, or type..."));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedHeight(22);

    topBar->addWidget(scopeLabel);
    topBar->addWidget(m_rankSelector, 0);
    topBar->addWidget(m_searchEdit, 1);
    layout->addWidget(headerBar);

    // Configure recursive filter/sort proxy wrapping the source VariableTreeModel.
    m_proxyModel = new VariableFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);

    // Main Area: Borderless sortable tree view rendering variable names, values, and data types.
    m_variablesTree = new QTreeView(this);
    m_variablesTree->setFrameShape(QFrame::NoFrame);
    m_variablesTree->setAlternatingRowColors(true);
    m_variablesTree->setModel(m_proxyModel);
    m_variablesTree->setRootIsDecorated(true);
    m_variablesTree->setIndentation(16);
    m_variablesTree->setItemsExpandable(true);
    m_variablesTree->setExpandsOnDoubleClick(true);
    m_variablesTree->setSortingEnabled(true);
    m_variablesTree->sortByColumn(0, Qt::AscendingOrder);
    m_variablesTree->setContextMenuPolicy(Qt::CustomContextMenu);

    // Enable interactive user resizing across all three columns with clean defaults.
    QHeaderView* hdr = m_variablesTree->header();
    hdr->setVisible(true);
    hdr->setMinimumSectionSize(48);
    hdr->setSectionsMovable(false);
    hdr->setStretchLastSection(true);
    hdr->setSectionResizeMode(0, QHeaderView::Interactive);
    hdr->setSectionResizeMode(1, QHeaderView::Interactive);
    hdr->setSectionResizeMode(2, QHeaderView::Interactive);
    m_variablesTree->setColumnWidth(0, 180);
    m_variablesTree->setColumnWidth(1, 220);
    m_variablesTree->setColumnWidth(2, 120);

    layout->addWidget(m_variablesTree, 1);

    // Re-query local variables when user changes active rank selection in combo box.
    connect(m_rankSelector, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VariablesDockWidget::onRankSelected);

    // Filter rows live across all columns and expand already-fetched subtrees containing matches.
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (!m_proxyModel || !m_variablesTree) return;
        // Escape regex metacharacters so substrings like `Iconfig.tol` or `vec[0]` match literally.
        const QString escaped = QRegularExpression::escape(text.trimmed());
        m_proxyModel->setFilterRegularExpression(
            QRegularExpression(escaped, QRegularExpression::CaseInsensitiveOption));
        if (!text.trimmed().isEmpty()) {
            // Only expand nodes whose children are already loaded to avoid triggering lazy MI queries.
            std::function<void(const QModelIndex&)> expandLoadedMatches = [&](const QModelIndex& parentIdx) {
                const int rows = m_proxyModel->rowCount(parentIdx);
                for (int r = 0; r < rows; ++r) {
                    const QModelIndex childProxy = m_proxyModel->index(r, 0, parentIdx);
                    const QModelIndex childSrc = m_proxyModel->mapToSource(childProxy);
                    // Skip canFetchMore nodes; expand only populated subtrees containing accepted matches.
                    if (m_model && !m_model->canFetchMore(childSrc) && m_proxyModel->rowCount(childProxy) > 0) {
                        m_variablesTree->expand(childProxy);
                        expandLoadedMatches(childProxy);
                    }
                }
            };
            expandLoadedMatches(QModelIndex());
        }
    });

    // Fetch lazy child members when any parent row expands (mapping proxy -> source index).
    connect(m_variablesTree, &QTreeView::expanded, this, [this](const QModelIndex& proxyIdx) {
        if (m_model && m_proxyModel) {
            m_model->fetchMore(m_proxyModel->mapToSource(proxyIdx));
        }
    });

    // Clicking a pointer value opens Memory View; clicking a {...} composite expands its fields.
    connect(m_variablesTree, &QTreeView::clicked, this, [this](const QModelIndex& proxyIdx) {
        if (!proxyIdx.isValid() || !m_model || !m_proxyModel) return;
        const QModelIndex srcIdx = m_proxyModel->mapToSource(proxyIdx);
        const QModelIndex valueIndex = m_model->index(srcIdx.row(), 1, srcIdx.parent());
        const QString valueStr = m_model->data(valueIndex, Qt::DisplayRole).toString().trimmed();
        if (valueStr.startsWith(QStringLiteral("0x")) && m_coordinator) {
            m_coordinator->readMemory(m_currentRankId, valueStr, 256);
        } else if (proxyIdx.column() == 1 &&
                   (valueStr == QStringLiteral("{...}") || valueStr.startsWith(QLatin1Char('{'))) &&
                   !m_variablesTree->isExpanded(proxyIdx)) {
            m_variablesTree->expand(proxyIdx);
        }
    });

    // Double-clicking any column of an expandable row expands it cleanly.
    connect(m_variablesTree, &QTreeView::doubleClicked, this, [this](const QModelIndex& proxyIdx) {
        if (proxyIdx.isValid() && !m_variablesTree->isExpanded(proxyIdx)) {
            m_variablesTree->expand(proxyIdx);
        }
    });

    // Right-clicking a variable row opens a context menu to promote its full C++ path to Watches/Watchpoints.
    connect(m_variablesTree, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex proxyIdx = m_variablesTree->indexAt(pos);
        if (!proxyIdx.isValid() || !m_model || !m_proxyModel) {
            return;
        }
        const QModelIndex srcIdx = m_proxyModel->mapToSource(proxyIdx);
        QString exprPath = m_model->expressionPath(srcIdx.siblingAtColumn(0)).trimmed();
        if (exprPath.isEmpty()) {
            exprPath = m_model->data(srcIdx.siblingAtColumn(0), Qt::DisplayRole).toString().trimmed();
        }
        if (exprPath.isEmpty()) {
            return;
        }
        QMenu menu(this);
        QAction* watchAct = menu.addAction(QStringLiteral("Add to Watch Expressions"));
        QAction* graphAct = menu.addAction(QStringLiteral("Visualize in Pointer / Struct Graph..."));
        menu.addSeparator();
        QAction* wpWriteAct =
            menu.addAction(QStringLiteral("Watch Memory Write (Hardware Watchpoint)"));
        QAction* wpReadAct =
            menu.addAction(QStringLiteral("Watch Memory Read (Hardware Watchpoint)"));
        QAction* wpRwAct =
            menu.addAction(QStringLiteral("Watch Memory Read/Write (Hardware Watchpoint)"));

        QAction* chosen = menu.exec(m_variablesTree->viewport()->mapToGlobal(pos));
        if (chosen == watchAct) {
            promoteVariableToWatch(exprPath);
        } else if (chosen == graphAct) {
            const QString varName = m_model->data(srcIdx.siblingAtColumn(0), Qt::DisplayRole).toString().trimmed();
            const QString varVal = m_model->data(srcIdx.siblingAtColumn(1), Qt::DisplayRole).toString().trimmed();
            const QString varTyp = m_model->data(srcIdx.siblingAtColumn(2), Qt::DisplayRole).toString().trimmed();
            emit visualizeInPointerGraphRequested(exprPath, varName, varVal, varTyp);
        } else if (chosen == wpWriteAct || chosen == wpReadAct || chosen == wpRwAct) {
            const auto mode = (chosen == wpReadAct)
                                  ? ::WatchpointAccessMode::Read
                                  : ((chosen == wpRwAct) ? ::WatchpointAccessMode::ReadWrite
                                                         : ::WatchpointAccessMode::Write);
            if (m_coordinator) {
                m_coordinator->setDataWatchpoint(exprPath, mode, -1);
            }
            emit dataWatchpointRequested(exprPath, static_cast<int>(mode));
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

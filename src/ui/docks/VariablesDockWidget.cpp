/**
 * @file VariablesDockWidget.cpp
 * @brief Implementation of hierarchical variable tree dock widget.
 *
 * Implements tree view configuration, asynchronous data fetching via
 * VariableTreeModel, rank selector updates, and memory inspector triggers.
 */

#include "VariablesDockWidget.hpp"
#include "CallStackDockWidget.hpp"
#include "../../core/hpc/dap/DapCoordinator.hpp"
#include "../../core/hpc/gdb/GdbRankCoordinator.hpp"
#include "../../core/hpc/IBackendCoordinator.hpp"
#include "../../core/managers/ThemeManager.hpp"
#include "../../core/plugins/PluginHostBridge.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QTextOption>
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
    connect(m_model, &VariableTreeModel::variableEditRequested, this, &VariablesDockWidget::variableEditRequested);
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
    if (m_model) { m_model->setParent(nullptr); m_model->deleteLater(); }
    m_model = new VariableTreeModel(coordinator, this);
    connect(m_model, &VariableTreeModel::variableEditRequested, this, &VariablesDockWidget::variableEditRequested);
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
    emit coordinatorBound(m_coordinator);
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
    scopeLabel->setObjectName(QStringLiteral("variablesScopeLabel"));
    m_rankSelector = new QComboBox(headerBar);
    m_rankSelector->setObjectName(QStringLiteral("variablesRankSelector"));
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
    m_variablesTree->setExpandsOnDoubleClick(false);
    m_variablesTree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
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
        const QString escaped = QRegularExpression::escape(text.trimmed());
        m_proxyModel->setFilterRegularExpression(
            QRegularExpression(escaped, QRegularExpression::CaseInsensitiveOption));
        if (!text.trimmed().isEmpty()) {
            std::function<void(const QModelIndex&)> expandLoadedMatches = [&](const QModelIndex& parentIdx) {
                const int rows = m_proxyModel->rowCount(parentIdx);
                for (int r = 0; r < rows; ++r) {
                    const QModelIndex childProxy = m_proxyModel->index(r, 0, parentIdx);
                    const QModelIndex childSrc = m_proxyModel->mapToSource(childProxy);
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

    // Double-clicking an expandable composite toggles expansion; double-clicking a leaf opens inline value editing.
    connect(m_variablesTree, &QTreeView::doubleClicked, this, [this](const QModelIndex& proxyIdx) {
        if (!proxyIdx.isValid() || !m_proxyModel) return;
        const QModelIndex row0 = proxyIdx.siblingAtColumn(0);
        if (m_proxyModel->hasChildren(row0)) {
            m_variablesTree->setExpanded(row0, !m_variablesTree->isExpanded(row0));
        } else if (proxyIdx.column() != 1) {
            m_variablesTree->edit(proxyIdx.siblingAtColumn(1));
        }
    });

    auto* copyShortcut = new QShortcut(QKeySequence::Copy, m_variablesTree);
    copyShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copyShortcut, &QShortcut::activated, this, [this]() { copySelectedVariableValue(); });

    // Right-clicking a variable row opens a context menu including dynamic Plugin Actions.
    connect(m_variablesTree, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QModelIndex proxyIdx = m_variablesTree->indexAt(pos);
        std::unique_ptr<QMenu> menu(createContextMenuForIndex(proxyIdx, this));
        if (menu) menu->exec(m_variablesTree->viewport()->mapToGlobal(pos));
    });
}

QMenu* VariablesDockWidget::createContextMenuForIndex(const QModelIndex& proxyIdx, QWidget* menuParent) {
    if (!proxyIdx.isValid() || !m_model || !m_proxyModel) return nullptr;
    const QModelIndex srcIdx = m_proxyModel->mapToSource(proxyIdx);
    QString exprPath = m_model->expressionPath(srcIdx.siblingAtColumn(0)).trimmed();
    if (exprPath.isEmpty()) exprPath = m_model->data(srcIdx.siblingAtColumn(0), Qt::DisplayRole).toString().trimmed();
    if (exprPath.isEmpty()) return nullptr;
    const QString varName = m_model->data(srcIdx.siblingAtColumn(0), Qt::DisplayRole).toString().trimmed();
    const QString varVal = m_model->data(srcIdx.siblingAtColumn(1), Qt::DisplayRole).toString().trimmed();
    const QString varTyp = m_model->data(srcIdx.siblingAtColumn(2), Qt::DisplayRole).toString().trimmed();
    const bool isLeafEditable = (m_model->flags(srcIdx.siblingAtColumn(1)) & Qt::ItemIsEditable);

    auto* menu = new QMenu(menuParent ? menuParent : this);
    QAction* setValAct = menu->addAction(QStringLiteral("Set Value..."));
    setValAct->setEnabled(isLeafEditable);
    connect(setValAct, &QAction::triggered, this, [this, proxyIdx, isLeafEditable]() {
        if (isLeafEditable && m_variablesTree) m_variablesTree->edit(proxyIdx.siblingAtColumn(1));
    });
    QAction* inspectFullAct = menu->addAction(QStringLiteral("Inspect Full Value..."));
    connect(inspectFullAct, &QAction::triggered, this, [this, exprPath, varTyp, varVal]() {
        inspectFullValue(QStringLiteral("%1 (%2)").arg(exprPath, varTyp.isEmpty() ? QStringLiteral("value") : varTyp),
                         varVal.isEmpty() ? exprPath : varVal);
    });
    menu->addSeparator();
    QAction* copyValAct = menu->addAction(QStringLiteral("Copy Value (Ctrl+C)"));
    connect(copyValAct, &QAction::triggered, this, [varVal, exprPath]() {
        QApplication::clipboard()->setText(varVal.isEmpty() ? exprPath : varVal);
    });
    QAction* copyExprAct = menu->addAction(QStringLiteral("Copy Expression"));
    connect(copyExprAct, &QAction::triggered, this, [exprPath]() { QApplication::clipboard()->setText(exprPath); });
    menu->addSeparator();
    QAction* watchAct = menu->addAction(QStringLiteral("Add to Watch Expressions"));
    connect(watchAct, &QAction::triggered, this, [this, exprPath]() { promoteVariableToWatch(exprPath); });
    QAction* plotAct = menu->addAction(QStringLiteral("Plot 1D/2D Array..."));
    connect(plotAct, &QAction::triggered, this, [this, exprPath, varTyp]() { emit plotArrayRequested(exprPath, varTyp); });
    QAction* graphAct = menu->addAction(QStringLiteral("Graph Pointer / Struct..."));
    connect(graphAct, &QAction::triggered, this, [this, exprPath, varName, varVal, varTyp]() {
        emit visualizeInPointerGraphRequested(exprPath, varName, varVal, varTyp);
    });
    QAction* calcAct = menu->addAction(QStringLiteral("Open Value in Programmer's Calculator"));
    connect(calcAct, &QAction::triggered, this, [this, varVal, exprPath]() {
        emit sendToCalculatorRequested(varVal.isEmpty() ? exprPath : varVal);
    });
    menu->addSeparator();
    for (const auto& [label, mode] : {std::pair{QStringLiteral("Watch Memory Write (Hardware Watchpoint)"), ::WatchpointAccessMode::Write},
                                      std::pair{QStringLiteral("Watch Memory Read (Hardware Watchpoint)"), ::WatchpointAccessMode::Read},
                                      std::pair{QStringLiteral("Watch Memory Read/Write (Hardware Watchpoint)"), ::WatchpointAccessMode::ReadWrite}}) {
        QAction* wpAct = menu->addAction(label);
        connect(wpAct, &QAction::triggered, this, [this, exprPath, mode]() {
            if (m_coordinator) m_coordinator->setDataWatchpoint(exprPath, mode, -1);
            emit dataWatchpointRequested(exprPath, static_cast<int>(mode));
        });
    }

    if (m_pluginBridge != nullptr) {
        const auto varMatches = varTyp.isEmpty() ? QVector<gridlock::core::plugins::RegisteredVariableAction>{}
                                                 : m_pluginBridge->matchingVariableActions(varTyp.left(2048));
        const QString ctxTarget = (varTyp.isEmpty() ? varName : varTyp).left(2048);
        const auto ctxMatches = m_pluginBridge->matchingContextActions(GRIDLOCK_SURFACE_VARIABLES, ctxTarget);
        if (!varMatches.isEmpty() || !ctxMatches.isEmpty()) {
            menu->addSeparator();
            QMenu* pluginSub = menu->addMenu(QStringLiteral("Plugin Actions"));
            pluginSub->setObjectName(QStringLiteral("variablePluginActionsMenu"));
            const QString hexTok = varVal.section(QLatin1Char(' '), 0, 0);
            bool addrOk = false;
            const uint64_t parsedAddr = hexTok.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)
                                            ? hexTok.mid(2).toULongLong(&addrOk, 16) : 0u;
            for (const auto& m : varMatches) {
                const QString actId = m.actionId;
                QAction* pAct = pluginSub->addAction(m.menuLabel);
                pAct->setObjectName(QStringLiteral("pluginVarAct_") + actId);
                connect(pAct, &QAction::triggered, this,
                        [this, actId, varName, exprPath, varTyp, varVal, parsedAddr]() {
                            if (!m_pluginBridge) return;
                            int activeFrame = 0;
                            if (QWidget* topWin = window()) {
                                if (const auto* csDock = topWin->findChild<gridlock::ui::CallStackDockWidget*>()) {
                                    activeFrame = std::max(0, csDock->activeFrameLevel());
                                }
                            }
                            const QByteArray aUtf8 = actId.toUtf8(), nUtf8 = varName.toUtf8();
                            const QByteArray eUtf8 = exprPath.toUtf8(), tUtf8 = varTyp.toUtf8(), vUtf8 = varVal.toUtf8();
                            GridLockVariableContext ctx{};
                            ctx.struct_size = static_cast<uint32_t>(sizeof(GridLockVariableContext));
                            ctx.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
                            ctx.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
                            ctx.rank = m_currentRankId;
                            ctx.frame_level = static_cast<int32_t>(activeFrame);
                            ctx.address = parsedAddr;
                            ctx.action_id_utf8 = aUtf8.constData();
                            ctx.name_utf8 = nUtf8.constData();
                            ctx.expression_utf8 = eUtf8.constData();
                            ctx.type_utf8 = tUtf8.constData();
                            ctx.value_utf8 = vUtf8.constData();
                            (void)m_pluginBridge->invokeVariableAction(actId, ctx);
                        });
            }
            for (const auto& cAct : ctxMatches) {
                const QString actId = cAct.actionId;
                QAction* pAct = pluginSub->addAction(cAct.menuLabel);
                pAct->setObjectName(QStringLiteral("pluginCtxAct_") + actId);
                connect(pAct, &QAction::triggered, this,
                        [this, actId, exprPath, varTyp, varVal, parsedAddr]() {
                            if (!m_pluginBridge) return;
                            int activeFrame = 0;
                            if (QWidget* topWin = window()) {
                                if (const auto* csDock = topWin->findChild<gridlock::ui::CallStackDockWidget*>()) {
                                    activeFrame = std::max(0, csDock->activeFrameLevel());
                                }
                            }
                            const QByteArray symUtf8 = exprPath.toUtf8(), tUtf8 = varTyp.toUtf8(), selUtf8 = varVal.toUtf8();
                            GridLockSurfaceContext ctx{};
                            ctx.struct_size = static_cast<uint32_t>(sizeof(GridLockSurfaceContext));
                            ctx.abi_major = GRIDLOCK_PLUGIN_ABI_MAJOR;
                            ctx.abi_minor = GRIDLOCK_PLUGIN_ABI_MINOR;
                            ctx.surface = GRIDLOCK_SURFACE_VARIABLES;
                            ctx.rank = m_currentRankId;
                            ctx.frame_level = static_cast<int32_t>(activeFrame);
                            ctx.line = -1;
                            ctx.column = -1;
                            ctx.bp_id = -1;
                            ctx.address = parsedAddr;
                            ctx.symbol_utf8 = symUtf8.isEmpty() ? nullptr : symUtf8.constData();
                            ctx.type_utf8 = tUtf8.isEmpty() ? nullptr : tUtf8.constData();
                            ctx.selected_text_utf8 = selUtf8.isEmpty() ? nullptr : selUtf8.constData();
                            (void)m_pluginBridge->invokeContextAction(actId, ctx);
                        });
            }
        }
    }
    return menu;
}

QDialog* VariablesDockWidget::inspectFullValue(const QString& title, const QString& fullText) {
    m_lastInspectedFullValue = fullText;
    auto* dlg = findChild<QDialog*>(QStringLiteral("fullValueInspectorDialog"));
    if (!dlg) {
        dlg = new QDialog(this); dlg->setObjectName(QStringLiteral("fullValueInspectorDialog")); dlg->resize(520, 260);
        auto* v = new QVBoxLayout(dlg); v->setContentsMargins(10, 10, 10, 10); v->setSpacing(8);
        auto* te = new QPlainTextEdit(dlg); te->setObjectName(QStringLiteral("fullValueInspectorTextEdit"));
        te->setReadOnly(true); te->setWordWrapMode(QTextOption::WrapAnywhere);
        QFont mf(QStringLiteral("JetBrains Mono"), 10); mf.setStyleHint(QFont::Monospace); te->setFont(mf);
        v->addWidget(te, 1);
        auto* hb = new QHBoxLayout(); hb->addStretch(1);
        auto* copyBtn = new QPushButton(QStringLiteral("Copy to Clipboard"), dlg); copyBtn->setObjectName(QStringLiteral("fullValueInspectorCopyBtn"));
        auto* closeBtn = new QPushButton(QStringLiteral("Close"), dlg); closeBtn->setObjectName(QStringLiteral("fullValueInspectorCloseBtn"));
        connect(copyBtn, &QPushButton::clicked, dlg, [te]() { QApplication::clipboard()->setText(te->toPlainText()); });
        connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);
        hb->addWidget(copyBtn); hb->addWidget(closeBtn); v->addLayout(hb);
    }
    dlg->setWindowTitle(QStringLiteral("Inspect Full Value - %1").arg(title));
    if (auto* te = dlg->findChild<QPlainTextEdit*>(QStringLiteral("fullValueInspectorTextEdit"))) te->setPlainText(fullText);
    dlg->show();
    return dlg;
}

bool VariablesDockWidget::copySelectedVariableValue() const {
    if (!m_variablesTree || !m_model || !m_proxyModel) return false;
    const QModelIndex proxyIdx = m_variablesTree->currentIndex();
    if (!proxyIdx.isValid()) return false;
    const QModelIndex srcIdx = m_proxyModel->mapToSource(proxyIdx);
    QString val = m_model->data(srcIdx.siblingAtColumn(1), Qt::DisplayRole).toString().trimmed();
    if (val.isEmpty()) val = m_model->expressionPath(srcIdx.siblingAtColumn(0)).trimmed();
    if (val.isEmpty()) return false;
    QApplication::clipboard()->setText(val);
    return true;
}

void VariablesDockWidget::setFocusedRank(int rankId, int rankCount) {
    const int target = qMax(0, rankId);
    const int coordCount = m_coordinator ? m_coordinator->getProcessCount() : 0;
    const int desiredCount = qMax(qMax(coordCount, rankCount), target + 1);
    if (m_rankSelector) {
        QSignalBlocker sb(m_rankSelector);
        if (m_rankSelector->count() < desiredCount || m_rankSelector->findData(target) < 0) {
            m_rankSelector->clear();
            for (int i = 0; i < desiredCount; ++i) {
                m_rankSelector->addItem(i == 0 ? QStringLiteral("Rank 0 (Master)") : QStringLiteral("Rank %1").arg(i), i);
            }
        }
        m_rankSelector->setVisible(desiredCount > 1);
        if (auto* sl = findChild<QLabel*>(QStringLiteral("variablesScopeLabel"))) sl->setVisible(desiredCount > 1);
        const int idx = m_rankSelector->findData(target);
        if (idx >= 0) m_rankSelector->setCurrentIndex(idx);
    }
    const bool rankChanged = (m_currentRankId != target);
    m_currentRankId = target;
    if (rankChanged && m_model && m_coordinator) m_model->loadLocals(m_currentRankId);
}

bool VariablesDockWidget::applyLocalsForRank(int rankId, const QJsonArray& locals) {
    if (rankId != m_currentRankId || !m_model) return false;
    m_model->onLocalsUpdated(rankId, 0, locals);
    return true;
}

/**
 * @brief Rebuilds rank selector dropdown items to match active MPI cluster size.
 */
void VariablesDockWidget::onProcessCountChanged() {
    if (!m_coordinator) return;
    const int count = m_coordinator->getProcessCount();
    const int target = (count > 0 && m_currentRankId >= 0 && m_currentRankId < count) ? m_currentRankId : 0;
    {
        QSignalBlocker sb(m_rankSelector);
        m_rankSelector->clear();
        for (int i = 0; i < count; ++i) {
            m_rankSelector->addItem(i == 0 ? QStringLiteral("Rank 0 (Master)") : QStringLiteral("Rank %1").arg(i), i);
        }
        if (count > 0) {
            m_rankSelector->setVisible(count > 1);
            if (auto* sl = findChild<QLabel*>(QStringLiteral("variablesScopeLabel"))) sl->setVisible(count > 1);
            const int idx = m_rankSelector->findData(target);
            if (idx >= 0) m_rankSelector->setCurrentIndex(idx);
        }
    }
    if (count > 0) {
        m_currentRankId = target;
        if (m_model) m_model->loadLocals(target);
    }
}

/**
 * @brief Updates focused rank ID and requests frame locals from tree model.
 * @param index Selected index in combobox.
 */
void VariablesDockWidget::onRankSelected(int index) {
    if (index < 0 || !m_rankSelector) return;
    const int rankId = qMax(0, m_rankSelector->itemData(index).toInt());
    m_currentRankId = rankId;
    if (m_coordinator && m_model) m_model->loadLocals(rankId);
    emit rankSelectionChanged(rankId);
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

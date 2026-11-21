/**
 * @file VariablesDockWidget.hpp
 * @brief Hierarchical local variable inspection tree dock widget.
 *
 * Implements a dockable widget presenting scope variables, memory addresses,
 * and types for any selected MPI rank via VariableTreeModel and GDB/DAP backends.
 */

#pragma once

#include <QComboBox>
#include <QLineEdit>
#include <QMap>
#include <QSortFilterProxyModel>
#include <QString>
#include <QTreeView>
#include <QWidget>

#include "../../RankState.hpp"
#include "../../core/models/VariableTreeModel.hpp"

class IBackendCoordinator;

namespace gridlock {

/**
 * @class VariablesDockWidget
 * @brief UI dock displaying stack frame local variables and memory addresses.
 */
class VariablesDockWidget : public QWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs the variables inspection dock widget.
     * @param parent Optional parent widget.
     */
    explicit VariablesDockWidget(QWidget* parent = nullptr);

    /**
     * @brief Destructor releasing attached models and UI components.
     */
    ~VariablesDockWidget() override;

    /**
     * @brief Attaches a backend coordinator and hooks execution stop/state events.
     * @param coordinator Pointer to active backend coordinator.
     */
    void setCoordinator(::IBackendCoordinator* coordinator);

    /**
     * @brief Promotes a local variable name to the Watch Expressions differential grid.
     * @param variableName Local variable or expression string to watch across MPI ranks.
     * @return True if a non-empty variable name was promoted, false otherwise.
     */
    bool promoteVariableToWatch(const QString& variableName);

    /**
     * @brief Copies the currently selected variable value (Ctrl+C) to the system clipboard.
     */
    bool copySelectedVariableValue() const;

signals:
    /**
     * @brief Emitted when a user promotes a variable from the tree into Watch Expressions.
     * @param expression The variable or expression string to add to Watch Expressions.
     */
    void watchExpressionRequested(const QString& expression);

    /**
     * @brief Emitted when a user arms a hardware data watchpoint from the variables context menu.
     * @param expression Watched variable or lvalue memory expression.
     * @param accessMode Access mode integer (`0 = Write`, `1 = Read`, `2 = ReadWrite`).
     */
    void dataWatchpointRequested(const QString& expression, int accessMode);

    /**
     * @brief Emitted when the user selects "Graph Pointer / Struct..." on a variable row.
     */
    void visualizeInPointerGraphRequested(const QString& exprPath,
                                          const QString& varName,
                                          const QString& varValue,
                                          const QString& varType);

    /**
     * @brief Emitted when the user selects "Plot 1D/2D Array..." on a variable row.
     */
    void plotArrayRequested(const QString& expression, const QString& typeString);

    /**
     * @brief Emitted when the user chooses "Open Value in Programmer's Calculator" on a variable row.
     * @param text Formatted numeric value or pointer address ("0x...").
     */
    void sendToCalculatorRequested(const QString& text);

private slots:
    /**
     * @brief Handles rank selection changes from the dropdown combo box.
     * @param index ComboBox item index.
     */
    void onRankSelected(int index);

    /**
     * @brief Refreshes local variable tree when an MPI rank changes state.
     * @param rankId Zero-based rank index.
     * @param state State snapshot containing status and registers.
     */
    void onRankStateChanged(int rankId, const RankState& state);

    /**
     * @brief Rebuilds rank selector items when process count changes.
     */
    void onProcessCountChanged();

private:
    /**
     * @brief Constructs layout, scope/search header bar, and sortable tree view columns.
     */
    void setupUi();

    ::IBackendCoordinator* m_coordinator{nullptr};   ///< Attached backend coordinator.
    VariableTreeModel* m_model{nullptr};              ///< Source hierarchical variable model.
    QSortFilterProxyModel* m_proxyModel{nullptr};     ///< Recursive case-insensitive filter & sort proxy.

    QComboBox* m_rankSelector{nullptr};               ///< Dropdown choosing target rank.
    QLineEdit* m_searchEdit{nullptr};                 ///< Live search box (`#variableSearchEdit`).
    QTreeView* m_variablesTree{nullptr};              ///< Multi-column sortable hierarchical tree view.

    int m_currentRankId{0};                           ///< Active focused rank ID.
    QMap<int, QString> m_lastRankStates;              ///< Cached prior operational states.
};

}  // namespace gridlock

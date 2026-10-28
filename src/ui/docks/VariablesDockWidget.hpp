/**
 * @file VariablesDockWidget.hpp
 * @brief Hierarchical local variable inspection tree dock widget.
 *
 * Implements a dockable widget presenting scope variables, memory addresses,
 * and types for any selected MPI rank via VariableTreeModel and GDB/DAP backends.
 */

#pragma once

#include <QComboBox>
#include <QMap>
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
     * @brief Constructs layout, scope combo box, and tree view columns.
     */
    void setupUi();

    ::IBackendCoordinator* m_coordinator{nullptr};   ///< Attached backend coordinator.
    VariableTreeModel* m_model{nullptr};              ///< Tree model populating variable rows.
    
    QComboBox* m_rankSelector{nullptr};               ///< Dropdown choosing target rank.
    QTreeView* m_variablesTree{nullptr};              ///< Multi-column hierarchical tree view.
    
    int m_currentRankId{0};                           ///< Active focused rank ID.
    QMap<int, QString> m_lastRankStates;              ///< Cached prior operational states.
};

}  // namespace gridlock

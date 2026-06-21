#pragma once
#include <QDockWidget>
#include <QComboBox>
#include <QTreeView>
#include "../core/VariableTreeModel.hpp"
#include "../RankState.hpp"

namespace gridlock {

class GdbRankCoordinator;

class VariablesDockWidget : public QDockWidget {
    Q_OBJECT
public:
    explicit VariablesDockWidget(GdbRankCoordinator* coordinator, QWidget* parent = nullptr);
    ~VariablesDockWidget() override;

private slots:
    void onRankSelected(int index);
    void onRankStateChanged(int rankId, const RankState& state);
    void onProcessCountChanged();

private:
    void setupUi();

    GdbRankCoordinator* m_coordinator;
    VariableTreeModel* m_model;
    
    QComboBox* m_rankSelector;
    QTreeView* m_variablesTree;
    
    int m_currentRankId;
};

} // namespace gridlock

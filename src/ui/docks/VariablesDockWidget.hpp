#pragma once
#include <QWidget>
#include <QComboBox>
#include <QTreeView>
#include "../../core/models/VariableTreeModel.hpp"
#include "../../RankState.hpp"

class IBackendCoordinator;

namespace gridlock {

class VariablesDockWidget : public QWidget {
    Q_OBJECT
public:
    explicit VariablesDockWidget(QWidget* parent = nullptr);
    ~VariablesDockWidget() override;

    void setCoordinator(::IBackendCoordinator* coordinator);

private slots:
    void onRankSelected(int index);
    void onRankStateChanged(int rankId, const RankState& state);
    void onProcessCountChanged();

private:
    void setupUi();

    ::IBackendCoordinator* m_coordinator;
    VariableTreeModel* m_model;
    
    QComboBox* m_rankSelector;
    QTreeView* m_variablesTree;
    
    int m_currentRankId;
    QMap<int, QString> m_lastRankStates;
};

} // namespace gridlock

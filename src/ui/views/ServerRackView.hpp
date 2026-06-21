#pragma once
#include <QListWidget>
#include <unordered_map>
#include <QString>
#include "../../RankState.hpp"

namespace gridlock::ui {

class ServerRackView : public QListWidget {
    Q_OBJECT
public:
    explicit ServerRackView(QWidget *parent = nullptr);
    ~ServerRackView() override = default;

    void resetRanks(int count);
    void updateRankState(int rank, const RankState& state);

signals:
    void rankSelected(int rankId);

private slots:
    void onItemClicked(QListWidgetItem* item);

private:
    int m_rankCount = 0;
    std::unordered_map<int, RankState> m_states;
};

} // namespace gridlock::ui

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
    int rankNodeCount() const { return count(); }
    bool selectRank(int rankId);
    int selectedRank() const { return m_selectedRank; }

signals:
    void rankSelected(int rankId, uint64_t pcAddress = 0ULL);

private slots:
    void onItemClicked(QListWidgetItem* item);

private:
    int m_rankCount = 0;
    int m_selectedRank = 0;
    std::unordered_map<int, RankState> m_states;
};

} // namespace gridlock::ui

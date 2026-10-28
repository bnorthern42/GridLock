#pragma once
#include <QList>
#include <QListWidget>
#include <QMap>
#include <QString>
#include <cstdint>
#include <unordered_map>
#include "../../RankState.hpp"

namespace gridlock::core::hpc {
using RankState = ::gridlock::RankState;
} // namespace gridlock::core::hpc

namespace gridlock::ui {

class ServerRackView : public QListWidget {
    Q_OBJECT
public:
    explicit ServerRackView(QWidget *parent = nullptr);
    ~ServerRackView() override = default;

    void resetRanks(int count);
    void updateRanks(const QList<RankState>& ranks);
    void updateRankState(int rank, const RankState& state);
    int rankNodeCount() const { return count(); }
    bool selectRank(int rankId);
    int selectedRank() const { return m_selectedRank; }
    uint64_t rankPcAddress(int rankId) const;
    QMap<int, uint64_t> rankPcAddresses() const;

signals:
    void rankSelected(int rankId, uint64_t pcAddress = 0ULL);
    void ranksUpdated(const QMap<int, uint64_t>& rankPcMap);

private slots:
    void onItemClicked(QListWidgetItem* item);

private:
    static uint64_t extractPcFromState(int rankId, const RankState& state);

    int m_rankCount = 0;
    int m_selectedRank = 0;
    std::unordered_map<int, RankState> m_states;
};

} // namespace gridlock::ui

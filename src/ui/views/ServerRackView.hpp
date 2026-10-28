#pragma once

/**
 * @file ServerRackView.hpp
 * @brief MPI server rack topology visualizer and rank selector.
 *
 * Implements a list-based representation of computing nodes/ranks in an MPI
 * job, displaying rank status, current program counter (PC), and emitting
 * synchronization signals when ranks are selected or updated.
 */

#include <QList>
#include <QListWidget>
#include <QMap>
#include <QString>
#include <cstdint>
#include <unordered_map>
#include "../../RankState.hpp"

namespace gridlock::core::hpc {
using RankState = ::gridlock::RankState;
}  // namespace gridlock::core::hpc

namespace gridlock::ui {

/**
 * @class ServerRackView
 * @brief Widget displaying MPI cluster rank nodes and their operational states.
 *
 * Renders each rank as a distinct node item with status indicators (Running,
 * Paused, Diverged, Terminated) and program counter values. Allows selecting
 * ranks to direct debugging views (registers, disassembly, memory) to that rank.
 */
class ServerRackView : public QListWidget {
    Q_OBJECT

public:
    /**
     * @brief Constructs a new ServerRackView.
     * @param parent Optional parent widget.
     */
    explicit ServerRackView(QWidget *parent = nullptr);

    /**
     * @brief Destructor.
     */
    ~ServerRackView() override = default;

    /**
     * @brief Reinitializes the rack list with a specific number of default ranks.
     * @param count Total rank count.
     */
    void resetRanks(int count);

    /**
     * @brief Batch updates the states of multiple ranks.
     * @param ranks List of RankState snapshots.
     */
    void updateRanks(const QList<RankState>& ranks);

    /**
     * @brief Updates the execution state of an individual rank node.
     * @param rank Zero-based rank index.
     * @param state New RankState.
     */
    void updateRankState(int rank, const RankState& state);

    /**
     * @brief Returns the total number of rank nodes rendered in the view.
     * @return Rank count.
     */
    int rankNodeCount() const { return count(); }

    /**
     * @brief Selects a rank programmatically.
     * @param rankId Zero-based rank identifier.
     * @return True if rank was found and selected; false otherwise.
     */
    bool selectRank(int rankId);

    /**
     * @brief Retrieves the currently selected rank ID.
     * @return Selected rank index.
     */
    int selectedRank() const { return m_selectedRank; }

    /**
     * @brief Retrieves the program counter (PC) for a specific rank.
     * @param rankId Rank index.
     * @return 64-bit PC address.
     */
    uint64_t rankPcAddress(int rankId) const;

    /**
     * @brief Retrieves a mapping of all active ranks to their respective program counters.
     * @return Map from rank ID to 64-bit PC address.
     */
    QMap<int, uint64_t> rankPcAddresses() const;

signals:
    /**
     * @brief Emitted when a rank node is selected by user click or programmatic seek.
     * @param rankId The selected rank ID.
     * @param pcAddress The rank's current program counter.
     */
    void rankSelected(int rankId, uint64_t pcAddress = 0ULL);

    /**
     * @brief Emitted when rank program counters change across the cluster.
     * @param rankPcMap Mapping of rank IDs to updated PC addresses.
     */
    void ranksUpdated(const QMap<int, uint64_t>& rankPcMap);

private slots:
    /**
     * @brief Handles user clicks on rank items in the list.
     * @param item Pointer to the clicked QListWidgetItem.
     */
    void onItemClicked(QListWidgetItem* item);

private:
    /**
     * @brief Extracts or synthesizes a program counter address from rank state telemetry.
     * @param rankId Target rank ID.
     * @param state Rank state object containing registers and execution info.
     * @return Extracted 64-bit PC address.
     */
    static uint64_t extractPcFromState(int rankId, const RankState& state);

    /// Total number of ranks in the current topology.
    int m_rankCount = 0;

    /// Currently highlighted/selected rank index.
    int m_selectedRank = 0;

    /// Internal cache of rank states indexed by rank ID.
    std::unordered_map<int, RankState> m_states;
};

}  // namespace gridlock::ui


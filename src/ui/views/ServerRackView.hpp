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

class QContextMenuEvent;
class QMenu;

namespace gridlock::core::plugins {
class PluginHostBridge;
}  // namespace gridlock::core::plugins

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
     * @brief Places all configured rank cards into pre-launch IDLE state with no instruction address.
     * @param configuredRanks Number of configured MPI ranks (-np).
     */
    void setSessionIdle(int configuredRanks);

    /**
     * @brief Batch updates the states of multiple ranks.
     * @param ranks List of RankState snapshots.
     */
    void updateRanks(const QList<RankState>& ranks);

    /**
     * @brief Returns active list canvas background color according to the theme.
     * @return Resolved QColor corresponding to active theme canvas base.
     */
    QColor activeCanvasBackgroundColor() const;

    /**
     * @brief Reloads widget styling according to the active theme palette.
     */
    Q_INVOKABLE void reloadStyle();

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

    /** @brief Applies a state filter (`"all"`, `"deadlock"`, `"stopped"`, `"running"`, `"exited"`). */
    void setStateFilter(const QString& filter);

    /** @brief Returns the active rank state filter key. */
    QString stateFilter() const { return m_stateFilter; }

    /** @brief Returns the number of rank cards currently visible under the active state filter. */
    int visibleRankCount() const;

    /** @brief Returns true if @p state matches filter key @p filter. */
    static bool stateMatchesFilter(const QString& state, const QString& filter);

    /** @brief Returns the short pill label rendered for rank execution @p state. */
    static QString badgeTextForState(const QString& state);

    /** @brief Returns the accent color rendered for rank execution @p state. */
    static QColor badgeColorForState(const QString& state);

    /** @brief Attaches the native plugin host bridge for `GRIDLOCK_SURFACE_RANK_LIST` context actions. */
    void setPluginHostBridge(gridlock::core::plugins::PluginHostBridge* bridge) { m_pluginBridge = bridge; }

    /** @brief Builds a caller-owned context menu for @p rankId including matching **Plugin Actions**. */
    QMenu* createContextMenuForRank(int rankId, QWidget* menuParent = nullptr);

protected:
    /** @brief Opens the rank context menu on right-click over a server-rack blade item. */
    void contextMenuEvent(QContextMenuEvent* event) override;

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
    /** @brief Re-evaluates item visibility for all rank cards against `m_stateFilter`. */
    void applyStateFilter();

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

    /// Active state filter key (`"all"`, `"deadlock"`, `"stopped"`, `"running"`, `"exited"`).
    QString m_stateFilter = QStringLiteral("all");

    /// Internal cache of rank states indexed by rank ID.
    std::unordered_map<int, RankState> m_states;

    /// Optional plugin host bridge providing `GRIDLOCK_SURFACE_RANK_LIST` actions.
    gridlock::core::plugins::PluginHostBridge* m_pluginBridge = nullptr;
};

}  // namespace gridlock::ui


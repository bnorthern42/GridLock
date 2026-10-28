/**
 * @file SessionManager.cpp
 * @brief Implementation of TOML session serialization, deserialization, and MRU history.
 *
 * Uses `toml++` to encode and decode the `[workspace]` table containing
 * `watched_variables`, `active_breakpoints`, and `selected_ranks`.
 */

#include "SessionManager.hpp"

#include <fstream>
#include <iostream>

#include <QDebug>
#include <toml++/toml.hpp>

namespace gridlock::core::managers {

/**
 * @brief Writes the `[workspace]` TOML table containing watches, breakpoints, and ranks to disk.
 */
bool SessionManager::saveSession(const QString& filePath, const SessionState& state) {
    try {
        // Populate the watched_variables TOML array from the session snapshot.
        toml::array watches;
        for (const auto& w : state.watchedVariables) {
            watches.push_back(w.toStdString());
        }

        // Populate the active_breakpoints TOML array.
        toml::array breakpoints;
        for (const auto& b : state.activeBreakpoints) {
            breakpoints.push_back(b.toStdString());
        }

        // Populate the selected_ranks TOML integer array.
        toml::array ranks;
        for (int r : state.selectedRanks) {
            ranks.push_back(r);
        }

        // Assemble the root TOML document containing the `[workspace]` section.
        toml::table tbl{
            {"workspace", toml::table{
                {"watched_variables", watches},
                {"active_breakpoints", breakpoints},
                {"selected_ranks", ranks}
            }}
        };

        // Truncate and write the serialized TOML payload to the target file path.
        std::ofstream os(filePath.toStdString(), std::ios_base::trunc);
        if (!os.is_open()) {
            qWarning() << "Failed to open file for writing:" << filePath;
            return false;
        }

        os << tbl << "\n";
        return true;
    } catch (const std::exception& e) {
        qWarning() << "Exception while saving session:" << e.what();
        return false;
    }
}

/**
 * @brief Parses a TOML session file, updates the MRU list, and emits sessionLoaded().
 */
std::optional<SessionState> SessionManager::loadSession(const QString& filePath) {
    try {
        // Parse the TOML file from disk and verify the `[workspace]` table exists.
        auto tbl = toml::parse_file(filePath.toStdString());
        auto* workspace = tbl["workspace"].as_table();
        if (!workspace) {
            qWarning() << "No [workspace] table found in session file.";
            return std::nullopt;
        }

        SessionState state;

        // Extract watched_variables string entries.
        if (auto* watches = workspace->get_as<toml::array>("watched_variables")) {
            for (const auto& elem : *watches) {
                if (auto str = elem.value<std::string>()) {
                    state.watchedVariables.push_back(QString::fromStdString(*str));
                }
            }
        }

        // Extract active_breakpoints string entries.
        if (auto* breakpoints = workspace->get_as<toml::array>("active_breakpoints")) {
            for (const auto& elem : *breakpoints) {
                if (auto str = elem.value<std::string>()) {
                    state.activeBreakpoints.push_back(QString::fromStdString(*str));
                }
            }
        }

        // Extract selected_ranks integer entries.
        if (auto* ranks = workspace->get_as<toml::array>("selected_ranks")) {
            for (const auto& elem : *ranks) {
                if (auto val = elem.value<int>()) {
                    state.selectedRanks.push_back(*val);
                }
            }
        }

        // Record the loaded file at the front of the MRU list and notify listeners.
        addMruSession(filePath);
        emit sessionLoaded(filePath);
        return state;
    } catch (const std::exception& e) {
        qWarning() << "Exception while loading session:" << e.what();
        return std::nullopt;
    }
}

/**
 * @brief Inserts filePath at the front of the MRU list, deduplicating and capping at MAX_MRU.
 */
void SessionManager::addMruSession(const QString& filePath) {
    // Remove any existing occurrence so filePath moves to index 0 without duplicates.
    m_mruSessions.removeAll(filePath);
    m_mruSessions.prepend(filePath);
    // Evict the oldest entry when the list exceeds MAX_MRU (5).
    if (m_mruSessions.size() > MAX_MRU) {
        m_mruSessions.removeLast();
    }
}

} // namespace gridlock::core::managers

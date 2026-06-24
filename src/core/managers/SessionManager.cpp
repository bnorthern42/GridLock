#include "SessionManager.hpp"
#include <toml++/toml.hpp>
#include <fstream>
#include <QDebug>
#include <iostream>

namespace gridlock::core::managers {

bool SessionManager::saveSession(const QString& filePath, const SessionState& state) {
    try {
        toml::array watches;
        for (const auto& w : state.watchedVariables) {
            watches.push_back(w.toStdString());
        }

        toml::array breakpoints;
        for (const auto& b : state.activeBreakpoints) {
            breakpoints.push_back(b.toStdString());
        }

        toml::array ranks;
        for (int r : state.selectedRanks) {
            ranks.push_back(r);
        }

        toml::table tbl{
            { "workspace", toml::table{
                { "watched_variables", watches },
                { "active_breakpoints", breakpoints },
                { "selected_ranks", ranks }
            }}
        };

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

std::optional<SessionState> SessionManager::loadSession(const QString& filePath) {
    try {
        auto tbl = toml::parse_file(filePath.toStdString());
        auto workspace = tbl["workspace"].as_table();
        if (!workspace) {
            qWarning() << "No [workspace] table found in session file.";
            return std::nullopt;
        }

        SessionState state;

        if (auto watches = workspace->get_as<toml::array>("watched_variables")) {
            for (const auto& elem : *watches) {
                if (auto str = elem.value<std::string>()) {
                    state.watchedVariables.push_back(QString::fromStdString(*str));
                }
            }
        }

        if (auto breakpoints = workspace->get_as<toml::array>("active_breakpoints")) {
            for (const auto& elem : *breakpoints) {
                if (auto str = elem.value<std::string>()) {
                    state.activeBreakpoints.push_back(QString::fromStdString(*str));
                }
            }
        }

        if (auto ranks = workspace->get_as<toml::array>("selected_ranks")) {
            for (const auto& elem : *ranks) {
                if (auto val = elem.value<int>()) {
                    state.selectedRanks.push_back(*val);
                }
            }
        }

        return state;
    } catch (const std::exception& e) {
        qWarning() << "Exception while loading session:" << e.what();
        return std::nullopt;
    }
}

} // namespace gridlock::core::managers

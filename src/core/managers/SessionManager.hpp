#pragma once

#include <QString>
#include <vector>
#include <optional>
#include <sys/types.h>

namespace gridlock::core::managers {

struct SessionState {
    std::vector<QString> watchedVariables;
    std::vector<QString> activeBreakpoints;
    std::vector<int> selectedRanks;
};

class SessionManager {
public:
    static SessionManager& instance() {
        static SessionManager instance;
        return instance;
    }

    bool saveSession(const QString& filePath, const SessionState& state);
    std::optional<SessionState> loadSession(const QString& filePath);

private:
    SessionManager() = default;
    ~SessionManager() = default;
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
};

} // namespace gridlock::core::managers

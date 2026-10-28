/**
 * @file SessionManager.hpp
 * @brief TOML-backed debugging session serializer and Most-Recently-Used (MRU) tracker.
 *
 * Persists and restores user debugging state (watched variables, active breakpoints,
 * and selected MPI ranks) to `.toml` session files while maintaining a bounded MRU
 * session history list.
 */

#pragma once

#include <sys/types.h>

#include <optional>
#include <vector>

#include <QObject>
#include <QString>
#include <QStringList>

namespace gridlock::core::managers {

/**
 * @brief Serializable snapshot of workspace watch expressions, breakpoints, and rank selections.
 */
struct SessionState {
    /** @brief Watched variable expressions pinned in the DifferentialGrid. */
    std::vector<QString> watchedVariables;

    /** @brief Active source breakpoints formatted as `file:line` strings. */
    std::vector<QString> activeBreakpoints;

    /** @brief Selected MPI rank indices active in the session. */
    std::vector<int> selectedRanks;
};

/**
 * @brief Singleton manager for saving, loading, and tracking recent debugging sessions.
 */
class SessionManager : public QObject {
    Q_OBJECT
public:
    /** @brief Returns the process-wide SessionManager singleton instance. */
    static SessionManager& instance() {
        static SessionManager instance;
        return instance;
    }

    /**
     * @brief Serializes the given SessionState to a TOML file at filePath.
     * @param filePath Destination filesystem path.
     * @param state Session state snapshot to write.
     * @return True if the TOML file was written cleanly; false on I/O or serialization error.
     */
    bool saveSession(const QString& filePath, const SessionState& state);

    /**
     * @brief Parses a TOML session file and reconstructs its SessionState.
     * @param filePath Source TOML file path.
     * @return Populated SessionState on success, or std::nullopt if missing/malformed.
     */
    std::optional<SessionState> loadSession(const QString& filePath);

    /** @brief Returns the Most-Recently-Used session file paths (up to MAX_MRU entries). */
    QStringList getMruSessions() const { return m_mruSessions; }

    /**
     * @brief Promotes or inserts filePath at the front of the MRU session list.
     * @param filePath Session file path to record.
     */
    void addMruSession(const QString& filePath);

signals:
    /** @brief Emitted after a session file has been successfully loaded. */
    void sessionLoaded(QString workspaceRoot);

private:
    SessionManager() = default;
    ~SessionManager() override = default;
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    QStringList m_mruSessions;
    static constexpr int MAX_MRU = 5;
};

} // namespace gridlock::core::managers

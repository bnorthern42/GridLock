#pragma once

#include <QString>
#include <vector>
#include <optional>
#include <sys/types.h>
#include <QObject>
#include <QStringList>

namespace gridlock::core::managers {

struct SessionState {
    std::vector<QString> watchedVariables;
    std::vector<QString> activeBreakpoints;
    std::vector<int> selectedRanks;
};

class SessionManager : public QObject {
    Q_OBJECT
public:
    static SessionManager& instance() {
        static SessionManager instance;
        return instance;
    }

    bool saveSession(const QString& filePath, const SessionState& state);
    std::optional<SessionState> loadSession(const QString& filePath);

    QStringList getMruSessions() const { return m_mruSessions; }
    void addMruSession(const QString& filePath);

signals:
    void sessionLoaded(QString workspaceRoot);

private:
    SessionManager() = default;
    ~SessionManager() = default;
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    QStringList m_mruSessions;
    static constexpr int MAX_MRU = 5;
};

} // namespace gridlock::core::managers

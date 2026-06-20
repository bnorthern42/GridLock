#pragma once
#include <QObject>
#include <QProcess>
#include <vector>
#include <memory>
#include "RankState.hpp"

namespace gridlock {

class GdbRankCoordinator : public QObject {
    Q_OBJECT
public:
    explicit GdbRankCoordinator(QObject *parent = nullptr);
    ~GdbRankCoordinator() override;

    void startDebugSession(int rankCount, const QString& executable);
    void launchParallelSession(const QString& executable, int rankCount);
    void insertBreakpoint(const QString& location);
    void broadcastBreakpoint(const QString& file, int line);
    void broadcastCommand(const QString& cmd);
    void terminateAllSessions();
    void requestDisassemblyFallback(int rankId);
    
    int getProcessCount() const { return m_processes.size(); }
    RankState getRankState(int rankId) const {
        if (rankId >= 0 && rankId < static_cast<int>(m_processes.size()) && m_processes[rankId]) {
            return m_processes[rankId]->state;
        }
        RankState st;
        st.currentState = "offline";
        return st;
    }

signals:
    void rankStateChanged(int rankId, const RankState& state);

public slots:
    void stepAll();
    void continueAll();
    void runAll();
    void haltAll();
    void pauseFocusedRank(int rankId);

public slots:
    void handleGdbOutput(int rankId);

private:
    struct RankProcess {
        int id;
        std::unique_ptr<QProcess> process;
        RankState state;
        QString buffer;
    };
    std::vector<std::unique_ptr<RankProcess>> m_processes;
    std::vector<QString> m_watchVariables;
};

} // namespace gridlock

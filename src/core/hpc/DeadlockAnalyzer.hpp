#pragma once
#include <QObject>
#include <QString>
#include <QMap>

namespace gridlock {
class GdbRankCoordinator;
struct RankState;
}

namespace gridlock::core {

struct DeadlockInfo {
    int rankId;
    QString blockedFunction;
    QString file;
    int line;
};

class DeadlockAnalyzer : public QObject {
    Q_OBJECT
public:
    explicit DeadlockAnalyzer(gridlock::GdbRankCoordinator* coordinator, QObject* parent = nullptr);

signals:
    void deadlockDetected(const gridlock::core::DeadlockInfo& info);
    void rankCleared(int rankId);

private slots:
    void onRankStateChanged(int rankId, const gridlock::RankState& state);
    void onGdbOutputReceived(int rankId, const QString& output);

private:
    gridlock::GdbRankCoordinator* m_coordinator;
    QMap<int, bool> m_pendingStackRequest;
};

} // namespace gridlock::core

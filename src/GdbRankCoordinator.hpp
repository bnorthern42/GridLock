#pragma once
#include "RankState.hpp"
#include <QMap>
#include <QPoint>
#include <QObject>
#include <QProcess>
#include <memory>
#include <vector>

namespace gridlock {

class GdbRankCoordinator : public QObject {
  Q_OBJECT
public:
  explicit GdbRankCoordinator(QObject *parent = nullptr);
  ~GdbRankCoordinator() override;

  void startDebugSession(int rankCount, const QString &executable);
  void launchParallelSession(const QString &executable, int rankCount);
  void insertBreakpoint(const QString &location);
  void broadcastBreakpoint(const QString &file, int line, bool isAdded = true);
  void broadcastCommand(const QString &cmd);
  void terminateAllSessions();
  void requestDisassemblyFallback(int rankId);
  void flushCachedBreakpoints(int rankId);
  void writeCmd(int rankId, const QString &cmd);
  void processGdbOutput(int rankId, const QString& output);
  void initializeMockSession(int rankCount, bool simulateInitialSync = false);
  void readMemory(int rankId, const QString &address, int length);

  int getProcessCount() const { return m_processes.size(); }
  RankState getRankState(int rankId) const {
    if (rankId >= 0 && rankId < static_cast<int>(m_processes.size()) &&
        m_processes[rankId]) {
      return m_processes[rankId]->state;
    }
    RankState st;
    st.currentState = "offline";
    return st;
  }

signals:
  void rankStateChanged(int rankId, const RankState &state);
  void gdbOutputReceived(int rankId, const QString &output);
  void commandSentToGdb(int rankId, const QString &cmd);
  void targetOutputReceived(const QString &text);
  void hoverEvaluationComplete(QString varName, QString result, QPoint globalPos);
  void memoryDataReady(int rankId, qint64 beginAddress, const QString &hexContents);

public slots:
  void stepAll();
  void continueAll();
  void runAll();
  void haltAll();
  void pauseFocusedRank(int rankId);
  void registerWatchVariable(const QString &varName);
  void sendCommand(int rankId, const QString &cmd);
  void handleGdbOutput(int rankId);
  void evaluateHoverVariable(int rankId, const QString& varName, QPoint globalPos);

private:
  struct RankProcess {
    int id;
    std::unique_ptr<QProcess> process;
    RankState state;
    QString buffer;
    QString lastHoverVarName;
    QPoint lastHoverPos;
  };
  std::unique_ptr<QProcess> m_mpirunProcess;
  std::vector<std::unique_ptr<RankProcess>> m_processes;
  QMap<int, QMap<QString, QString>>
      m_varNameMap; // rankId -> (varName -> varId)
  QMap<int, QMap<QString, QString>>
      m_varNameRevMap; // rankId -> (varId -> varName)
  std::vector<QString> m_watchVariables;
};

} // namespace gridlock
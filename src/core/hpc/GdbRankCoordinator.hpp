#pragma once
#include "../../RankState.hpp"
#include <QMap>
#include <QPoint>
#include <QObject>
#include <QProcess>
#include <memory>
#include <vector>
#include "IBackendCoordinator.hpp"

namespace gridlock {

class GdbRankCoordinator : public IBackendCoordinator {
  Q_OBJECT
public:
  explicit GdbRankCoordinator(QObject *parent = nullptr);
  ~GdbRankCoordinator() override;

  void startDebugSession(int rankCount, const QString &executable);
  void launchParallelSession(const QString &executable, int rankCount) override;
  pid_t getPidForRank([[maybe_unused]] int rankId) const override { return 0; }
  QString getCurrentBinaryPath() const override { return m_currentBinaryPath; }
  void insertBreakpoint(const QString &location);
  void broadcastBreakpoint(const QString &file, int line, bool isSet, const QString& condition = QString());
  void broadcastCommand(const QString &cmd);
  void terminateAllSessions();
  void requestDisassemblyFallback(int rankId);
  void flushCachedBreakpoints(int rankId);
  void writeCmd(int rankId, const QString &cmd);
  void processGdbOutput(int rankId, const QString& output);
  void initializeMockSession(int rankCount, bool simulateInitialSync = false);
  void readMemory(int rankId, const QString &address, int length) override;
  void evaluateExpression(int rankId, const QString& expression);

  int getProcessCount() const { return m_processes.size(); }
  RankState getRankState(int rankId) const {
    if (rankId >= 0 && rankId < static_cast<int>(m_processes.size()) &&
        m_processes[rankId]) {
      return m_processes[rankId]->state;
    }
    RankState st{};
    st.currentState = "offline";
    return st;
  }

signals:
  void rankStateChanged(int rankId, const RankState &state);
  void gdbOutputReceived(int rankId, const QString &output);
  void commandSentToGdb(int rankId, const QString &cmd);
  void hoverEvaluationComplete(QString varName, QString result, QPoint globalPos);
  void memoryDataReady(int rankId, qint64 beginAddress, const QString &hexContents);
  void expressionEvaluated(int rankId, const QString& expr, const QString& result);

public slots:
  virtual void stepAll();
  virtual void continueAll();
  virtual void runAll();
  virtual void haltAll();
  virtual void pauseFocusedRank(int rankId);
  
  void terminateSession() override;

  // IBackendCoordinator overrides
  void stepOver(int threadId) override { Q_UNUSED(threadId); stepAll(); }
  void stepInto(int threadId) override { Q_UNUSED(threadId); stepAll(); }
  void continueExecution(int threadId) override { Q_UNUSED(threadId); continueAll(); }
  void pauseExecution(int threadId) override { pauseFocusedRank(threadId - 1); }

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
    QString lastEvalExpression;
  };
  std::unique_ptr<QProcess> m_mpirunProcess;
  std::vector<std::unique_ptr<RankProcess>> m_processes;
  QMap<int, QMap<QString, QString>>
      m_varNameMap; // rankId -> (varName -> varId)
  QMap<int, QMap<QString, QString>>
      m_varNameRevMap; // rankId -> (varId -> varName)
  std::vector<QString> m_watchVariables;
  QString m_currentBinaryPath;
};

} // namespace gridlock
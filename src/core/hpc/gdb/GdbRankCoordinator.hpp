/**
 * @file GdbRankCoordinator.hpp
 * @brief Multi-rank GDB/MI3 session coordinator managing `mpiexec` + per-rank `gdb` instances.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QPoint>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QSocketNotifier>
#include <QString>

#include "../../../RankState.hpp"
#include "../IBackendCoordinator.hpp"
#include "../system/ProcessScanner.hpp"

namespace gridlock {

class GdbRankCoordinator : public IBackendCoordinator {
  Q_OBJECT

 public:
  explicit GdbRankCoordinator(QObject* parent = nullptr);
  ~GdbRankCoordinator() override;

  void startDebugSession(int rankCount, const QString& executable);
  void launchParallelSession(const QString& executable, int rankCount) override;
  void launchParallelSession(const QString& executable, int rankCount, const QString& mpiArgs) override;
  qint64 getPidForRank(int rank) const override;
  bool attachToProcess(qint64 pid) override;
  bool attachToPids(const QList<qint64>& pids);
  bool attachToProcesses(const QList<ProcessEntry>& targets);
  bool attachToRemoteTargets(const QList<RemoteTargetSpec>& targets);
  QString activeEngineName() const override { return QStringLiteral("gdb"); }
  bool validateLaunchEnvironment(int ranks, QString* outError = nullptr) const override;

  void writeMemory(int rankId, uint64_t address, const QByteArray& data) override {
    if (!data.isEmpty())
      queueOrWriteCmd(rankId, QStringLiteral("-data-write-memory-bytes 0x%1 \"%2\"\n").arg(address, 0, 16).arg(QString::fromLatin1(data.toHex())));
  }

  int allocatePluginMiToken() noexcept { return m_nextPluginMiToken++; }
  int requestPluginExpressionEval(int rank, const QString& expr, int* outToken = nullptr);
  void cancelPluginEvalRequest(int token) { m_pendingPluginEvals.remove(token); }
  void sendPluginMemoryRead(int rankId, int token, uint64_t address, size_t length);
  void sendPluginMemoryWrite(int rankId, int token, uint64_t address, const QByteArray& data);
  void cancelPluginMemoryRequest(int rankId, int token);
  bool isMockRank(int rankId) const;

  void setBreakpoint(const QString& file, int line, const QString& condition = QString(), int rankFilter = -1) override;
  void configureBreakpointOptions(const QString& file, int line, int ignoreCount, const QString& logMessage) override;

  static QString formatBreakCommandsMi(int bkptId, const QStringList& commands) {
    QString cmd = QStringLiteral("-break-commands %1").arg(bkptId);
    for (const QString& raw : commands) {
      const QString t = raw.trimmed();
      if (t.isEmpty() || t.contains(QLatin1Char('\n')) || t.contains(QLatin1Char('\r')) || t.contains(QChar('\0'))) continue;
      QString esc = t;
      esc.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\""));
      cmd += QStringLiteral(" \"%1\"").arg(esc);
    }
    return cmd + QLatin1Char('\n');
  }

  void setBreakpointEnabled(const QString& filePath, int line, bool enabled) override;
  void insertTemporaryBreakpoint(const QString& filePath, int line, const QString& condition = QString(), int targetRank = -1) override;
  void insertRegexBreakpoint(const QString& regexPattern) override;
  void setBreakpointCommands(const QString& filePath, int line, const QStringList& commands) override;
  QVector<BreakpointSpec> regexCreatedBreakpoints() const { return m_regexCreatedBreakpoints; }
  void clearBreakpoints() override;

  void setReverseRecordingEnabled(int rank, bool enabled) override;
  bool isReverseRecordingEnabled(int rank) const override;
  void reverseContinue(int rank) override;
  void reverseStepOver(int rank) override;
  void reverseStepInto(int rank) override;
  void reverseStepInstruction(int rank) override;

  QString getCurrentBinaryPath() const override { return m_currentBinaryPath; }
  QString lastLaunchCommandSummary() const override { return m_lastLaunchCommandSummary; }
  void insertBreakpoint(const QString& location);
  void broadcastBreakpoint(const QString& file, int line, bool isSet, const QString& condition = QString());
  void broadcastCommand(const QString& cmd);
  void terminateAllSessions();
  void requestDisassemblyFallback(int rankId);
  void flushCachedBreakpoints(int rankId);
  void writeCmd(int rankId, const QString& cmd);
  void processGdbOutput(int rankId, const QString& output);
  void initializeMockSession(int rankCount, bool simulateInitialSync = false);
  void readMemory(int rankId, const QString& address, int length) override;
  void evaluateExpression(int rankId, const QString& expression) override;
  int getProcessCount() const override { return static_cast<int>(m_processes.size()); }
  void requestStackAndThreads(int rank) override;
  void selectStackFrame(int rank, int threadId, int frameLevel) override;
  void requestVariableChildren(int rank, const QString& parentExpression) override;
  void writeVariableValue(int rank, const QString& expression, const QString& newValue) override;
  void writeRegisterValue(int rank, const QString& regName, const QString& newValue) override;
  void sendDebuggerCommand(int rank, const QString& command) override;
  void setDataWatchpoint(const QString& expression, WatchpointAccessMode mode, int rankFilter = -1) override;
  void removeDataWatchpoint(const QString& expression) override;
  void setSignalDisposition(const QString& signalName, bool stop, bool print, bool pass) override;
  void requestSignalDispositions() override;
  void insertCatchpoint(const CatchpointSpec& spec) override;
  void removeCatchpoint(int gdbId) override;
  void requestConsoleCompletions(const QString& prefix) override;
  QVector<CatchpointSpec> activeCatchpoints() const { return m_activeCatchpoints; }

  void createCheckpoint(int rank, const QString& note = QString()) override {
    const int r = qMax(0, rank);
    if (r >= static_cast<int>(m_processes.size()) || !m_processes[r]) return;
    m_pendingCheckpointNote[r] = note.trimmed(); m_pendingCheckpointCreate[r] = true; m_checkpointCreateBuffer[r].clear();
    queueOrWriteCmd(r, QStringLiteral("212-interpreter-exec console \"checkpoint\"\n"));
  }
  void restoreCheckpoint(int rank, int checkpointId) override {
    const int r = qMax(0, rank);
    if (checkpointId < 0 || r >= static_cast<int>(m_processes.size()) || !m_processes[r]) return;
    queueOrWriteCmd(r, QStringLiteral("-interpreter-exec console \"restart %1\"\n").arg(checkpointId));
    requestStackAndThreads(r);
    queueOrWriteCmd(r, QStringLiteral("201-stack-list-variables --all-values\n"));
    queueOrWriteCmd(r, QStringLiteral("-data-list-register-values x\n"));
  }
  void deleteCheckpoint(int rank, int checkpointId) override {
    const int r = qMax(0, rank);
    if (checkpointId <= 0 || r >= static_cast<int>(m_processes.size()) || !m_processes[r]) return;
    auto& list = m_rankCheckpoints[r];
    for (int i = list.size() - 1; i >= 0; --i) if (list[i].id == checkpointId) list.removeAt(i);
    queueOrWriteCmd(r, QStringLiteral("-interpreter-exec console \"delete checkpoint %1\"\n").arg(checkpointId));
    emit checkpointsReady(r, list);
  }
  void requestCheckpoints(int rank) override {
    const int r = qMax(0, rank);
    if (r >= static_cast<int>(m_processes.size()) || !m_processes[r]) { emit checkpointsReady(r, m_rankCheckpoints.value(r)); return; }
    m_pendingInfoCheckpoints[r] = true; m_infoCheckpointsBuffer[r].clear();
    queueOrWriteCmd(r, QStringLiteral("213-interpreter-exec console \"info checkpoints\"\n"));
  }
  QVector<ProcessCheckpoint> checkpointsForRank(int rank) const { return m_rankCheckpoints.value(qMax(0, rank)); }

  RankState getRankState(int rankId) const {
    if (rankId >= 0 && rankId < static_cast<int>(m_processes.size()) && m_processes[rankId]) return m_processes[rankId]->state;
    RankState st{}; st.currentState = "offline"; return st;
  }

  int activeLaneForRank(int rank) const { return m_activeLaneIds.value(rank, -1); }
  QString activeArchitectureForRank(int rank) const { return m_rankArch.value(rank); }

 signals:
  void rankStateChanged(int rankId, const RankState& state);
  void localsUpdated(int rankId, const QJsonArray& locals);
  void gdbOutputReceived(int rankId, const QString& output);
  void commandSentToGdb(int rankId, const QString& cmd);
  void hoverEvaluationComplete(const QString& varName, const QString& result, QPoint globalPos);
  void memoryDataReady(int rankId, qint64 beginAddress, const QString& hexContents);
  void pluginMemoryChunkReady(int rank, int token, const QByteArray& bytes, const QString& error);
  void pluginMemoryWriteDone(int rank, int token, const QString& error);
  void pluginExpressionReady(int rank, int token, bool ok, const QString& resultOrError);

 public slots:
  virtual void stepAll();
  virtual void continueAll();
  virtual void runAll();
  void haltAll() override;
  virtual void pauseFocusedRank(int rankId);
  void terminateSession() override;
  void stopAll(bool silentShutdown = false);
  void stepOver(int threadId) override {
    if (threadId <= 0 || m_processes.empty()) stepAll();
    else dispatchStoppedExecCmd(threadId - 1, QStringLiteral("-exec-next\n"));
  }
  void stepInto(int threadId) override;
  void stepOut(int threadId) override;
  void stepInstruction(int threadId) override;
  void nextInstruction(int rank = -1) override;
  void runToCursor(const QString& filePath, int line, int rank = -1) override;
  void runToAddress(uint64_t address, int rank = -1) override;
  void jumpToLocation(const QString& filePath, int line, int rank = -1) override;
  void jumpToAddress(uint64_t address, int rank = -1) override;
  void forceEarlyReturn(const QString& returnExpr = QString(), int rank = -1) override;
  void continueExecution(int threadId) override;
  void pauseExecution(int threadId) override { if (threadId <= 0) haltAll(); else pauseFocusedRank(threadId - 1); }
  void pauseRank(int rankId) override { if (rankId < 0) haltAll(); else pauseFocusedRank(rankId); }
  void registerWatchVariable(const QString& varName) override;
  void unregisterWatchVariable(const QString& varName) override {
    m_watchVariables.erase(std::remove(m_watchVariables.begin(), m_watchVariables.end(), varName), m_watchVariables.end());
    for (auto it = m_varNameMap.begin(); it != m_varNameMap.end(); ++it) {
      if (it.value().contains(varName)) {
        const QString vo = it.value().take(varName);
        if (!vo.isEmpty()) writeCmd(it.key(), QStringLiteral("-var-delete %1\n").arg(vo));
      }
    }
  }
  const std::vector<QString>& registeredWatchVariables() const { return m_watchVariables; }
  void sendCommand(int rankId, const QString& cmd);
  void handleGdbOutput(int rankId);
  void evaluateHoverVariable(int rankId, const QString& varName, QPoint globalPos);

 private:
  void queueOrWriteCmd(int rankId, const QString& cmd);
  void dispatchStoppedExecCmd(int rank, const QString& cmd);
  bool handleMiStackOrThreadRecord(int rankId, const QString& line);
  void handleWatchpointStopRecord(int rankId, const QString& line);

  struct PendingBreakpoint {
    QString file; int line = 0; QString condition; int rankFilter = -1; int ignoreCount = 0;
    QString logMessage; bool enabled = true; bool temporary = false; QStringList commands;
  };
  struct RankProcess {
    int id = 0; std::unique_ptr<QProcess> process; int fifoInFd = -1, fifoOutFd = -1;
    QSocketNotifier* fifoNotifier = nullptr; pid_t inferiorPid = 0; RankState state; QString buffer;
    QString lastHoverVarName; QPoint lastHoverPos; QString lastEvalExpression; QStringList pendingAfterStopCmds;
    bool autoContinueAfterPending = false, pendingAttach = false, isAttached = false, isRemoteAttach = false;
    int pendingInterruptSignals = 0; QString pendingLogpointMessage; QMap<QString, QString> pendingLogpointArgs;
  };
  struct PendingWatchpoint {
    QString expression; WatchpointAccessMode mode = WatchpointAccessMode::Write; int rankFilter = -1;
  };
  struct PendingVarChildQuery {
    int rankId = 0; QString parentExpression, rootVarObj; int inFlightLists = 0; QVariantList resolved;
  };

  bool handleBreakpointMiRecord(int rankId, const QString& line) {
    auto& rp = m_processes[rankId];
    auto parsePendingLoc = [](const QString& rawLoc, QString* outFile, int* outLine) {
      static const QRegularExpression locRe(QStringLiteral("^(.+):(\\d+)$"));
      if (const auto m = locRe.match(rawLoc.trimmed()); m.hasMatch()) { *outFile = m.captured(1); *outLine = m.captured(2).toInt(); }
    };
    if (line.startsWith("^done,bkpt={") || line.startsWith("=breakpoint-created,bkpt={") || line.startsWith("=breakpoint-modified,bkpt={")) {
      static const QRegularExpression idRe("number=\"(\\d+)\""), fileRe("(?:fullname|file)=\"([^\"]+)\""),
          lnRe("line=\"(\\d+)\""), fnRe("func=\"([^\"]+)\""), pendRe("(?:pending|original-location)=\"((?:\\\\.|[^\"])*)\"");
      if (const auto idM = idRe.match(line); idM.hasMatch()) {
        const int bkptId = idM.captured(1).toInt();
        const auto fM = fileRe.match(line), lM = lnRe.match(line);
        QString fPath = fM.hasMatch() ? fM.captured(1) : QString();
        int bkptLn = lM.hasMatch() ? lM.captured(1).toInt() : 0;
        if (fPath.isEmpty() || bkptLn <= 0) if (const auto pM = pendRe.match(line); pM.hasMatch()) parsePendingLoc(pM.captured(1), &fPath, &bkptLn);
        const bool isModified = line.startsWith("=breakpoint-modified"), isTemp = line.contains(QStringLiteral("disp=\"del\"")), isCreated = line.startsWith("=breakpoint-created");
        if (!fPath.isEmpty() && bkptLn > 0) rp->state.breakpoints[bkptId] = QString("%1:%2").arg(fPath).arg(bkptLn);
        else if (const auto fnM = fnRe.match(line); fnM.hasMatch()) rp->state.breakpoints[bkptId] = fnM.captured(1);
        if (isModified) { if (rp->state.currentState != "stopped") emit rankStateChanged(rankId, rp->state); return true; }
        if (isCreated) {
          BreakpointSpec sp; sp.file = fPath; sp.line = bkptLn; sp.temporary = isTemp; sp.gdbId = bkptId;
          sp.enabled = !line.contains(QStringLiteral("enabled=\"n\""));
          if (const auto fnM = fnRe.match(line); fnM.hasMatch()) sp.regexPattern = fnM.captured(1);
          m_regexCreatedBreakpoints.append(sp);
        }
        emit breakpointRecordCreated(rankId, fPath, bkptLn, bkptId, isTemp);
        const QString bkptBase = QFileInfo(fPath).fileName();
        for (const auto& bp : m_pendingBreakpoints) if (bkptLn > 0 && bp.line == bkptLn && QFileInfo(bp.file).fileName() == bkptBase) {
          if (bp.ignoreCount > 0) writeCmd(rankId, QString("-break-after %1 %2\n").arg(bkptId).arg(bp.ignoreCount));
          if (!bp.enabled) writeCmd(rankId, QString("-break-disable %1\n").arg(bkptId));
          if (!bp.commands.isEmpty()) writeCmd(rankId, formatBreakCommandsMi(bkptId, bp.commands));
        }
        if (rp->state.currentState == "sync_flushing") { if (--rp->state.pendingBreakpoints <= 0) { writeCmd(rankId, "-exec-continue\n"); rp->state.currentState = "running"; } }
        else if (rp->state.currentState != "stopped") emit rankStateChanged(rankId, rp->state);
      }
      return true;
    }
    if (line.startsWith("=breakpoint-deleted,id=")) {
      static const QRegularExpression delRe("id=\"(\\d+)\"");
      if (const auto dm = delRe.match(line); dm.hasMatch()) {
        const QString loc = rp->state.breakpoints.take(dm.captured(1).toInt());
        if (!loc.isEmpty()) {
          const QString f = loc.section(QLatin1Char(':'), 0, -2);
          const int ln = loc.section(QLatin1Char(':'), -1).toInt();
          for (auto it = m_pendingBreakpoints.begin(); it != m_pendingBreakpoints.end(); ++it)
            if (it->temporary && it->line == ln && QFileInfo(it->file).fileName() == QFileInfo(f).fileName()) {
              emit temporaryBreakpointHit(rankId, it->file, it->line); m_pendingBreakpoints.erase(it); break;
            }
        }
      }
      return true;
    }
    return false;
  }

  bool handleCheckpointMiRecord(int rankId, const QString& line) {
    auto unquoteConsole = [](const QString& raw) {
      QString p = raw.mid(1).trimmed();
      if (p.startsWith(QLatin1Char('"')) && p.endsWith(QLatin1Char('"')) && p.size() >= 2) p = p.mid(1, p.size() - 2);
      return p.replace(QStringLiteral("\\n"), QStringLiteral("\n")).replace(QStringLiteral("\\t"), QStringLiteral("\t")).replace(QStringLiteral("\\\""), QStringLiteral("\"")).replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
    };
    auto upsertCheckpoint = [&](int r, const ProcessCheckpoint& entry) {
      auto& list = m_rankCheckpoints[r];
      for (auto& existing : list) if (existing.id == entry.id) {
        existing.forkPid = entry.forkPid;
        if (!entry.location.isEmpty()) existing.location = entry.location;
        if (!entry.note.isEmpty()) existing.note = entry.note;
        if (existing.timestamp.isEmpty()) existing.timestamp = entry.timestamp;
        return;
      }
      list.append(entry);
    };
    if (m_pendingCheckpointCreate.value(rankId, false) && (line.startsWith(QLatin1Char('~')) || line.startsWith(QLatin1Char('&')))) {
      m_checkpointCreateBuffer[rankId] += unquoteConsole(line); return true;
    }
    if (m_pendingCheckpointCreate.value(rankId, false) && (line.startsWith(QLatin1String("212^done")) || line.startsWith(QLatin1String("212^error")) || line == QLatin1String("^done"))) {
      m_pendingCheckpointCreate[rankId] = false;
      const QString buf = m_checkpointCreateBuffer.take(rankId), note = m_pendingCheckpointNote.take(rankId);
      if (!line.contains(QLatin1String("^error"))) {
        static const QRegularExpression cpRe(QStringLiteral("checkpoint\\s+(\\d+)\\s*:\\s*fork returned pid\\s+(\\d+)"), QRegularExpression::CaseInsensitiveOption);
        if (const auto m = cpRe.match(buf); m.hasMatch()) {
          ProcessCheckpoint cp{m.captured(1).toInt(), rankId, m.captured(2).toLongLong(), QString(), QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")), note};
          if (rankId >= 0 && rankId < static_cast<int>(m_processes.size()) && m_processes[rankId]) {
            const auto& st = m_processes[rankId]->state;
            const QString fn = st.currentFunction.isEmpty() ? QStringLiteral("main") : st.currentFunction;
            cp.location = (!st.currentFile.isEmpty() && st.currentLine > 0) ? QStringLiteral("%1 (%2:%3)").arg(fn, QFileInfo(st.currentFile).fileName()).arg(st.currentLine) : fn;
          }
          upsertCheckpoint(rankId, cp); emit checkpointsReady(rankId, m_rankCheckpoints.value(rankId)); requestCheckpoints(rankId);
        }
      }
      return true;
    }
    if (m_pendingInfoCheckpoints.value(rankId, false) && (line.startsWith(QLatin1Char('~')) || line.startsWith(QLatin1Char('&')))) {
      m_infoCheckpointsBuffer[rankId] += unquoteConsole(line); return true;
    }
    if (line.startsWith(QLatin1String("213^done")) || line.startsWith(QLatin1String("213^error"))) {
      m_pendingInfoCheckpoints[rankId] = false;
      const QString buf = m_infoCheckpointsBuffer.take(rankId);
      if (line.startsWith(QLatin1String("213^done"))) {
        QMap<int, ProcessCheckpoint> prevById;
        for (const auto& existing : m_rankCheckpoints.value(rankId)) prevById.insert(existing.id, existing);
        static const QRegularExpression rowRe(QStringLiteral("^\\s*\\*?\\s*(\\d+)\\s+process\\s+(\\d+)\\s*(.*)$"), QRegularExpression::CaseInsensitiveOption);
        QVector<ProcessCheckpoint> parsed;
        for (const QString& rawLn : buf.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
          if (const auto m = rowRe.match(rawLn); m.hasMatch()) {
            const int cid = m.captured(1).toInt();
            const ProcessCheckpoint prev = prevById.value(cid);
            QString loc = m.captured(3).trimmed();
            if (loc.startsWith(QStringLiteral("at "), Qt::CaseInsensitive)) loc = loc.mid(3).trimmed();
            parsed.append(ProcessCheckpoint{cid, rankId, m.captured(2).toLongLong(), loc,
                                            prev.timestamp.isEmpty() ? QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")) : prev.timestamp, prev.note});
          }
        }
        if (!parsed.isEmpty()) m_rankCheckpoints[rankId] = parsed;
        emit checkpointsReady(rankId, m_rankCheckpoints.value(rankId));
      }
      return true;
    }
    return false;
  }

  std::unique_ptr<QProcess> m_mpirunProcess;
  std::vector<std::unique_ptr<RankProcess>> m_processes;
  std::vector<PendingBreakpoint> m_pendingBreakpoints;
  std::vector<PendingWatchpoint> m_pendingWatchpoints;
  QString m_fifoTempDir, m_currentBinaryPath, m_lastLaunchCommandSummary;
  QMap<int, QMap<QString, QString>> m_varNameMap, m_varNameRevMap;
  QMap<int, qint64> m_rankPids;
  QMap<int, QVector<StackFrameEntry>> m_rankFrames;
  QMap<int, QVector<ThreadEntry>> m_rankThreads;
  QMap<int, int> m_activeThreadIds, m_pendingFrameSelect, m_pendingConsoleCmdCount, m_activeLaneIds;
  QMap<int, QString> m_consoleReplyBuffer, m_infoHandleBuffer, m_pendingCompletePrefix, m_completeBuffer, m_rankArch;
  QMap<int, QString> m_checkpointCreateBuffer, m_pendingCheckpointNote, m_infoCheckpointsBuffer;
  QMap<int, bool> m_pendingInfoHandle, m_pendingCheckpointCreate, m_pendingInfoCheckpoints;
  QMap<int, QMap<QString, int>> m_watchpointIds;
  QMap<int, PendingVarChildQuery> m_varChildQueries;
  QMap<int, int> m_tokenToVarCreateQuery, m_tokenToVarListQuery;
  QMap<int, QList<int>> m_pendingPluginMemReads, m_pendingPluginMemWrites;
  QSet<int> m_pendingPluginEvals;
  int m_nextVarQueryId = 1, m_nextVarQueryToken = 4000, m_nextPluginMiToken = 500000, m_nextCatchpointId = 1;
  std::vector<QString> m_watchVariables;
  bool m_breakpointsCleared = false, m_shuttingDown = false;
  QMap<QString, SignalDisposition> m_signalDispositions;
  QVector<CatchpointSpec> m_activeCatchpoints;
  QStringList m_pendingRegexBreakpoints;
  QVector<BreakpointSpec> m_regexCreatedBreakpoints;
  QMap<int, QVector<ProcessCheckpoint>> m_rankCheckpoints;
  QMap<QString, QHash<int, QString>> m_archRegisterNames;
};

inline void GdbRankCoordinator::startDebugSession(int rankCount, const QString& executable) {
  launchParallelSession(executable, rankCount);
}
inline void GdbRankCoordinator::terminateSession() { stopAll(m_shuttingDown); }
inline void GdbRankCoordinator::terminateAllSessions() { stopAll(m_shuttingDown); }
inline qint64 GdbRankCoordinator::getPidForRank(int rank) const {
  if (m_rankPids.contains(rank) && m_rankPids.value(rank) > 0) return m_rankPids.value(rank);
  if (rank >= 0 && rank < static_cast<int>(m_processes.size()) && m_processes[rank] && m_processes[rank]->inferiorPid > 0)
    return static_cast<qint64>(m_processes[rank]->inferiorPid);
  return 0;
}
inline bool GdbRankCoordinator::isMockRank(int rankId) const {
  return rankId >= 0 && rankId < static_cast<int>(m_processes.size()) && m_processes[rankId] &&
         m_processes[rankId]->fifoInFd < 0 && !m_processes[rankId]->process;
}
inline void GdbRankCoordinator::sendPluginMemoryRead(int rankId, int token, uint64_t address, size_t length) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size()) || !m_processes[rankId] || length == 0u) return;
  if (m_processes[rankId]->state.currentState != QLatin1String("stopped")) return;
  m_pendingPluginMemReads[rankId].append(token);
  writeCmd(rankId, QStringLiteral("%1-data-read-memory-bytes 0x%2 %3\n").arg(token).arg(address, 0, 16).arg(length));
}
inline void GdbRankCoordinator::sendPluginMemoryWrite(int rankId, int token, uint64_t address, const QByteArray& data) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size()) || !m_processes[rankId] || data.isEmpty()) return;
  if (m_processes[rankId]->state.currentState != QLatin1String("stopped")) return;
  m_pendingPluginMemWrites[rankId].append(token);
  writeCmd(rankId, QStringLiteral("%1-data-write-memory-bytes 0x%2 \"%3\"\n").arg(token).arg(address, 0, 16).arg(QString::fromLatin1(data.toHex())));
}
inline void GdbRankCoordinator::cancelPluginMemoryRequest(int rankId, int token) {
  if (m_pendingPluginMemReads.contains(rankId)) m_pendingPluginMemReads[rankId].removeOne(token);
  if (m_pendingPluginMemWrites.contains(rankId)) m_pendingPluginMemWrites[rankId].removeOne(token);
}
inline void GdbRankCoordinator::initializeMockSession(int rankCount, bool simulateInitialSync) {
  terminateAllSessions(); m_watchVariables.clear();
  for (int i = 0; i < rankCount; ++i) {
    auto rp = std::make_unique<RankProcess>();
    rp->id = i; rp->state.rankId = i; rp->state.currentState = "running"; rp->state.currentLine = 0;
    if (!simulateInitialSync) rp->state.lastFiredTimestamp = "mocked";
    m_processes.push_back(std::move(rp));
  }
}

}  // namespace gridlock

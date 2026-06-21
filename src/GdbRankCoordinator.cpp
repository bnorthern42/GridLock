#include "GdbRankCoordinator.hpp"
#include "core/ConfigManager.hpp"
#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>

namespace gridlock {

GdbRankCoordinator::GdbRankCoordinator(QObject *parent) : QObject(parent) {
  m_watchVariables.push_back("calc");
  m_watchVariables.push_back("rank");
}

GdbRankCoordinator::~GdbRankCoordinator() { terminateAllSessions(); }

void GdbRankCoordinator::writeCmd(int rankId, const QString &cmd) {
  if (rankId >= 0 && rankId < static_cast<int>(m_processes.size())) {
    auto &rp = m_processes[rankId];
    if (rp && rp->process && rp->process->state() != QProcess::NotRunning) {
      rp->process->write(cmd.toUtf8());
      emit commandSentToGdb(rankId, cmd.trimmed());
    }
  }
}

void GdbRankCoordinator::flushCachedBreakpoints(int rankId) {
  auto bps = core::ConfigManager::instance().getBreakpoints();
  auto &rp = m_processes[rankId];
  int count = 0;
  for (auto it = bps.constBegin(); it != bps.constEnd(); ++it) {
    QString fileName = QFileInfo(it.key()).fileName();
    for (int line : it.value()) {
      QString cmd = QString("-break-insert -f %1:%2\n").arg(fileName).arg(line);
      writeCmd(rankId, cmd);
      count++;
    }
  }

  if (count == 0) {
    writeCmd(rankId, "-exec-continue\n");
    rp->state.currentState = "running";
  } else {
    rp->state.pendingBreakpoints = count;
    rp->state.currentState = "sync_flushing";
  }
}

void GdbRankCoordinator::registerWatchVariable(const QString &name) {
  if (std::find(m_watchVariables.begin(), m_watchVariables.end(), name) ==
      m_watchVariables.end()) {
    m_watchVariables.push_back(name);
  }

  int varIdx = std::distance(
      m_watchVariables.begin(),
      std::find(m_watchVariables.begin(), m_watchVariables.end(), name));

  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto &rp = m_processes[i];
    if (rp && rp->process && rp->process->state() == QProcess::Running) {
      if (!m_varNameMap[i].contains(name)) {
        QString evalCmd =
            QString("30%1-var-create - * %2\n").arg(varIdx).arg(name);
        writeCmd(i, evalCmd);
      }
    }
  }
}

void GdbRankCoordinator::terminateAllSessions() {
  for (size_t i = 0; i < m_processes.size(); ++i) {
    auto &rp = m_processes[i];
    if (rp && rp->process) {
      if (rp->process->state() == QProcess::Running) {
        rp->process->write("-gdb-exit\n");
        rp->process->waitForFinished(500);
        if (rp->process->state() == QProcess::Running) {
          rp->process->kill();
          rp->process->waitForFinished();
        }
      }
    }
  }
  m_processes.clear();

  if (m_mpirunProcess) {
    if (m_mpirunProcess->state() == QProcess::Running) {
      m_mpirunProcess->kill();
      m_mpirunProcess->waitForFinished();
    }
    m_mpirunProcess.reset();
  }
}

void GdbRankCoordinator::startDebugSession(int rankCount,
                                           const QString &executable) {
  launchParallelSession(executable, rankCount);
}

void GdbRankCoordinator::launchParallelSession(const QString &executable,
                                               int explicitRankCount) {
  QSettings settings("GridLock", "Debugger");
  QString mpiExec = settings.value("mpi_executable", "mpiexec").toString();
  int rankCount =
      settings
          .value("rank_count", explicitRankCount > 0 ? explicitRankCount : 4)
          .toInt();

  // 1. Terminate any previous stragglers
  terminateAllSessions();

  // 2. Launch the underlying MPI context fabric via gdbserver
  m_mpirunProcess = std::make_unique<QProcess>();
  QStringList mpiArgs;

  for (int i = 0; i < rankCount; ++i) {
    if (i > 0)
      mpiArgs << ":";
    mpiArgs << "-np" << "1"
            << "gdbserver" << QString("localhost:%1").arg(2000 + i)
            << executable;
  }
  
  connect(m_mpirunProcess.get(), &QProcess::readyReadStandardOutput, this, [this]() {
      emit targetOutputReceived(QString::fromUtf8(m_mpirunProcess->readAllStandardOutput()));
  });
  connect(m_mpirunProcess.get(), &QProcess::readyReadStandardError, this, [this]() {
      emit targetOutputReceived(QString::fromUtf8(m_mpirunProcess->readAllStandardError()));
  });

  m_mpirunProcess->start(mpiExec, mpiArgs);

  // 3. Launch isolated GDB frontends to connect via TCP
  for (int i = 0; i < rankCount; ++i) {
    auto rp = std::make_unique<RankProcess>();
    rp->id = i;
    rp->process = std::make_unique<QProcess>();
    rp->state.rankId = i;
    rp->state.currentState = "running";
    rp->state.currentLine = 0;

    connect(rp->process.get(), &QProcess::readyReadStandardOutput, this,
            [this, id = i]() { handleGdbOutput(id); });

    QStringList args;
    args << core::ConfigManager::instance().getGdbPath() << "--interpreter=mi3";
    rp->process->start(args[0], args.mid(1));

    m_processes.push_back(std::move(rp));

    // Delay the connection to give mpiexec and gdbserver time to bind to ports
    QTimer::singleShot(500, this, [this, id = i]() {
        // OpenMPI relies heavily on internal socket pipelines that can occasionally 
        // raise SIGPIPE. GDB intercepts this and halts. We must suppress it.
        writeCmd(id, "-interpreter-exec console \"handle SIGPIPE nostop noprint pass\"\n");

        QString connectCmd =
            QString("-target-select remote localhost:%1\n").arg(2000 + id);
        writeCmd(id, connectCmd);
    });
  }
}

void GdbRankCoordinator::insertBreakpoint(const QString &location) {
  QString fileName = QFileInfo(location).fileName();
  if (fileName.isEmpty())
    fileName = location;
  QString cmd = QString("-break-insert -f %1\n").arg(fileName);
  for (size_t i = 0; i < m_processes.size(); ++i)
    writeCmd(i, cmd);
}

void GdbRankCoordinator::broadcastCommand(const QString &cmd) {
  for (size_t i = 0; i < m_processes.size(); ++i)
    writeCmd(i, cmd);
}

void GdbRankCoordinator::sendCommand(int rankId, const QString &cmd) {
  if (rankId == -1) {
    broadcastCommand(cmd + "\n");
  } else {
    writeCmd(rankId, cmd + "\n");
  }
}

void GdbRankCoordinator::broadcastBreakpoint(const QString &file, int line,
                                             bool isAdded) {
  QString fileName = QFileInfo(file).fileName();
  QString locStr = QString("%1:%2").arg(fileName).arg(line);

  for (size_t i = 0; i < m_processes.size(); ++i) {
    if (isAdded) {
      QString breakCmd = QString("-break-insert -f %1\n").arg(locStr);
      writeCmd(i, breakCmd);
    } else {
      auto &rp = m_processes[i];
      int bkptIdToDelete = -1;
      for (auto it = rp->state.breakpoints.constBegin();
           it != rp->state.breakpoints.constEnd(); ++it) {
        if (it.value() == locStr) {
          bkptIdToDelete = it.key();
          break;
        }
      }
      if (bkptIdToDelete != -1) {
        writeCmd(i, QString("-break-delete %1\n").arg(bkptIdToDelete));
        rp->state.breakpoints.remove(bkptIdToDelete);
      }
    }
  }
}

void GdbRankCoordinator::stepAll() {
  for (size_t i = 0; i < m_processes.size(); ++i)
    writeCmd(i, "-exec-next\n");
}

void GdbRankCoordinator::continueAll() {
  for (size_t i = 0; i < m_processes.size(); ++i)
    writeCmd(i, "-exec-continue\n");
}

void GdbRankCoordinator::runAll() {
  for (size_t i = 0; i < m_processes.size(); ++i)
    writeCmd(i, "-exec-run\n");
}

void GdbRankCoordinator::haltAll() {
  for (size_t i = 0; i < m_processes.size(); ++i)
    writeCmd(i, "-exec-interrupt\n");
}

void GdbRankCoordinator::pauseFocusedRank(int rankId) {
  writeCmd(rankId, "-exec-interrupt\n");
}

void GdbRankCoordinator::requestDisassemblyFallback(int rankId) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size()))
    return;
  auto &rp = m_processes[rankId];
  if (rp && rp->process && rp->process->state() == QProcess::Running) {
    if (rp->state.currentState != "stopped")
      return;
    qDebug() << "[GDB IN Rank" << rankId << "]: Sending Disassembly Command";
    writeCmd(rankId, "-thread-info\n");
    if (!rp->state.currentFile.isEmpty() && rp->state.currentLine > 0) {
      QString disasmCmd =
          QString("200-data-disassemble -f %1 -l %2 -n 30 -- 0\n")
              .arg(rp->state.currentFile)
              .arg(rp->state.currentLine);
      writeCmd(rankId, disasmCmd);
    } else {
      writeCmd(rankId, "200-data-disassemble -s $pc -e \"$pc + 40\" -- 0\n");
    }
  }
}

void GdbRankCoordinator::handleGdbOutput(int rankId) {
  if (rankId < 0 || rankId >= static_cast<int>(m_processes.size()))
    return;
  auto &rp = m_processes[rankId];
  if (!rp->process)
    return;

  rp->buffer += QString::fromUtf8(rp->process->readAllStandardOutput());

  int newlineIdx = -1;
  while ((newlineIdx = rp->buffer.indexOf('\n')) != -1) {
    QString line = rp->buffer.left(newlineIdx).trimmed();
    rp->buffer.remove(0, newlineIdx + 1);
    std::string_view sv(line.toUtf8().constData(), line.toUtf8().length());

    emit gdbOutputReceived(rankId, line);

    qDebug() << "[GDB OUT Rank" << rankId << "]:" << line;

    if (sv.starts_with("*stopped")) {
      // Immediately catch and cleanly sink exit notifications to prevent Regex
      // parsing errors
      if (line.contains("reason=\"exited") ||
          line.contains("reason=\"exited-normally\"")) {
        rp->state.currentState = "exited";
        emit rankStateChanged(rankId, rp->state);
        continue;
      }

      rp->state.currentState = "stopped";
      if (rp->state.executionTimer.isValid()) {
        rp->state.totalRuntimeMs += rp->state.executionTimer.elapsed();
        rp->state.executionTimer.invalidate();
      }

      QRegularExpression rxFile("fullname=\"([^\"]+)\"");
      QRegularExpression rxLine("frame=\\{[^}]*line=\"(\\d+)\"");
      QString file = rp->state.currentFile;
      int lineNum = rp->state.currentLine;

      auto matchFile = rxFile.match(line);
      if (matchFile.hasMatch())
        file = matchFile.captured(1);

      auto matchLine = rxLine.match(line);
      if (matchLine.hasMatch()) {
        lineNum = matchLine.captured(1).toInt();
      } else {
        if (lineNum <= 0)
          lineNum = -1;
      }

      rp->state.currentFile = file;
      rp->state.currentLine = lineNum;

      QRegularExpression rxAddr("frame=\\{[^}]*addr=\"([^\"]+)\"");
      auto matchAddr = rxAddr.match(line);
      QString stopAddress =
          matchAddr.hasMatch() ? matchAddr.captured(1) : "$pc";

      QString asmCmd =
          QString("200-data-disassemble -s %1 -e \"%1 + 40\" -- 0\n").arg(stopAddress);
      writeCmd(rankId, asmCmd);

      QRegularExpression threadRe("thread-id=\"(\\d+)\"");
      auto matchThread = threadRe.match(line);
      if (matchThread.hasMatch()) {
        QString threadId = matchThread.captured(1);
        QString threadCmd = QString("-thread-select %1\n").arg(threadId);
        writeCmd(rankId, threadCmd);
      } else {
        writeCmd(rankId, "-thread-info\n");
      }

      bool isInternalSync = !rp->state.breakpoints.contains(-1);
      if (rp->state.lastFiredTimestamp.isEmpty()) {
        rp->state.lastFiredTimestamp = "synced";
        isInternalSync = true;
      } else {
        isInternalSync = false;
      }

      if (isInternalSync) {
        flushCachedBreakpoints(rankId);
      } else {
        emit rankStateChanged(rankId, rp->state);
      }
    } else if (sv.starts_with("*running")) {
      rp->state.currentState = "running";
      rp->state.executionTimer.start();
      emit rankStateChanged(rankId, rp->state);
    } else if (sv.starts_with("30")) {
      if (line.contains("^done")) {
        QRegularExpression evalRe(
            "30(\\d+)\\^done,name=\"([^\"]+)\".*?value=\"([^\"]+)\"");
        auto match = evalRe.match(line);
        if (match.hasMatch()) {
          int varIdx = match.captured(1).toInt();
          if (varIdx >= 0 &&
              varIdx < static_cast<int>(m_watchVariables.size())) {
            QString varName = m_watchVariables[varIdx];
            QString varId = match.captured(2);
            QString value = match.captured(3);
            m_varNameMap[rankId][varName] = varId;
            m_varNameRevMap[rankId][varId] = varName;
            rp->state.variableWatches[varName] = value;
            emit rankStateChanged(rankId, rp->state);
          }
        }
      } else if (line.contains("^error")) {
        QRegularExpression errRe("30(\\d+)\\^error");
        auto match = errRe.match(line);
        if (match.hasMatch()) {
          int varIdx = match.captured(1).toInt();
          if (varIdx >= 0 &&
              varIdx < static_cast<int>(m_watchVariables.size())) {
            rp->state.variableWatches[m_watchVariables[varIdx]] = "N/A";
            emit rankStateChanged(rankId, rp->state);
          }
        }
      }
    } else if (line.contains("^done,changelist=")) {
      QRegularExpression changeRe("name=\"([^\"]+)\".*?value=\"([^\"]+)\"");
      auto i = changeRe.globalMatch(line);
      bool updated = false;
      while (i.hasNext()) {
        auto match = i.next();
        QString varId = match.captured(1);
        QString value = match.captured(2);
        if (m_varNameRevMap.contains(rankId) &&
            m_varNameRevMap[rankId].contains(varId)) {
          QString varName = m_varNameRevMap[rankId][varId];
          rp->state.variableWatches[varName] = value;
          updated = true;
        }
      }
      if (updated)
        emit rankStateChanged(rankId, rp->state);
    } else if (line.startsWith("^done,bkpt={")) {
      QRegularExpression bkptRe(
          "number=\"(\\d+)\".*?file=\"([^\"]+)\".*?line=\"(\\d+)\"");
      auto match = bkptRe.match(line);
      if (match.hasMatch()) {
        int bkptId = match.captured(1).toInt();
        QString file = match.captured(2);
        QString lineNum = match.captured(3);
        rp->state.breakpoints[bkptId] = QString("%1:%2").arg(file).arg(lineNum);

        if (rp->state.currentState == "sync_flushing") {
          rp->state.pendingBreakpoints--;
          if (rp->state.pendingBreakpoints <= 0) {
            writeCmd(rankId, "-exec-continue\n");
            rp->state.currentState = "running";
          }
        } else {
          emit rankStateChanged(rankId, rp->state);
        }
      }
    } else if (sv.starts_with("200")) {
      if (line.contains("asm_insns=")) {
        QString prettyAsm;
        QRegularExpression instRe("address=\"([^\"]+)\".*?inst=\"([^\"]+)\"");
        auto i = instRe.globalMatch(line);
        while (i.hasNext()) {
          auto match = i.next();
          prettyAsm +=
              QString("%1: %2\n").arg(match.captured(1)).arg(match.captured(2));
        }
        if (!prettyAsm.isEmpty()) {
          rp->state.disassemblyText = prettyAsm;
          emit rankStateChanged(rankId, rp->state);
        }
      } else if (line.contains("^error")) {
        QRegularExpression errorRe("msg=\"([^\"]+)\"");
        auto match = errorRe.match(line);
        QString errMsg = match.hasMatch() ? match.captured(1) : "Unknown Error";

        rp->state.disassemblyText = QString("; GDB Error: %1").arg(errMsg);
        emit rankStateChanged(rankId, rp->state);
      }

      // Chain variable evaluation after disassembly finishes
      writeCmd(rankId, "-var-update 1 *\n");
    } else if (line.contains("^error") && !sv.starts_with("30") &&
               !sv.starts_with("40") && !sv.starts_with("200")) {
      QRegularExpression errorRe("msg=\"([^\"]+)\"");
      auto match = errorRe.match(line);
      QString errMsg = match.hasMatch() ? match.captured(1) : "Unknown Error";

      rp->state.disassemblyText = QString("; GDB Error: %1").arg(errMsg);
      emit rankStateChanged(rankId, rp->state);
    } else if (line.contains("asm_insns=") && !sv.starts_with("200")) {
      QString prettyAsm;
      QRegularExpression instRe("address=\"([^\"]+)\".*?inst=\"([^\"]+)\"");
      auto i = instRe.globalMatch(line);
      while (i.hasNext()) {
        auto match = i.next();
        prettyAsm +=
            QString("%1: %2\n").arg(match.captured(1)).arg(match.captured(2));
      }
      if (!prettyAsm.isEmpty()) {
        rp->state.disassemblyText = prettyAsm;
        emit rankStateChanged(rankId, rp->state);
      }
    }
  }
}

} // namespace gridlock
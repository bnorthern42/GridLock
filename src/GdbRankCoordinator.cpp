#include "GdbRankCoordinator.hpp"
#include <QDebug>
#include <QRegularExpression>
#include <QSettings>
#include <iostream>
#include "core/ConfigManager.hpp"

namespace gridlock {

GdbRankCoordinator::GdbRankCoordinator(QObject *parent)
    : QObject(parent) {
    m_watchVariables.push_back("calc");
    m_watchVariables.push_back("rank");
}

GdbRankCoordinator::~GdbRankCoordinator() {
    terminateAllSessions();
}

void GdbRankCoordinator::registerWatchVariable(const QString& name) {
    if (std::find(m_watchVariables.begin(), m_watchVariables.end(), name) == m_watchVariables.end()) {
        m_watchVariables.push_back(name);
    }
    
    int varIdx = std::distance(m_watchVariables.begin(), std::find(m_watchVariables.begin(), m_watchVariables.end(), name));
    
    for (size_t i = 0; i < m_processes.size(); ++i) {
        auto& rp = m_processes[i];
        if (rp && rp->process && rp->process->state() == QProcess::Running) {
            if (!m_varNameMap[i].contains(name)) {
                QString evalCmd = QString("30%1-var-create - * %2\n").arg(varIdx).arg(name);
                rp->process->write(evalCmd.toUtf8());
            }
        }
    }
}

void GdbRankCoordinator::terminateAllSessions() {
    for (auto& rp : m_processes) {
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
}

void GdbRankCoordinator::startDebugSession(int rankCount, const QString& executable) {
    launchParallelSession(executable, rankCount);
}

void GdbRankCoordinator::launchParallelSession(const QString& executable, int explicitRankCount) {
    QSettings settings("GridLock", "Debugger");
    QString mpiExec = settings.value("mpi_executable", "mpiexec").toString();
    int rankCount = settings.value("rank_count", explicitRankCount > 0 ? explicitRankCount : 4).toInt();
    QString extraArgsStr = settings.value("extra_args", "--oversubscribe").toString();
    QStringList extraArgs = extraArgsStr.split(" ", Qt::SkipEmptyParts);

    for (int i = 0; i < rankCount; ++i) {
        auto rp = std::make_unique<RankProcess>();
        rp->id = i;
        rp->process = std::make_unique<QProcess>();
        rp->state.rankId = i;
        rp->state.currentState = "running";
        rp->state.currentLine = 0;
        
        connect(rp->process.get(), &QProcess::readyReadStandardOutput, this, [this, id = i]() {
            handleGdbOutput(id);
        });

        // Construct the parallel job. Since we are running independent GDB instances
        // attached to each rank conceptually, we use mpiexec to run one process
        // or just invoke mpiexec directly for that rank.
        QStringList args;
        args << "-n" << "1" << extraArgs << core::ConfigManager::instance().getGdbPath() << "--interpreter=mi3" << "--args" << executable;
        rp->process->start(mpiExec, args);
        
        m_processes.push_back(std::move(rp));
    }
}

void GdbRankCoordinator::insertBreakpoint(const QString& location) {
    QString cmd = QString("-break-insert %1\n").arg(location);
    for (auto& rp : m_processes) {
        if (rp->process && rp->process->state() == QProcess::Running) {
            rp->process->write(cmd.toUtf8());
        }
    }
}

void GdbRankCoordinator::broadcastCommand(const QString& cmd) {
    for (auto& rp : m_processes) {
        if (rp->process && rp->process->state() == QProcess::Running) {
            rp->process->write(cmd.toUtf8());
        }
    }
}

void GdbRankCoordinator::broadcastBreakpoint(const QString& file, int line) {
    QString breakCmd = QString("-break-insert -f %1:%2\n").arg(file).arg(line);
    for (auto& rp : m_processes) {
        if (rp->process && rp->process->state() == QProcess::Running) {
            rp->process->write(breakCmd.toUtf8());
        }
    }
}

void GdbRankCoordinator::stepAll() {
    for (auto& rp : m_processes) {
        if (rp->process && rp->process->state() == QProcess::Running) {
            rp->process->write("-exec-next\n");
        }
    }
}

void GdbRankCoordinator::continueAll() {
    for (auto& rp : m_processes) {
        if (rp->process && rp->process->state() == QProcess::Running) {
            rp->process->write("-exec-continue\n");
        }
    }
}

void GdbRankCoordinator::runAll() {
    for (auto& rp : m_processes) {
        if (rp->process && rp->process->state() == QProcess::Running) {
            rp->process->write("-exec-run\n");
        }
    }
}

void GdbRankCoordinator::haltAll() {
    for (auto& rp : m_processes) {
        if (rp->process && rp->process->state() == QProcess::Running) {
            rp->process->write("-exec-interrupt\n");
        }
    }
}

void GdbRankCoordinator::pauseFocusedRank(int rankId) {
    if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
    auto& rp = m_processes[rankId];
    if (rp && rp->process && rp->process->state() == QProcess::Running) {
        rp->process->write("-exec-interrupt\n");
    }
}

void GdbRankCoordinator::requestDisassemblyFallback(int rankId) {
    if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
    auto& rp = m_processes[rankId];
    if (rp && rp->process && rp->process->state() == QProcess::Running) {
        if (rp->state.currentState != "stopped") return;
        qDebug() << "[GDB IN Rank" << rankId << "]: Sending Disassembly Command";
        rp->process->write("-thread-info\n");
        if (!rp->state.currentFile.isEmpty() && rp->state.currentLine > 0) {
            QString disasmCmd = QString("200-data-disassemble -f %1 -l %2 -n 30 -- 0\n").arg(rp->state.currentFile).arg(rp->state.currentLine);
            rp->process->write(disasmCmd.toUtf8());
        } else {
            rp->process->write("200-data-disassemble -s $pc -e \"$pc + 40\" -- 0\n");
        }
    }
}

void GdbRankCoordinator::handleGdbOutput(int rankId) {
    if (rankId < 0 || rankId >= static_cast<int>(m_processes.size())) return;
    auto& rp = m_processes[rankId];
    if (!rp->process) return;

    rp->buffer += QString::fromUtf8(rp->process->readAllStandardOutput());
    
    int newlineIdx = -1;
    while ((newlineIdx = rp->buffer.indexOf('\n')) != -1) {
        QString line = rp->buffer.left(newlineIdx).trimmed();
        rp->buffer.remove(0, newlineIdx + 1);
        std::string_view sv(line.toUtf8().constData(), line.toUtf8().length());

        qDebug() << "[GDB OUT Rank" << rankId << "]:" << line;

        if (sv.starts_with("*stopped") && !line.contains("reason=\"exited\"")) {
            rp->state.currentState = "stopped";
            if (rp->state.executionTimer.isValid()) {
                rp->state.totalRuntimeMs += rp->state.executionTimer.elapsed();
                rp->state.executionTimer.invalidate();
            }

            QRegularExpression rxFile("fullname=\"([^\"]+)\"");
            QRegularExpression rxLine("frame=\\{[^}]*line=\"(\\d+)\"");
            QString file = rp->state.currentFile; // fallback to known file
            int lineNum = rp->state.currentLine; // fallback to known line

            auto matchFile = rxFile.match(line);
            if (matchFile.hasMatch()) file = matchFile.captured(1);

            auto matchLine = rxLine.match(line);
            if (matchLine.hasMatch()) {
                lineNum = matchLine.captured(1).toInt();
            } else {
                qDebug() << "GDB PARSE WARNING: Could not find line token in stop event record:" << line;
                if (lineNum <= 0) lineNum = -1; // Do not default to 1 blindly
            }

            rp->state.currentFile = file;
            rp->state.currentLine = lineNum;

            QRegularExpression rxAddr("frame=\\{[^}]*addr=\"([^\"]+)\"");
            auto matchAddr = rxAddr.match(line);
            QString stopAddress = matchAddr.hasMatch() ? matchAddr.captured(1) : "$pc";

            // Construct disassembly command for the exact stop address
            QString asmCmd = QString("200-data-disassemble -a %1 -- 0\n").arg(stopAddress);
            rp->process->write(asmCmd.toUtf8());

            QRegularExpression threadRe("thread-id=\"(\\d+)\"");
            auto matchThread = threadRe.match(line);
            if (matchThread.hasMatch()) {
                QString threadId = matchThread.captured(1);
                QString threadCmd = QString("-thread-select %1\n").arg(threadId);
                rp->process->write(threadCmd.toUtf8());
            } else {
                rp->process->write("-thread-info\n");
            }

            emit rankStateChanged(rankId, rp->state);
        } else if (sv.starts_with("*running")) {
            rp->state.currentState = "running";
            rp->state.executionTimer.start();
            emit rankStateChanged(rankId, rp->state);
        } else if (sv.starts_with("30")) { 
            if (line.contains("^done")) {
                QRegularExpression evalRe("30(\\d+)\\^done,name=\"([^\"]+)\".*?value=\"([^\"]+)\"");
                auto match = evalRe.match(line);
                if (match.hasMatch()) {
                    int varIdx = match.captured(1).toInt();
                    if (varIdx >= 0 && varIdx < static_cast<int>(m_watchVariables.size())) {
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
                    if (varIdx >= 0 && varIdx < static_cast<int>(m_watchVariables.size())) {
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
                if (m_varNameRevMap.contains(rankId) && m_varNameRevMap[rankId].contains(varId)) {
                    QString varName = m_varNameRevMap[rankId][varId];
                    rp->state.variableWatches[varName] = value;
                    updated = true;
                }
            }
            if (updated) emit rankStateChanged(rankId, rp->state);
        } else if (line.startsWith("^done,bkpt={")) {
            QRegularExpression bkptRe("number=\"(\\d+)\".*?file=\"([^\"]+)\".*?line=\"(\\d+)\"");
            auto match = bkptRe.match(line);
            if (match.hasMatch()) {
                int bkptId = match.captured(1).toInt();
                QString file = match.captured(2);
                QString lineNum = match.captured(3);
                rp->state.breakpoints[bkptId] = QString("%1:%2").arg(file).arg(lineNum);
                emit rankStateChanged(rankId, rp->state);
            }
        } else if (sv.starts_with("200")) {
            if (line.contains("asm_insns=")) {
                QString prettyAsm;
                QRegularExpression instRe("address=\"([^\"]+)\".*?inst=\"([^\"]+)\"");
                auto i = instRe.globalMatch(line);
                while (i.hasNext()) {
                    auto match = i.next();
                    prettyAsm += QString("%1: %2\n")
                        .arg(match.captured(1))
                        .arg(match.captured(2));
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
            rp->process->write("-var-update 1 *\n");
        } else if (line.contains("^error") && !sv.starts_with("30") && !sv.starts_with("40") && !sv.starts_with("200")) {
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
                prettyAsm += QString("%1: %2\n")
                    .arg(match.captured(1))
                    .arg(match.captured(2));
            }
            if (!prettyAsm.isEmpty()) {
                rp->state.disassemblyText = prettyAsm;
                emit rankStateChanged(rankId, rp->state);
            }
        }
    }
}

} // namespace gridlock

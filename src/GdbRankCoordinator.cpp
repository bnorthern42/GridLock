#include "GdbRankCoordinator.hpp"
#include <QDebug>
#include <QRegularExpression>
#include <QSettings>
#include <iostream>

namespace gridlock {

GdbRankCoordinator::GdbRankCoordinator(QObject *parent)
    : QObject(parent) {
    m_watchVariables.push_back("calc");
    m_watchVariables.push_back("rank");
}

GdbRankCoordinator::~GdbRankCoordinator() {
    terminateAllSessions();
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
        args << "-n" << "1" << extraArgs << "gdb" << "--interpreter=mi3" << "--args" << executable;
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
    QString location = QString("%1:%2").arg(file).arg(line);
    QString cmd = QString("-break-insert %1\n").arg(location);
    for (auto& rp : m_processes) {
        if (rp->process && rp->process->state() == QProcess::Running) {
            rp->process->write(cmd.toUtf8());
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
            QString disasmCmd = QString("-data-disassemble -f %1 -l %2 -n 30 -- 0\n").arg(rp->state.currentFile).arg(rp->state.currentLine);
            rp->process->write(disasmCmd.toUtf8());
        } else {
            rp->process->write("-data-disassemble -s $pc -e \"$pc + 40\" -- 0\n");
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
            QRegularExpression rxLine("line=\"(\\d+)\"");
            QString file = rp->state.currentFile; // fallback to known file
            int lineNum = 1;

            auto matchFile = rxFile.match(line);
            if (matchFile.hasMatch()) file = matchFile.captured(1);

            auto matchLine = rxLine.match(line);
            if (matchLine.hasMatch()) lineNum = matchLine.captured(1).toInt();

            rp->state.currentFile = file;
            rp->state.currentLine = lineNum;

            if (!file.isEmpty()) {
                QString asmCmd = QString("-data-disassemble -f %1 -l %2 -n 30 -- 0\n").arg(file).arg(lineNum);
                rp->process->write(asmCmd.toUtf8());
            } else {
                // Fallback if GDB didn't provide frame file info
                rp->process->write("-data-disassemble -s $pc -e \"$pc + 40\" -- 0\n");
            }

            QRegularExpression threadRe("thread-id=\"(\\d+)\"");
            auto matchThread = threadRe.match(line);
            if (matchThread.hasMatch()) {
                QString threadId = matchThread.captured(1);
                QString threadCmd = QString("-thread-select %1\n").arg(threadId);
                rp->process->write(threadCmd.toUtf8());
            } else {
                rp->process->write("-thread-info\n");
            }

            for (size_t i = 0; i < m_watchVariables.size(); ++i) {
                QString evalCmd = QString("10%1-data-evaluate-expression %2\n").arg(i).arg(m_watchVariables[i]);
                rp->process->write(evalCmd.toUtf8());
            }

            emit rankStateChanged(rankId, rp->state);
        } else if (sv.starts_with("*running")) {
            rp->state.currentState = "running";
            rp->state.executionTimer.start();
            emit rankStateChanged(rankId, rp->state);
        } else if (sv.starts_with("10") || line.contains("^done,value=")) { 
            QRegularExpression evalRe("10(\\d+)\\^done,value=\"([^\"]+)\"");
            auto match = evalRe.match(line);
            if (match.hasMatch()) {
                int varIdx = match.captured(1).toInt();
                if (varIdx >= 0 && varIdx < static_cast<int>(m_watchVariables.size())) {
                    rp->state.variableWatches[m_watchVariables[varIdx]] = match.captured(2);
                    emit rankStateChanged(rankId, rp->state);
                }
            }
        } else if (line.contains("^error")) {
            QRegularExpression errorRe("msg=\"([^\"]+)\"");
            auto match = errorRe.match(line);
            QString errMsg = match.hasMatch() ? match.captured(1) : "Unknown Error";
            
            rp->state.disassemblyText = QString("; GDB Error: %1").arg(errMsg);
            emit rankStateChanged(rankId, rp->state);
        } else if (line.contains("asm_insns=")) {
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

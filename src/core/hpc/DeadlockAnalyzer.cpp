#include "DeadlockAnalyzer.hpp"
#include "GdbRankCoordinator.hpp"
#include <QRegularExpression>
#include <QFile>
#include <QTextStream>

namespace gridlock::core {

DeadlockAnalyzer::DeadlockAnalyzer(gridlock::GdbRankCoordinator* coordinator, QObject* parent)
    : QObject(parent), m_coordinator(coordinator)
{
    if (m_coordinator) {
        connect(m_coordinator, &gridlock::GdbRankCoordinator::rankStateChanged,
                this, &DeadlockAnalyzer::onRankStateChanged);
        connect(m_coordinator, &gridlock::GdbRankCoordinator::gdbOutputReceived,
                this, &DeadlockAnalyzer::onGdbOutputReceived);
    }
}

void DeadlockAnalyzer::onRankStateChanged(int rankId, const gridlock::RankState& state) {
    if (state.currentState == "stopped") {
        m_pendingStackRequest[rankId] = true;
        m_coordinator->writeCmd(rankId, "-stack-list-frames 0 3\n");
    } else if (state.currentState == "running") {
        m_pendingStackRequest[rankId] = false;
        emit rankCleared(rankId);
    }
}

void DeadlockAnalyzer::onGdbOutputReceived(int rankId, const QString& output) {
    if (!m_pendingStackRequest.value(rankId, false)) {
        return;
    }

    if (output.startsWith("^done,stack=[")) {
        m_pendingStackRequest[rankId] = false;

        QRegularExpression funcRegex("func=\"([^\"]+)\"");
        QRegularExpression fileRegex("file=\"([^\"]+)\"");
        QRegularExpression lineRegex("line=\"(\\d+)\"");

        QStringList blockingFuncs = {
            "MPI_Barrier", "MPI_Wait", "MPI_Waitall", "MPI_Recv", 
            "MPI_Send", "MPI_Ssend", "MPI_Bcast", "MPI_Reduce", "MPI_Allreduce",
            "MPI_Gather", "MPI_Scatter", "MPI_Allgather", "MPI_Alltoall",
            "PMPI_Barrier", "PMPI_Wait", "PMPI_Waitall", "PMPI_Recv",
            "PMPI_Send", "PMPI_Ssend", "PMPI_Bcast", "PMPI_Reduce", "PMPI_Allreduce",
            "PMPI_Gather", "PMPI_Scatter", "PMPI_Allgather", "PMPI_Alltoall"
        };
        
        QRegularExpressionMatchIterator funcIt = funcRegex.globalMatch(output);
        QRegularExpressionMatchIterator fileIt = fileRegex.globalMatch(output);
        QRegularExpressionMatchIterator lineIt = lineRegex.globalMatch(output);

        QString detectedFunc;
        QString detectedFile;
        int detectedLine = 0;
        bool found = false;

        // Extract first frame's file and line just in case we need to read the source
        QString topFile;
        int topLine = 0;
        if (fileIt.hasNext()) {
            topFile = fileIt.next().captured(1);
        }
        if (lineIt.hasNext()) {
            topLine = lineIt.next().captured(1).toInt();
        }

        while (funcIt.hasNext()) {
            QRegularExpressionMatch match = funcIt.next();
            QString func = match.captured(1);
            if (blockingFuncs.contains(func)) {
                detectedFunc = func;
                detectedFile = topFile;
                detectedLine = topLine;
                found = true;
                break;
            }
        }

        // Fallback: Check the source code line of the top frame
        if (!found && !topFile.isEmpty() && topLine > 0) {
            QFile file(topFile);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&file);
                int currentLine = 1;
                while (!in.atEnd()) {
                    QString lineCode = in.readLine();
                    if (currentLine == topLine) {
                        for (const QString& bFunc : blockingFuncs) {
                            if (lineCode.contains(bFunc)) {
                                detectedFunc = bFunc;
                                detectedFile = topFile;
                                detectedLine = topLine;
                                found = true;
                                break;
                            }
                        }
                        break;
                    }
                    currentLine++;
                }
            }
        }

        if (found) {

            DeadlockInfo info;
            info.rankId = rankId;
            info.blockedFunction = detectedFunc;
            info.file = detectedFile;
            info.line = detectedLine;
            emit deadlockDetected(info);
        } else {
            emit rankCleared(rankId);
        }
    }
}

} // namespace gridlock::core

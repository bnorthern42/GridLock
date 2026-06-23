#include "DeadlockAnalyzer.hpp"
#include "GdbRankCoordinator.hpp"
#include <QRegularExpression>

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

        QStringList blockingFuncs = {"MPI_Barrier", "MPI_Wait", "MPI_Waitall", "MPI_Recv", "PMPI_Barrier", "PMPI_Wait", "PMPI_Waitall", "PMPI_Recv"};
        
        QRegularExpressionMatchIterator funcIt = funcRegex.globalMatch(output);
        QRegularExpressionMatchIterator fileIt = fileRegex.globalMatch(output);
        QRegularExpressionMatchIterator lineIt = lineRegex.globalMatch(output);

        QString detectedFunc;
        QString detectedFile;
        int detectedLine = 0;
        bool found = false;

        while (funcIt.hasNext()) {
            QRegularExpressionMatch match = funcIt.next();
            QString func = match.captured(1);
            if (blockingFuncs.contains(func)) {
                detectedFunc = func;
                found = true;
                break;
            }
        }

        if (found) {
            if (fileIt.hasNext()) detectedFile = fileIt.next().captured(1);
            if (lineIt.hasNext()) detectedLine = lineIt.next().captured(1).toInt();

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

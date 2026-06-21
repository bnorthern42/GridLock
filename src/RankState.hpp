#pragma once
#include <QString>
#include <vector>
#include <QHash>
#include <QElapsedTimer>

namespace gridlock {

struct RankState {
    int rankId;
    QString currentState; // e.g., "running", "stopped"
    int currentLine = 0;
    QString currentFile;
    QString currentFunction;
    QString disassemblyText;
    QHash<QString, QString> variableWatches; // watch variables
    QHash<int, QString> breakpoints; // bkpt number -> location
    
    QElapsedTimer executionTimer;
    qint64 totalRuntimeMs = 0;
    QString lastFiredTimestamp;
    int pendingBreakpoints = 0;
};

} // namespace gridlock

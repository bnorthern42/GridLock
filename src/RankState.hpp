#pragma once
#include <QString>
#include <vector>
#include <unordered_map>
#include <QElapsedTimer>

namespace gridlock {

struct RankState {
    int rankId;
    QString currentState; // e.g., "running", "stopped"
    int currentLine = 0;
    QString currentFile;
    QString currentFunction;
    QString disassemblyText;
    std::unordered_map<QString, QString> variableWatches; // watch variables
    
    QElapsedTimer executionTimer;
    qint64 totalRuntimeMs = 0;
    QString lastFiredTimestamp;
};

} // namespace gridlock

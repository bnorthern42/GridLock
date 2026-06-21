#include <QCoreApplication>
#include <QDebug>
#include "src/GdbRankCoordinator.hpp"

int main() {
    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(1);
    
    coord.registerWatchVariable("offset");
    coord.processGdbOutput(0, "300^error,msg=\"-var-create: unable to create variable object\"\n");
    qDebug() << "Value:" << coord.getRankState(0).variableWatches["offset"];
    return 0;
}

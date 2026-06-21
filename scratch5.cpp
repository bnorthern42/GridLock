#include <QCoreApplication>
#include <QDebug>
#include "src/GdbRankCoordinator.hpp"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(1);
    
    coord.registerWatchVariable("offset");
    
    coord.processGdbOutput(0, "300^error,msg=\"-var-create: unable to create variable object\"\n");
    qDebug() << "Watches map:" << coord.getRankState(0).variableWatches;
    return 0;
}

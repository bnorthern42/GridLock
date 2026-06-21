#include <QDebug>
#include "../src/GdbRankCoordinator.hpp"

int main() {
    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(1);
    
    coord.registerWatchVariable("offset");
    qDebug() << "Vars size:" << coord.getWatchVariables().size();
    
    coord.processGdbOutput(0, "300^error,msg=\"-var-create: unable to create variable object\"\n");
    qDebug() << "Val:" << coord.getRankState(0).variableWatches["offset"];
    return 0;
}

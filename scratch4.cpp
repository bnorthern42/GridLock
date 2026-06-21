#include <QDebug>
#include "src/GdbRankCoordinator.hpp"

int main() {
    gridlock::GdbRankCoordinator coord;
    coord.initializeMockSession(1);
    coord.registerWatchVariable("offset");
    coord.processGdbOutput(0, "300^error,msg=\"-var-create: unable to create variable object\"\n");
    auto watches = coord.getRankState(0).variableWatches;
    qDebug() << "size:" << watches.size();
    if (watches.contains("offset")) {
        qDebug() << "offset:" << watches["offset"];
    } else {
        qDebug() << "NOT FOUND";
    }
    return 0;
}

#include <QCoreApplication>
#include <QTimer>
#include "src/core/hpc/DapCoordinator.hpp"
#include <iostream>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    DapCoordinator dap;
    QObject::connect(&dap, &DapCoordinator::stateChanged, [](SessionState state){
        std::cout << "State changed to: " << static_cast<int>(state) << std::endl;
    });
    dap.launchParallelSession("test", 1);
    QTimer::singleShot(2000, &app, &QCoreApplication::quit);
    return app.exec();
}

#include <QApplication>
#include <QCommandLineParser>
#include "ui/MainWindow.hpp"
#include "backend/GridLockAutomationRunner.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    
    QCommandLineParser parser;
    parser.setApplicationDescription("GridLock MPI Debugger");
    parser.addHelpOption();
    
    QCommandLineOption testModeOption("test-mode", "Run interactive visual simulation tests");
    parser.addOption(testModeOption);
    
    parser.process(app);

    gridlock::ui::MainWindow window;
    window.show();

    gridlock::backend::GridLockAutomationRunner* runner = nullptr;
    if (parser.isSet(testModeOption)) {
        runner = new gridlock::backend::GridLockAutomationRunner(&window, &app);
    }
    
    return app.exec();
}

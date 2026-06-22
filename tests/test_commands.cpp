#include "test_commands.hpp"
#include "../src/core/commands/DebugCommands.hpp"
#include "../src/core/hpc/GdbRankCoordinator.hpp"

class MockGdbCoordinator : public gridlock::GdbRankCoordinator {
public:
    MockGdbCoordinator() : gridlock::GdbRankCoordinator(nullptr) {}

    bool stepCalled = false;
    bool pauseCalled = false;
    int pauseRankId = -1;

    void stepAll() override {
        stepCalled = true;
    }

    void pauseFocusedRank(int rankId) override {
        pauseCalled = true;
        pauseRankId = rankId;
    }
};

void TestCommands::testStepCommand() {
    MockGdbCoordinator mock;
    gridlock::core::commands::StepCommand cmd(&mock, 1);
    cmd.execute();
    QVERIFY(mock.stepCalled);
}

void TestCommands::testPauseCommand() {
    MockGdbCoordinator mock;
    gridlock::core::commands::PauseCommand cmd(&mock, 43);
    cmd.execute();
    QVERIFY(mock.pauseCalled);
    QCOMPARE(mock.pauseRankId, 42);
}

QTEST_GUILESS_MAIN(TestCommands)

#include "../src/ui/MainWindow.hpp"
namespace gridlock::ui {
    void MainWindow::startDebuggingSession(const QString& binaryPath, int ranks) {}
}

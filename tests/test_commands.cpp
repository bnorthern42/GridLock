/**
 * @file test_commands.cpp
 * @brief Implementation of debugger command pattern tests.
 *
 * Implements a test mock coordinator tracking command invocations and validates
 * StepCommand and PauseCommand dispatching semantics.
 */

#include "test_commands.hpp"
#include "../src/core/commands/DebugCommands.hpp"
#include "../src/core/hpc/GdbRankCoordinator.hpp"

/**
 * @class MockGdbCoordinator
 * @brief Mock coordinator recording step and pause method invocations.
 */
class MockGdbCoordinator : public gridlock::GdbRankCoordinator {
public:
    MockGdbCoordinator() : gridlock::GdbRankCoordinator(nullptr) {}

    bool stepCalled = false;       ///< Flag indicating if stepAll was called.
    bool pauseCalled = false;      ///< Flag indicating if pauseFocusedRank was called.
    int pauseRankId = -1;          ///< Record of rank ID passed to pause.

    /**
     * @brief Records stepAll execution.
     */
    void stepAll() override {
        stepCalled = true;
    }

    /**
     * @brief Records paused rank index.
     * @param rankId Zero-based rank index.
     */
    void pauseFocusedRank(int rankId) override {
        pauseCalled = true;
        pauseRankId = rankId;
    }
};

/**
 * @brief Tests StepCommand execution routing to coordinator stepAll().
 */
void TestCommands::testStepCommand() {
    MockGdbCoordinator mock;
    gridlock::core::commands::StepCommand cmd(&mock, 1);
    cmd.execute();
    QVERIFY(mock.stepCalled);
}

/**
 * @brief Tests PauseCommand execution converting 1-based rank (43) to 0-based rank (42).
 */
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

/**
 * @brief Test stub for MainWindow startDebuggingSession.
 * @param binaryPath Target path.
 * @param ranks Rank count.
 */
void MainWindow::startDebuggingSession(const QString& binaryPath, int ranks) {
    Q_UNUSED(binaryPath);
    Q_UNUSED(ranks);
}

}  // namespace gridlock::ui

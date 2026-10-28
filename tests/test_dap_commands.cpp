/**
 * @file test_dap_commands.cpp
 * @brief Implementation of DAP initialization and stepping command unit tests.
 *
 * Simulates a DAP debug adapter, testing request generation for initialization,
 * configuration completion, and thread execution stepping.
 */

#include "test_dap_commands.hpp"

#include "../src/core/commands/DebugCommands.hpp"
#include "../src/core/hpc/DapCoordinator.hpp"
#include "../src/ui/MainWindow.hpp"

/**
 * @class MockDapCoordinator
 * @brief Mock coordinator recording outbound adapter messages and call counts.
 */
class MockDapCoordinator : public DapCoordinator {
public:
    MockDapCoordinator() { m_state = SessionState::Running; }
    QByteArray lastWrittenData;
    int writeCount = 0;

protected:
    /**
     * @brief Reports adapter as running for mock purposes.
     * @return Always true in mock environment.
     */
    bool isAdapterRunning() const override {
        return true;
    }

    /**
     * @brief Records outbound command bytes sent to adapter.
     * @param data Raw payload.
     */
    void writeToAdapter(const QByteArray& data) override {
        lastWrittenData = data;
        writeCount++;
    }
};

/**
 * @brief Verifies that receiving 'initialized' event triggers configurationDone.
 */
void TestDapCommands::testHandshake() {
    MockDapCoordinator coordinator;

    // Simulate adapter sending the initialized lifecycle event.
    QByteArray data = "Content-Length: 38\r\n\r\n{\"type\":\"event\",\"event\":\"initialized\"}";
    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);
    QTest::qWait(50);

    // Verify coordinator sent the configurationDone request in response.
    QCOMPARE(coordinator.writeCount, 1);
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"configurationDone\""));
}

/**
 * @brief Verifies that StepCommand generates a valid 'next' DAP request.
 */
void TestDapCommands::testExecutionCommands() {
    MockDapCoordinator coordinator;

    // Issue step command for thread 1.
    gridlock::core::commands::StepCommand cmd(&coordinator, 1, false);
    cmd.execute();

    // Verify outbound JSON payload contains next command for thread 1.
    QCOMPARE(coordinator.writeCount, 1);
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"next\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"threadId\":1"));
}

QTEST_GUILESS_MAIN(TestDapCommands)

namespace gridlock::ui {
/**
 * @brief Stub implementation of MainWindow::startDebuggingSession for unit test linkage.
 * @param binaryPath Executable path.
 * @param ranks MPI rank count.
 */
void MainWindow::startDebuggingSession(const QString& binaryPath, int ranks) {
    Q_UNUSED(binaryPath);
    Q_UNUSED(ranks);
}
}  // namespace gridlock::ui

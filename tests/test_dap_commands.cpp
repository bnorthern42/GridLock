#include "test_dap_commands.hpp"
#include "../src/core/hpc/DapCoordinator.hpp"
#include "../src/core/commands/DebugCommands.hpp"

class MockDapCoordinator : public DapCoordinator {
public:
    MockDapCoordinator() { m_state = SessionState::Running; }
    QByteArray lastWrittenData;
    int writeCount = 0;

protected:
    bool isAdapterRunning() const override {
        return true;
    }

    void writeToAdapter(const QByteArray& data) override {
        lastWrittenData = data;
        writeCount++;
    }
};

void TestDapCommands::testHandshake() {
    MockDapCoordinator coordinator;
    
    QByteArray data = "Content-Length: 38\r\n\r\n{\"type\":\"event\",\"event\":\"initialized\"}";
    coordinator.processRawData(data);
    
    QCOMPARE(coordinator.writeCount, 1);
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"configurationDone\""));
}

void TestDapCommands::testExecutionCommands() {
    MockDapCoordinator coordinator;
    
    gridlock::core::commands::StepCommand cmd(&coordinator, 1, false);
    cmd.execute();
    
    QCOMPARE(coordinator.writeCount, 1);
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"next\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"threadId\":1"));
}

QTEST_GUILESS_MAIN(TestDapCommands)

#include "../src/ui/MainWindow.hpp"
namespace gridlock::ui {
    void MainWindow::startDebuggingSession(const QString& binaryPath, int ranks) {
        Q_UNUSED(binaryPath);
        Q_UNUSED(ranks);
    }
}

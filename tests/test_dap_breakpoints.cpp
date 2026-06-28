#include "test_dap_breakpoints.hpp"
#include "../src/core/hpc/DapCoordinator.hpp"

class MockDapCoordinator : public DapCoordinator {
public:
    MockDapCoordinator() { m_state = SessionState::Running; }
    QByteArray lastWrittenData;

protected:
    bool isAdapterRunning() const override {
        return true;
    }

    void writeToAdapter(const QByteArray& data) override {
        lastWrittenData = data;
    }
};

void TestDapBreakpoints::testBreakpointAccumulation() {
    MockDapCoordinator coordinator;
    
    coordinator.toggleBreakpoint("main.cpp", 10);
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"setBreakpoints\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"path\":\"main.cpp\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"line\":10"));
    
    coordinator.lastWrittenData.clear();
    coordinator.toggleBreakpoint("main.cpp", 15);
    QVERIFY(coordinator.lastWrittenData.contains("\"line\":10"));
    QVERIFY(coordinator.lastWrittenData.contains("\"line\":15"));
    
    coordinator.lastWrittenData.clear();
    coordinator.toggleBreakpoint("main.cpp", 10);
    QVERIFY(!coordinator.lastWrittenData.contains("\"line\":10"));
    QVERIFY(coordinator.lastWrittenData.contains("\"line\":15"));
}

void TestDapBreakpoints::testStopEventParsing() {
    MockDapCoordinator coordinator;
    QSignalSpy spy(&coordinator, &DapCoordinator::executionStopped);
    
    QByteArray json = "{\"type\":\"event\",\"event\":\"stopped\",\"body\":{\"reason\":\"breakpoint\",\"threadId\":1}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    coordinator.processRawData(data);
    
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0); // Rank 0
    QCOMPARE(args.at(1).toString(), QString("breakpoint"));
}

QTEST_GUILESS_MAIN(TestDapBreakpoints)

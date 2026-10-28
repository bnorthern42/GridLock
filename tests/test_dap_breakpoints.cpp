/**
 * @file test_dap_breakpoints.cpp
 * @brief Implementation of DAP breakpoint command and event handling tests.
 *
 * Simulates a DAP adapter interface to inspect outbound JSON-RPC commands for
 * breakpoint requests and verifies inbound DAP stop notifications.
 */

#include "test_dap_breakpoints.hpp"

#include "../src/core/hpc/DapCoordinator.hpp"

/**
 * @class MockDapCoordinator
 * @brief Test double capturing outbound adapter data without launching an external process.
 */
class MockDapCoordinator : public DapCoordinator {
public:
    MockDapCoordinator() { m_state = SessionState::Running; }
    QByteArray lastWrittenData;

protected:
    /**
     * @brief Reports adapter as running for mock purposes.
     * @return Always true in mock environment.
     */
    bool isAdapterRunning() const override {
        return true;
    }

    /**
     * @brief Captures JSON-RPC payload written towards debug adapter.
     * @param data Raw byte payload.
     */
    void writeToAdapter(const QByteArray& data) override {
        lastWrittenData = data;
    }
};

/**
 * @brief Verifies that toggling breakpoints accumulates and removes lines correctly.
 */
void TestDapBreakpoints::testBreakpointAccumulation() {
    MockDapCoordinator coordinator;

    // Toggle breakpoint on line 10.
    coordinator.toggleBreakpoint("main.cpp", 10);
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"setBreakpoints\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"path\":\"main.cpp\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"line\":10"));

    // Add breakpoint on line 15; payload should now contain both lines 10 and 15.
    coordinator.lastWrittenData.clear();
    coordinator.toggleBreakpoint("main.cpp", 15);
    QVERIFY(coordinator.lastWrittenData.contains("\"line\":10"));
    QVERIFY(coordinator.lastWrittenData.contains("\"line\":15"));

    // Remove breakpoint on line 10; payload should only retain line 15.
    coordinator.lastWrittenData.clear();
    coordinator.toggleBreakpoint("main.cpp", 10);
    QVERIFY(!coordinator.lastWrittenData.contains("\"line\":10"));
    QVERIFY(coordinator.lastWrittenData.contains("\"line\":15"));
}

/**
 * @brief Verifies that inbound DAP stopped events trigger the executionStopped signal.
 */
void TestDapBreakpoints::testStopEventParsing() {
    MockDapCoordinator coordinator;
    QSignalSpy spy(&coordinator, &DapCoordinator::executionStopped);

    // Construct valid DAP stopped event envelope.
    QByteArray json = "{\"type\":\"event\",\"event\":\"stopped\",\"body\":{\"reason\":\"breakpoint\",\"threadId\":1}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;

    // Process raw stream into coordinator.
    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);

    // Verify signal emission count and parsed parameters.
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0); // Rank 0
    QCOMPARE(args.at(1).toString(), QString("breakpoint"));
}

QTEST_GUILESS_MAIN(TestDapBreakpoints)

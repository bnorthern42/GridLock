/**
 * @file test_dap_lifecycle.cpp
 * @brief Implementation of DAP session lifecycle and output stream unit tests.
 *
 * Simulates DAP adapter stdout events, session disconnect requests, and
 * thread stop notifications for MPI ranks.
 */

#include "test_dap_lifecycle.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "../src/core/hpc/DapCoordinator.hpp"

/**
 * @class MockDapCoordinatorLifecycle
 * @brief Test double capturing outbound adapter messages for lifecycle testing.
 */
class MockDapCoordinatorLifecycle : public DapCoordinator {
public:
    MockDapCoordinatorLifecycle() { m_state = SessionState::Running; }

    QByteArray lastWrittenData;

protected:
    /**
     * @brief Reports adapter as running for mock lifecycle checks.
     * @return Always true in mock environment.
     */
    bool isAdapterRunning() const override { return true; }

    /**
     * @brief Records outbound adapter byte payload.
     * @param data Raw outbound bytes.
     */
    void writeToAdapter(const QByteArray& data) override {
        lastWrittenData = data;
    }
};

/**
 * @brief Tests target stdout stream output event parsing and signal emission.
 */
void TestDapLifecycle::testOutputEvent() {
    MockDapCoordinatorLifecycle coordinator;

    // Spy on targetOutputReceived signal.
    QSignalSpy spy(&coordinator, &DapCoordinator::targetOutputReceived);

    // Construct DAP output event carrying stdout text.
    QByteArray json = "{\"type\":\"event\",\"event\":\"output\",\"body\":{\"category\":\"stdout\",\"output\":\"MPI Task 0 started\\n\"}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;

    // Feed message into coordinator.
    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);

    // Verify signal caught output category and text.
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QString("stdout"));
    QCOMPARE(args.at(1).toString(), QString("MPI Task 0 started\n"));
}

/**
 * @brief Tests terminateSession sending disconnect command with terminateDebuggee: true.
 */
void TestDapLifecycle::testDisconnectRequest() {
    MockDapCoordinatorLifecycle coordinator;

    // Initiate session termination.
    coordinator.terminateSession();

    // Verify disconnect request payload.
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"disconnect\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"terminateDebuggee\":true"));
}

/**
 * @brief Tests thread identifier mapping to MPI rank IDs during stopped events.
 */
void TestDapLifecycle::testRankStateBroadcasting() {
    MockDapCoordinatorLifecycle coordinator;

    QSignalSpy spy(&coordinator, &DapCoordinator::executionStopped);

    // Simulating Rank 1 (threadId = 2)
    QByteArray json1 = "{\"type\":\"event\",\"event\":\"stopped\",\"body\":{\"reason\":\"breakpoint\",\"threadId\":2}}";
    QByteArray data1 = "Content-Length: " + QByteArray::number(json1.size()) + "\r\n\r\n" + json1;

    coordinator.processRawData(data1);
    QTest::qWait(50);
    QTest::qWait(50);
    QTest::qWait(50); // allow QtConcurrent to process

    // Thread 2 maps to Rank 1.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 1); // Rank 1
    QCOMPARE(spy.first().at(1).toString(), QString("breakpoint"));
    spy.clear();

    // Simulating Rank 3 (threadId = 4)
    QByteArray json3 = "{\"type\":\"event\",\"event\":\"stopped\",\"body\":{\"reason\":\"step\",\"threadId\":4}}";
    QByteArray data3 = "Content-Length: " + QByteArray::number(json3.size()) + "\r\n\r\n" + json3;

    coordinator.processRawData(data3);
    QTest::qWait(50);
    QTest::qWait(50);
    QTest::qWait(50);

    // Thread 4 maps to Rank 3.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 3); // Rank 3
    QCOMPARE(spy.first().at(1).toString(), QString("step"));
}

QTEST_GUILESS_MAIN(TestDapLifecycle)

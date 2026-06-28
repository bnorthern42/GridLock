#include "test_dap_lifecycle.hpp"
#include "../src/core/hpc/DapCoordinator.hpp"
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>

class MockDapCoordinatorLifecycle : public DapCoordinator {
public:
    MockDapCoordinatorLifecycle() { m_state = SessionState::Running; }
    
    QByteArray lastWrittenData;

protected:
    bool isAdapterRunning() const override { return true; }
    void writeToAdapter(const QByteArray& data) override {
        lastWrittenData = data;
    }
};

void TestDapLifecycle::testOutputEvent() {
    MockDapCoordinatorLifecycle coordinator;
    
    QSignalSpy spy(&coordinator, &DapCoordinator::targetOutputReceived);
    
    QByteArray json = "{\"type\":\"event\",\"event\":\"output\",\"body\":{\"category\":\"stdout\",\"output\":\"MPI Task 0 started\\n\"}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    
    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);
    
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toString(), QString("stdout"));
    QCOMPARE(args.at(1).toString(), QString("MPI Task 0 started\n"));
}

void TestDapLifecycle::testDisconnectRequest() {
    MockDapCoordinatorLifecycle coordinator;
    
    coordinator.terminateSession();
    
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"disconnect\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"terminateDebuggee\":true"));
}

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
    
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toInt(), 3); // Rank 3
    QCOMPARE(spy.first().at(1).toString(), QString("step"));
}

QTEST_GUILESS_MAIN(TestDapLifecycle)

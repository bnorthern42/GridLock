#include "test_dap_lifecycle.hpp"
#include "../src/core/hpc/DapCoordinator.hpp"
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>

class MockDapCoordinatorLifecycle : public DapCoordinator {
public:
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

QTEST_GUILESS_MAIN(TestDapLifecycle)

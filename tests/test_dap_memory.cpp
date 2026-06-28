#include "test_dap_memory.hpp"
#include "../src/core/hpc/DapCoordinator.hpp"
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>

class MockDapCoordinatorMemory : public DapCoordinator {
public:
    MockDapCoordinatorMemory() { m_state = SessionState::Running; }
    
    QByteArray lastWrittenData;

protected:
    bool isAdapterRunning() const override { return true; }
    void writeToAdapter(const QByteArray& data) override {
        lastWrittenData = data;
    }
};

void TestDapMemory::testRegistersFetch() {
    MockDapCoordinatorMemory coordinator;
    
    QSignalSpy spy(&coordinator, &DapCoordinator::scopesUpdated);

    // We need to inject a scopes response. Wait, `scopesRequests` maps request_seq to rankId.
    // So we first need to populate `scopesRequests`.
    coordinator.requestScopes(100, 0); // rank 0
    
    QByteArray json = "{\"type\":\"response\",\"command\":\"scopes\",\"success\":true,\"request_seq\":1,\"body\":{\"scopes\":[{\"name\":\"Registers\",\"variablesReference\":2000}]}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    
    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);
    
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0); // rankId
    QJsonArray scopes = args.at(1).toJsonArray();
    QCOMPARE(scopes.size(), 1);
    QCOMPARE(scopes[0].toObject()["name"].toString(), QString("Registers"));
}

void TestDapMemory::testMemoryRequest() {
    MockDapCoordinatorMemory coordinator;
    
    coordinator.readMemory(0, "0x1234", 256);
    
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"readMemory\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"memoryReference\":\"0x1234\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"count\":256"));
}

void TestDapMemory::testMemoryResponse() {
    MockDapCoordinatorMemory coordinator;
    
    QSignalSpy spy(&coordinator, &DapCoordinator::memoryRead);
    
    // Send request so m_memoryRequests maps request_seq to rankId
    coordinator.readMemory(0, "0x1234", 256);
    
    QByteArray json = "{\"type\":\"response\",\"command\":\"readMemory\",\"success\":true,\"request_seq\":1,\"body\":{\"address\":\"0x1234\",\"data\":\"SGVsbG8=\"}}"; // "Hello"
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    
    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);
    
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0);
    QCOMPARE(args.at(1).toString(), QString("0x1234"));
    QCOMPARE(args.at(2).toByteArray(), QByteArray("Hello"));
}

QTEST_GUILESS_MAIN(TestDapMemory)

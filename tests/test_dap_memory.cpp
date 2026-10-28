/**
 * @file test_dap_memory.cpp
 * @brief Implementation of DAP memory and register inspection unit tests.
 *
 * Simulates DAP adapter responses for variable scopes and binary memory blocks,
 * validating base64 decoding and rank-aware signal dispatches.
 */

#include "test_dap_memory.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "../src/core/hpc/DapCoordinator.hpp"

/**
 * @class MockDapCoordinatorMemory
 * @brief Test double capturing outbound adapter requests for memory operations.
 */
class MockDapCoordinatorMemory : public DapCoordinator {
public:
    MockDapCoordinatorMemory() { m_state = SessionState::Running; }

    QByteArray lastWrittenData;

protected:
    /**
     * @brief Reports adapter as running for memory testing.
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
 * @brief Verifies that scopes responses containing Registers are parsed and signaled.
 */
void TestDapMemory::testRegistersFetch() {
    MockDapCoordinatorMemory coordinator;

    // Spy on scopesUpdated signal.
    QSignalSpy spy(&coordinator, &DapCoordinator::scopesUpdated);

    // Request scopes for frame 100 on rank 0 to register sequence ID mapping.
    coordinator.requestScopes(100, 0); // rank 0

    // Feed mock scopes response containing Registers scope reference.
    QByteArray json = "{\"type\":\"response\",\"command\":\"scopes\",\"success\":true,\"request_seq\":1,\"body\":{\"scopes\":[{\"name\":\"Registers\",\"variablesReference\":2000}]}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;

    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);

    // Verify signal caught parsed scopes array.
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0); // rankId
    QJsonArray scopes = args.at(1).toJsonArray();
    QCOMPARE(scopes.size(), 1);
    QCOMPARE(scopes[0].toObject()["name"].toString(), QString("Registers"));
}

/**
 * @brief Verifies readMemory JSON-RPC request construction with address and count.
 */
void TestDapMemory::testMemoryRequest() {
    MockDapCoordinatorMemory coordinator;

    // Issue memory read of 256 bytes starting at 0x1234.
    coordinator.readMemory(0, "0x1234", 256);

    // Verify outbound request contents.
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"readMemory\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"memoryReference\":\"0x1234\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"count\":256"));
}

/**
 * @brief Verifies readMemory base64 response decoding and memoryRead signal emission.
 */
void TestDapMemory::testMemoryResponse() {
    MockDapCoordinatorMemory coordinator;

    // Connect spy to memoryRead signal.
    QSignalSpy spy(&coordinator, &DapCoordinator::memoryRead);

    // Send request so sequence number is tracked.
    coordinator.readMemory(0, "0x1234", 256);

    // Base64 "SGVsbG8=" decodes to "Hello".
    QByteArray json = "{\"type\":\"response\",\"command\":\"readMemory\",\"success\":true,\"request_seq\":1,\"body\":{\"address\":\"0x1234\",\"data\":\"SGVsbG8=\"}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;

    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);

    // Verify decoded bytes match expected string.
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0);
    QCOMPARE(args.at(1).toString(), QString("0x1234"));
    QCOMPARE(args.at(2).toByteArray(), QByteArray("Hello"));
}

QTEST_GUILESS_MAIN(TestDapMemory)

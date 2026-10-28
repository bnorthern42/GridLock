/**
 * @file test_dap_stacktrace.cpp
 * @brief Implementation of DAP stack trace request and response unit tests.
 *
 * Simulates DAP adapter responses for call stack unwinding, ensuring that
 * stack frame line numbers and filenames map back to specific MPI rank states.
 */

#include "test_dap_stacktrace.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "../src/core/hpc/DapCoordinator.hpp"

/**
 * @class MockDapCoordinator
 * @brief Test double capturing outbound adapter messages for stack trace testing.
 */
class MockDapCoordinator : public DapCoordinator {
public:
    MockDapCoordinator() { m_state = SessionState::Running; }

    QByteArray lastWrittenData;

protected:
    /**
     * @brief Reports adapter as running for stack trace mock operations.
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
 * @brief Verifies that receiving a stopped event issues a stackTrace query.
 */
void TestDapStacktrace::testStackTraceRequest() {
    MockDapCoordinator coordinator;

    // Simulate thread 1 stopped on breakpoint.
    QByteArray json = "{\"type\":\"event\",\"event\":\"stopped\",\"body\":{\"reason\":\"breakpoint\",\"threadId\":1}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;

    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);

    // Verify stackTrace request sent for threadId 1 with depth 1.
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"stackTrace\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"threadId\":1"));
    QVERIFY(coordinator.lastWrittenData.contains("\"levels\":1"));
}

/**
 * @brief Verifies parsing of stack frame responses and emission of locationChanged.
 */
void TestDapStacktrace::testStackTraceResponseParsing() {
    MockDapCoordinator coordinator;
    QSignalSpy spy(&coordinator, &DapCoordinator::locationChanged);

    // Trigger stopped event first so coordinator registers outbound sequence number.
    QByteArray stoppedJson = "{\"type\":\"event\",\"event\":\"stopped\",\"body\":{\"reason\":\"breakpoint\",\"threadId\":1}}";
    QByteArray stoppedData = "Content-Length: " + QByteArray::number(stoppedJson.size()) + "\r\n\r\n" + stoppedJson;
    coordinator.processRawData(stoppedData);
    QTest::qWait(50);

    // Extract sequence number from the coordinator's stackTrace request.
    QJsonDocument doc = QJsonDocument::fromJson(coordinator.lastWrittenData.mid(coordinator.lastWrittenData.indexOf("\r\n\r\n") + 4));
    int seq = doc.object()["seq"].toInt();

    // Construct mock stackTrace response matching the request sequence number.
    QByteArray responseJson = "{\"type\":\"response\",\"command\":\"stackTrace\",\"success\":true,\"request_seq\":" + QByteArray::number(seq) + ",\"body\":{\"stackFrames\":[{\"id\":0,\"name\":\"main\",\"source\":{\"path\":\"/src/main.cpp\"},\"line\":42,\"column\":5}]}}";
    QByteArray responseData = "Content-Length: " + QByteArray::number(responseJson.size()) + "\r\n\r\n" + responseJson;
    coordinator.processRawData(responseData);
    QTest::qWait(50);

    // Assert coordinator emitted locationChanged signal with mapped rank 0, path, and line 42.
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0); // Rank ID 0
    QCOMPARE(args.at(1).toString(), QString("/src/main.cpp"));
    QCOMPARE(args.at(2).toInt(), 42);
}

QTEST_GUILESS_MAIN(TestDapStacktrace)

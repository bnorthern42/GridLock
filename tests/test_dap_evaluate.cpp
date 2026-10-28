/**
 * @file test_dap_evaluate.cpp
 * @brief Implementation of DAP expression evaluation unit tests.
 *
 * Simulates DAP adapter responses for stack traces and watch expression evaluation,
 * validating JSON-RPC serialization, frame ID caching, and signal dispatch.
 */

#include "test_dap_evaluate.hpp"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include "../src/core/hpc/DapCoordinator.hpp"

/**
 * @class MockDapCoordinatorEval
 * @brief Test double capturing outbound evaluation payloads.
 */
class MockDapCoordinatorEval : public DapCoordinator {
public:
    MockDapCoordinatorEval() { m_state = SessionState::Running; }

    QByteArray lastWrittenData;

protected:
    /**
     * @brief Reports adapter as running for evaluation testing.
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
 * @brief Verifies evaluate request construction using cached stack frame identifiers.
 */
void TestDapEvaluate::testEvaluateRequest() {
    MockDapCoordinatorEval coordinator;

    // Seed stackTrace so the frameId is cached in coordinator state.
    coordinator.requestStackTrace(0); // rank=0
    QByteArray json = "{\"type\":\"response\",\"command\":\"stackTrace\",\"success\":true,\"request_seq\":1,\"body\":{\"stackFrames\":[{\"id\":1005,\"name\":\"main\",\"source\":{\"path\":\"/test.cpp\"},\"line\":42}]}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);

    // Evaluate an expression under rank 0; should bind to frameId 1005.
    coordinator.evaluateExpression(0, "sizeof(int)");

    // Verify generated JSON payload properties.
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"evaluate\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"expression\":\"sizeof(int)\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"frameId\":1005"));
    QVERIFY(coordinator.lastWrittenData.contains("\"context\":\"watch\""));
}

/**
 * @brief Verifies that evaluate response payloads emit the expressionEvaluated signal.
 */
void TestDapEvaluate::testEvaluateResponse() {
    MockDapCoordinatorEval coordinator;

    // Connect spy to expressionEvaluated signal.
    QSignalSpy spy(&coordinator, &DapCoordinator::expressionEvaluated);

    // Send evaluate request to associate sequence number with rank 0 and expression.
    coordinator.evaluateExpression(0, "sizeof(int)"); // rank=0

    // Feed mock evaluation response matching the request sequence number.
    QByteArray json = "{\"type\":\"response\",\"command\":\"evaluate\",\"success\":true,\"request_seq\":1,\"body\":{\"result\":\"4\",\"variablesReference\":0}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);

    // Assert that expressionEvaluated signal was fired with parsed result "4".
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0); // rankId
    QCOMPARE(args.at(1).toString(), QString("sizeof(int)")); // expression
    QCOMPARE(args.at(2).toString(), QString("4")); // result
}

QTEST_GUILESS_MAIN(TestDapEvaluate)

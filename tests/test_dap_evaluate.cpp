#include "test_dap_evaluate.hpp"
#include "../src/core/hpc/DapCoordinator.hpp"
#include <QSignalSpy>
#include <QJsonObject>
#include <QJsonDocument>
#include <QByteArray>

class MockDapCoordinatorEval : public DapCoordinator {
public:
    MockDapCoordinatorEval() { m_state = SessionState::Running; }
    
    QByteArray lastWrittenData;

protected:
    bool isAdapterRunning() const override { return true; }
    void writeToAdapter(const QByteArray& data) override {
        lastWrittenData = data;
    }
};

void TestDapEvaluate::testEvaluateRequest() {
    MockDapCoordinatorEval coordinator;
    
    // Seed stackTrace so the frameId is cached
    coordinator.requestStackTrace(0); // rank=0
    QByteArray json = "{\"type\":\"response\",\"command\":\"stackTrace\",\"success\":true,\"request_seq\":1,\"body\":{\"stackFrames\":[{\"id\":1005,\"name\":\"main\",\"source\":{\"path\":\"/test.cpp\"},\"line\":42}]}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    coordinator.processRawData(data);
    
    coordinator.evaluateExpression(0, "sizeof(int)");
    
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"evaluate\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"expression\":\"sizeof(int)\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"frameId\":1005"));
    QVERIFY(coordinator.lastWrittenData.contains("\"context\":\"watch\""));
}

void TestDapEvaluate::testEvaluateResponse() {
    MockDapCoordinatorEval coordinator;
    
    QSignalSpy spy(&coordinator, &DapCoordinator::expressionEvaluated);
    
    // Simulate sending an evaluate expression to register rankId mapping
    coordinator.evaluateExpression(0, "sizeof(int)"); // rank=0
    
    // In our MockDapCoordinatorEval, seq will be 2 (if requestStackTrace was not called). Or 1. 
    // Since we just instantiated a new coordinator, m_sequenceNumber will be 1.
    // However, if we didn't seed the stack trace, evaluateExpression might fail or send frameId=0. 
    // It still generates a sequence number.
    
    QByteArray json = "{\"type\":\"response\",\"command\":\"evaluate\",\"success\":true,\"request_seq\":1,\"body\":{\"result\":\"4\",\"variablesReference\":0}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    coordinator.processRawData(data);
    
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0); // rankId
    QCOMPARE(args.at(1).toString(), QString("sizeof(int)")); // expression
    QCOMPARE(args.at(2).toString(), QString("4")); // result
}

QTEST_GUILESS_MAIN(TestDapEvaluate)

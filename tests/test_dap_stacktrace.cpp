#include "test_dap_stacktrace.hpp"
#include "../src/core/hpc/DapCoordinator.hpp"

class MockDapCoordinator : public DapCoordinator {
public:
    QByteArray lastWrittenData;

protected:
    bool isAdapterRunning() const override {
        return true;
    }

    void writeToAdapter(const QByteArray& data) override {
        lastWrittenData = data;
    }
};

void TestDapStacktrace::testStackTraceRequest() {
    MockDapCoordinator coordinator;
    
    QByteArray json = "{\"type\":\"event\",\"event\":\"stopped\",\"body\":{\"reason\":\"breakpoint\",\"threadId\":1}}";
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    
    coordinator.processRawData(data);
    
    QVERIFY(coordinator.lastWrittenData.contains("\"command\":\"stackTrace\""));
    QVERIFY(coordinator.lastWrittenData.contains("\"threadId\":1"));
    QVERIFY(coordinator.lastWrittenData.contains("\"levels\":1"));
}

void TestDapStacktrace::testStackTraceResponseParsing() {
    MockDapCoordinator coordinator;
    QSignalSpy spy(&coordinator, &DapCoordinator::locationChanged);
    
    QByteArray json = "{\"type\":\"response\",\"command\":\"stackTrace\",\"success\":true,\"request_seq\":2,\"body\":{\"stackFrames\":[{\"id\":0,\"name\":\"main\",\"source\":{\"path\":\"/src/main.cpp\"},\"line\":42,\"column\":5}]}}";
    // Also simulate request memory if required by coordinator (e.g., mapping request_seq to threadId)
    // Wait, if the response does not contain threadId, we need to ensure the implementation handles mapping sequence number to rankId!
    // But the prompt says "Assert that the coordinator emits a new signal ... mapping Thread 1 back to Rank 0".
    // I should inject a threadId in the response? DAP stackTrace response DOES NOT contain threadId natively!
    // Wait, we can test it directly if the prompt implies we must store seq -> threadId map, or just inject it to see what happens.
    // Let's add the basic payload first.
    QByteArray data = "Content-Length: " + QByteArray::number(json.size()) + "\r\n\r\n" + json;
    
    // In order for the coordinator to know which rank it is, we first need to send the request (so it remembers the seq number)
    // The test framework can mock that by just sending a stopped event first, which triggers the request and records seq.
    QByteArray stoppedJson = "{\"type\":\"event\",\"event\":\"stopped\",\"body\":{\"reason\":\"breakpoint\",\"threadId\":1}}";
    QByteArray stoppedData = "Content-Length: " + QByteArray::number(stoppedJson.size()) + "\r\n\r\n" + stoppedJson;
    coordinator.processRawData(stoppedData);
    
    // Now we must parse the sequence number of the outgoing request to build the mock response!
    // But let's just make the coordinator map seq to threadId, or assume the latest stopped thread.
    // I will extract the seq from lastWrittenData.
    QJsonDocument doc = QJsonDocument::fromJson(coordinator.lastWrittenData.mid(coordinator.lastWrittenData.indexOf("\r\n\r\n") + 4));
    int seq = doc.object()["seq"].toInt();
    
    QByteArray responseJson = "{\"type\":\"response\",\"command\":\"stackTrace\",\"success\":true,\"request_seq\":" + QByteArray::number(seq) + ",\"body\":{\"stackFrames\":[{\"id\":0,\"name\":\"main\",\"source\":{\"path\":\"/src/main.cpp\"},\"line\":42,\"column\":5}]}}";
    QByteArray responseData = "Content-Length: " + QByteArray::number(responseJson.size()) + "\r\n\r\n" + responseJson;
    coordinator.processRawData(responseData);
    
    QCOMPARE(spy.count(), 1);
    QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0); // Rank ID 0
    QCOMPARE(args.at(1).toString(), QString("/src/main.cpp"));
    QCOMPARE(args.at(2).toInt(), 42);
}

QTEST_GUILESS_MAIN(TestDapStacktrace)

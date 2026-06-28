#include "test_dap_parser.hpp"
#include "../src/core/hpc/DapCoordinator.hpp"

void TestDapParser::testFullMessage() {
    DapCoordinator coordinator;
    QSignalSpy spy(&coordinator, &DapCoordinator::messageReceived);
    
    QByteArray data = "Content-Length: 38\r\n\r\n{\"type\":\"event\",\"event\":\"initialized\"}";
    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);
    
    QCOMPARE(spy.count(), 1);
    QJsonObject msg = spy.takeFirst().at(0).toJsonObject();
    QCOMPARE(msg["type"].toString(), QString("event"));
    QCOMPARE(msg["event"].toString(), QString("initialized"));
}

void TestDapParser::testFragmentedMessage() {
    DapCoordinator coordinator;
    QSignalSpy spy(&coordinator, &DapCoordinator::messageReceived);
    
    QByteArray chunk1 = "Content-Length: 38\r\n\r\n{\"type\":\"event\",";
    QByteArray chunk2 = "\"event\":\"initialized\"}";
    
    coordinator.processRawData(chunk1);
    QTest::qWait(50);
    QCOMPARE(spy.count(), 0);
    
    coordinator.processRawData(chunk2);
    QTest::qWait(50);
    QCOMPARE(spy.count(), 1);
    
    QJsonObject msg = spy.takeFirst().at(0).toJsonObject();
    QCOMPARE(msg["event"].toString(), QString("initialized"));
}

void TestDapParser::testMultipleMessages() {
    DapCoordinator coordinator;
    QSignalSpy spy(&coordinator, &DapCoordinator::messageReceived);
    
    QByteArray data = "Content-Length: 38\r\n\r\n{\"type\":\"event\",\"event\":\"initialized\"}Content-Length: 36\r\n\r\n{\"type\":\"response\",\"command\":\"next\"}";
    coordinator.processRawData(data);
    QTest::qWait(50);
    QTest::qWait(50);
    
    QCOMPARE(spy.count(), 2);
    QJsonObject msg1 = spy.takeFirst().at(0).toJsonObject();
    QCOMPARE(msg1["event"].toString(), QString("initialized"));
    
    QJsonObject msg2 = spy.takeFirst().at(0).toJsonObject();
    QCOMPARE(msg2["command"].toString(), QString("next"));
}

QTEST_GUILESS_MAIN(TestDapParser)

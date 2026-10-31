/**
 * @file test_dap_parser.cpp
 * @brief Implementation of DAP wire framing protocol unit tests.
 *
 * Validates Content-Length header processing, fragmented TCP packet stitching,
 * and concurrent message extraction from raw byte streams.
 */

#include "test_dap_parser.hpp"

#include <QByteArray>
#include <QJsonObject>
#include <QSignalSpy>

#include "../src/core/hpc/DapCoordinator.hpp"

/**
 * @brief Tests parsing of a single, well-formed Content-Length framed JSON message.
 */
void TestDapParser::testFullMessage() {
    DapCoordinator coordinator;
    QSignalSpy spy(&coordinator, &DapCoordinator::messageReceived);

    // Frame a single initialized event.
    QByteArray data = "Content-Length: 38\r\n\r\n{\"type\":\"event\",\"event\":\"initialized\"}";
    coordinator.processRawData(data);

    // Verify signal caught single deserialized JSON object deterministically on loaded CI runners.
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    QJsonObject msg = spy.takeFirst().at(0).toJsonObject();
    QCOMPARE(msg["type"].toString(), QString("event"));
    QCOMPARE(msg["event"].toString(), QString("initialized"));
}

/**
 * @brief Tests reassembly when a message is split across multiple TCP chunks.
 */
void TestDapParser::testFragmentedMessage() {
    DapCoordinator coordinator;
    QSignalSpy spy(&coordinator, &DapCoordinator::messageReceived);

    // Split message across two separate raw chunks.
    QByteArray chunk1 = "Content-Length: 38\r\n\r\n{\"type\":\"event\",";
    QByteArray chunk2 = "\"event\":\"initialized\"}";

    // First chunk should not trigger message dispatch.
    coordinator.processRawData(chunk1);
    QTest::qWait(50);
    QCOMPARE(spy.count(), 0);

    // Second chunk completes the frame, triggering message delivery.
    coordinator.processRawData(chunk2);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);

    QJsonObject msg = spy.takeFirst().at(0).toJsonObject();
    QCOMPARE(msg["event"].toString(), QString("initialized"));
}

/**
 * @brief Tests unpacking when multiple DAP messages arrive in a single packet.
 */
void TestDapParser::testMultipleMessages() {
    DapCoordinator coordinator;
    QSignalSpy spy(&coordinator, &DapCoordinator::messageReceived);

    // Combine two consecutive messages into single buffer.
    QByteArray data = "Content-Length: 38\r\n\r\n{\"type\":\"event\",\"event\":\"initialized\"}Content-Length: 36\r\n\r\n{\"type\":\"response\",\"command\":\"next\"}";
    coordinator.processRawData(data);

    // Both messages should be parsed and emitted sequentially.
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 5000);
    QJsonObject msg1 = spy.takeFirst().at(0).toJsonObject();
    QCOMPARE(msg1["event"].toString(), QString("initialized"));

    QJsonObject msg2 = spy.takeFirst().at(0).toJsonObject();
    QCOMPARE(msg2["command"].toString(), QString("next"));
}

QTEST_GUILESS_MAIN(TestDapParser)

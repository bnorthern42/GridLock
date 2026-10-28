/**
 * @file test_obfuscator.cpp
 * @brief Implementation of telemetry obfuscator unit tests.
 *
 * Verifies that variable-length payload buffers pushed to TelemetryObfuscator are
 * serialized with an explicit size prefix and padded with zeroes up to MTU_THRESHOLD.
 */

#include "test_obfuscator.hpp"

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "../src/core/hpc/TelemetryObfuscator.hpp"

/**
 * @brief Tests padding of variable-length messages to MTU_THRESHOLD.
 */
void TestObfuscator::testConstantSizePadding() {
    gridlock::core::TelemetryObfuscator obfuscator;
    QSignalSpy spy(&obfuscator, &gridlock::core::TelemetryObfuscator::dataFlushed);

    // Push short sample payload.
    QByteArray data = "Hello World";
    obfuscator.pushData(data);

    // Wait for internal timer to fire and emit padded buffer.
    QVERIFY(spy.wait(100)); // Wait for timer to flush
    QCOMPARE(spy.count(), 1);

    // Verify output buffer matches exact MTU threshold.
    QList<QVariant> args = spy.takeFirst();
    QByteArray payload = args.at(0).toByteArray();
    QCOMPARE(payload.size(), static_cast<int>(gridlock::core::TelemetryObfuscator::MTU_THRESHOLD));

    // Decode length prefix and assert content identity.
    QDataStream in(&payload, QIODevice::ReadOnly);
    quint32 actualSize;
    in >> actualSize;
    QCOMPARE(actualSize, static_cast<quint32>(data.size()));

    QByteArray actualData;
    actualData.resize(actualSize);
    in.readRawData(actualData.data(), actualSize);
    QCOMPARE(actualData, data);
}

/**
 * @brief Tests dummy packet flushing when no telemetry data has been queued.
 */
void TestObfuscator::testEmptyFlush() {
    gridlock::core::TelemetryObfuscator obfuscator;
    QSignalSpy spy(&obfuscator, &gridlock::core::TelemetryObfuscator::dataFlushed);

    // Even without user data, timer should periodically flush empty frames.
    QVERIFY(spy.wait(100));
    QCOMPARE(spy.count(), 1); // Should flush even if empty

    // Output payload must still measure MTU_THRESHOLD bytes.
    QList<QVariant> args = spy.takeFirst();
    QByteArray payload = args.at(0).toByteArray();
    QCOMPARE(payload.size(), static_cast<int>(gridlock::core::TelemetryObfuscator::MTU_THRESHOLD));

    // Decoded size should be 0.
    QDataStream in(&payload, QIODevice::ReadOnly);
    quint32 actualSize;
    in >> actualSize;
    QCOMPARE(actualSize, 0u);
}

QTEST_MAIN(TestObfuscator)

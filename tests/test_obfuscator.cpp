#include "test_obfuscator.hpp"
#include <QtTest/QtTest>
#include "../src/core/hpc/TelemetryObfuscator.hpp"
#include <QSignalSpy>
#include <QDataStream>
#include <QIODevice>

void TestObfuscator::testConstantSizePadding() {
    gridlock::core::TelemetryObfuscator obfuscator;
    QSignalSpy spy(&obfuscator, &gridlock::core::TelemetryObfuscator::dataFlushed);

    QByteArray data = "Hello World";
    obfuscator.pushData(data);

    QVERIFY(spy.wait(100)); // Wait for timer to flush
    QCOMPARE(spy.count(), 1);

    QList<QVariant> args = spy.takeFirst();
    QByteArray payload = args.at(0).toByteArray();
    
    QCOMPARE(payload.size(), static_cast<int>(gridlock::core::TelemetryObfuscator::MTU_THRESHOLD));

    QDataStream in(&payload, QIODevice::ReadOnly);
    quint32 actualSize;
    in >> actualSize;
    QCOMPARE(actualSize, static_cast<quint32>(data.size()));
    
    QByteArray actualData;
    actualData.resize(actualSize);
    in.readRawData(actualData.data(), actualSize);
    QCOMPARE(actualData, data);
}

void TestObfuscator::testEmptyFlush() {
    gridlock::core::TelemetryObfuscator obfuscator;
    QSignalSpy spy(&obfuscator, &gridlock::core::TelemetryObfuscator::dataFlushed);

    QVERIFY(spy.wait(100));
    QCOMPARE(spy.count(), 1); // Should flush even if empty
    
    QList<QVariant> args = spy.takeFirst();
    QByteArray payload = args.at(0).toByteArray();
    
    QCOMPARE(payload.size(), static_cast<int>(gridlock::core::TelemetryObfuscator::MTU_THRESHOLD));
    
    QDataStream in(&payload, QIODevice::ReadOnly);
    quint32 actualSize;
    in >> actualSize;
    QCOMPARE(actualSize, 0u);
}

QTEST_MAIN(TestObfuscator)

#include "test_sanitizer.hpp"
#include <QtTest/QtTest>
#include "../src/core/hpc/MiSanitizer.hpp"
#include <string>

void TestSanitizer::testValidPayload() {
    std::string payload = "*stopped,reason=\"breakpoint-hit\"";
    auto result = gridlock::core::MiSanitizer::sanitize(payload);
    QCOMPARE(result.wasTruncated, false);
    QCOMPARE(result.dropped, false);
    QCOMPARE(result.depthExceeded, false);
    QCOMPARE(QString::fromStdString(result.sanitizedPayload), QString::fromStdString(payload));
}

void TestSanitizer::testDepthExceeded() {
    std::string payload = "";
    for (int i = 0; i < 33; ++i) {
        payload += "{";
    }
    auto result = gridlock::core::MiSanitizer::sanitize(payload);
    QCOMPARE(result.depthExceeded, true);
    QCOMPARE(result.dropped, true);
}

void TestSanitizer::testTruncation() {
    std::string payload(gridlock::core::MiSanitizer::MAX_PAYLOAD_BYTES + 10, 'a');
    auto result = gridlock::core::MiSanitizer::sanitize(payload);
    QCOMPARE(result.wasTruncated, true);
    QCOMPARE(result.dropped, false);
    QCOMPARE(result.sanitizedPayload.size(), gridlock::core::MiSanitizer::MAX_PAYLOAD_BYTES);
}

void TestSanitizer::testControlCharacterEscaping() {
    std::string payload = "test\x01\x02\n\r\t";
    auto result = gridlock::core::MiSanitizer::sanitize(payload);
    std::string expected = "test\\x01\\x02\n\r\t";
    QCOMPARE(QString::fromStdString(result.sanitizedPayload), QString::fromStdString(expected));
}

QTEST_MAIN(TestSanitizer)

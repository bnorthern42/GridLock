/**
 * @file test_sanitizer.cpp
 * @brief Implementation of GDB/MI output sanitization unit tests.
 *
 * Evaluates parser defensive bounds against malformed, overly nested, or
 * maliciously oversized debugger output records.
 */

#include "test_sanitizer.hpp"

#include <string>

#include <QtTest/QtTest>

#include "../src/core/hpc/MiSanitizer.hpp"

/**
 * @brief Tests that standard GDB/MI breakpoint records pass through unmodified.
 */
void TestSanitizer::testValidPayload() {
    std::string payload = "*stopped,reason=\"breakpoint-hit\"";
    // Sanitize valid record.
    auto result = gridlock::core::MiSanitizer::sanitize(payload);

    // Verify record is preserved intact without truncation or drops.
    QCOMPARE(result.wasTruncated, false);
    QCOMPARE(result.dropped, false);
    QCOMPARE(result.depthExceeded, false);
    QCOMPARE(QString::fromStdString(result.sanitizedPayload), QString::fromStdString(payload));
}

/**
 * @brief Tests dropping of heavily nested structures exceeding recursion limit.
 */
void TestSanitizer::testDepthExceeded() {
    std::string payload = "";
    // Exceed maximum allowed nesting depth of 32 opening braces.
    for (int i = 0; i < 33; ++i) {
        payload += "{";
    }

    // Process deeply nested string.
    auto result = gridlock::core::MiSanitizer::sanitize(payload);

    // Payload should be marked as exceeding depth and dropped.
    QCOMPARE(result.depthExceeded, true);
    QCOMPARE(result.dropped, true);
}

/**
 * @brief Tests truncation of payloads that exceed MAX_PAYLOAD_BYTES.
 */
void TestSanitizer::testTruncation() {
    // Construct payload larger than max allowed bytes.
    std::string payload(gridlock::core::MiSanitizer::MAX_PAYLOAD_BYTES + 10, 'a');

    // Run sanitizer on oversized buffer.
    auto result = gridlock::core::MiSanitizer::sanitize(payload);

    // Assert payload was clipped to MAX_PAYLOAD_BYTES without being dropped.
    QCOMPARE(result.wasTruncated, true);
    QCOMPARE(result.dropped, false);
    QCOMPARE(result.sanitizedPayload.size(), gridlock::core::MiSanitizer::MAX_PAYLOAD_BYTES);
}

/**
 * @brief Tests escaping of non-printable ASCII control characters.
 */
void TestSanitizer::testControlCharacterEscaping() {
    std::string payload = "test\x01\x02\n\r\t";

    // Run sanitizer to convert dangerous control characters to hexadecimal escapes.
    auto result = gridlock::core::MiSanitizer::sanitize(payload);

    // Assert \x01 and \x02 were converted to printable hex escape representations.
    std::string expected = "test\\x01\\x02\n\r\t";
    QCOMPARE(QString::fromStdString(result.sanitizedPayload), QString::fromStdString(expected));
}

QTEST_MAIN(TestSanitizer)

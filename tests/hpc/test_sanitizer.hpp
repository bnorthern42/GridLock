/**
 * @file test_sanitizer.hpp
 * @brief Unit tests for GDB/MI payload sanitization and defense-in-depth filters.
 *
 * Verifies payload size truncation limits, nesting recursion depth guards,
 * and ASCII control character escaping in incoming MI response records.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestSanitizer
 * @brief Test suite validating MiSanitizer security boundaries and validation rules.
 */
class TestSanitizer : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies passthrough of benign, well-formed MI records.
     */
    void testValidPayload();

    /**
     * @brief Verifies dropping payloads that exceed the maximum JSON/MI recursion depth.
     */
    void testDepthExceeded();

    /**
     * @brief Verifies truncation of oversized payloads exceeding byte limits.
     */
    void testTruncation();

    /**
     * @brief Verifies escaping of non-printable control characters.
     */
    void testControlCharacterEscaping();
};

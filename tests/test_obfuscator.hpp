/**
 * @file test_obfuscator.hpp
 * @brief Unit tests for HPC network telemetry padding and traffic obfuscation.
 *
 * Verifies constant MTU packet size normalization, length-prefixed serialization,
 * and periodic flush timers to counteract side-channel traffic analysis.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestObfuscator
 * @brief Test suite validating TelemetryObfuscator constant-size packet framing.
 */
class TestObfuscator : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies that variable-sized buffers are padded to constant MTU threshold.
     */
    void testConstantSizePadding();

    /**
     * @brief Verifies periodic flushing of empty or dummy packets to conceal activity bursts.
     */
    void testEmptyFlush();
};

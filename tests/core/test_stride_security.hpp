/**
 * @file test_stride_security.hpp
 * @brief Unit tests for memory bounds validation and stride overflow protection.
 *
 * Verifies arithmetic overflow checks, buffer size constraints, out-of-bounds
 * rendering guards, and null node handling in MemoryBoundsValidator.
 */

#pragma once

#include <QObject>
#include <QtTest/QtTest>

/**
 * @class TestStrideSecurity
 * @brief Test suite validating memory bounds checking algorithms and security limits.
 */
class TestStrideSecurity : public QObject {
    Q_OBJECT

private slots:
    /**
     * @brief Verifies that valid stride and offset within allocated range pass.
     */
    void testValidStride();

    /**
     * @brief Verifies rejection of stride/offset combinations exceeding buffer capacity.
     */
    void testOutOfBoundsStride();

    /**
     * @brief Verifies detection of integer overflow in stride offset arithmetic.
     */
    void testOverflowStride();

    /**
     * @brief Verifies isSafeToRender approval for valid VariableNode dimensions.
     */
    void testSafeToRenderValid();

    /**
     * @brief Verifies isSafeToRender denial when render count exceeds allocation.
     */
    void testSafeToRenderInvalid();

    /**
     * @brief Verifies overflow detection when computing baseAddress + offset.
     */
    void testBaseAddressOverflow();

    /**
     * @brief Verifies graceful rejection when passing a null VariableNode pointer.
     */
    void testNullNode();

    /**
     * @brief Verifies overflow detection during stride * renderCount multiplication.
     */
    void testRenderCountOverflow();

    /**
     * @brief Verifies edge case where requested range exactly equals buffer capacity.
     */
    void testExactBoundary();

    /**
     * @brief Verifies that zero-byte read requests within bounds are permitted.
     */
    void testZeroBytes();
};

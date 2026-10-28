/**
 * @file test_stride_security.cpp
 * @brief Implementation of MemoryBoundsValidator unit tests.
 *
 * Exercises defensive boundaries against buffer overruns, address wrap-around,
 * unsigned arithmetic overflow, and invalid memory visualization parameters.
 */

#include "test_stride_security.hpp"

#include <string>

#include <QTest>

#include "../src/core/hpc/MemoryBoundsValidator.hpp"

using namespace gridlock::hpc;

/**
 * @brief Tests validation of legitimate memory request within allocated boundaries.
 */
void TestStrideSecurity::testValidStride() {
    uintptr_t baseAddress = 0x1000;
    size_t totalAllocatedBytes = 10000;
    size_t requestedOffset = 100;
    size_t requestedBytes = 4000;
    std::string warning;

    // Validate request within capacity.
    bool result = MemoryBoundsValidator::validateBounds(baseAddress, totalAllocatedBytes, requestedOffset, requestedBytes, warning);
    QVERIFY(result);
    QVERIFY(warning.empty());
}

/**
 * @brief Tests rejection of requests where offset + size exceeds buffer capacity.
 */
void TestStrideSecurity::testOutOfBoundsStride() {
    uintptr_t baseAddress = 0x1000;
    size_t totalAllocatedBytes = 10000;
    size_t requestedOffset = 5000;
    size_t requestedBytes = 6000;
    std::string warning;

    // 5000 + 6000 = 11000 > 10000 -> Should fail.
    bool result = MemoryBoundsValidator::validateBounds(baseAddress, totalAllocatedBytes, requestedOffset, requestedBytes, warning);
    QVERIFY(!result);
    QVERIFY(warning.find("Memory bounds violation") != std::string::npos);
}

/**
 * @brief Tests rejection of requests causing integer overflow in offset addition.
 */
void TestStrideSecurity::testOverflowStride() {
    uintptr_t baseAddress = 0x1000;
    size_t totalAllocatedBytes = 10000;
    size_t requestedOffset = SIZE_MAX;
    size_t requestedBytes = 1;
    std::string warning;

    // Offset SIZE_MAX + 1 wraps around -> Should detect overflow.
    bool result = MemoryBoundsValidator::validateBounds(baseAddress, totalAllocatedBytes, requestedOffset, requestedBytes, warning);
    QVERIFY(!result);
    QVERIFY(warning.find("Integer overflow detected") != std::string::npos);
}

/**
 * @brief Tests isSafeToRender with valid VariableNode dimensions and stride.
 */
void TestStrideSecurity::testSafeToRenderValid() {
    gridlock::VariableNode node;
    node.memoryBaseAddress = 0x2000;
    node.memoryAllocatedBytes = 2000;

    size_t requestedStride = 8;
    size_t renderCount = 100; // 8 * 100 = 800 <= 2000
    std::string warning;

    bool result = MemoryBoundsValidator::isSafeToRender(&node, requestedStride, renderCount, warning);
    QVERIFY(result);
    QVERIFY(warning.empty());
}

/**
 * @brief Tests isSafeToRender rejection when requested stride exceeds allocation.
 */
void TestStrideSecurity::testSafeToRenderInvalid() {
    gridlock::VariableNode node;
    node.memoryBaseAddress = 0x2000;
    node.memoryAllocatedBytes = 2000;

    size_t requestedStride = 8;
    size_t renderCount = 300; // 8 * 300 = 2400 > 2000
    std::string warning;

    bool result = MemoryBoundsValidator::isSafeToRender(&node, requestedStride, renderCount, warning);
    QVERIFY(!result);
    QVERIFY(warning.find("Memory bounds violation") != std::string::npos);
}

/**
 * @brief Tests rejection when baseAddress + requestedOffset overflows pointer arithmetic.
 */
void TestStrideSecurity::testBaseAddressOverflow() {
    uintptr_t baseAddress = SIZE_MAX;
    size_t totalAllocatedBytes = 10000;
    size_t requestedOffset = 100;
    size_t requestedBytes = 1;
    std::string warning;

    bool result = MemoryBoundsValidator::validateBounds(baseAddress, totalAllocatedBytes, requestedOffset, requestedBytes, warning);
    QVERIFY(!result);
    QVERIFY(warning.find("absolute address calculation") != std::string::npos);
}

/**
 * @brief Tests isSafeToRender handling of null VariableNode pointers.
 */
void TestStrideSecurity::testNullNode() {
    size_t requestedStride = 8;
    size_t renderCount = 100;
    std::string warning;

    bool result = MemoryBoundsValidator::isSafeToRender(nullptr, requestedStride, renderCount, warning);
    QVERIFY(!result);
    QVERIFY(warning.find("VariableNode is null") != std::string::npos);
}

/**
 * @brief Tests multiplication overflow when computing requestedStride * renderCount.
 */
void TestStrideSecurity::testRenderCountOverflow() {
    gridlock::VariableNode node;
    node.memoryBaseAddress = 0x2000;
    node.memoryAllocatedBytes = 100000;

    size_t requestedStride = SIZE_MAX;
    size_t renderCount = 2;
    std::string warning;

    bool result = MemoryBoundsValidator::isSafeToRender(&node, requestedStride, renderCount, warning);
    QVERIFY(!result);
    QVERIFY(warning.find("requested bytes calculation") != std::string::npos);
}

/**
 * @brief Tests exact buffer boundary access (offset + bytes == total capacity).
 */
void TestStrideSecurity::testExactBoundary() {
    uintptr_t baseAddress = 0x1000;
    size_t totalAllocatedBytes = 10000;
    size_t requestedOffset = 5000;
    size_t requestedBytes = 5000; // 5000 + 5000 = 10000 == totalAllocatedBytes
    std::string warning;

    bool result = MemoryBoundsValidator::validateBounds(baseAddress, totalAllocatedBytes, requestedOffset, requestedBytes, warning);
    QVERIFY(result);
    QVERIFY(warning.empty());
}

/**
 * @brief Tests zero-byte read requests within allocated offset range.
 */
void TestStrideSecurity::testZeroBytes() {
    uintptr_t baseAddress = 0x1000;
    size_t totalAllocatedBytes = 10000;
    size_t requestedOffset = 1000;
    size_t requestedBytes = 0;
    std::string warning;

    bool result = MemoryBoundsValidator::validateBounds(baseAddress, totalAllocatedBytes, requestedOffset, requestedBytes, warning);
    QVERIFY(result);
    QVERIFY(warning.empty());
}

QTEST_GUILESS_MAIN(TestStrideSecurity)

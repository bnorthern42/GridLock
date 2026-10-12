#include "test_stride_security.hpp"
#include "../src/core/hpc/MemoryBoundsValidator.hpp"
#include <QTest>
#include <string>

using namespace gridlock::hpc;

void TestStrideSecurity::testValidStride() {
    uintptr_t baseAddress = 0x1000;
    size_t totalAllocatedBytes = 10000;
    size_t requestedOffset = 100;
    size_t requestedBytes = 4000;
    std::string warning;

    bool result = MemoryBoundsValidator::validateBounds(baseAddress, totalAllocatedBytes, requestedOffset, requestedBytes, warning);
    QVERIFY(result);
    QVERIFY(warning.empty());
}

void TestStrideSecurity::testOutOfBoundsStride() {
    uintptr_t baseAddress = 0x1000;
    size_t totalAllocatedBytes = 10000;
    size_t requestedOffset = 5000;
    size_t requestedBytes = 6000;
    std::string warning;

    bool result = MemoryBoundsValidator::validateBounds(baseAddress, totalAllocatedBytes, requestedOffset, requestedBytes, warning);
    QVERIFY(!result);
    QVERIFY(warning.find("Memory bounds violation") != std::string::npos);
}

void TestStrideSecurity::testOverflowStride() {
    uintptr_t baseAddress = 0x1000;
    size_t totalAllocatedBytes = 10000;
    size_t requestedOffset = SIZE_MAX;
    size_t requestedBytes = 1;
    std::string warning;

    bool result = MemoryBoundsValidator::validateBounds(baseAddress, totalAllocatedBytes, requestedOffset, requestedBytes, warning);
    QVERIFY(!result);
    QVERIFY(warning.find("Integer overflow detected") != std::string::npos);
}

void TestStrideSecurity::testSafeToRenderValid() {
    gridlock::VariableNode node;
    node.memoryBaseAddress = 0x2000;
    node.memoryAllocatedBytes = 2000;

    size_t requestedStride = 8;
    size_t renderCount = 100;
    std::string warning;

    bool result = MemoryBoundsValidator::isSafeToRender(&node, requestedStride, renderCount, warning);
    QVERIFY(result);
    QVERIFY(warning.empty());
}

void TestStrideSecurity::testSafeToRenderInvalid() {
    gridlock::VariableNode node;
    node.memoryBaseAddress = 0x2000;
    node.memoryAllocatedBytes = 2000;

    size_t requestedStride = 8;
    size_t renderCount = 300; // 2400 > 2000
    std::string warning;

    bool result = MemoryBoundsValidator::isSafeToRender(&node, requestedStride, renderCount, warning);
    QVERIFY(!result);
    QVERIFY(warning.find("Memory bounds violation") != std::string::npos);
}

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

void TestStrideSecurity::testNullNode() {
    size_t requestedStride = 8;
    size_t renderCount = 100;
    std::string warning;

    bool result = MemoryBoundsValidator::isSafeToRender(nullptr, requestedStride, renderCount, warning);
    QVERIFY(!result);
    QVERIFY(warning.find("VariableNode is null") != std::string::npos);
}

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

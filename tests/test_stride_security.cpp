/**
 * @file test_stride_security.cpp
 * @brief Implementation of MemoryBoundsValidator unit tests.
 *
 * Exercises defensive boundaries against buffer overruns, address wrap-around,
 * unsigned arithmetic overflow, and invalid memory visualization parameters.
 */

#include "test_stride_security.hpp"

#include <limits>
#include <string>

#include <QTest>

#include "../src/core/hpc/MemoryBoundsValidator.hpp"
#include "../src/core/utils/ElfBinaryInspector.cpp"

using namespace gridlock::hpc;
using namespace gridlock::core;

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

    // Reject single-read requests exceeding the maximum safe IPC cap (64 MiB).
    const size_t hugeAlloc = 256ULL * 1024ULL * 1024ULL;
    const size_t excessiveRead = 128ULL * 1024ULL * 1024ULL;
    warning.clear();
    QVERIFY(!MemoryBoundsValidator::validateBounds(baseAddress, hugeAlloc, 0, excessiveRead, warning));
    QVERIFY(!warning.empty());
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
 * @brief Tests isSafeToRender rejection when requested stride exceeds allocation,
 *        or when negative/zero strides or unaligned/null base addresses are used.
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

    // Reject zero stride.
    warning.clear();
    QVERIFY(!MemoryBoundsValidator::isSafeToRender(&node, 0, 10, warning));
    QVERIFY(!warning.empty());

    // Reject negative stride cast to size_t.
    warning.clear();
    const size_t negativeStride = static_cast<size_t>(static_cast<ptrdiff_t>(-8));
    QVERIFY(!MemoryBoundsValidator::isSafeToRender(&node, negativeStride, 1, warning));
    QVERIFY(!warning.empty());

    // Reject null/low guard page base addresses (0x0 to 0xfff).
    warning.clear();
    QVERIFY(!MemoryBoundsValidator::validateBounds(0x0, 4096, 0, 64, warning));
    warning.clear();
    QVERIFY(!MemoryBoundsValidator::validateBounds(0xfff, 4096, 0, 64, warning));

    // Reject unaligned base address for multi-byte stride rendering.
    gridlock::VariableNode unalignedNode;
    unalignedNode.memoryBaseAddress = 0x2003;
    unalignedNode.memoryAllocatedBytes = 4096;
    warning.clear();
    QVERIFY(!MemoryBoundsValidator::isSafeToRender(&unalignedNode, 8, 4, warning));
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
 * @brief Tests multiplication overflow when computing count * stride * elementSize
 *        and validates ElfBinaryInspector against truncated/corrupted ELF64 headers.
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

    // Verify count * stride * elementSize wrap-around rejection with std::numeric_limits<size_t>::max().
    gridlock::VariableNode typedNode;
    typedNode.type = QStringLiteral("double");
    typedNode.memoryBaseAddress = 0x4000;
    typedNode.memoryAllocatedBytes = std::numeric_limits<size_t>::max();
    const size_t wrapCount = (std::numeric_limits<size_t>::max() / sizeof(double)) + 1ULL;
    warning.clear();
    QVERIFY(!MemoryBoundsValidator::isSafeToRender(&typedNode, 1, wrapCount, warning));
    QVERIFY(!warning.empty());

    // Verify ElfBinaryInspector rejects truncated (< 64 bytes), bad magic, and corrupted section offsets.
    ElfBinaryInspector inspector;
    const QByteArray truncatedElf("\x7f""ELF\x02\x01\x01", 7);
    QVERIFY(!inspector.loadFromData(truncatedElf) && !inspector.isValid());

    QByteArray badMagicElf(sizeof(Elf64_Ehdr), '\0');
    badMagicElf[0] = 0x7f;
    badMagicElf[1] = 'B';
    badMagicElf[2] = 'A';
    badMagicElf[3] = 'D';
    QVERIFY(!inspector.loadFromData(badMagicElf) && !inspector.isValid());

    // Construct a 128-byte ELF64 image with a corrupted section header entry size / offset.
    QByteArray bogusShdrElf(128, '\0');
    auto* ehdr = reinterpret_cast<Elf64_Ehdr*>(bogusShdrElf.data());
    ehdr->e_ident[EI_MAG0] = ELFMAG0;
    ehdr->e_ident[EI_MAG1] = ELFMAG1;
    ehdr->e_ident[EI_MAG2] = ELFMAG2;
    ehdr->e_ident[EI_MAG3] = ELFMAG3;
    ehdr->e_ident[EI_CLASS] = ELFCLASS64;
    ehdr->e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr->e_ehsize = sizeof(Elf64_Ehdr);
    ehdr->e_shoff = sizeof(Elf64_Ehdr);
    ehdr->e_shentsize = 1; // Corrupted section header entry size
    ehdr->e_shnum = 2;
    QVERIFY(!inspector.loadFromData(bogusShdrElf) && !inspector.isValid());

    // Construct an ELF64 header with section header offset wrapping uint64_t arithmetic.
    ehdr->e_shentsize = sizeof(Elf64_Shdr);
    ehdr->e_shoff = std::numeric_limits<uint64_t>::max() - 32ULL;
    ehdr->e_shnum = 2;
    QVERIFY(!inspector.loadFromData(bogusShdrElf) && !inspector.isValid());
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

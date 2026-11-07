/**
 * @file MemoryBoundsValidator.cpp
 * @brief Implementation of overflow-checked memory bounds validation for HPC array inspection.
 */

#include "MemoryBoundsValidator.hpp"

#include <algorithm>
#include <sstream>

namespace gridlock {
namespace hpc {

/**
 * @brief Resolves element byte size from a VariableNode C/C++ type signature.
 *
 * Inspects the normalized type string reported by GDB/MI or LLDB-DAP to determine
 * the scalar element width used in `count * stride * elementSize` overflow checks.
 *
 * @param typeStr Type name reported by the debugger backend.
 * @return Inferred scalar element size in bytes (defaults to 1 when unspecified).
 */
static size_t inferElementSizeFromType(const QString& typeStr) {
  // Normalize whitespace and case before matching primitive C/C++ scalar types.
  const QString cleaned = typeStr.trimmed().toLower();
  if (cleaned.isEmpty()) {
    return 1;
  }
  // Check 64-bit floating-point and integer types (8 bytes).
  if (cleaned.contains(QStringLiteral("double")) ||
      cleaned.contains(QStringLiteral("int64")) ||
      cleaned.contains(QStringLiteral("uint64")) ||
      cleaned.contains(QStringLiteral("long long")) ||
      cleaned.contains(QStringLiteral("size_t")) ||
      cleaned.contains(QStringLiteral("uintptr_t"))) {
    return 8;
  }
  // Check 32-bit floating-point and integer types (4 bytes).
  if (cleaned.contains(QStringLiteral("float")) ||
      cleaned.contains(QStringLiteral("int32")) ||
      cleaned.contains(QStringLiteral("uint32")) ||
      cleaned == QStringLiteral("int") ||
      cleaned == QStringLiteral("unsigned int")) {
    return 4;
  }
  // Check 16-bit integer types (2 bytes).
  if (cleaned.contains(QStringLiteral("int16")) ||
      cleaned.contains(QStringLiteral("uint16")) ||
      cleaned.contains(QStringLiteral("short"))) {
    return 2;
  }
  // Default to 1-byte width for char, uint8_t, or untyped byte buffers.
  return 1;
}

/**
 * @brief Checks that `requestedOffset + requestedBytes` neither overflows nor exceeds @p totalAllocatedBytes.
 */
bool MemoryBoundsValidator::validateBounds(uintptr_t baseAddress,
                                           size_t totalAllocatedBytes,
                                           size_t requestedOffset,
                                           size_t requestedBytes,
                                           std::string& outWarning) {
  outWarning.clear();

  // Reject null or low guard-page virtual addresses (0x0..0xfff).
  if (baseAddress < MIN_VALID_BASE_ADDRESS) {
    outWarning =
        "Invalid base address: Null or low guard-page address (0x0..0xfff) rejected.";
    return false;
  }

  // Guard against `size_t` wrap-around when adding offset and length.
  size_t endOffset = 0;
  if (__builtin_add_overflow(requestedOffset, requestedBytes, &endOffset)) {
    outWarning = "Integer overflow detected during bounds calculation.";
    return false;
  }

  // Enforce strict upper-bound clamping before single process_vm_readv IPC calls.
  if (requestedBytes > MAX_SAFE_IPC_BYTES) {
    outWarning =
        "Memory bounds violation: Requested read exceeds maximum safe IPC cap.";
    return false;
  }

  // Reject reads that extend past the known allocation size.
  if (endOffset > totalAllocatedBytes) {
    std::ostringstream oss;
    oss << "Memory bounds violation: Request for " << requestedBytes
        << " bytes at offset " << requestedOffset
        << " exceeds total allocated size of " << totalAllocatedBytes
        << " bytes.";
    outWarning = oss.str();
    return false;
  }

  // Guard against `uintptr_t` wrap-around at the top of the virtual address space.
  uintptr_t endAddress = 0;
  if (__builtin_add_overflow(baseAddress, endOffset, &endAddress)) {
    outWarning =
        "Integer overflow detected during absolute address calculation.";
    return false;
  }

  return true;
}

/**
 * @brief Verifies that `renderCount * requestedStride * elementSize` fits inside @p node's allocated byte span.
 */
bool MemoryBoundsValidator::isSafeToRender(gridlock::VariableNode* node,
                                           size_t requestedStride,
                                           size_t renderCount,
                                           std::string& outWarning,
                                           size_t elementSize) {
  outWarning.clear();
  if (!node) {
    outWarning = "VariableNode is null.";
    return false;
  }

  // Reject zero stride before computing byte spans.
  if (requestedStride == 0) {
    outWarning = "Invalid zero stride rejected during requested bytes calculation.";
    return false;
  }

  // Compute stride * count using compiler intrinsic multiplication overflow detection.
  size_t strideTimesCount = 0;
  if (__builtin_mul_overflow(requestedStride, renderCount, &strideTimesCount)) {
    outWarning =
        "Integer overflow detected during requested bytes calculation (stride * count).";
    return false;
  }

  // Reject negative signed strides cast to size_t (e.g. when renderCount == 1).
  if (static_cast<ptrdiff_t>(requestedStride) <= 0) {
    outWarning =
        "Negative stride rejected during requested bytes calculation.";
    return false;
  }

  // Resolve effective element size and check count * stride * elementSize overflow.
  const size_t resolvedElementSize =
      (elementSize > 0) ? elementSize : inferElementSizeFromType(node->type);
  size_t requestedBytes = 0;
  if (__builtin_mul_overflow(strideTimesCount, resolvedElementSize,
                             &requestedBytes)) {
    outWarning =
        "Integer overflow detected during requested bytes calculation (count * stride * elementSize).";
    return false;
  }

  // Enforce natural pointer alignment for power-of-two strides/elements up to 8 bytes.
  const size_t alignReq = std::min<size_t>(
      8, std::max<size_t>(resolvedElementSize, requestedStride));
  if ((alignReq == 2 || alignReq == 4 || alignReq == 8) &&
      (node->memoryBaseAddress % alignReq != 0)) {
    outWarning = "Unaligned base address rejected for strided memory read.";
    return false;
  }

  return validateBounds(node->memoryBaseAddress, node->memoryAllocatedBytes, 0,
                        requestedBytes, outWarning);
}

}  // namespace hpc
}  // namespace gridlock

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
 * @brief Checks that `requestedOffset + requestedBytes` neither overflows nor exceeds @p totalAllocatedBytes.
 */
bool MemoryBoundsValidator::validateBounds(uintptr_t baseAddress,
                                           size_t totalAllocatedBytes,
                                           size_t requestedOffset,
                                           size_t requestedBytes,
                                           std::string& outWarning) {
  outWarning.clear();

  // Guard against `size_t` wrap-around when adding offset and length.
  size_t endOffset = 0;
  if (__builtin_add_overflow(requestedOffset, requestedBytes, &endOffset)) {
    outWarning = "Integer overflow detected during bounds calculation.";
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
 * @brief Verifies that `requestedStride * renderCount` fits inside @p node's allocated byte span.
 */
bool MemoryBoundsValidator::isSafeToRender(gridlock::VariableNode* node,
                                           size_t requestedStride,
                                           size_t renderCount,
                                           std::string& outWarning) {
  if (!node) {
    outWarning = "VariableNode is null.";
    return false;
  }

  // Compute total byte span using compiler intrinsic multiplication overflow detection.
  size_t requestedBytes = 0;
  if (__builtin_mul_overflow(requestedStride, renderCount, &requestedBytes)) {
    outWarning =
        "Integer overflow detected during requested bytes calculation (stride * count).";
    return false;
  }

  return validateBounds(node->memoryBaseAddress, node->memoryAllocatedBytes, 0,
                        requestedBytes, outWarning);
}

}  // namespace hpc
}  // namespace gridlock

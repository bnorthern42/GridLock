/**
 * @file MemoryBoundsValidator.hpp
 * @brief Overflow-safe memory bounds verification before remote array reads.
 *
 * Prevents out-of-bounds reads and `size_t` / `uintptr_t` arithmetic overflow
 * when slicing or visualizing user arrays in the Memory View.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "../models/VariableNode.hpp"

namespace gridlock {
namespace hpc {

/**
 * @struct MemoryBoundsValidator
 * @brief Static helper utilities that verify offset/stride/count requests against allocation bounds.
 */
struct MemoryBoundsValidator {
  /**
   * @brief Validates that `[requestedOffset, requestedOffset + requestedBytes)` lies within allocation bounds.
   */
  static bool validateBounds(uintptr_t baseAddress,
                             size_t totalAllocatedBytes,
                             size_t requestedOffset,
                             size_t requestedBytes,
                             std::string& outWarning);

  /**
   * @brief Validates whether rendering @p renderCount elements of @p requestedStride bytes from @p node is safe.
   */
  static bool isSafeToRender(gridlock::VariableNode* node,
                             size_t requestedStride,
                             size_t renderCount,
                             std::string& outWarning);
};

}  // namespace hpc
}  // namespace gridlock

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
  /// Minimum valid user-space virtual address (rejects null/guard page 0x0..0xfff).
  static constexpr uintptr_t MIN_VALID_BASE_ADDRESS = 0x1000;
  /// Maximum safe single-read IPC transfer size (64 MiB) before process_vm_readv.
  static constexpr size_t MAX_SAFE_IPC_BYTES = 64ULL * 1024ULL * 1024ULL;

  /**
   * @brief Validates that `[requestedOffset, requestedOffset + requestedBytes)` lies within allocation bounds.
   * @param baseAddress Target virtual memory base address (must be >= 0x1000).
   * @param totalAllocatedBytes Total known byte allocation at @p baseAddress.
   * @param requestedOffset Byte offset from @p baseAddress.
   * @param requestedBytes Number of bytes requested for single IPC read.
   * @param outWarning Populated with diagnostic reason when validation fails.
   * @return True if the read range is overflow-safe and within bounds; false otherwise.
   */
  static bool validateBounds(uintptr_t baseAddress,
                             size_t totalAllocatedBytes,
                             size_t requestedOffset,
                             size_t requestedBytes,
                             std::string& outWarning);

  /**
   * @brief Validates whether rendering @p renderCount elements of @p requestedStride bytes from @p node is safe.
   * @param node Target VariableNode carrying base address, allocation size, and type metadata.
   * @param requestedStride Stride in bytes (or element stride) between consecutive elements.
   * @param renderCount Number of elements to read and render.
   * @param outWarning Populated with diagnostic reason when validation fails.
   * @param elementSize Explicit element byte width (0 infers width from @p node->type, defaulting to 1).
   * @return True if `renderCount * requestedStride * elementSize` is safe to read; false otherwise.
   */
  static bool isSafeToRender(gridlock::VariableNode* node,
                             size_t requestedStride,
                             size_t renderCount,
                             std::string& outWarning,
                             size_t elementSize = 0);
};

}  // namespace hpc
}  // namespace gridlock

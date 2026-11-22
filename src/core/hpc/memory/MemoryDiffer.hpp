/**
 * @file MemoryDiffer.hpp
 * @brief Cross-rank memory comparison engine using `process_vm_readv`.
 *
 * Reads identical virtual address ranges from two MPI rank processes (baseline
 * vs. target) and computes a byte-level divergence mask for `MemView`.
 */

#pragma once

#include <sys/types.h>

#include <cstdint>
#include <vector>

namespace gridlock::core::hpc {

/**
 * @struct CompareResult
 * @brief Holds the raw buffers read from two ranks and their byte-by-byte inequality mask.
 */
struct CompareResult {
  std::vector<uint8_t> baselineBuffer; ///< Bytes read from the baseline rank process.
  std::vector<uint8_t> targetBuffer;   ///< Bytes read from the target rank process.
  std::vector<bool> diffMask;          ///< `true` at index `i` where `baselineBuffer[i] != targetBuffer[i]`.
  bool success{false};                 ///< `true` if both cross-process reads succeeded completely.
};

/**
 * @class MemoryDiffer
 * @brief Performs byte-level memory diffing between two local MPI rank PIDs.
 */
class MemoryDiffer {
 public:
  /**
   * @brief Reads @p length bytes at @p remoteAddress from @p baselinePid and @p targetPid and computes `diffMask`.
   */
  static CompareResult compareMemory(pid_t baselinePid,
                                     pid_t targetPid,
                                     void* remoteAddress,
                                     size_t length);
};

}  // namespace gridlock::core::hpc

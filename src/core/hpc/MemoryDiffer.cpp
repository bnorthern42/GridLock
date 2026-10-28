/**
 * @file MemoryDiffer.cpp
 * @brief Implementation of cross-rank memory comparison via Linux `process_vm_readv`.
 */

#include "MemoryDiffer.hpp"

#include <sys/uio.h>

#include <cerrno>

namespace gridlock::core::hpc {

/**
 * @brief Reads `[remoteAddress, remoteAddress + length)` from two PIDs and flags differing bytes.
 */
CompareResult MemoryDiffer::compareMemory(pid_t baselinePid,
                                          pid_t targetPid,
                                          void* remoteAddress,
                                          size_t length) {
  CompareResult result;
  if (length == 0) {
    return result;
  }

  // Allocate baseline, target, and boolean mask buffers of `length` elements.
  result.baselineBuffer.resize(length, 0);
  result.targetBuffer.resize(length, 0);
  result.diffMask.resize(length, false);

  // Set up local iovec descriptors for both destination buffers.
  struct iovec local_iov_baseline;
  local_iov_baseline.iov_base = result.baselineBuffer.data();
  local_iov_baseline.iov_len = length;

  struct iovec local_iov_target;
  local_iov_target.iov_base = result.targetBuffer.data();
  local_iov_target.iov_len = length;

  // Both ranks are queried at the same virtual address.
  struct iovec remote_iov;
  remote_iov.iov_base = remoteAddress;
  remote_iov.iov_len = length;

  // Read memory from both processes using Linux zero-copy cross-process I/O.
  const ssize_t bytes_read_baseline =
      process_vm_readv(baselinePid, &local_iov_baseline, 1, &remote_iov, 1, 0);
  const ssize_t bytes_read_target =
      process_vm_readv(targetPid, &local_iov_target, 1, &remote_iov, 1, 0);

  // Require full reads from both processes before marking the comparison valid.
  if (bytes_read_baseline == -1 || bytes_read_target == -1 ||
      static_cast<size_t>(bytes_read_baseline) != length ||
      static_cast<size_t>(bytes_read_target) != length) {
    result.success = false;
    return result;
  }

  // Mark every byte offset where the target rank diverges from the baseline rank.
  result.success = true;
  for (size_t i = 0; i < length; ++i) {
    if (result.baselineBuffer[i] != result.targetBuffer[i]) {
      result.diffMask[i] = true;
    }
  }

  return result;
}

}  // namespace gridlock::core::hpc

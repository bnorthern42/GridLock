/**
 * @file NativeMemoryReader.hpp
 * @brief Zero-copy cross-process memory reader using Linux `process_vm_readv`.
 *
 * Bypasses GDB/MI text serialization overhead when reading large contiguous
 * IEEE-754 `double` buffers from local MPI rank processes on Linux.
 */

#pragma once

#include <sys/types.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

/**
 * @class NativeMemoryReader
 * @brief Reads contiguous `double` arrays directly from a target process virtual address space.
 */
class NativeMemoryReader {
 public:
  /**
   * @brief Reads @p count `double` elements starting at @p baseAddress from @p targetPid.
   * @param targetPid OS process ID of the target MPI rank.
   * @param baseAddress Virtual memory address in the target process.
   * @param count Number of `double` elements to transfer.
   * @return Vector of doubles read, or an empty vector if `process_vm_readv` fails.
   */
  static std::vector<double> readDoubles(pid_t targetPid,
                                         uintptr_t baseAddress,
                                         size_t count);
};

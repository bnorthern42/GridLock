/**
 * @file NativeMemoryReader.cpp
 * @brief Implementation of Linux `process_vm_readv` cross-process memory transfers.
 */

#include "NativeMemoryReader.hpp"

#ifdef __linux__
#include <sys/uio.h>
#endif

#include <cerrno>
#include <cstring>

#include <QDebug>

/**
 * @brief Transfers @p count IEEE-754 doubles from @p targetPid at @p baseAddress.
 *
 * Constructs single-element local and remote `iovec` descriptors and invokes the
 * Linux `process_vm_readv` syscall to copy directly between virtual address spaces.
 */
std::vector<double> NativeMemoryReader::readDoubles(pid_t targetPid,
                                                    uintptr_t baseAddress,
                                                    size_t count) {
  std::vector<double> result;
  if (count == 0) {
    return result;
  }

  // Pre-allocate destination buffer for the kernel to populate directly.
  result.resize(count);

#ifdef __linux__
  // Describe the destination buffer in the debugger's own address space.
  struct iovec local[1];
  local[0].iov_base = result.data();
  local[0].iov_len = count * sizeof(double);

  // Describe the source buffer in the target MPI rank's address space.
  struct iovec remote[1];
  remote[0].iov_base = reinterpret_cast<void*>(baseAddress);
  remote[0].iov_len = count * sizeof(double);

  // Perform a single kernel-assisted cross-process memory copy.
  const ssize_t bytesRead = process_vm_readv(targetPid, local, 1, remote, 1, 0);

  if (bytesRead < 0) {
    qDebug() << "[NativeMemory] process_vm_readv failed. Errno:" << errno << "-"
             << strerror(errno);
    return std::vector<double>();
  }

  // Reject partial reads so callers never visualize half-populated arrays.
  if (static_cast<size_t>(bytesRead) != count * sizeof(double)) {
    qDebug() << "[NativeMemory] process_vm_readv partial read. Expected:"
             << (count * sizeof(double)) << "Got:" << bytesRead;
    return std::vector<double>();
  }
#else
  throw std::runtime_error("NativeMemoryReader is only supported on Linux");
#endif

  return result;
}

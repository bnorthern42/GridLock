#include "MemoryDiffer.hpp"
#include <sys/uio.h>
#include <cerrno>

namespace gridlock::core::hpc {

CompareResult MemoryDiffer::compareMemory(pid_t baselinePid, pid_t targetPid, void* remoteAddress, size_t length) {
    CompareResult result;
    if (length == 0) return result;

    result.baselineBuffer.resize(length, 0);
    result.targetBuffer.resize(length, 0);
    result.diffMask.resize(length, false);

    struct iovec local_iov_baseline;
    local_iov_baseline.iov_base = result.baselineBuffer.data();
    local_iov_baseline.iov_len = length;

    struct iovec local_iov_target;
    local_iov_target.iov_base = result.targetBuffer.data();
    local_iov_target.iov_len = length;

    struct iovec remote_iov;
    remote_iov.iov_base = remoteAddress;
    remote_iov.iov_len = length;

    ssize_t bytes_read_baseline = process_vm_readv(baselinePid, &local_iov_baseline, 1, &remote_iov, 1, 0);
    ssize_t bytes_read_target = process_vm_readv(targetPid, &local_iov_target, 1, &remote_iov, 1, 0);

    if (bytes_read_baseline == -1 || bytes_read_target == -1 || 
        static_cast<size_t>(bytes_read_baseline) != length || 
        static_cast<size_t>(bytes_read_target) != length) {
        result.success = false;
        return result;
    }

    result.success = true;
    for (size_t i = 0; i < length; ++i) {
        if (result.baselineBuffer[i] != result.targetBuffer[i]) {
            result.diffMask[i] = true;
        }
    }

    return result;
}

} // namespace gridlock::core::hpc

#pragma once

#include <vector>
#include <cstdint>
#include <sys/types.h>

namespace gridlock::core::hpc {

struct CompareResult {
    std::vector<uint8_t> baselineBuffer;
    std::vector<uint8_t> targetBuffer;
    std::vector<bool> diffMask;
    bool success{false};
};

class MemoryDiffer {
public:
    static CompareResult compareMemory(pid_t baselinePid, pid_t targetPid, void* remoteAddress, size_t length);
};

} // namespace gridlock::core::hpc

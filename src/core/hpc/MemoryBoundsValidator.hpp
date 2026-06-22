#pragma once
#include <string>
#include <cstddef>
#include <cstdint>
#include "../models/VariableNode.hpp"

namespace gridlock {
namespace hpc {

class MemoryBoundsValidator {
public:
    static bool validateBounds(uintptr_t baseAddress, size_t totalAllocatedBytes, size_t requestedOffset, size_t requestedBytes, std::string& outWarning);
    
    static bool isSafeToRender(gridlock::VariableNode* node, size_t requestedStride, size_t renderCount, std::string& outWarning);
};

} // namespace hpc
} // namespace gridlock

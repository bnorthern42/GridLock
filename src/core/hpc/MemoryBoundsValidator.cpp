#include "MemoryBoundsValidator.hpp"
#include <sstream>
#include <algorithm>

namespace gridlock {
namespace hpc {

bool MemoryBoundsValidator::validateBounds(uintptr_t baseAddress, size_t totalAllocatedBytes, size_t requestedOffset, size_t requestedBytes, std::string& outWarning) {
    outWarning.clear();
    
    size_t endOffset;
    if (__builtin_add_overflow(requestedOffset, requestedBytes, &endOffset)) {
        outWarning = "Integer overflow detected during bounds calculation.";
        return false;
    }

    if (endOffset > totalAllocatedBytes) {
        std::ostringstream oss;
        oss << "Memory bounds violation: Request for " << requestedBytes 
            << " bytes at offset " << requestedOffset 
            << " exceeds total allocated size of " << totalAllocatedBytes << " bytes.";
        outWarning = oss.str();
        return false;
    }

    uintptr_t endAddress;
    if (__builtin_add_overflow(baseAddress, endOffset, &endAddress)) {
        outWarning = "Integer overflow detected during absolute address calculation.";
        return false;
    }

    return true;
}

bool MemoryBoundsValidator::isSafeToRender(gridlock::VariableNode* node, size_t requestedStride, size_t renderCount, std::string& outWarning) {
    if (!node) {
        outWarning = "VariableNode is null.";
        return false;
    }

    size_t requestedBytes;
    if (__builtin_mul_overflow(requestedStride, renderCount, &requestedBytes)) {
        outWarning = "Integer overflow detected during requested bytes calculation (stride * count).";
        return false;
    }

    return validateBounds(node->memoryBaseAddress, node->memoryAllocatedBytes, 0, requestedBytes, outWarning);
}

} // namespace hpc
} // namespace gridlock

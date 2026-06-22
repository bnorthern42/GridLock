#pragma once

#include <vector>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <cstdint>

class MemoryAccessException : public std::runtime_error {
public:
    explicit MemoryAccessException(const std::string& message) : std::runtime_error(message) {}
};

class NativeMemoryReader {
public:
    static std::vector<double> readDoubles(pid_t targetPid, uintptr_t baseAddress, size_t count);
};

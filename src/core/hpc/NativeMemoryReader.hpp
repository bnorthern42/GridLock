#pragma once

#include <vector>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <cstdint>


class NativeMemoryReader {
public:
    static std::vector<double> readDoubles(pid_t targetPid, uintptr_t baseAddress, size_t count);
};

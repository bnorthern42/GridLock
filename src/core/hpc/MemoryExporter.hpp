#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace gridlock::core::hpc {

enum class DataType {
    Int32,
    Float,
    Double
};

class MemoryExporter {
public:
    static bool exportToCsv(const std::string& filePath, const std::vector<uint8_t>& buffer, int rows, int cols, DataType type);
    static bool exportToBinary(const std::string& filePath, const std::vector<uint8_t>& buffer);
};

} // namespace gridlock::core::hpc

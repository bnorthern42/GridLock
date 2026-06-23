#include "MemoryExporter.hpp"
#include <fstream>
#include <iomanip>
#include <cstring>

namespace gridlock::core::hpc {

bool MemoryExporter::exportToCsv(const std::string& filePath, const std::vector<uint8_t>& buffer, int rows, int cols, DataType type) {
    std::ofstream out(filePath);
    if (!out.is_open()) return false;

    size_t offset = 0;
    out << std::setprecision(9);

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (type == DataType::Int32) {
                if (offset + sizeof(int32_t) > buffer.size()) return false;
                int32_t val;
                std::memcpy(&val, buffer.data() + offset, sizeof(int32_t));
                out << val;
                offset += sizeof(int32_t);
            } else if (type == DataType::Float) {
                if (offset + sizeof(float) > buffer.size()) return false;
                float val;
                std::memcpy(&val, buffer.data() + offset, sizeof(float));
                out << val;
                offset += sizeof(float);
            } else if (type == DataType::Double) {
                if (offset + sizeof(double) > buffer.size()) return false;
                double val;
                std::memcpy(&val, buffer.data() + offset, sizeof(double));
                out << val;
                offset += sizeof(double);
            }
            
            if (c < cols - 1) {
                out << ",";
            }
        }
        out << "\n";
    }

    return true;
}

bool MemoryExporter::exportToBinary(const std::string& filePath, const std::vector<uint8_t>& buffer) {
    std::ofstream out(filePath, std::ios::binary);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    return out.good();
}

} // namespace gridlock::core::hpc

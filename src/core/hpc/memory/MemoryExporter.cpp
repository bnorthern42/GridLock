/**
 * @file MemoryExporter.cpp
 * @brief Implementation of CSV matrix and raw binary memory buffer exporters.
 */

#include "MemoryExporter.hpp"

#include <cstring>
#include <fstream>
#include <iomanip>

namespace gridlock::core::hpc {

/**
 * @brief Formats a contiguous byte buffer into a `rows x cols` CSV table.
 *
 * Uses `std::memcpy` for strict-aliasing-safe scalar extraction and returns
 * `false` immediately if the buffer is smaller than `rows * cols * sizeof(T)`.
 *
 * @param filePath Destination CSV file path.
 * @param buffer Raw target memory bytes in host endianness.
 * @param rows Number of matrix rows.
 * @param cols Number of matrix columns.
 * @param type Scalar element type (`Int32`, `Float`, or `Double`).
 * @return `true` if the full matrix was serialized, `false` on I/O or bounds error.
 */
bool MemoryExporter::exportToCsv(const std::string& filePath,
                                 const std::vector<uint8_t>& buffer,
                                 int rows,
                                 int cols,
                                 DataType type) {
  std::ofstream out(filePath);
  if (!out.is_open()) {
    return false;
  }

  size_t offset = 0;
  // Use 9 significant digits so IEEE-754 single-precision floats round-trip losslessly.
  out << std::setprecision(9);

  // Iterate row-major over the matrix and decode each scalar according to `type`.
  for (int r = 0; r < rows; ++r) {
    for (int c = 0; c < cols; ++c) {
      if (type == DataType::Int32) {
        if (offset + sizeof(int32_t) > buffer.size()) {
          return false;
        }
        int32_t val = 0;
        std::memcpy(&val, buffer.data() + offset, sizeof(int32_t));
        out << val;
        offset += sizeof(int32_t);
      } else if (type == DataType::Float) {
        if (offset + sizeof(float) > buffer.size()) {
          return false;
        }
        float val = 0.0f;
        std::memcpy(&val, buffer.data() + offset, sizeof(float));
        out << val;
        offset += sizeof(float);
      } else if (type == DataType::Double) {
        if (offset + sizeof(double) > buffer.size()) {
          return false;
        }
        double val = 0.0;
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

/**
 * @brief Dumps @p buffer directly to @p filePath as an unformatted binary stream.
 * @param filePath Destination `.bin` file path.
 * @param buffer Raw byte vector to write.
 * @return `true` if the binary stream write succeeded without error.
 */
bool MemoryExporter::exportToBinary(const std::string& filePath,
                                    const std::vector<uint8_t>& buffer) {
  std::ofstream out(filePath, std::ios::binary);
  if (!out.is_open()) {
    return false;
  }
  out.write(reinterpret_cast<const char*>(buffer.data()),
            static_cast<std::streamsize>(buffer.size()));
  return out.good();
}

}  // namespace gridlock::core::hpc

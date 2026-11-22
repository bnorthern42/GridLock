/**
 * @file MemoryExporter.hpp
 * @brief Exports raw target memory buffers to structured 2D CSV matrices or raw binary files.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gridlock::core::hpc {

/**
 * @enum DataType
 * @brief Element scalar type used when unpacking a raw byte buffer into CSV cells.
 */
enum class DataType {
  Int32,  ///< 32-bit signed two's-complement integer (`int32_t`).
  Float,  ///< 32-bit IEEE-754 single-precision float (`float`).
  Double  ///< 64-bit IEEE-754 double-precision float (`double`).
};

/**
 * @class MemoryExporter
 * @brief Serializes captured target memory buffers to disk for offline analysis.
 */
class MemoryExporter {
 public:
  /**
   * @brief Unpacks @p buffer as a `rows x cols` matrix of @p type and writes comma-separated values to @p filePath.
   */
  static bool exportToCsv(const std::string& filePath,
                          const std::vector<uint8_t>& buffer,
                          int rows,
                          int cols,
                          DataType type);

  /**
   * @brief Writes the raw byte vector @p buffer verbatim to @p filePath in binary mode.
   */
  static bool exportToBinary(const std::string& filePath,
                             const std::vector<uint8_t>& buffer);
};

}  // namespace gridlock::core::hpc

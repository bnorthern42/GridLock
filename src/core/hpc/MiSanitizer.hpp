/**
 * @file MiSanitizer.hpp
 * @brief Defensive sanitizer for untrusted or deeply nested GDB/MI output records.
 *
 * Enforces hard upper bounds on payload byte length (`MAX_PAYLOAD_BYTES`) and
 * tuple/list nesting depth (`MAX_MI_NESTING_DEPTH`), and hex-escapes raw control
 * characters before downstream MI parsing.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace gridlock::core {

/**
 * @class MiSanitizer
 * @brief Validates and scrubs raw GDB/MI response strings against parser resource exhaustion.
 */
class MiSanitizer {
 public:
  /// Maximum allowed `{` / `[` nesting depth before an MI packet is dropped.
  static constexpr size_t MAX_MI_NESTING_DEPTH = 32;
  /// Maximum allowed byte length of a single MI record before truncation.
  static constexpr size_t MAX_PAYLOAD_BYTES = 65536;

  /**
   * @struct SanitizationResult
   * @brief Outcome of sanitizing a raw GDB/MI line.
   */
  struct SanitizationResult {
    std::string sanitizedPayload; ///< Scrubbed MI payload safe for downstream parsing.
    bool wasTruncated;            ///< `true` if the input exceeded `MAX_PAYLOAD_BYTES`.
    bool dropped;                 ///< `true` if the packet was rejected entirely.
    bool depthExceeded;           ///< `true` if tuple/list nesting exceeded `MAX_MI_NESTING_DEPTH`.
  };

  /**
   * @brief Sanitizes @p payload, truncating oversized lines and rejecting excessive nesting.
   */
  static SanitizationResult sanitize(std::string_view payload);
};

}  // namespace gridlock::core

/**
 * @file MiSanitizer.cpp
 * @brief Implementation of GDB/MI payload truncation, nesting-depth enforcement, and control-char escaping.
 */

#include "MiSanitizer.hpp"

#include <iomanip>
#include <sstream>

namespace gridlock::core {

/**
 * @brief Scrubs a raw GDB/MI string_view against size, nesting, and control-character hazards.
 */
MiSanitizer::SanitizationResult MiSanitizer::sanitize(std::string_view payload) {
  SanitizationResult result{"", false, false, false};

  // Clamp oversized MI payloads to `MAX_PAYLOAD_BYTES` before scanning.
  if (payload.size() > MAX_PAYLOAD_BYTES) {
    payload = payload.substr(0, MAX_PAYLOAD_BYTES);
    result.wasTruncated = true;
  }

  size_t currentDepth = 0;
  std::ostringstream out;

  // Scan character-by-character to track bracket depth and escape control bytes.
  for (char c : payload) {
    if (c == '{' || c == '[') {
      currentDepth++;
      if (currentDepth > MAX_MI_NESTING_DEPTH) {
        result.depthExceeded = true;
        result.dropped = true;
        // Drop the entire packet immediately if structural nesting is excessive.
        return result;
      }
    } else if (c == '}' || c == ']') {
      if (currentDepth > 0) {
        currentDepth--;
      }
    }

    // Neutralize non-whitespace ASCII control characters (`0x00`..`0x1F`).
    if (static_cast<unsigned char>(c) <= 0x1F) {
      // Preserve tab, newline, and carriage return used in valid MI output.
      if (c == '\t' || c == '\n' || c == '\r') {
        out << c;
      } else {
        // Hex-escape any other control byte as `\xNN`.
        out << "\\x" << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(static_cast<unsigned char>(c));
      }
    } else {
      out << c;
    }
  }

  result.sanitizedPayload = out.str();
  return result;
}

}  // namespace gridlock::core

/**
 * @file MiSanitizer.cpp
 * @brief Implementation of GDB/MI payload truncation, nesting-depth enforcement, and control-char escaping.
 */

#include "MiSanitizer.hpp"

#include <iomanip>
#include <sstream>

namespace gridlock::core {

/**
 * @brief Scrubs a raw GDB/MI string_view against size, nesting, injection, and control-character hazards.
 *
 * Enforces `MAX_PAYLOAD_BYTES` length clamping, rejects embedded NUL bytes (`\0`),
 * rejects unquoted shell metacharacters, blocks multi-line command-chaining
 * injections (`\n-interpreter-exec ...`), and caps structural nesting depth at
 * `MAX_MI_NESTING_DEPTH`.
 *
 * @param payload Input GDB/MI record, watch expression, or binary path.
 * @return SanitizationResult containing the scrubbed string and diagnostic flags.
 */
MiSanitizer::SanitizationResult MiSanitizer::sanitize(std::string_view payload) {
  SanitizationResult result{"", false, false, false};

  // Clamp oversized MI payloads to `MAX_PAYLOAD_BYTES` before scanning.
  if (payload.size() > MAX_PAYLOAD_BYTES) {
    payload = payload.substr(0, MAX_PAYLOAD_BYTES);
    result.wasTruncated = true;
  }

  size_t currentDepth = 0;
  bool inQuotes = false;
  bool escaped = false;
  std::ostringstream out;

  // Scan character-by-character to track bracket depth, detect injections, and escape control bytes.
  for (size_t i = 0; i < payload.size(); ++i) {
    const char c = payload[i];

    // Reject embedded NUL bytes ('\0') immediately to prevent C-string truncation attacks.
    if (c == '\0') {
      result.dropped = true;
      result.sanitizedPayload.clear();
      return result;
    }

    // Track backslash escapes and double-quoted string literal boundaries.
    if (escaped) {
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      inQuotes = !inQuotes;
    }

    // Reject unquoted shell metacharacters in watch expressions or paths.
    if (!inQuotes && (c == ';' || c == '|' || c == '&' || c == '`' || c == '$')) {
      result.dropped = true;
      result.sanitizedPayload.clear();
      return result;
    }

    // Reject newline command-chaining injections (e.g. "\n-interpreter-exec console ...").
    if (c == '\n' || c == '\r') {
      size_t nextIdx = i + 1;
      // Skip trailing whitespace/newlines at the end of a legitimate single-line MI record.
      while (nextIdx < payload.size() &&
             (payload[nextIdx] == ' ' || payload[nextIdx] == '\t' ||
              payload[nextIdx] == '\r' || payload[nextIdx] == '\n')) {
        ++nextIdx;
      }
      // Any non-whitespace token after a newline indicates a chained command injection.
      if (nextIdx < payload.size()) {
        result.dropped = true;
        result.sanitizedPayload.clear();
        return result;
      }
    }

    // Track tuple `{}` and list `[]` structural nesting depth.
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

    // Neutralize non-whitespace ASCII control characters (`0x01`..`0x1F` and `0x7F`).
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc <= 0x1F || uc == 0x7F) {
      // Preserve trailing tab, newline, and carriage return used in valid MI output.
      if (c == '\t' || c == '\n' || c == '\r') {
        out << c;
      } else {
        // Hex-escape any other control byte as `\xNN`.
        out << "\\x" << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(uc);
      }
    } else {
      out << c;
    }
  }

  result.sanitizedPayload = out.str();
  return result;
}

/**
 * @brief Validates that a user watch expression or binary path contains no NULs,
 *        newlines, control characters, or shell metacharacters.
 * @param input User-supplied watch expression or binary path string.
 * @return True if safe for debugger evaluation or process invocation; false otherwise.
 */
bool MiSanitizer::isSafeUserInput(std::string_view input) {
  // Reject empty or oversized user inputs immediately.
  if (input.empty() || input.size() > MAX_PAYLOAD_BYTES) {
    return false;
  }
  // Reject any ASCII control character (including NUL, CR, LF) or shell metacharacter.
  for (char c : input) {
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc <= 0x1F || uc == 0x7F) {
      return false;
    }
    if (c == ';' || c == '|' || c == '&' || c == '`' || c == '$') {
      return false;
    }
  }
  // Delegate to primary sanitizer for structural nesting validation.
  const auto res = sanitize(input);
  return !res.dropped && !res.wasTruncated;
}

}  // namespace gridlock::core

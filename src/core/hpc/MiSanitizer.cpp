#include "MiSanitizer.hpp"
#include <sstream>
#include <iomanip>

namespace gridlock::core {

MiSanitizer::SanitizationResult MiSanitizer::sanitize(std::string_view payload) {
    SanitizationResult result{ "", false, false, false };

    if (payload.size() > MAX_PAYLOAD_BYTES) {
        payload = payload.substr(0, MAX_PAYLOAD_BYTES);
        result.wasTruncated = true;
    }

    size_t currentDepth = 0;
    std::ostringstream out;

    for (char c : payload) {
        if (c == '{' || c == '[') {
            currentDepth++;
            if (currentDepth > MAX_MI_NESTING_DEPTH) {
                result.depthExceeded = true;
                result.dropped = true;
                return result; // Drop the entire packet if depth exceeded
            }
        } else if (c == '}' || c == ']') {
            if (currentDepth > 0) {
                currentDepth--;
            }
        }

        // Neutralize unhandled control characters
        if ((unsigned char)c <= 0x1F) {
            // Allow tab, newline, carriage return, which are often used in valid payloads
            if (c == '\t' || c == '\n' || c == '\r') {
                out << c;
            } else {
                // Escape other control characters
                out << "\\x" << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)c;
            }
        } else {
            out << c;
        }
    }

    result.sanitizedPayload = out.str();
    return result;
}

} // namespace gridlock::core

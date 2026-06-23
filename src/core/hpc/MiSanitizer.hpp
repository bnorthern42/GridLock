#pragma once
#include <string>
#include <string_view>
#include <cstdint>

namespace gridlock::core {

class MiSanitizer {
public:
    static constexpr size_t MAX_MI_NESTING_DEPTH = 32;
    static constexpr size_t MAX_PAYLOAD_BYTES = 65536;

    struct SanitizationResult {
        std::string sanitizedPayload;
        bool wasTruncated;
        bool dropped;
        bool depthExceeded;
    };

    static SanitizationResult sanitize(std::string_view payload);
};

} // namespace gridlock::core

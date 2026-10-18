#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace nascent {
    struct CapturedFrame {
        int32_t width = 0;
        int32_t height = 0;
        std::vector<uint8_t> pixels;
    };

    std::optional<CapturedFrame> capture_displayed_frame();
}

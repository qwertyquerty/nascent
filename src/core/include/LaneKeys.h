#pragma once

#include <cstdint>

namespace nascent {
    constexpr bool get_lane_key(uint32_t keys, uint8_t lane) {
        return (keys & (1u << lane)) != 0;
    }
}

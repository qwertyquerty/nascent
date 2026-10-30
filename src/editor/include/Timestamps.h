#pragma once

#include "Hit.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nascent {
    struct Timestamp {
        int32_t time = 0;
        std::vector<std::pair<int32_t, uint8_t>> notes;
        size_t start = 0;
        size_t length = 0;
    };

    std::string format_clock(int32_t time);
    std::string format_timestamp(int32_t time, const std::vector<Hit>& notes);
    std::vector<Timestamp> find_timestamps(std::string_view text);
}

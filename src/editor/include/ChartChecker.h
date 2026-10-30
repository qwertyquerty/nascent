#pragma once

#include "ChartDocument.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    enum class CheckLevel : uint8_t {
        PROBLEM,
        WARNING,
        INFO,
        PASSED,
    };

    struct CheckFinding {
        CheckLevel level = CheckLevel::INFO;
        std::string title;
        std::string detail;
        std::vector<int32_t> times;
    };

    struct HandRatings {
        double left = 0;
        double right = 0;
        size_t left_notes = 0;
        size_t right_notes = 0;
    };

    struct ChartCheckInput {
        double audio_length = 0;
        std::filesystem::path folder;
        std::optional<HandRatings> hands;
        size_t merged_on_open = 0;
    };

    inline constexpr int32_t MIN_JACK_GAP_MS = 50;
    inline constexpr double SNAP_TOLERANCE_MS = 1.0;
    inline constexpr double HAND_IMBALANCE = 1.25;

    int hand_of(uint8_t key, uint8_t key_count);
    HandRatings rate_hands(const ChartData& data);
    std::vector<CheckFinding> check_chart(const ChartData& data, const ChartCheckInput& input);
}

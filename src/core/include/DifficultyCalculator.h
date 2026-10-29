#pragma once

#include "Chart.h"
#include "Hit.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace nascent {
    enum class DifficultySkill : uint8_t {
        STREAM,
        JUMPSTREAM,
        HANDSTREAM,
        STAMINA,
        JACKSPEED,
        CHORDJACK,
        TECHNICAL,
        LONG_NOTES,
    };

    inline constexpr std::array<std::string_view, 8> DIFFICULTY_SKILL_NAMES = {"stream", "jumpstream", "handstream", "stamina", "jackspeed", "chordjack", "technical", "long_notes"};
    inline constexpr size_t DIFFICULTY_SKILL_COUNT = DIFFICULTY_SKILL_NAMES.size();

    struct DifficultyBreakdown {
        double rating = 0.0;
        std::array<double, DIFFICULTY_SKILL_COUNT> mix = {};
        std::array<double, DIFFICULTY_SKILL_COUNT> ratings = {};
        double sustained_seconds = 0.0;
    };

    class DifficultyCalculator {
        public:
        static double calculate(const Chart& chart);
        static double calculate_at_pitch(const Chart& chart, float pitch);
        static double calculate(const std::vector<Hit>& hits, uint8_t key_count, const std::vector<ChartTimingPoint>& timing_points, uint8_t hit_accuracy);
        static DifficultyBreakdown breakdown(const Chart& chart);
        static DifficultyBreakdown breakdown_at_pitch(const Chart& chart, float pitch);
        static DifficultyBreakdown breakdown(const std::vector<Hit>& hits, uint8_t key_count, const std::vector<ChartTimingPoint>& timing_points, uint8_t hit_accuracy);
        static double strictness_factor(uint8_t hit_accuracy);
    };
}

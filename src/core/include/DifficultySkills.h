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
        CHORDSTREAM,
        JACK,
        CHORDJACK,
        VIBRO,
        LONG_NOTES,
        STAMINA,
        TECHNICAL,
    };

    inline constexpr std::array<std::string_view, 8> DIFFICULTY_SKILL_NAMES = {"stream", "chordstream", "jack", "chordjack", "vibro", "long_notes", "stamina", "technical"};
    inline constexpr size_t DIFFICULTY_SKILL_COUNT = DIFFICULTY_SKILL_NAMES.size();

    struct DifficultyBreakdown {
        double rating = 0.0;
        std::array<double, DIFFICULTY_SKILL_COUNT> mix = {};
        std::array<double, DIFFICULTY_SKILL_COUNT> ratings = {};
        double sustained_seconds = 0.0;
        double sustained_notes = 0.0;
    };

    class DifficultySkills {
        public:
        static DifficultyBreakdown breakdown(const Chart& chart);
        static DifficultyBreakdown breakdown_at_pitch(const Chart& chart, float pitch);
        static DifficultyBreakdown breakdown(const std::vector<Hit>& hits, uint8_t key_count, const std::vector<ChartTimingPoint>& timing_points, uint8_t hit_accuracy);
    };
}

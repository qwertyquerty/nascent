#pragma once

#include "Chart.h"
#include "Hit.h"

#include <cstdint>
#include <vector>

namespace nascent {
    class DifficultyCalculator {
        public:
        static double calculate(const Chart& chart);
        static double calculate_at_pitch(const Chart& chart, float pitch);
        static double calculate(const std::vector<Hit>& hits, uint8_t key_count, const std::vector<ChartTimingPoint>& timing_points, uint8_t hit_accuracy);
        static double strictness_factor(uint8_t hit_accuracy);
    };
}

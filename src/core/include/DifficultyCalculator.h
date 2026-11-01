#pragma once

#include "Chart.h"
#include "DifficultyEvents.h"
#include "Hit.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace nascent {
    struct PreparedDifficulty;

    struct DifficultyGroup {
        size_t first = 0;
        size_t last = 0;
        bool release = false;
    };

    class DifficultyAnalysis {
        std::shared_ptr<const PreparedDifficulty> prepared;
        friend class DifficultyCalculator;

        public:
        std::vector<DifficultyEvent> events;
        std::vector<DifficultyGroup> groups;
        std::vector<double> importance;
        double skill = 0.0;
        double rating = 0.0;
        size_t presses = 0;
        double strictness = 1.0;

        double part_skill(const std::vector<double>& shares) const;
        double displayed(double part) const;
    };

    class DifficultyCalculator {
        public:
        static double calculate(const Chart& chart);
        static double calculate_at_pitch(const Chart& chart, float pitch);
        static double calculate(const std::vector<Hit>& hits, uint8_t key_count, const std::vector<ChartTimingPoint>& timing_points, uint8_t hit_accuracy);
        static std::optional<DifficultyAnalysis> analyse(const std::vector<Hit>& hits, uint8_t key_count, const std::vector<ChartTimingPoint>& timing_points, uint8_t hit_accuracy);
        static double empty_rating(size_t presses, uint8_t hit_accuracy);
        static double strictness_factor(uint8_t hit_accuracy);
    };
}

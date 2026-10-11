#pragma once

#include "Chart.h"
#include "Hit.h"
#include "json.h"

#include <cstdint>
#include <vector>

namespace nascent {
    struct DifficultyParameters {
        double finger_power = 8.0;
        double hand_power = 2.158218;
        double finger_weight = 1.298431;
        double hand_weight = 0.320148;
        double onset_weight = 0.0;
        double chord_bonus = 0.0;
        double release_weight = 0.173369;
        double hold_busy_weight = 0.25906;
        double rhythm_weight = 0.0;
        double fatigue_time = 102.220816;
        double fatigue_blend = 0.002601;
        double aggregate_power = 10.073249;
        double hand_speed_weight = 0.123205;
        double hand_burst_weight = 0.0;
        double finger_speed_weight = 0.99962;
        double snap_weight = 0.0;
        double burst_weight = 0.060647;
        double variety_weight = 0.0;
        double tempo_weight = 0.002959;
        double hold_coverage_weight = 3.0;
        double rating_scale = 1.340184;
        double strictness_reference = 5.0;
        double strictness_exponent = 1.0;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
        DifficultyParameters,
        finger_power,
        hand_power,
        finger_weight,
        hand_weight,
        onset_weight,
        chord_bonus,
        release_weight,
        hold_busy_weight,
        rhythm_weight,
        fatigue_time,
        fatigue_blend,
        aggregate_power,
        hand_speed_weight,
        hand_burst_weight,
        finger_speed_weight,
        snap_weight,
        burst_weight,
        variety_weight,
        tempo_weight,
        hold_coverage_weight,
        rating_scale,
        strictness_reference,
        strictness_exponent
    )

    class DifficultyCalculator {
        public:
        static double calculate(const Chart& chart, const DifficultyParameters& parameters = DifficultyParameters());
        static double calculate_at_pitch(const Chart& chart, float pitch, const DifficultyParameters& parameters = DifficultyParameters());
        static double calculate(const std::vector<Hit>& hits, uint8_t key_count, const std::vector<ChartTimingPoint>& timing_points, uint8_t hit_accuracy, const DifficultyParameters& parameters = DifficultyParameters());
        static double strictness_factor(uint8_t hit_accuracy, const DifficultyParameters& parameters = DifficultyParameters());
    };
}

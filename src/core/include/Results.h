#pragma once

#include "Accuracy.h"
#include "Attempt.h"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace nascent {
    enum class Grade : uint8_t {
        X,
        S,
        A,
        B,
        C,
        D,
        E,
        F
    };

    inline constexpr double PERFORMANCE_ACCURACY_FLOOR = 0.80;
    inline constexpr double PERFORMANCE_ACCURACY_RANGE = 0.20;

    struct ErrorPoint {
        int32_t time = 0;
        int32_t error = 0;
        HitScore score = HitScore::NONE;
        bool release = false;
        bool played = false;
    };

    struct AttemptSummary {
        double accuracy = 1.0;
        Grade grade = Grade::X;
        uint32_t max_combo = 0;
        std::array<uint32_t, HIT_SCORE_COUNT> score_counts = {};
        double average_error = 0.0;
        double error_deviation = 0.0;
        uint8_t hit_accuracy = RANKED_HIT_ACC_DIFFICULTY;
        double difficulty = 0.0;
        double performance = 0.0;
        int32_t chart_start = 0;
        int32_t chart_end = 0;
        std::vector<ErrorPoint> errors;
    };

    Grade grade_for_accuracy(double accuracy);
    std::string_view grade_name(Grade grade);
    double performance_for(double difficulty, double accuracy);
    AttemptSummary summarize_attempt(const Attempt& attempt, double difficulty);
}

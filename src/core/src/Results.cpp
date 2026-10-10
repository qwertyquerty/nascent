#include "Results.h"

#include <algorithm>

namespace nascent {
    Grade grade_for_accuracy(double accuracy) {
        if (accuracy >= 1.0) {
            return Grade::X;
        }

        if (accuracy >= 0.98) {
            return Grade::S;
        }

        if (accuracy >= 0.95) {
            return Grade::A;
        }

        if (accuracy >= 0.90) {
            return Grade::B;
        }

        if (accuracy >= 0.85) {
            return Grade::C;
        }

        if (accuracy >= 0.80) {
            return Grade::D;
        }

        if (accuracy >= 0.75) {
            return Grade::E;
        }

        return Grade::F;
    }

    std::string_view grade_name(Grade grade) {
        switch (grade) {
            case Grade::X:
                return "X";
            case Grade::S:
                return "S";
            case Grade::A:
                return "A";
            case Grade::B:
                return "B";
            case Grade::C:
                return "C";
            case Grade::D:
                return "D";
            case Grade::E:
                return "E";
            default:
                return "F";
        }
    }

    double performance_for(double difficulty, double accuracy) {
        return difficulty * std::max(accuracy - PERFORMANCE_ACCURACY_FLOOR, 0.0) / PERFORMANCE_ACCURACY_RANGE;
    }

    AttemptSummary summarize_attempt(const Attempt& attempt, double difficulty) {
        AttemptSummary summary;
        summary.accuracy = attempt.get_accuracy();
        summary.grade = grade_for_accuracy(summary.accuracy);
        summary.max_combo = attempt.max_combo;
        summary.average_error = attempt.get_avg_err();
        summary.hit_accuracy = attempt.hit_accuracy;
        summary.difficulty = difficulty;
        summary.performance = performance_for(difficulty, summary.accuracy);
        summary.chart_start = attempt.chart->get_chart_start_ms();
        summary.chart_end = attempt.chart->get_chart_end_ms();

        for (const JudgedHit& jhit : attempt.judged_hits) {
            if (jhit.hit) {
                summary.score_counts[(size_t)jhit.hit_score]++;
                summary.errors.push_back({jhit.chart_hit.time, jhit.hit_err, jhit.hit_score, false, jhit.hit_played});
            }

            if (jhit.released) {
                summary.score_counts[(size_t)jhit.release_score]++;
                summary.errors.push_back({jhit.chart_hit.end_time, jhit.release_err, jhit.release_score, true, jhit.release_played});
            }
        }

        std::sort(summary.errors.begin(), summary.errors.end(), [](const ErrorPoint& a, const ErrorPoint& b) {
            return a.time < b.time;
        });

        return summary;
    }
}

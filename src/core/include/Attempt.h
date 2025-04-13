#pragma once

#include <vector>
#include <cstdint>

#include "Accuracy.h"
#include "Hit.h"
#include "Chart.h"

namespace nascent {
    struct JudgedHit {
        Hit chart_hit;
        Hit attempt_hit;
        HitScore score = HitScore::NONE;
        bool played = false;
        int32_t err = 0;
    };

    class Attempt {
        public:
        Chart* chart;

        std::vector<Hit> attempt_hits;
        std::vector<JudgedHit*> judged_hits;

        std::vector<Hit> current_holds;

        uint32_t n_scored_notes = 0;
        uint32_t total_score = 0;
        int32_t total_err = 0;

        uint32_t current_time = 0;
        uint32_t held_keys = 0;

        Attempt(Chart* chart);

        void update(int32_t current_time, uint32_t current_keys);
        void score_hit(JudgedHit* jhit, int32_t err, bool played);
        double get_accuracy();
        double get_avg_err();
    };
}

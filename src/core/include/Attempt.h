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

        bool hit = false;
        bool hit_played;
        int32_t hit_err = 0;
        HitScore hit_score = HitScore::NONE;

        bool released = false;
        bool release_played = false;
        int32_t release_err = 0;
        HitScore release_score = HitScore::NONE;
    };

    class Attempt {
        public:
        Chart* chart;

        std::vector<Hit> attempt_hits;
        std::vector<JudgedHit*> judged_hits;

        std::vector<std::pair<Hit, JudgedHit*>> current_holds;

        uint32_t n_scored_notes = 0;
        uint32_t total_score = 0;
        int32_t total_err = 0;
        
        uint32_t current_combo = 0;
        uint32_t max_combo = 0;

        uint32_t current_time = 0;
        uint32_t held_keys = 0;

        Attempt(Chart* chart);

        void update(int32_t current_time, uint32_t current_keys);
        void score_hit(JudgedHit* jhit, int32_t err, bool release = false, bool played = true);
        double get_accuracy();
        double get_avg_err();
        uint32_t get_combo();
    };
}

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
    };

    class Attempt {
        public:
        Chart* chart;

        std::vector<Hit> attempt_hits;
        std::vector<JudgedHit*> judged_hits;

        std::vector<Hit> current_holds;

        uint32_t current_time = 0;
        uint32_t held_keys = 0;

        Attempt(Chart* chart);

        void update(int32_t current_time, uint32_t current_keys);
    };
}

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "Accuracy.h"
#include "Hit.h"
#include "Chart.h"

namespace nascent {
    struct JudgedHit {
        Hit chart_hit;
        Hit attempt_hit;

        bool hit = false;
        bool hit_played = false;
        int32_t hit_err = 0;
        HitScore hit_score = HitScore::NONE;

        bool released = false;
        bool release_played = false;
        int32_t release_err = 0;
        HitScore release_score = HitScore::NONE;

        uint32_t index = 0;
    };

    struct AttemptInput {
        int32_t time = 0;
        uint8_t key = 0;
        bool pressed = false;
    };

    class Attempt {
        public:
        const Chart* chart;
        uint8_t hit_accuracy = RANKED_HIT_ACC_DIFFICULTY;

        std::vector<Hit> attempt_hits;
        std::vector<JudgedHit> judged_hits;
        uint32_t soonest_passed_hit_index = 0;
        uint32_t soonest_scored_hit_index = 0;

        std::optional<uint32_t> last_scored_index;

        std::vector<Hit> current_holds;
        std::vector<std::optional<uint32_t>> current_hold_targets;

        uint32_t n_scored_notes = 0;
        uint32_t total_score = 0;
        int64_t total_err = 0;
        int64_t total_squared_err = 0;
        uint32_t n_timed_hits = 0;

        uint32_t current_combo = 0;
        uint32_t max_combo = 0;

        uint32_t held_keys = 0;

        std::vector<AttemptInput> inputs;
        std::optional<int32_t> latest_time;

        Attempt(const Chart* chart, uint8_t hit_accuracy = RANKED_HIT_ACC_DIFFICULTY);

        void press(uint8_t key, int32_t time);
        void release(uint8_t key, int32_t time);
        void set_keys(uint32_t keys, int32_t time);
        void update(int32_t time);

        void score_hit(JudgedHit& jhit, int32_t err, bool release = false, bool played = true);
        const JudgedHit* get_last_scored() const;
        double get_accuracy() const;
        double get_avg_err() const;
        double get_err_deviation() const;
        uint32_t get_combo() const;

        private:
        void process_misses(int32_t time);
        void process_hold_overruns(int32_t time);
        void advance_passed_index();
    };
}

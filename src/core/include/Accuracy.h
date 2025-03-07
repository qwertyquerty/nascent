#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <map>

namespace nascent {
    enum HitScore {
        PERFECT,
        EXCELLENT,
        GREAT,
        GOOD,
        BAD,
        MISS,
        NONE
    };

    const std::map<HitScore, uint8_t> HIT_SCORE_ACC = {
        {PERFECT,   100},
        {EXCELLENT, 95},
        {GREAT,     75},
        {GOOD,      50},
        {BAD,       25},
        {MISS,      0}
    };

    const std::map<HitScore, uint8_t> HIT_SCORE_BASE_WINDOW = { // +/- ms
        {PERFECT,   10},
        {EXCELLENT, 20},
        {GREAT,     40},
        {GOOD,      60},
        {BAD,       80},
        {MISS,      100}
    };

    const uint8_t MIN_HIT_ACC_DIFFICULTY = 0;
    const uint8_t MAX_HIT_ACC_DIFFICULTY = 10;

    uint8_t get_window_from_hit_acc_diff(HitScore score, uint8_t hit_acc_diff);

    /**
     * Return HitScore from a hit error and an accuracy difficulty, returns HitScore::NONE if the note isn't hit at all (outside of miss range)
     */
    HitScore get_score_from_hit_err(uint32_t error, uint8_t hit_acc_diff);
}

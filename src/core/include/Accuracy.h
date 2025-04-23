#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <map>

namespace nascent {
    enum HitScore {
        PERFECT,
        AMAZING,
        GREAT,
        GOOD,
        BAD,
        MISS,
        NONE
    };

    const std::map<HitScore, std::string> HIT_SCORE_NAME = {
        {PERFECT,   "PERFECT"},
        {AMAZING,   "AMAZING"},
        {GREAT,     "GREAT"},
        {GOOD,      "GOOD"},
        {BAD,       "BAD"},
        {MISS,      "MISS"},
        {NONE,      "NONE"}
    };

    const std::map<HitScore, uint32_t> HIT_SCORE_COLOR = {
        {PERFECT,   0xff44ddff},
        {AMAZING,   0xff0099cc},
        {GREAT,     0xff33cc33},
        {GOOD,      0xffff6600},
        {BAD,       0xffcccccc},
        {MISS,      0xff0000ff},
        {NONE,      0x00000000}
    };

    const std::map<HitScore, uint8_t> HIT_SCORE_ACC = { // %
        {PERFECT,   100},
        {AMAZING,   95},
        {GREAT,     70},
        {GOOD,      40},
        {BAD,       10},
        {MISS,      0},
        {NONE,      0}
    };

    const std::map<HitScore, uint8_t> HIT_SCORE_BASE_WINDOW = { // +/- ms
        {PERFECT,   10},
        {AMAZING,   25},
        {GREAT,     50},
        {GOOD,      80},
        {BAD,       160}
    };

    const uint8_t MIN_HIT_ACC_DIFFICULTY = 0;
    const uint8_t MAX_HIT_ACC_DIFFICULTY = 10;
    const double HOLD_RELEASE_WINDOW_MULTIPLIER = 2;
    const uint8_t MAX_BAD_WINDOW_SIZE = 200;
    
    uint16_t get_window_from_hit_acc_diff(HitScore score, uint8_t hit_acc_diff, bool release = false);

    /**
     * Return HitScore from a hit error and an accuracy difficulty, returns HitScore::NONE if the note isn't hit at all (outside of miss range)
     */
    HitScore get_score_from_hit_err(uint32_t error, uint8_t hit_acc_diff, bool release = false);
}

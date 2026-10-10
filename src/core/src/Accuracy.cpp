#include "Accuracy.h"

#include <algorithm>

namespace nascent {
    uint16_t get_window_from_hit_acc_diff(HitScore score, uint8_t hit_acc_diff, bool release) {
        int32_t difficulty = std::clamp<int32_t>(hit_acc_diff, MIN_HIT_ACC_DIFFICULTY, MAX_HIT_ACC_DIFFICULTY);
        int32_t base_window = HIT_SCORE_BASE_WINDOWS.at((size_t)score);

        int32_t window = std::min<int32_t>(
            base_window * (MAX_HIT_ACC_DIFFICULTY + (MAX_HIT_ACC_DIFFICULTY - difficulty)) / MAX_HIT_ACC_DIFFICULTY,
            MAX_BAD_WINDOW_SIZE
        );

        return (uint16_t)(window * (release ? HOLD_RELEASE_WINDOW_MULTIPLIER : 1));
    }

    HitScore get_score_from_hit_err(uint32_t error, uint8_t hit_acc_diff, bool release) {
        for (HitScore score : TIMED_HIT_SCORES) {
            if (error <= get_window_from_hit_acc_diff(score, hit_acc_diff, release)) {
                return score;
            }
        }

        return HitScore::NONE;
    }
}

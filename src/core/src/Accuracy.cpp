#include "Accuracy.h"

namespace nascent{
    uint8_t get_window_from_hit_acc_diff(HitScore score, uint8_t hit_acc_diff, bool release) {
        return (HIT_SCORE_BASE_WINDOW.at(score) * (MAX_HIT_ACC_DIFFICULTY + (MAX_HIT_ACC_DIFFICULTY - hit_acc_diff))) / MAX_HIT_ACC_DIFFICULTY * (release ? HOLD_RELEASE_WINDOW_MULTIPLIER : 1);
    }

    /**
     * Return HitScore from a hit error and an accuracy difficulty, returns HitScore::NONE if the note isn't hit at all (outside of miss range)
     */
    HitScore get_score_from_hit_err(uint32_t error, uint8_t hit_acc_diff, bool release) {
        for (HitScore score = HitScore::PERFECT; score < HitScore::NONE; score = (HitScore)(score+1)) {
            if (error <= get_window_from_hit_acc_diff(score, hit_acc_diff, release)) {
                return score;
            }
        }

        return HitScore::NONE;
    }
}

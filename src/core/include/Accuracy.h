#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nascent {
    enum class HitScore : uint8_t {
        PERFECT,
        AMAZING,
        GREAT,
        GOOD,
        BAD,
        MISS,
        NONE
    };

    inline constexpr size_t HIT_SCORE_COUNT = 7;

    inline constexpr std::array<HitScore, HIT_SCORE_COUNT> ALL_HIT_SCORES = {
        HitScore::PERFECT, HitScore::AMAZING, HitScore::GREAT, HitScore::GOOD, HitScore::BAD, HitScore::MISS, HitScore::NONE
    };

    inline constexpr std::array<HitScore, 5> TIMED_HIT_SCORES = {
        HitScore::PERFECT, HitScore::AMAZING, HitScore::GREAT, HitScore::GOOD, HitScore::BAD
    };

    inline constexpr std::array<std::string_view, HIT_SCORE_COUNT> HIT_SCORE_NAMES = {
        "PERFECT", "AMAZING", "GREAT", "GOOD", "BAD", "MISS", "NONE"
    };

    inline constexpr std::array<uint32_t, HIT_SCORE_COUNT> HIT_SCORE_COLORS = {
        0xff44ddff, 0xff0099cc, 0xff33cc33, 0xffff6600, 0xffcccccc, 0xff0000ff, 0x00000000
    };

    inline constexpr std::array<uint8_t, HIT_SCORE_COUNT> HIT_SCORE_ACCS = {
        100, 95, 70, 40, 10, 0, 0 // %
    };

    inline constexpr std::array<uint8_t, TIMED_HIT_SCORES.size()> HIT_SCORE_BASE_WINDOWS = {
        10, 25, 50, 80, 160 // +/- ms
    };

    constexpr std::string_view hit_score_name(HitScore score) {
        return HIT_SCORE_NAMES[(size_t)score];
    }

    constexpr uint32_t hit_score_color(HitScore score) {
        return HIT_SCORE_COLORS[(size_t)score];
    }

    constexpr uint8_t hit_score_acc(HitScore score) {
        return HIT_SCORE_ACCS[(size_t)score];
    }

    inline constexpr uint8_t MIN_HIT_ACC_DIFFICULTY = 0;
    inline constexpr uint8_t MAX_HIT_ACC_DIFFICULTY = 10;
    inline constexpr uint8_t RANKED_HIT_ACC_DIFFICULTY = 5;
    inline constexpr double HOLD_RELEASE_WINDOW_MULTIPLIER = 2;
    inline constexpr uint8_t MAX_BAD_WINDOW_SIZE = 200;

    uint16_t get_window_from_hit_acc_diff(HitScore score, uint8_t hit_acc_diff, bool release = false);

    /**
     * Return HitScore from a hit error and an accuracy difficulty, returns HitScore::NONE if the note isn't hit at all (outside of miss range)
     */
    HitScore get_score_from_hit_err(uint32_t error, uint8_t hit_acc_diff, bool release);
}

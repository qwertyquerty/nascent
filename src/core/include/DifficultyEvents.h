#pragma once

#include "Chart.h"
#include "Hit.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace nascent {
    enum class EventChannel : uint8_t {
        TIME,
        COLUMN,
        HAND,
        RELEASE,
        HOLD,
        HOLD_LENGTH,
        COLUMN_GAP,
        PREVIOUS_COLUMN_GAP,
        HAND_GAP,
        PREVIOUS_HAND_GAP,
        ONSET_GAP,
        PREVIOUS_ONSET_GAP,
        CHORD_SIZE,
        HAND_CHORD_SIZE,
        CHORD_OVERLAP,
        HAND_SWITCH,
        OTHER_HAND_GAP,
        SNAP,
        HELD_SAME_HAND,
        HELD_TOTAL,
        NEAREST_HAND_EVENT,
        KEY_COUNT,
        ROLL_EVENNESS,
        PATTERN_VARIETY,
        JACK_RUN,
        FLAM_GAP,
        TRILL,
        ANCHOR,
        BURST,
        CHORD_SPREAD,
    };

    inline constexpr std::array<std::string_view, 30> EVENT_CHANNEL_NAMES = {
        "time",
        "column",
        "hand",
        "release",
        "hold",
        "hold_length",
        "column_gap",
        "previous_column_gap",
        "hand_gap",
        "previous_hand_gap",
        "onset_gap",
        "previous_onset_gap",
        "chord_size",
        "hand_chord_size",
        "chord_overlap",
        "hand_switch",
        "other_hand_gap",
        "snap",
        "held_same_hand",
        "held_total",
        "nearest_hand_event",
        "key_count",
        "roll_evenness",
        "pattern_variety",
        "jack_run",
        "flam_gap",
        "trill",
        "anchor",
        "burst",
        "chord_spread",
    };

    inline constexpr size_t EVENT_CHANNEL_COUNT = EVENT_CHANNEL_NAMES.size();
    inline constexpr double NO_PREVIOUS_GAP = 10000.0;
    inline constexpr size_t ROLL_WINDOW = 8;
    inline constexpr size_t ROLL_MIN_NOTES = 4;
    inline constexpr double ROLL_CV_LIMIT = 0.2;
    inline constexpr double ROLL_BREAK_RATIO = 1.5;
    inline constexpr size_t VARIETY_WINDOW = 16;
    inline constexpr size_t VARIETY_MIN_ONSETS = 5;
    inline constexpr double CHORD_MERGED_MS = 15.0;
    inline constexpr double CHORD_SEPARATE_MS = 30.0;
    inline constexpr double ACTIVE_FULL_GAP_MS = 500.0;
    inline constexpr double ACTIVE_REST_GAP_MS = 2000.0;
    inline constexpr size_t ALIGN_MIN_NOTES = 16;
    inline constexpr size_t ALIGN_CANDIDATES = 48;
    inline constexpr double ALIGN_MIN_GAIN = 0.25;
    inline constexpr double UNTIMED_SNAP = 0.85;
    inline constexpr double JACK_RUN_RATIO = 1.5;
    inline constexpr double TRILL_MAX_GAP_MS = 300.0;
    inline constexpr size_t ANCHOR_WINDOW = 8;
    inline constexpr double BURST_WINDOW_MS = 2500.0;
    inline constexpr double HOLD_BUSY_MIN_MS = 30.0;
    inline constexpr double HOLD_BUSY_FULL_MS = 150.0;

    using DifficultyEvent = std::array<double, EVENT_CHANNEL_COUNT>;

    uint8_t difficulty_hand(uint8_t column, uint8_t key_count);
    double grid_snap(double time, double origin, double beat_length);
    double snap_complexity(double time, const std::vector<ChartTimingPoint>& timing_points);
    std::vector<ChartTimingPoint> align_timing(const std::vector<ChartTimingPoint>& timing_points, const std::vector<Hit>& hits);
    double roll_evenness(const std::vector<std::pair<uint8_t, double>>& window, uint8_t key_count);
    double pattern_variety(const std::vector<uint32_t>& masks);
    std::vector<DifficultyEvent> extract_difficulty_events(const std::vector<Hit>& hits, uint8_t key_count, const std::vector<ChartTimingPoint>& timing_points);
    double active_time_ms(const std::vector<Hit>& hits);
}

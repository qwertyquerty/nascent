#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nascent {
    enum class PatternFamily {
        STREAM,
        SINGLE_TRILL,
        SPLIT_TRILL,
        JACKS,
    };

    enum class StreamFlow {
        RANDOM,
        ROLL,
        ZIGZAG,
    };

    enum class StreamChords {
        NONE,
        JUMPS,
        HANDS,
        QUADS,
        CUSTOM,
    };

    enum class SplitStyle {
        ALTERNATING,
        HALVES,
        INNER_OUTER,
    };

    struct PatternVariant {
        const char* name;
        PatternFamily family;
    };

    struct PatternStyle {
        const char* name;
        std::vector<PatternVariant> variants;
    };

    struct PatternSettings {
        uint8_t first_lane = 0;
        uint8_t last_lane = 255;
        StreamFlow flow = StreamFlow::RANDOM;
        int disconnect_chance = 50;
        int trill_chance = 0;
        bool reverse = false;
        StreamChords chords = StreamChords::NONE;
        int chord_every = 2;
        int chord_size = 2;
        bool jumps_between = false;
        int jack_chance = 0;
        bool anchor = false;
        uint8_t anchor_lane = 0;
        int anchor_every = 2;
        bool anchor_alone = true;
        uint8_t lane_a = 0;
        uint8_t lane_b = 1;
        SplitStyle split = SplitStyle::ALTERNATING;
        int density = 50;
        bool vary_sizes = true;
        int kept_lanes = 1;
    };

    using PatternRow = std::vector<uint8_t>;

    const std::vector<PatternStyle>& pattern_styles();
    size_t stream_chord_size(const PatternSettings& settings);
    std::vector<PatternRow> generate_pattern(PatternFamily family, uint8_t keys, size_t rows, uint32_t seed, const PatternSettings& settings = {});
}

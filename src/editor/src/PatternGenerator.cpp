#include "PatternGenerator.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace nascent {
    namespace {
        bool contains(const PatternRow& row, int column) {
            return std::find(row.begin(), row.end(), column) != row.end();
        }

        bool chance(int percent, std::mt19937& rng) {
            return std::uniform_int_distribution<int>(0, 99)(rng) < percent;
        }

        template <typename T>
        T pick(const std::vector<T>& options, std::mt19937& rng) {
            return options[std::uniform_int_distribution<size_t>(0, options.size() - 1)(rng)];
        }

        PatternRow chord(const PatternRow& previous, size_t size, size_t shared, uint8_t keys, std::mt19937& rng) {
            size = std::clamp<size_t>(size, 1, keys);
            PatternRow kept;
            PatternRow fresh;

            for (uint8_t column = 0; column < keys; column++) {
                (contains(previous, column) ? kept : fresh).push_back(column);
            }

            std::shuffle(kept.begin(), kept.end(), rng);
            std::shuffle(fresh.begin(), fresh.end(), rng);
            size_t from_previous = std::min(kept.size(), std::max(std::min(shared, size), size > fresh.size() ? size - fresh.size() : 0));
            PatternRow row(kept.begin(), kept.begin() + (std::ptrdiff_t)from_previous);

            for (size_t index = 0; row.size() < size && index < fresh.size(); index++) {
                row.push_back(fresh[index]);
            }

            std::sort(row.begin(), row.end());
            return row;
        }

        uint8_t next_single(const PatternRow& previous, int last, int before, uint8_t keys, const PatternSettings& settings, std::mt19937& rng) {
            if (keys < 2) {
                return 0;
            }

            if (before >= 0 && before != last && !contains(previous, before) && chance(settings.trill_chance, rng)) {
                return (uint8_t)before;
            }

            bool connected = !chance(settings.disconnect_chance, rng);
            std::vector<uint8_t> options;

            bool strict = settings.disconnect_chance <= 0 || settings.disconnect_chance >= 100;

            for (int pass = 0; pass < 5 && options.empty(); pass++) {
                for (int column = 0; column < keys; column++) {
                    bool free = !contains(previous, column) && column != last;
                    bool wanted = last < 0 || (std::abs(column - last) == 1) == connected;
                    bool fresh = column != before;
                    bool accepted[] = {free && wanted && fresh, free && (strict ? wanted : fresh), free && (strict ? fresh : wanted), free, column != last};

                    if (accepted[pass]) {
                        options.push_back((uint8_t)column);
                    }
                }
            }

            return pick(options, rng);
        }

        uint8_t flowing(size_t step, uint8_t keys, const PatternSettings& settings) {
            uint8_t column = 0;

            if (settings.flow == StreamFlow::ROLL) {
                column = (uint8_t)(step % keys);
            }
            else if (keys > 1) {
                size_t period = (size_t)keys * 2 - 2;
                size_t place = step % period;
                column = (uint8_t)(place < keys ? place : period - place);
            }

            return settings.reverse ? (uint8_t)(keys - 1 - column) : column;
        }

        std::vector<PatternRow> stream_rows(uint8_t keys, size_t rows, int anchor, size_t anchor_every, const PatternSettings& settings, std::mt19937& rng) {
            std::vector<PatternRow> result;
            PatternRow previous;
            int last = -1;
            int before = -1;
            bool jacked = false;
            size_t flow_step = 0;
            size_t every = (size_t)std::max(1, settings.chord_every);
            size_t chord_size = std::min<size_t>(stream_chord_size(settings), keys);

            for (size_t row = 0; row < rows; row++) {
                if (anchor >= 0 && row % anchor_every == 0) {
                    jacked = contains(previous, anchor);
                    previous = {(uint8_t)anchor};
                    before = last;
                    last = anchor;
                    result.push_back(previous);
                    continue;
                }

                PatternRow blocked = previous;

                if (anchor >= 0 && (row + 1) % anchor_every == 0) {
                    blocked.push_back((uint8_t)anchor);
                }

                bool jack = !previous.empty() && !jacked && chance(settings.jack_chance, rng);
                size_t step = row % every;
                size_t size = chord_size > 1 && step == 0 ? chord_size : settings.jumps_between && chord_size > 2 && every >= 4 && step == every / 2 ? 2 : 1;

                if (size > 1) {
                    previous = jack ? chord(previous, size, 1, keys, rng) : chord(blocked, size, 0, keys, rng);
                    before = last;
                    last = -1;
                }
                else {
                    uint8_t column = 0;

                    if (jack) {
                        column = pick(previous, rng);
                    }
                    else if (settings.flow == StreamFlow::RANDOM) {
                        column = next_single(blocked, last, before, keys, settings, rng);
                    }
                    else {
                        column = flowing(flow_step++, keys, settings);

                        for (uint8_t attempt = 1; attempt < keys && contains(blocked, column); attempt++) {
                            column = flowing(flow_step++, keys, settings);
                        }
                    }

                    before = last;
                    last = column;
                    previous = {column};
                }

                jacked = jack;
                result.push_back(previous);
            }

            return result;
        }

        std::vector<PatternRow> stream(uint8_t keys, size_t rows, uint8_t anchor, const PatternSettings& settings, std::mt19937& rng) {
            size_t every = (size_t)std::max(1, settings.anchor_every);

            if (!settings.anchor) {
                return stream_rows(keys, rows, -1, 1, settings, rng);
            }

            if (!settings.anchor_alone || keys < 2) {
                return stream_rows(keys, rows, anchor, every, settings, rng);
            }

            size_t anchors = (rows + every - 1) / every;
            std::vector<PatternRow> rest = stream_rows((uint8_t)(keys - 1), rows - anchors, -1, 1, settings, rng);
            std::vector<PatternRow> result;
            size_t next = 0;

            for (size_t row = 0; row < rows; row++) {
                if (row % every == 0) {
                    result.push_back({anchor});
                    continue;
                }

                PatternRow notes = rest[next++];

                for (uint8_t& column : notes) {
                    column = column >= anchor ? (uint8_t)(column + 1) : column;
                }

                result.push_back(notes);
            }

            return result;
        }

        std::vector<PatternRow> split_trill(uint8_t keys, size_t rows, const PatternSettings& settings) {
            PatternRow first;
            PatternRow second;

            for (SplitStyle split : {settings.split, SplitStyle::ALTERNATING}) {
                first.clear();
                second.clear();

                for (uint8_t column = 0; column < keys; column++) {
                    int edge = std::min<int>(column, keys - 1 - column);
                    bool grouped = split == SplitStyle::ALTERNATING ? column % 2 == 0 : split == SplitStyle::HALVES ? column < (keys + 1) / 2 : edge * 2 < keys / 2;
                    (grouped ? first : second).push_back(column);
                }

                if (!first.empty() && !second.empty()) {
                    break;
                }
            }

            if (first.empty()) {
                std::swap(first, second);
            }

            if (settings.reverse && !second.empty()) {
                std::swap(first, second);
            }

            std::vector<PatternRow> result;

            for (size_t row = 0; row < rows; row++) {
                result.push_back(row % 2 == 1 && !second.empty() ? second : first);
            }

            return result;
        }

        std::vector<PatternRow> jacks(uint8_t keys, size_t rows, const PatternSettings& settings, std::mt19937& rng) {
            std::vector<PatternRow> result;
            PatternRow previous;
            int base = std::clamp((int)std::lround(settings.density / 100.0 * keys), 1, (int)keys);

            for (size_t row = 0; row < rows; row++) {
                int size = settings.vary_sizes && keys > 1 ? std::clamp(base + std::uniform_int_distribution<int>(-1, 1)(rng), 1, (int)keys) : base;
                size_t kept = row == 0 ? 0 : (size_t)std::clamp(settings.kept_lanes, 1, size);
                previous = chord(previous, (size_t)size, kept, keys, rng);
                result.push_back(previous);
            }

            return result;
        }
    }

    const std::vector<PatternStyle>& pattern_styles() {
        static const std::vector<PatternStyle> styles = {
            {"Stream", {
                {"Stream", PatternFamily::STREAM},
            }},
            {"Trill", {
                {"Single trill", PatternFamily::SINGLE_TRILL},
                {"Split trill", PatternFamily::SPLIT_TRILL},
            }},
            {"Jacks", {
                {"Jacks", PatternFamily::JACKS},
            }},
        };
        return styles;
    }

    size_t stream_chord_size(const PatternSettings& settings) {
        switch (settings.chords) {
            case StreamChords::NONE:
                return 1;
            case StreamChords::JUMPS:
                return 2;
            case StreamChords::HANDS:
                return 3;
            case StreamChords::QUADS:
                return 4;
            case StreamChords::CUSTOM:
                return (size_t)std::max(1, settings.chord_size);
        }

        return 1;
    }

    std::vector<PatternRow> generate_pattern(PatternFamily family, uint8_t keys, size_t rows, uint32_t seed, const PatternSettings& settings) {
        std::mt19937 rng(seed);
        keys = std::max<uint8_t>(keys, 1);

        if (family == PatternFamily::SINGLE_TRILL) {
            uint8_t lane_a = std::min<uint8_t>(settings.lane_a, (uint8_t)(keys - 1));
            uint8_t lane_b = std::min<uint8_t>(settings.lane_b, (uint8_t)(keys - 1));
            std::vector<PatternRow> result;

            for (size_t row = 0; row < rows; row++) {
                result.push_back({row % 2 == 0 ? lane_a : lane_b});
            }

            return result;
        }

        uint8_t first = std::min<uint8_t>(std::min(settings.first_lane, settings.last_lane), (uint8_t)(keys - 1));
        uint8_t last = std::min<uint8_t>(std::max(settings.first_lane, settings.last_lane), (uint8_t)(keys - 1));
        uint8_t span = (uint8_t)(last - first + 1);
        uint8_t anchor = (uint8_t)std::clamp((int)settings.anchor_lane - first, 0, span - 1);
        std::vector<PatternRow> result;

        switch (family) {
            case PatternFamily::STREAM:
                result = stream(span, rows, anchor, settings, rng);
                break;
            case PatternFamily::SPLIT_TRILL:
                result = split_trill(span, rows, settings);
                break;
            case PatternFamily::JACKS:
                result = jacks(span, rows, settings, rng);
                break;
            case PatternFamily::SINGLE_TRILL:
                break;
        }

        for (PatternRow& row : result) {
            for (uint8_t& column : row) {
                column = (uint8_t)(column + first);
            }
        }

        return result;
    }
}

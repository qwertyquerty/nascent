#include "StepManiaFormat.h"

#include "Hit.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace nascent {
    namespace {
        constexpr std::string_view UTF8_BOM = "\xEF\xBB\xBF";
        constexpr int64_t ROWS_PER_BEAT = 48;
        constexpr int64_t ROWS_PER_MEASURE = ROWS_PER_BEAT * 4;
        constexpr double MAX_HOLD_MS = 600000.0;
        constexpr double MAX_TIME_MS = 2147483000.0;
        constexpr int32_t DEFAULT_HIT_ACCURACY = 5;
        constexpr int32_t DEFAULT_DAMAGE = 5;

        struct StepsType {
            std::string_view name;
            uint8_t key_count;
        };

        constexpr std::array STEPS_TYPES = {
            StepsType{"dance-single", 4},
            StepsType{"dance-double", 8},
            StepsType{"dance-solo", 6},
            StepsType{"dance-threepanel", 3},
            StepsType{"pump-single", 5},
            StepsType{"pump-halfdouble", 6},
            StepsType{"pump-double", 10},
            StepsType{"kb7-single", 7},
            StepsType{"ez2-single", 5},
            StepsType{"ez2-double", 10},
            StepsType{"ez2-real", 7},
            StepsType{"para-single", 5},
            StepsType{"ds3ddx-single", 8},
            StepsType{"beat-single5", 6},
            StepsType{"beat-double5", 12},
            StepsType{"beat-single7", 8},
            StepsType{"beat-double7", 16},
            StepsType{"maniax-single", 4},
            StepsType{"maniax-double", 8},
            StepsType{"techno-single4", 4},
            StepsType{"techno-single5", 5},
            StepsType{"techno-single8", 8},
            StepsType{"techno-double4", 8},
            StepsType{"techno-double5", 10},
            StepsType{"techno-double8", 16},
            StepsType{"popn-five", 5},
            StepsType{"popn-nine", 9},
            StepsType{"pnm-five", 5},
            StepsType{"pnm-nine", 9},
        };

        constexpr std::array<std::pair<std::string_view, std::string_view>, 17> DIFFICULTY_NAMES = {{
            {"beginner", "Beginner"},
            {"easy", "Easy"},
            {"basic", "Easy"},
            {"light", "Easy"},
            {"medium", "Medium"},
            {"another", "Medium"},
            {"trick", "Medium"},
            {"standard", "Medium"},
            {"difficult", "Medium"},
            {"hard", "Hard"},
            {"maniac", "Hard"},
            {"heavy", "Hard"},
            {"challenge", "Challenge"},
            {"smaniac", "Challenge"},
            {"expert", "Challenge"},
            {"oni", "Challenge"},
            {"edit", "Edit"},
        }};

        constexpr std::array<std::string_view, 7> TIMING_TAGS = {"OFFSET", "BPMS", "STOPS", "FREEZES", "DELAYS", "WARPS", "FAKES"};

        struct Tag {
            std::string name;
            std::vector<std::string> params;
        };

        using Tags = std::map<std::string, std::string, std::less<>>;

        struct Source {
            Tags tags;
            std::string notes;
            std::string steps_type;
            std::string difficulty;
            std::string description;
            std::string meter;
        };

        struct Pair {
            double beat = 0;
            double value = 0;
        };

        std::string_view trim(std::string_view text) {
            size_t start = text.find_first_not_of(" \t\r\n");

            if (start == std::string_view::npos) {
                return {};
            }

            size_t end = text.find_last_not_of(" \t\r\n");
            return text.substr(start, end - start + 1);
        }

        std::string lower(std::string_view text) {
            std::string result(text);

            for (char& c : result) {
                c = (char)std::tolower((unsigned char)c);
            }

            return result;
        }

        std::string upper(std::string_view text) {
            std::string result(text);

            for (char& c : result) {
                c = (char)std::toupper((unsigned char)c);
            }

            return result;
        }

        std::string latin1_to_utf8(std::string_view text) {
            std::string result;
            result.reserve(text.size());

            for (char c : text) {
                unsigned char byte = (unsigned char)c;

                if (byte < 0x80) {
                    result.push_back(c);
                }
                else {
                    result.push_back((char)(0xC0 | (byte >> 6)));
                    result.push_back((char)(0x80 | (byte & 0x3F)));
                }
            }

            return result;
        }

        std::string clean_text(std::string_view text) {
            std::string value(trim(text));

            try {
                (void)nlohmann::json(value).dump();
                return value;
            }
            catch (const nlohmann::json::exception&) {
                return latin1_to_utf8(value);
            }
        }

        std::vector<Tag> read_tags(std::string_view text) {
            std::vector<Tag> tags;
            std::vector<std::string> params;
            std::string value;
            bool reading = false;
            bool line_blank = true;

            auto finish = [&]() {
                params.push_back(std::move(value));
                value.clear();
                Tag tag;
                tag.name = upper(trim(params.front()));
                tag.params.assign(std::make_move_iterator(params.begin() + 1), std::make_move_iterator(params.end()));
                tags.push_back(std::move(tag));
                params.clear();
                reading = false;
            };

            size_t i = 0;

            while (i < text.size()) {
                char c = text[i];

                if (c == '/' && i + 1 < text.size() && text[i + 1] == '/') {
                    size_t newline = text.find('\n', i);
                    i = newline == std::string_view::npos ? text.size() : newline;
                    continue;
                }

                if (!reading) {
                    if (c == '#') {
                        reading = true;
                    }

                    line_blank = c == '\n' || (line_blank && (c == ' ' || c == '\t' || c == '\r'));
                    i++;
                    continue;
                }

                if (c == '#' && line_blank) {
                    finish();
                    reading = true;
                    line_blank = false;
                    i++;
                    continue;
                }

                if (c == '\\' && i + 1 < text.size()) {
                    value.push_back(text[i + 1]);
                    line_blank = false;
                    i += 2;
                    continue;
                }

                if (c == ':') {
                    params.push_back(std::move(value));
                    value.clear();
                }
                else if (c == ';') {
                    finish();
                }
                else {
                    value.push_back(c);
                }

                line_blank = c == '\n' || (line_blank && (c == ' ' || c == '\t' || c == '\r'));
                i++;
            }

            if (reading) {
                finish();
            }

            return tags;
        }

        std::string joined(const std::vector<std::string>& params) {
            std::string result;

            for (size_t index = 0; index < params.size(); index++) {
                if (index > 0) {
                    result.push_back(':');
                }

                result += params[index];
            }

            return result;
        }

        bool parse_number(std::string_view text, double& value) {
            text = trim(text);

            if (text.empty()) {
                return false;
            }

            std::istringstream stream{std::string(text)};
            stream.imbue(std::locale::classic());
            stream >> value;
            return !stream.fail() && std::isfinite(value);
        }

        double number_or(std::string_view text, double fallback) {
            double value = 0;
            return parse_number(text, value) ? value : fallback;
        }

        std::vector<Pair> pairs(std::string_view text) {
            std::vector<Pair> result;

            while (!text.empty()) {
                size_t comma = text.find(',');
                std::string_view item = text.substr(0, comma);
                text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
                size_t equals = item.find('=');

                if (equals == std::string_view::npos) {
                    continue;
                }

                std::string_view right = item.substr(equals + 1);
                Pair pair;

                if (parse_number(item.substr(0, equals), pair.beat) && parse_number(right.substr(0, right.find('=')), pair.value)) {
                    result.push_back(pair);
                }
            }

            std::stable_sort(result.begin(), result.end(), [](const Pair& a, const Pair& b) {
                return a.beat < b.beat;
            });

            return result;
        }

        int64_t beat_to_row(double beat) {
            return std::llround(beat * (double)ROWS_PER_BEAT);
        }

        std::string tag_or(const Tags& tags, std::string_view name, std::string_view fallback = "") {
            auto it = tags.find(name);
            return it != tags.end() ? it->second : std::string(fallback);
        }

        struct Interval {
            int64_t start = 0;
            int64_t end = 0;
        };

        std::vector<Interval> merged_intervals(const std::vector<Pair>& items) {
            std::vector<Interval> intervals;

            for (const Pair& item : items) {
                if (item.value > 0) {
                    int64_t start = beat_to_row(item.beat);
                    intervals.push_back({start, start + std::max<int64_t>(1, beat_to_row(item.value))});
                }
            }

            std::sort(intervals.begin(), intervals.end(), [](const Interval& a, const Interval& b) {
                return a.start < b.start;
            });

            std::vector<Interval> merged;

            for (const Interval& interval : intervals) {
                if (!merged.empty() && interval.start <= merged.back().end) {
                    merged.back().end = std::max(merged.back().end, interval.end);
                }
                else {
                    merged.push_back(interval);
                }
            }

            return merged;
        }

        bool inside(const std::vector<Interval>& intervals, int64_t row) {
            auto it = std::upper_bound(intervals.begin(), intervals.end(), row, [](int64_t value, const Interval& interval) {
                return value < interval.start;
            });

            return it != intervals.begin() && row < std::prev(it)->end;
        }

        class Timing {
            public:
            Timing(double offset, const std::vector<Pair>& bpm_pairs, const std::vector<Pair>& stop_pairs, const std::vector<Pair>& delay_pairs, const std::vector<Pair>& warp_pairs, const std::vector<Pair>& fake_pairs) {
                if (bpm_pairs.empty()) {
                    throw std::runtime_error("chart has no BPMs");
                }

                std::map<int64_t, double> bpm_rows;

                for (const Pair& pair : bpm_pairs) {
                    if (!(pair.value > 0)) {
                        throw std::runtime_error("negative or zero BPMs are not supported");
                    }

                    bpm_rows[std::max<int64_t>(0, beat_to_row(pair.beat))] = pair.value;
                }

                bpms.assign(bpm_rows.begin(), bpm_rows.end());
                bpms.front().first = 0;

                double shift = 0;

                for (auto [items, target] : {std::pair{&stop_pairs, &stops}, std::pair{&delay_pairs, &delays}}) {
                    for (const Pair& pair : *items) {
                        if (pair.value < 0) {
                            throw std::runtime_error("negative stops and delays are not supported");
                        }

                        if (pair.value == 0) {
                            continue;
                        }

                        int64_t row = beat_to_row(pair.beat);

                        if (row < 0) {
                            shift += pair.value;
                        }
                        else {
                            (*target)[row] += pair.value;
                        }
                    }
                }

                warps = merged_intervals(warp_pairs);
                fakes = merged_intervals(fake_pairs);

                std::vector<int64_t> breaks = {0};

                for (const auto& [row, bpm] : bpms) {
                    breaks.push_back(row);
                }

                for (const auto& [row, length] : stops) {
                    breaks.push_back(row);
                }

                for (const auto& [row, length] : delays) {
                    breaks.push_back(row);
                }

                for (const Interval& warp : warps) {
                    breaks.push_back(std::max<int64_t>(0, warp.start));
                    breaks.push_back(std::max<int64_t>(0, warp.end));
                }

                std::sort(breaks.begin(), breaks.end());
                breaks.erase(std::unique(breaks.begin(), breaks.end()), breaks.end());

                double time = -offset + shift;
                size_t bpm_index = 0;

                for (size_t index = 0; index < breaks.size(); index++) {
                    int64_t row = breaks[index];

                    while (bpm_index + 1 < bpms.size() && bpms[bpm_index + 1].first <= row) {
                        bpm_index++;
                    }

                    Point point;
                    point.row = row;
                    point.bpm = bpms[bpm_index].second;
                    point.seconds_per_row = 60.0 / point.bpm / (double)ROWS_PER_BEAT;
                    point.warped = inside(warps, row);
                    point.delay = length_at(delays, row);
                    point.before = time;
                    point.after = time + point.delay + length_at(stops, row);
                    points.push_back(point);

                    if (index + 1 < breaks.size()) {
                        time = point.after + (point.warped ? 0.0 : (double)(breaks[index + 1] - row) * point.seconds_per_row);
                    }
                }
            }

            double seconds_at(int64_t row) const {
                const Point& point = point_at(row);

                if (point.row == row) {
                    return point.before + point.delay;
                }

                return point.after + (point.warped ? 0.0 : (double)(row - point.row) * point.seconds_per_row);
            }

            bool judged(int64_t row) const {
                if (inside(fakes, row)) {
                    return false;
                }

                return !inside(warps, row) || stops.contains(row) || delays.contains(row);
            }

            nlohmann::json timing_points() const {
                std::vector<int64_t> anchors;

                for (const auto& [row, bpm] : bpms) {
                    anchors.push_back(row);
                }

                for (const auto& [row, length] : stops) {
                    anchors.push_back(row);
                }

                for (const auto& [row, length] : delays) {
                    anchors.push_back(row);
                }

                for (const Interval& warp : warps) {
                    anchors.push_back(std::max<int64_t>(0, warp.end));
                }

                std::sort(anchors.begin(), anchors.end());
                anchors.erase(std::unique(anchors.begin(), anchors.end()), anchors.end());

                nlohmann::json result = nlohmann::json::array();
                int64_t last_time = std::numeric_limits<int64_t>::min();
                double last_beat_length = 0;

                for (int64_t row : anchors) {
                    if (inside(warps, row)) {
                        continue;
                    }

                    const Point& point = point_at(row);
                    double beat_length = 60000.0 / point.bpm;
                    double after_ms = point.after * 1000.0;
                    double beat = (double)row / (double)ROWS_PER_BEAT;
                    int64_t time = std::llround(after_ms - (beat - std::floor(beat)) * beat_length);

                    if (time < last_time) {
                        time = std::llround(after_ms);
                    }

                    if (time == last_time && beat_length == last_beat_length) {
                        continue;
                    }

                    if (std::abs((double)time) > MAX_TIME_MS) {
                        throw std::runtime_error("timing is out of range");
                    }

                    result.push_back({{"time", (int32_t)time}, {"beat_length", beat_length}, {"meter", 4}});
                    last_time = time;
                    last_beat_length = beat_length;
                }

                return result;
            }

            private:
            struct Point {
                int64_t row = 0;
                double bpm = 0;
                double seconds_per_row = 0;
                double before = 0;
                double after = 0;
                double delay = 0;
                bool warped = false;
            };

            std::vector<std::pair<int64_t, double>> bpms;
            std::map<int64_t, double> stops;
            std::map<int64_t, double> delays;
            std::vector<Interval> warps;
            std::vector<Interval> fakes;
            std::vector<Point> points;

            static double length_at(const std::map<int64_t, double>& lengths, int64_t row) {
                auto it = lengths.find(row);
                return it != lengths.end() ? it->second : 0.0;
            }

            const Point& point_at(int64_t row) const {
                auto it = std::upper_bound(points.begin(), points.end(), row, [](int64_t value, const Point& point) {
                    return value < point.row;
                });

                return it == points.begin() ? points.front() : *std::prev(it);
            }
        };

        int32_t milliseconds(double seconds) {
            double ms = seconds * 1000.0;

            if (!std::isfinite(ms) || std::abs(ms) > MAX_TIME_MS) {
                throw std::runtime_error("note time is out of range");
            }

            return (int32_t)std::lround(ms);
        }

        std::vector<Hit> parse_notes(std::string_view data, uint8_t key_count, const Timing& timing) {
            struct OpenHold {
                Hit hit;
                bool open = false;
            };

            std::vector<Hit> hits;
            std::vector<OpenHold> holds(key_count);
            int64_t measure = 0;

            auto close_as_tap = [&](OpenHold& hold) {
                if (hold.open) {
                    hold.hit.hit_type = HitType::HIT;
                    hold.hit.end_time = hold.hit.time;
                    hits.push_back(hold.hit);
                    hold.open = false;
                }
            };

            while (true) {
                size_t comma = data.find(',');
                std::string_view block = data.substr(0, comma);
                std::vector<std::string_view> lines;

                while (!block.empty()) {
                    size_t newline = block.find('\n');
                    std::string_view line = trim(block.substr(0, newline));
                    block = newline == std::string_view::npos ? std::string_view() : block.substr(newline + 1);

                    if (!line.empty()) {
                        lines.push_back(line);
                    }
                }

                for (size_t position = 0; position < lines.size(); position++) {
                    std::string_view line = lines[position];
                    int64_t row = std::llround(((double)measure * (double)lines.size() + (double)position) * (double)ROWS_PER_MEASURE / (double)lines.size());
                    std::optional<int32_t> time;
                    size_t index = 0;

                    for (uint8_t column = 0; column < key_count && index < line.size(); column++) {
                        char symbol = line[index++];

                        for (auto [open_mark, close_mark] : {std::pair{'{', '}'}, std::pair{'[', ']'}}) {
                            if (index < line.size() && line[index] == open_mark) {
                                size_t end = line.find(close_mark, index);
                                index = end == std::string_view::npos ? line.size() : end + 1;
                            }
                        }

                        bool tap = symbol == '1' || symbol == 'L' || symbol == 'l';
                        bool head = symbol == '2' || symbol == '4';
                        bool tail = symbol == '3';

                        if (!tap && !head && !tail) {
                            continue;
                        }

                        if (!time) {
                            time = milliseconds(timing.seconds_at(row));
                        }

                        OpenHold& hold = holds[column];

                        if (tail) {
                            if (hold.open) {
                                hold.hit.end_time = *time;
                                double length = (double)hold.hit.end_time - (double)hold.hit.time;

                                if (length <= 0 || length > MAX_HOLD_MS) {
                                    hold.hit.hit_type = HitType::HIT;
                                    hold.hit.end_time = hold.hit.time;
                                }

                                hits.push_back(hold.hit);
                                hold.open = false;
                            }

                            continue;
                        }

                        if (head) {
                            close_as_tap(hold);
                        }

                        if (!timing.judged(row)) {
                            continue;
                        }

                        Hit hit;
                        hit.key = column;
                        hit.time = *time;
                        hit.end_time = *time;
                        hit.hit_type = head ? HitType::HOLD : HitType::HIT;

                        if (head) {
                            hold.hit = hit;
                            hold.open = true;
                        }
                        else {
                            hits.push_back(hit);
                        }
                    }
                }

                if (comma == std::string_view::npos) {
                    break;
                }

                data = data.substr(comma + 1);
                measure++;
            }

            for (OpenHold& hold : holds) {
                close_as_tap(hold);
            }

            std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
                return std::tie(a.time, a.key) < std::tie(b.time, b.key);
            });

            return hits;
        }

        std::optional<uint8_t> key_count_for(std::string_view steps_type) {
            std::string name = lower(trim(steps_type));

            for (const StepsType& type : STEPS_TYPES) {
                if (type.name == name) {
                    return type.key_count;
                }
            }

            return std::nullopt;
        }

        std::string canonical_difficulty(std::string_view difficulty) {
            std::string name = lower(trim(difficulty));

            for (const auto& [alias, canonical] : DIFFICULTY_NAMES) {
                if (alias == name) {
                    return std::string(canonical);
                }
            }

            std::string raw(trim(difficulty));
            return raw.empty() ? std::string("Edit") : raw;
        }

        bool has_own_timing(const Tags& tags) {
            return std::any_of(TIMING_TAGS.begin(), TIMING_TAGS.end(), [&](std::string_view name) {
                return tags.contains(name);
            });
        }

        Timing timing_for(const Tags& song, const Tags& chart) {
            const Tags& source = has_own_timing(chart) ? chart : song;
            std::string offset = tag_or(source, "OFFSET", tag_or(song, "OFFSET", "0"));
            std::string bpms = tag_or(source, "BPMS", tag_or(song, "BPMS"));
            std::string stops = tag_or(source, "STOPS", tag_or(source, "FREEZES"));
            return Timing(number_or(offset, 0.0), pairs(bpms), pairs(stops), pairs(tag_or(source, "DELAYS")), pairs(tag_or(source, "WARPS")), pairs(tag_or(source, "FAKES")));
        }

        std::string sha1_hex(std::string_view data) {
            uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
            std::string message(data);
            uint64_t bits = (uint64_t)data.size() * 8;
            message.push_back((char)0x80);

            while (message.size() % 64 != 56) {
                message.push_back('\0');
            }

            for (int shift = 56; shift >= 0; shift -= 8) {
                message.push_back((char)((bits >> shift) & 0xFF));
            }

            auto rotate = [](uint32_t value, int count) {
                return (value << count) | (value >> (32 - count));
            };

            for (size_t block = 0; block < message.size(); block += 64) {
                uint32_t w[80];

                for (int i = 0; i < 16; i++) {
                    w[i] = ((uint32_t)(uint8_t)message[block + i * 4] << 24) | ((uint32_t)(uint8_t)message[block + i * 4 + 1] << 16) | ((uint32_t)(uint8_t)message[block + i * 4 + 2] << 8) | (uint32_t)(uint8_t)message[block + i * 4 + 3];
                }

                for (int i = 16; i < 80; i++) {
                    w[i] = rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
                }

                uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

                for (int i = 0; i < 80; i++) {
                    uint32_t f = 0, k = 0;

                    if (i < 20) {
                        f = (b & c) | (~b & d);
                        k = 0x5A827999u;
                    }
                    else if (i < 40) {
                        f = b ^ c ^ d;
                        k = 0x6ED9EBA1u;
                    }
                    else if (i < 60) {
                        f = (b & c) | (b & d) | (c & d);
                        k = 0x8F1BBCDCu;
                    }
                    else {
                        f = b ^ c ^ d;
                        k = 0xCA62C1D6u;
                    }

                    uint32_t next = rotate(a, 5) + f + e + k + w[i];
                    e = d;
                    d = c;
                    c = rotate(b, 30);
                    b = a;
                    a = next;
                }

                h[0] += a;
                h[1] += b;
                h[2] += c;
                h[3] += d;
                h[4] += e;
            }

            return std::format("{:08x}{:08x}{:08x}{:08x}{:08x}", h[0], h[1], h[2], h[3], h[4]);
        }

        constexpr float KEY_ROWS_PER_BEAT = 48.0f;
        constexpr float KEY_BPM_ROUNDING = 0.374643f;

        int64_t key_row(float beat) {
            return (int64_t)std::lrint(beat * KEY_ROWS_PER_BEAT);
        }

        struct KeyTiming {
            std::vector<std::pair<int64_t, float>> bpm_rows;
            std::vector<std::pair<float, float>> warps;
            std::vector<std::pair<float, float>> fakes;
            std::set<int64_t> stop_rows;
            bool pauses = false;

            static std::vector<std::pair<double, double>> sorted_pairs(std::string_view text) {
                std::vector<std::pair<double, double>> result;

                for (const Pair& pair : pairs(text)) {
                    result.emplace_back(pair.beat, pair.value);
                }

                std::sort(result.begin(), result.end());
                return result;
            }

            KeyTiming(const std::vector<std::pair<double, double>>& bpms, const std::vector<std::pair<double, double>>& stops, const std::vector<std::pair<double, double>>& warp_pairs, const std::vector<std::pair<double, double>>& fake_pairs) {
                for (const auto& [beat, bpm] : bpms) {
                    bpm_rows.emplace_back(key_row((float)beat), (float)bpm);
                }

                for (const auto& [beat, length] : warp_pairs) {
                    warps.emplace_back((float)beat, (float)length);
                }

                for (const auto& [beat, length] : fake_pairs) {
                    fakes.emplace_back((float)beat, (float)length);
                }

                for (const auto& [beat, length] : stops) {
                    if (length != 0) {
                        stop_rows.insert(key_row((float)beat));
                    }
                }

                pauses = !stops.empty();
            }

            static const std::pair<float, float>* segment(const std::vector<std::pair<float, float>>& segments, int64_t row) {
                const std::pair<float, float>* chosen = nullptr;

                for (const auto& item : segments) {
                    if (key_row(item.first) <= row) {
                        chosen = &item;
                    }
                }

                return chosen;
            }

            bool judgable(int64_t row) const {
                float beat = (float)row / KEY_ROWS_PER_BEAT;
                const std::pair<float, float>* warp = segment(warps, row);

                if (warp != nullptr && warp->first <= beat && beat < warp->first + warp->second && (!pauses || !stop_rows.contains(row))) {
                    return false;
                }

                const std::pair<float, float>* fake = segment(fakes, row);
                return !(fake != nullptr && fake->first <= beat && beat < fake->first + fake->second);
            }

            float bpm_at(int64_t row) const {
                float bpm = bpm_rows.front().second;

                for (const auto& [start, value] : bpm_rows) {
                    if (start <= row) {
                        bpm = value;
                    }
                }

                return bpm;
            }
        };

        std::string chart_key_from_rows(const std::map<int64_t, std::vector<uint8_t>>& rows, const KeyTiming& timing) {
            std::string text;

            for (const auto& [row, kinds] : rows) {
                if (!timing.judgable(row)) {
                    continue;
                }

                for (uint8_t kind : kinds) {
                    text.push_back((char)('0' + kind));
                }

                text += std::to_string((int64_t)(timing.bpm_at(row) + KEY_BPM_ROUNDING));
            }

            return "X" + sha1_hex(text);
        }

        uint8_t key_kind(char symbol) {
            switch (symbol) {
                case '1':
                    return 1;
                case '2':
                case '4':
                    return 2;
                case 'M':
                    return 4;
                case 'L':
                    return 5;
                case 'K':
                    return 6;
                case 'F':
                    return 7;
                default:
                    return 0;
            }
        }

        std::string stepmania_chart_key(std::string_view data, uint8_t key_count, const Tags& song, const Tags& chart) {
            const Tags& source = has_own_timing(chart) ? chart : song;
            std::vector<std::pair<double, double>> bpms = KeyTiming::sorted_pairs(tag_or(source, "BPMS", tag_or(song, "BPMS")));

            if (bpms.empty() || std::any_of(bpms.begin(), bpms.end(), [](const auto& pair) { return !(pair.second > 0); })) {
                return {};
            }

            std::vector<std::pair<double, double>> stops = KeyTiming::sorted_pairs(tag_or(source, "STOPS", tag_or(source, "FREEZES")));
            std::vector<std::pair<double, double>> delays = KeyTiming::sorted_pairs(tag_or(source, "DELAYS"));
            stops.insert(stops.end(), delays.begin(), delays.end());
            KeyTiming timing(bpms, stops, KeyTiming::sorted_pairs(tag_or(source, "WARPS")), KeyTiming::sorted_pairs(tag_or(source, "FAKES")));
            std::map<int64_t, std::vector<uint8_t>> rows;
            int64_t measure = 0;

            while (true) {
                size_t comma = data.find(',');
                std::string_view block = data.substr(0, comma);
                std::vector<std::string_view> lines;

                while (!block.empty()) {
                    size_t newline = block.find('\n');
                    std::string_view line = trim(block.substr(0, newline));
                    block = newline == std::string_view::npos ? std::string_view() : block.substr(newline + 1);

                    if (!line.empty()) {
                        lines.push_back(line);
                    }
                }

                for (size_t position = 0; position < lines.size(); position++) {
                    std::string_view line = lines[position];
                    float beat = ((float)measure + (float)position / (float)lines.size()) * 4.0f;
                    int64_t row = key_row(beat);
                    size_t index = 0;

                    for (uint8_t track = 0; track < key_count && index < line.size(); track++) {
                        char symbol = line[index++];

                        if (index < line.size() && line[index] == '[') {
                            size_t end = line.find(']', index);
                            index = end == std::string_view::npos ? line.size() : end + 1;
                        }

                        uint8_t kind = key_kind(symbol);

                        if (kind != 0) {
                            auto [entry, inserted] = rows.try_emplace(row, key_count, (uint8_t)0);
                            entry->second[track] = kind;
                        }
                    }
                }

                if (comma == std::string_view::npos) {
                    break;
                }

                data = data.substr(comma + 1);
                measure++;
            }

            return chart_key_from_rows(rows, timing);
        }

        std::string with_subtitle(const std::string& title, const std::string& subtitle) {
            if (subtitle.empty()) {
                return title;
            }

            return title.empty() ? subtitle : std::format("{} {}", title, subtitle);
        }

        nlohmann::json chart_json(const Tags& song, const Source& source, uint8_t key_count, std::vector<Hit> hits, nlohmann::json timing_points) {
            std::string title = clean_text(tag_or(song, "TITLE"));
            std::string subtitle = clean_text(tag_or(song, "SUBTITLE"));
            std::string artist = clean_text(tag_or(song, "ARTIST"));
            std::string title_translit = clean_text(tag_or(song, "TITLETRANSLIT"));
            std::string subtitle_translit = clean_text(tag_or(song, "SUBTITLETRANSLIT"));
            std::string artist_translit = clean_text(tag_or(song, "ARTISTTRANSLIT"));
            std::string description = clean_text(source.description);
            std::string credit = clean_text(tag_or(source.tags, "CREDIT"));
            std::string difficulty = canonical_difficulty(source.difficulty);
            std::string chart_name = clean_text(tag_or(source.tags, "CHARTNAME"));

            if (credit.empty()) {
                credit = description.empty() ? clean_text(tag_or(song, "CREDIT")) : description;
            }

            if (difficulty == "Edit") {
                difficulty = !chart_name.empty() ? chart_name : (!description.empty() ? description : difficulty);
            }

            double preview = number_or(tag_or(source.tags, "SAMPLESTART", tag_or(song, "SAMPLESTART")), 0.0);
            std::string music = tag_or(source.tags, "MUSIC", tag_or(song, "MUSIC"));

            return nlohmann::json{
                {"info",
                 {
                     {"title", with_subtitle(title_translit.empty() ? title : title_translit, subtitle_translit.empty() ? subtitle : subtitle_translit)},
                     {"title_unicode", with_subtitle(title, subtitle)},
                     {"artist", artist_translit.empty() ? artist : artist_translit},
                     {"artist_unicode", artist},
                     {"mapper", credit},
                     {"difficulty_name", clean_text(difficulty)},
                     {"audio_file", clean_text(music)},
                     {"image_file", clean_text(tag_or(song, "BACKGROUND"))},
                     {"audio_lead_in", 0},
                     {"preview_time", milliseconds(std::max(0.0, preview))},
                     {"hit_accuracy", DEFAULT_HIT_ACCURACY},
                     {"key_count", key_count},
                     {"damage", DEFAULT_DAMAGE},
                 }},
                {"timing_points", std::move(timing_points)},
                {"hit_objects", std::move(hits)},
            };
        }

        void read_sources(const std::vector<Tag>& tags, Tags& song, std::vector<Source>& sources) {
            bool ssc = std::any_of(tags.begin(), tags.end(), [](const Tag& tag) {
                return tag.name == "NOTEDATA";
            });

            Source* current = nullptr;

            for (const Tag& tag : tags) {
                if (ssc) {
                    if (tag.name == "NOTEDATA") {
                        sources.emplace_back();
                        current = &sources.back();
                    }
                    else if (current == nullptr) {
                        song.insert_or_assign(tag.name, joined(tag.params));
                    }
                    else if (tag.name == "NOTES" || tag.name == "NOTES2") {
                        current->notes = joined(tag.params);
                    }
                    else if (tag.name == "STEPSTYPE") {
                        current->steps_type = joined(tag.params);
                    }
                    else if (tag.name == "DIFFICULTY") {
                        current->difficulty = joined(tag.params);
                    }
                    else if (tag.name == "DESCRIPTION") {
                        current->description = joined(tag.params);
                    }
                    else if (tag.name == "METER") {
                        current->meter = joined(tag.params);
                    }
                    else {
                        current->tags.insert_or_assign(tag.name, joined(tag.params));
                    }
                }
                else if (tag.name == "NOTES" || tag.name == "NOTES2") {
                    if (tag.params.size() >= 6) {
                        Source source;
                        source.steps_type = tag.params[0];
                        source.description = tag.params[1];
                        source.difficulty = tag.params[2];
                        source.meter = tag.params[3];
                        source.notes = tag.params[5];
                        sources.push_back(std::move(source));
                    }
                }
                else {
                    song.insert_or_assign(tag.name, joined(tag.params));
                }
            }
        }

        enum class Panel : uint8_t {
            LEFT,
            DOWN,
            UP,
            RIGHT,
            UP_LEFT,
            UP_RIGHT,
            NONE
        };

        struct DwiStyle {
            std::string_view tag;
            std::string_view steps_type;
            size_t pads;
            bool solo;
        };

        constexpr std::array DWI_STYLES = {
            DwiStyle{"SINGLE", "dance-single", 1, false},
            DwiStyle{"DOUBLE", "dance-double", 2, false},
            DwiStyle{"SOLO", "dance-solo", 1, true},
            DwiStyle{"COUPLE", "dance-couple", 2, false},
        };

        constexpr double DWI_EIGHTH = 0.5;
        constexpr double DWI_BEATS_PER_POSITION = 0.25;
        constexpr size_t DWI_PANELS_PER_PAD = 4;

        std::pair<Panel, Panel> dwi_panels(char symbol) {
            switch (std::toupper((unsigned char)symbol)) {
                case '1':
                    return {Panel::LEFT, Panel::DOWN};
                case '2':
                    return {Panel::DOWN, Panel::NONE};
                case '3':
                    return {Panel::DOWN, Panel::RIGHT};
                case '4':
                    return {Panel::LEFT, Panel::NONE};
                case '6':
                    return {Panel::RIGHT, Panel::NONE};
                case '7':
                    return {Panel::LEFT, Panel::UP};
                case '8':
                    return {Panel::UP, Panel::NONE};
                case '9':
                    return {Panel::UP, Panel::RIGHT};
                case 'A':
                    return {Panel::UP, Panel::DOWN};
                case 'B':
                    return {Panel::LEFT, Panel::RIGHT};
                case 'C':
                    return {Panel::UP_LEFT, Panel::NONE};
                case 'D':
                    return {Panel::UP_RIGHT, Panel::NONE};
                case 'E':
                    return {Panel::LEFT, Panel::UP_LEFT};
                case 'F':
                    return {Panel::UP_LEFT, Panel::DOWN};
                case 'G':
                    return {Panel::UP_LEFT, Panel::UP};
                case 'H':
                    return {Panel::UP_LEFT, Panel::RIGHT};
                case 'I':
                    return {Panel::LEFT, Panel::UP_RIGHT};
                case 'J':
                    return {Panel::DOWN, Panel::UP_RIGHT};
                case 'K':
                    return {Panel::UP, Panel::UP_RIGHT};
                case 'L':
                    return {Panel::UP_RIGHT, Panel::RIGHT};
                case 'M':
                    return {Panel::UP_LEFT, Panel::UP_RIGHT};
                default:
                    return {Panel::NONE, Panel::NONE};
            }
        }

        int32_t dwi_column(Panel panel, bool solo) {
            constexpr std::array<int32_t, 6> SINGLE = {0, 1, 2, 3, -1, -1};
            constexpr std::array<int32_t, 6> SOLO = {0, 2, 3, 5, 1, 4};
            return panel == Panel::NONE ? -1 : (solo ? SOLO : SINGLE)[(size_t)panel];
        }

        void read_dwi_steps(std::string_view steps, size_t column_offset, bool solo, std::vector<std::map<int64_t, bool>>& columns) {
            std::string data;

            for (char c : steps) {
                if (!std::isspace((unsigned char)c)) {
                    data.push_back(c);
                }
            }

            auto place = [&](char symbol, int64_t row, bool head) {
                auto [first, second] = dwi_panels(symbol);

                for (Panel panel : {first, second}) {
                    int32_t column = dwi_column(panel, solo);

                    if (column >= 0 && column_offset + (size_t)column < columns.size()) {
                        columns[column_offset + (size_t)column][row] = head;
                    }
                }
            };

            double beat = 0;
            double step = DWI_EIGHTH;
            size_t index = 0;

            while (index < data.size()) {
                char c = data[index++];

                switch (c) {
                    case '(':
                        step = 4.0 / 16.0;
                        continue;
                    case '[':
                        step = 4.0 / 24.0;
                        continue;
                    case '{':
                        step = 4.0 / 64.0;
                        continue;
                    case '`':
                        step = 4.0 / 192.0;
                        continue;
                    case ')':
                    case ']':
                    case '}':
                    case '\'':
                    case '>':
                        step = DWI_EIGHTH;
                        continue;
                    case '!':
                        continue;
                    default:
                        break;
                }

                bool jump = c == '<';

                if (jump) {
                    if (index < data.size() && data[index] == '`') {
                        step = 4.0 / 192.0;
                        continue;
                    }

                    if (index >= data.size()) {
                        break;
                    }

                    c = data[index++];
                }

                int64_t row = beat_to_row(beat);

                while (!(jump && c == '>')) {
                    place(c, row, false);

                    if (index < data.size() && data[index] == '!') {
                        index++;

                        if (index < data.size()) {
                            place(data[index++], row, true);
                        }
                    }

                    if (!jump || index >= data.size()) {
                        break;
                    }

                    c = data[index++];
                }

                beat += step;
            }
        }

        std::vector<Pair> dwi_positions(std::string_view text, double value_scale) {
            std::vector<Pair> result = pairs(text);

            for (Pair& pair : result) {
                pair.beat *= DWI_BEATS_PER_POSITION;
                pair.value *= value_scale;
            }

            return result;
        }

        std::string dwi_seconds(std::string_view text) {
            double total = 0;
            double part = 0;

            while (true) {
                size_t colon = text.find(':');

                if (!parse_number(text.substr(0, colon), part)) {
                    return "0";
                }

                total = total * 60.0 + part;

                if (colon == std::string_view::npos) {
                    return std::format("{}", total);
                }

                text = text.substr(colon + 1);
            }
        }

        bool is_dwi(const std::vector<Tag>& tags) {
            bool stepmania = std::any_of(tags.begin(), tags.end(), [](const Tag& tag) {
                return tag.name == "NOTES" || tag.name == "NOTES2" || tag.name == "NOTEDATA";
            });

            return !stepmania && std::any_of(tags.begin(), tags.end(), [](const Tag& tag) {
                return std::any_of(DWI_STYLES.begin(), DWI_STYLES.end(), [&](const DwiStyle& style) {
                    return style.tag == tag.name;
                });
            });
        }

        std::vector<StepManiaChart> dwi_charts(const std::vector<Tag>& tags) {
            Tags song;

            for (const Tag& tag : tags) {
                song.insert_or_assign(tag.name, joined(tag.params));
            }

            song.insert_or_assign("MUSIC", tag_or(song, "FILE"));
            song.insert_or_assign("SAMPLESTART", dwi_seconds(tag_or(song, "SAMPLESTART", "0")));
            std::vector<StepManiaChart> charts;

            for (const Tag& tag : tags) {
                auto style = std::find_if(DWI_STYLES.begin(), DWI_STYLES.end(), [&](const DwiStyle& candidate) {
                    return candidate.tag == tag.name;
                });

                if (style == DWI_STYLES.end()) {
                    continue;
                }

                StepManiaChart chart;
                chart.index = charts.size();
                chart.steps_type = std::string(style->steps_type);
                chart.difficulty = canonical_difficulty(tag.params.empty() ? std::string_view() : std::string_view(tag.params[0]));
                chart.meter = (int32_t)std::lround(std::clamp(number_or(tag.params.size() > 1 ? tag.params[1] : std::string(), 0.0), -1e6, 1e6));

                try {
                    std::optional<uint8_t> key_count = key_count_for(chart.steps_type);

                    if (!key_count) {
                        throw std::runtime_error(std::format("steps type '{}' is not supported", chart.steps_type));
                    }

                    if (tag.params.size() < 2 + style->pads) {
                        throw std::runtime_error("chart has no notes");
                    }

                    std::vector<Pair> bpms = {{0.0, number_or(tag_or(song, "BPM"), 0.0)}};
                    std::vector<Pair> changes = dwi_positions(tag_or(song, "CHANGEBPM", tag_or(song, "BPMCHANGE")), 1.0);
                    bpms.insert(bpms.end(), changes.begin(), changes.end());
                    Timing timing(-number_or(tag_or(song, "GAP"), 0.0) / 1000.0, bpms, dwi_positions(tag_or(song, "FREEZE"), 0.001), {}, {}, {});
                    std::vector<std::map<int64_t, bool>> columns(*key_count);

                    for (size_t pad = 0; pad < style->pads; pad++) {
                        read_dwi_steps(tag.params[2 + pad], pad * DWI_PANELS_PER_PAD, style->solo, columns);
                    }

                    std::vector<Hit> hits;
                    std::map<int64_t, std::vector<uint8_t>> key_rows;

                    for (size_t column = 0; column < columns.size(); column++) {
                        std::map<int64_t, bool>& notes = columns[column];

                        for (auto it = notes.begin(); it != notes.end(); ++it) {
                            Hit hit;
                            hit.key = (uint8_t)column;
                            hit.time = milliseconds(timing.seconds_at(it->first));
                            hit.end_time = hit.time;
                            hit.hit_type = HitType::HIT;

                            if (it->second) {
                                auto tail = std::next(it);

                                if (tail == notes.end()) {
                                    continue;
                                }

                                hit.end_time = milliseconds(timing.seconds_at(tail->first));
                                hit.hit_type = hit.end_time > hit.time ? HitType::HOLD : HitType::HIT;
                                hit.end_time = hit.hit_type == HitType::HOLD ? hit.end_time : hit.time;
                                it = notes.erase(tail);
                                --it;
                            }

                            key_rows.try_emplace(it->first, *key_count, (uint8_t)0).first->second[column] = it->second ? 2 : 1;

                            if (timing.judged(it->first)) {
                                hits.push_back(hit);
                            }
                        }
                    }

                    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
                        return std::tie(a.time, a.key) < std::tie(b.time, b.key);
                    });

                    if (hits.empty()) {
                        throw std::runtime_error("chart has no notes");
                    }

                    std::vector<std::pair<double, double>> key_bpms;

                    for (const Pair& pair : bpms) {
                        key_bpms.emplace_back(pair.beat, pair.value);
                    }

                    std::sort(key_bpms.begin(), key_bpms.end());
                    std::vector<std::pair<double, double>> key_stops;

                    for (const Pair& pair : dwi_positions(tag_or(song, "FREEZE"), 0.001)) {
                        key_stops.emplace_back(pair.beat, pair.value);
                    }

                    chart.chart_key = chart_key_from_rows(key_rows, KeyTiming(key_bpms, key_stops, {}, {}));
                    Source source;
                    source.steps_type = chart.steps_type;
                    source.difficulty = tag.params[0];
                    chart.chart = chart_json(song, source, *key_count, std::move(hits), timing.timing_points());
                }
                catch (const std::exception& e) {
                    chart.chart = nullptr;
                    chart.error = e.what();
                }

                charts.push_back(std::move(chart));
            }

            return charts;
        }
    }

    std::vector<StepManiaChart> stepmania_to_chart_jsons(std::string_view text, std::string& error) {
        if (text.starts_with(UTF8_BOM)) {
            text.remove_prefix(UTF8_BOM.size());
        }

        std::vector<Tag> tags = read_tags(text);

        if (is_dwi(tags)) {
            return dwi_charts(tags);
        }

        Tags song;
        std::vector<Source> sources;
        read_sources(tags, song, sources);

        if (sources.empty()) {
            error = song.empty() ? "not a StepMania file" : "the file has no charts";
            return {};
        }

        std::vector<StepManiaChart> charts;

        for (size_t index = 0; index < sources.size(); index++) {
            const Source& source = sources[index];
            StepManiaChart chart;
            chart.index = index;
            chart.steps_type = lower(trim(source.steps_type));
            chart.difficulty = canonical_difficulty(source.difficulty);
            chart.description = clean_text(source.description);
            chart.meter = (int32_t)std::lround(std::clamp(number_or(source.meter, 0.0), -1e6, 1e6));

            try {
                std::optional<uint8_t> key_count = key_count_for(chart.steps_type);

                if (!key_count) {
                    throw std::runtime_error(std::format("steps type '{}' is not supported", clean_text(chart.steps_type)));
                }

                Timing timing = timing_for(song, source.tags);
                std::vector<Hit> hits = parse_notes(source.notes, *key_count, timing);

                if (hits.empty()) {
                    throw std::runtime_error("chart has no notes");
                }

                chart.chart_key = stepmania_chart_key(source.notes, *key_count, song, source.tags);
                chart.chart = chart_json(song, source, *key_count, std::move(hits), timing.timing_points());
            }
            catch (const std::exception& e) {
                chart.chart = nullptr;
                chart.error = e.what();
            }

            charts.push_back(std::move(chart));
        }

        return charts;
    }
}

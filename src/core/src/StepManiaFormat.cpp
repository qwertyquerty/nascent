#include "StepManiaFormat.h"

#include "Hit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <locale>
#include <map>
#include <optional>
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
    }

    std::vector<StepManiaChart> stepmania_to_chart_jsons(std::string_view text, std::string& error) {
        if (text.starts_with(UTF8_BOM)) {
            text.remove_prefix(UTF8_BOM.size());
        }

        Tags song;
        std::vector<Source> sources;
        read_sources(read_tags(text), song, sources);

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

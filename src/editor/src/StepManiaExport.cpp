#include "StepManiaExport.h"

#include "ChartDocument.h"
#include "Utf8Path.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <string_view>
#include <vector>

namespace nascent {
    namespace {
        constexpr int64_t ROWS_PER_BEAT = 48;
        constexpr int64_t ROWS_PER_MEASURE = ROWS_PER_BEAT * 4;
        constexpr std::array<int64_t, 10> MEASURE_LINES = {4, 8, 12, 16, 24, 32, 48, 64, 96, 192};
        constexpr double SAMPLE_LENGTH_SECONDS = 15.0;

        struct StepsType {
            uint8_t keys;
            std::string_view name;
        };

        constexpr std::array STEPS_TYPES = {
            StepsType{3, "dance-threepanel"},
            StepsType{4, "dance-single"},
            StepsType{5, "pump-single"},
            StepsType{6, "dance-solo"},
            StepsType{7, "kb7-single"},
            StepsType{8, "dance-double"},
            StepsType{10, "pump-double"},
            StepsType{12, "beat-double5"},
            StepsType{16, "beat-double7"},
        };

        struct Segment {
            double start_ms = 0;
            double start_beat = 0;
            double beat_length = 0;
            uint8_t meter = 4;
        };

        class BeatMap {
            std::vector<Segment> segments;

            public:
            bool build(const ChartData& data, std::string& error) {
                const std::vector<ChartTimingPoint>& points = data.timing_points;

                if (points.empty()) {
                    error = "add a timing point first, StepMania charts are written in beats";
                    return false;
                }

                double origin = points.front().time;
                double earliest = origin;

                for (const Hit& hit : data.hits) {
                    earliest = std::min(earliest, (double)hit.time);
                }

                double measure = points.front().beat_length * 4.0;

                if (earliest < origin) {
                    origin -= std::ceil((origin - earliest) / measure) * measure;
                }

                segments.push_back({origin, 0.0, points.front().beat_length, points.front().meter});

                for (size_t index = 1; index < points.size(); index++) {
                    const Segment& previous = segments.back();
                    double beat = previous.start_beat + (points[index].time - previous.start_ms) / previous.beat_length;
                    segments.push_back({(double)points[index].time, beat, points[index].beat_length, points[index].meter});
                }

                return true;
            }

            double beat_at(double ms) const {
                auto after = std::upper_bound(segments.begin(), segments.end(), ms, [](double value, const Segment& segment) {
                    return value < segment.start_ms;
                });
                const Segment& segment = after == segments.begin() ? segments.front() : *(after - 1);
                return segment.start_beat + (ms - segment.start_ms) / segment.beat_length;
            }

            int64_t row_at(double ms) const {
                return std::max<int64_t>(0, std::llround(beat_at(ms) * ROWS_PER_BEAT));
            }

            double offset_seconds() const {
                return -segments.front().start_ms / 1000.0;
            }

            const std::vector<Segment>& all() const {
                return segments;
            }
        };

        std::string escaped(std::string_view text) {
            std::string out;

            for (char c : text) {
                if (c == '\n' || c == '\r') {
                    out.push_back(' ');
                    continue;
                }

                if (c == ':' || c == ';' || c == '#' || c == '\\' || c == '/') {
                    out.push_back('\\');
                }

                out.push_back(c);
            }

            return out;
        }

        std::string number(double value) {
            std::string text = std::format("{:.6f}", value);
            text.erase(text.find_last_not_of('0') + 1);

            if (!text.empty() && text.back() == '.') {
                text.push_back('0');
            }

            return text == "-0.0" ? "0.0" : text;
        }

        std::string unicode_or(const std::string& unicode, const std::string& fallback) {
            return unicode.empty() ? fallback : unicode;
        }

        std::optional<std::string_view> steps_type_for(uint8_t keys) {
            for (const StepsType& type : STEPS_TYPES) {
                if (type.keys == keys) {
                    return type.name;
                }
            }

            return std::nullopt;
        }

        std::string bpm_list(const BeatMap& map) {
            std::string out;

            for (const Segment& segment : map.all()) {
                out += std::format("{}{}={}", out.empty() ? "" : ",", number(segment.start_beat), number(60000.0 / segment.beat_length));
            }

            return out;
        }

        std::string time_signatures(const BeatMap& map) {
            std::string out;
            uint8_t last = 0;

            for (const Segment& segment : map.all()) {
                if (segment.meter == last) {
                    continue;
                }

                last = segment.meter;
                out += std::format("{}{}={}=4", out.empty() ? "" : ",", number(segment.start_beat), segment.meter);
            }

            return out;
        }

        std::string measures(const ChartData& data, const BeatMap& map) {
            uint8_t keys = data.info.key_count;
            std::map<int64_t, std::string> rows;

            auto mark = [&](int64_t row, uint8_t key, char symbol) {
                std::string& line = rows.try_emplace(row, std::string(keys, '0')).first->second;

                if (line[key] == '0') {
                    line[key] = symbol;
                }
            };

            for (const Hit& hit : data.hits) {
                int64_t head = map.row_at(hit.time);
                int64_t tail = hit.hit_type == HitType::HOLD ? map.row_at(hit.end_time) : head;

                if (tail > head) {
                    mark(head, hit.key, '2');
                    mark(tail, hit.key, '3');
                }
                else {
                    mark(head, hit.key, '1');
                }
            }

            int64_t last_measure = rows.empty() ? 0 : rows.rbegin()->first / ROWS_PER_MEASURE;
            std::string out;

            for (int64_t measure = 0; measure <= last_measure; measure++) {
                int64_t start = measure * ROWS_PER_MEASURE;
                auto first = rows.lower_bound(start);
                auto end = rows.lower_bound(start + ROWS_PER_MEASURE);
                int64_t lines = MEASURE_LINES.back();

                for (int64_t candidate : MEASURE_LINES) {
                    int64_t spacing = ROWS_PER_MEASURE / candidate;

                    if (std::all_of(first, end, [&](const auto& row) { return (row.first - start) % spacing == 0; })) {
                        lines = candidate;
                        break;
                    }
                }

                if (measure > 0) {
                    out += ",\n";
                }

                int64_t spacing = ROWS_PER_MEASURE / lines;

                for (int64_t line = 0; line < lines; line++) {
                    auto found = rows.find(start + line * spacing);
                    out += found == rows.end() ? std::string(keys, '0') : found->second;
                    out += '\n';
                }
            }

            return out;
        }

        std::string song_header(const ChartData& data, const BeatMap& map, bool ssc) {
            const ChartInfo& info = data.info;
            std::string out;

            if (ssc) {
                out += "#VERSION:0.83;\n";
            }

            out += std::format("#TITLE:{};\n", escaped(unicode_or(info.title_unicode, info.title)));
            out += "#SUBTITLE:;\n";
            out += std::format("#ARTIST:{};\n", escaped(unicode_or(info.artist_unicode, info.artist)));
            out += std::format("#TITLETRANSLIT:{};\n", escaped(info.title));
            out += "#SUBTITLETRANSLIT:;\n";
            out += std::format("#ARTISTTRANSLIT:{};\n", escaped(info.artist));
            out += "#GENRE:;\n";
            out += std::format("#CREDIT:{};\n", escaped(info.mapper));
            out += "#BANNER:;\n";
            out += std::format("#BACKGROUND:{};\n", escaped(info.image_file));
            out += "#CDTITLE:;\n";
            out += std::format("#MUSIC:{};\n", escaped(info.audio_file));
            out += std::format("#OFFSET:{};\n", number(map.offset_seconds()));
            out += std::format("#SAMPLESTART:{};\n", number(std::max(0, info.preview_time) / 1000.0));
            out += std::format("#SAMPLELENGTH:{};\n", number(SAMPLE_LENGTH_SECONDS));
            out += "#SELECTABLE:YES;\n";
            out += std::format("#BPMS:{};\n", bpm_list(map));
            out += "#STOPS:;\n";

            if (ssc) {
                out += std::format("#TIMESIGNATURES:{};\n", time_signatures(map));
            }

            out += "#BGCHANGES:;\n#KEYSOUNDS:;\n#ATTACKS:;\n";
            return out;
        }

        bool prepare(const ChartData& data, BeatMap& map, std::string_view& steps_type, std::string& error) {
            std::optional<std::string_view> type = steps_type_for(data.info.key_count);

            if (!type) {
                error = std::format("StepMania has no {}-key style", data.info.key_count);
                return false;
            }

            steps_type = *type;
            return map.build(data, error);
        }

        enum class Panel : uint8_t {
            LEFT,
            DOWN,
            UP,
            RIGHT,
            UP_LEFT,
            UP_RIGHT,
        };

        struct DwiStyle {
            uint8_t keys;
            std::string_view tag;
            std::array<Panel, 8> panels;
        };

        constexpr std::array DWI_STYLES = {
            DwiStyle{4, "SINGLE", {Panel::LEFT, Panel::DOWN, Panel::UP, Panel::RIGHT}},
            DwiStyle{6, "SOLO", {Panel::LEFT, Panel::UP_LEFT, Panel::DOWN, Panel::UP, Panel::UP_RIGHT, Panel::RIGHT}},
            DwiStyle{8, "DOUBLE", {Panel::LEFT, Panel::DOWN, Panel::UP, Panel::RIGHT, Panel::LEFT, Panel::DOWN, Panel::UP, Panel::RIGHT}},
        };

        char dwi_symbol(const std::set<Panel>& panels) {
            static const std::map<std::set<Panel>, char> SYMBOLS = {
                {{Panel::LEFT, Panel::DOWN}, '1'},
                {{Panel::DOWN}, '2'},
                {{Panel::DOWN, Panel::RIGHT}, '3'},
                {{Panel::LEFT}, '4'},
                {{Panel::RIGHT}, '6'},
                {{Panel::LEFT, Panel::UP}, '7'},
                {{Panel::UP}, '8'},
                {{Panel::UP, Panel::RIGHT}, '9'},
                {{Panel::UP, Panel::DOWN}, 'A'},
                {{Panel::LEFT, Panel::RIGHT}, 'B'},
                {{Panel::UP_LEFT}, 'C'},
                {{Panel::UP_RIGHT}, 'D'},
                {{Panel::LEFT, Panel::UP_LEFT}, 'E'},
                {{Panel::UP_LEFT, Panel::DOWN}, 'F'},
                {{Panel::UP_LEFT, Panel::UP}, 'G'},
                {{Panel::UP_LEFT, Panel::RIGHT}, 'H'},
                {{Panel::LEFT, Panel::UP_RIGHT}, 'I'},
                {{Panel::DOWN, Panel::UP_RIGHT}, 'J'},
                {{Panel::UP, Panel::UP_RIGHT}, 'K'},
                {{Panel::UP_RIGHT, Panel::RIGHT}, 'L'},
                {{Panel::UP_LEFT, Panel::UP_RIGHT}, 'M'},
            };

            auto found = SYMBOLS.find(panels);
            return found == SYMBOLS.end() ? '0' : found->second;
        }

        struct DwiRow {
            std::set<Panel> steps;
            std::set<Panel> holds;
        };

        std::string dwi_row(const DwiRow& row) {
            if (row.steps.empty()) {
                return "0";
            }

            bool holds_fit = row.holds.empty() || dwi_symbol(row.holds) != '0';

            if (row.steps.size() <= 2 && holds_fit) {
                std::string out(1, dwi_symbol(row.steps));

                if (!row.holds.empty()) {
                    out += '!';
                    out += dwi_symbol(row.holds);
                }

                return out;
            }

            std::string out = "<";

            for (Panel panel : row.steps) {
                out += dwi_symbol({panel});

                if (row.holds.count(panel) > 0) {
                    out += '!';
                    out += dwi_symbol({panel});
                }
            }

            return out + ">";
        }

        std::string dwi_steps(const std::map<int64_t, DwiRow>& rows) {
            constexpr std::array<std::pair<int64_t, std::pair<char, char>>, 5> RESOLUTIONS = {{
                {2, {0, 0}},
                {4, {'(', ')'}},
                {6, {'[', ']'}},
                {16, {'{', '}'}},
                {48, {'`', '\''}},
            }};

            int64_t last_beat = rows.empty() ? 0 : rows.rbegin()->first / ROWS_PER_BEAT;
            std::string out;

            for (int64_t beat = 0; beat <= last_beat; beat++) {
                int64_t start = beat * ROWS_PER_BEAT;
                auto first = rows.lower_bound(start);
                auto end = rows.lower_bound(start + ROWS_PER_BEAT);
                const auto* chosen = &RESOLUTIONS.back();

                for (const auto& resolution : RESOLUTIONS) {
                    int64_t spacing = ROWS_PER_BEAT / resolution.first;

                    if (std::all_of(first, end, [&](const auto& row) { return (row.first - start) % spacing == 0; })) {
                        chosen = &resolution;
                        break;
                    }
                }

                int64_t spacing = ROWS_PER_BEAT / chosen->first;

                if (chosen->second.first != 0) {
                    out += chosen->second.first;
                }

                for (int64_t slot = 0; slot < chosen->first; slot++) {
                    auto found = rows.find(start + slot * spacing);
                    out += found == rows.end() ? "0" : dwi_row(found->second);
                }

                if (chosen->second.second != 0) {
                    out += chosen->second.second;
                }

                if (beat % 4 == 3) {
                    out += '\n';
                }
            }

            return out;
        }
    }

    std::optional<std::string> chart_to_sm(const ChartData& data, int32_t meter, std::string& error) {
        BeatMap map;
        std::string_view steps_type;

        if (!prepare(data, map, steps_type, error)) {
            return std::nullopt;
        }

        std::string out = song_header(data, map, false);
        out += std::format("\n//---------------{} - {}----------------\n", steps_type, escaped(data.info.mapper));
        out += "#NOTES:\n";
        out += std::format("     {}:\n", steps_type);
        out += std::format("     {}:\n", escaped(data.info.mapper));
        out += "     Challenge:\n";
        out += std::format("     {}:\n", std::max(1, meter));
        out += "     0,0,0,0,0:\n";
        out += measures(data, map);
        out += ";\n";
        return out;
    }

    std::optional<std::string> chart_to_ssc(const ChartData& data, int32_t meter, std::string& error) {
        BeatMap map;
        std::string_view steps_type;

        if (!prepare(data, map, steps_type, error)) {
            return std::nullopt;
        }

        const ChartInfo& info = data.info;
        std::string out = song_header(data, map, true);
        out += std::format("\n//---------------{} - {}----------------\n", steps_type, escaped(info.difficulty_name));
        out += "#NOTEDATA:;\n";
        out += std::format("#CHARTNAME:{};\n", escaped(info.difficulty_name));
        out += std::format("#STEPSTYPE:{};\n", steps_type);
        out += std::format("#DESCRIPTION:{};\n", escaped(info.difficulty_name));
        out += "#CHARTSTYLE:;\n";
        out += "#DIFFICULTY:Edit;\n";
        out += std::format("#METER:{};\n", std::max(1, meter));
        out += "#RADARVALUES:0,0,0,0,0;\n";
        out += std::format("#CREDIT:{};\n", escaped(info.mapper));
        out += "#NOTES:\n";
        out += measures(data, map);
        out += ";\n";
        return out;
    }

    std::optional<std::string> chart_to_dwi(const ChartData& data, int32_t meter, std::string& error) {
        const ChartInfo& info = data.info;
        auto style = std::find_if(DWI_STYLES.begin(), DWI_STYLES.end(), [&](const DwiStyle& candidate) { return candidate.keys == info.key_count; });

        if (style == DWI_STYLES.end()) {
            error = "DWI only has 4-key, 6-key and 8-key styles";
            return std::nullopt;
        }

        BeatMap map;

        if (!map.build(data, error)) {
            return std::nullopt;
        }

        size_t pads = style->keys == 8 ? 2 : 1;
        std::vector<std::map<int64_t, DwiRow>> rows(pads);

        for (const Hit& hit : data.hits) {
            size_t pad = pads == 2 && hit.key >= 4 ? 1 : 0;
            Panel panel = style->panels[hit.key];
            int64_t head = map.row_at(hit.time);
            int64_t tail = hit.hit_type == HitType::HOLD ? map.row_at(hit.end_time) : head;
            rows[pad][head].steps.insert(panel);

            if (tail > head) {
                rows[pad][head].holds.insert(panel);
                rows[pad][tail].steps.insert(panel);
            }
        }

        const std::vector<Segment>& segments = map.all();
        std::string changes;

        for (size_t index = 1; index < segments.size(); index++) {
            changes += std::format("{}{}={}", changes.empty() ? "" : ",", number(segments[index].start_beat * 4.0), number(60000.0 / segments[index].beat_length));
        }

        std::string out;
        out += std::format("#TITLE:{};\n", escaped(info.title));
        out += std::format("#ARTIST:{};\n", escaped(info.artist));
        out += std::format("#FILE:{};\n", escaped(info.audio_file));
        out += std::format("#BPM:{};\n", number(60000.0 / segments.front().beat_length));
        out += std::format("#GAP:{};\n", number(-map.offset_seconds() * 1000.0));

        if (!changes.empty()) {
            out += std::format("#CHANGEBPM:{};\n", changes);
        }

        out += std::format("#SAMPLESTART:{};\n", number(std::max(0, info.preview_time) / 1000.0));
        out += std::format("#SAMPLELENGTH:{};\n", number(SAMPLE_LENGTH_SECONDS));
        out += std::format("\n#{}:MANIAC:{}", style->tag, std::max(1, meter));

        for (const auto& pad : rows) {
            out += ":\n" + dwi_steps(pad);
        }

        out += ";\n";
        return out;
    }

    bool export_stepmania(const ChartData& data, const std::filesystem::path& path, int32_t meter, std::string& error) {
        std::string extension = path_utf8(path.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        std::optional<std::string> text = extension == ".ssc" ? chart_to_ssc(data, meter, error) : extension == ".dwi" ? chart_to_dwi(data, meter, error) : chart_to_sm(data, meter, error);

        if (!text) {
            return false;
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);

        if (!out || !(out << *text)) {
            error = "could not write " + path_utf8(path);
            return false;
        }

        return true;
    }
}

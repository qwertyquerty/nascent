#include "OsuFormat.h"
#include "ChartRules.h"

#include "Hit.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <locale>
#include <map>
#include <sstream>
#include <tuple>
#include <vector>

namespace nascent {
    static constexpr int32_t OSU_MANIA_MODE = 3;
    static constexpr int32_t OSU_HOLD_TYPE = 1 << 7;
    static constexpr int32_t OSU_PLAYFIELD_WIDTH = 512;
    static constexpr int32_t OSU_OD_OFFSET = 4;
    static constexpr int32_t OSU_LATEST_VERSION = 14;
    static constexpr int32_t OSU_EARLY_VERSION = 5;
    static constexpr int32_t OSU_EARLY_VERSION_OFFSET_MS = 24;
    static constexpr int32_t OSU_NO_PREVIEW = -1;
    static constexpr std::string_view OSU_HEADER = "osu file format v";
    static constexpr std::string_view UTF8_BOM = "\xEF\xBB\xBF";

    using Section = std::vector<std::string_view>;

    static std::string_view trim(std::string_view text) {
        size_t start = text.find_first_not_of(" \t\r\n");

        if (start == std::string_view::npos) {
            return {};
        }

        size_t end = text.find_last_not_of(" \t\r\n");
        return text.substr(start, end - start + 1);
    }

    static std::map<std::string, Section, std::less<>> read_sections(std::string_view text) {
        std::map<std::string, Section, std::less<>> sections;
        Section* current = nullptr;

        while (!text.empty()) {
            size_t newline = text.find('\n');
            std::string_view line = trim(text.substr(0, newline));
            text = newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);

            if (line.empty() || line.starts_with("//")) {
                continue;
            }

            if (line.front() == '[' && line.back() == ']') {
                current = &sections[std::string(line.substr(1, line.size() - 2))];
            }
            else if (current != nullptr) {
                current->push_back(line);
            }
        }

        return sections;
    }

    static std::map<std::string, std::string, std::less<>> key_values(const Section& lines) {
        std::map<std::string, std::string, std::less<>> values;

        for (std::string_view line : lines) {
            size_t colon = line.find(':');
            std::string_view key = trim(line.substr(0, colon));
            std::string_view value = colon == std::string_view::npos ? std::string_view() : trim(line.substr(colon + 1));
            values.emplace(std::string(key), std::string(value));
        }

        return values;
    }

    static std::vector<std::string> split_row(std::string_view line) {
        std::vector<std::string> fields(1);
        bool quoted = false;

        for (char c : line) {
            if (c == '"') {
                quoted = !quoted;
            }
            else if (c == ',' && !quoted) {
                fields.emplace_back();
            }
            else {
                fields.back().push_back(c);
            }
        }

        return fields;
    }

    static bool parse_number(std::string_view text, double& value) {
        text = trim(text);
        int64_t integer = 0;
        auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), integer);

        if (ec == std::errc() && end == text.data() + text.size()) {
            value = (double)integer;
            return true;
        }

        std::istringstream stream{std::string(text)};
        stream.imbue(std::locale::classic());
        stream >> value;
        return !stream.fail() && std::isfinite(value);
    }

    static double number(std::string_view text, std::string_view what) {
        double value = 0;

        if (!parse_number(text, value)) {
            throw std::runtime_error(std::format("{} '{}' is not a number", what, text));
        }

        return value;
    }

    static int32_t format_version(std::string_view text) {
        text = trim(text);

        if (!text.starts_with(OSU_HEADER)) {
            return OSU_LATEST_VERSION;
        }

        text.remove_prefix(OSU_HEADER.size());
        int32_t version = 0;
        auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), version);
        return ec == std::errc() && end != text.data() ? version : OSU_LATEST_VERSION;
    }

    static std::string value_or(const std::map<std::string, std::string, std::less<>>& values, std::string_view key, std::string_view fallback = "") {
        auto it = values.find(key);
        return it != values.end() ? it->second : std::string(fallback);
    }

    std::optional<nlohmann::json> osu_to_chart_json(std::string_view text, std::string& error) {
        bool modified = false;
        return osu_to_chart_json(text, error, modified);
    }

    std::optional<nlohmann::json> osu_to_chart_json(std::string_view text, std::string& error, bool& modified, bool allow_empty) {
        modified = false;

        if (text.starts_with(UTF8_BOM)) {
            text.remove_prefix(UTF8_BOM.size());
        }

        if (!trim(text).starts_with("osu file format")) {
            error = "not an .osu file";
            return std::nullopt;
        }

        try {
            int32_t shift = format_version(text) < OSU_EARLY_VERSION ? OSU_EARLY_VERSION_OFFSET_MS : 0;
            auto sections = read_sections(text);
            auto general = key_values(sections["General"]);
            auto metadata = key_values(sections["Metadata"]);
            auto difficulty = key_values(sections["Difficulty"]);

            if ((int32_t)number(value_or(general, "Mode", "0"), "Mode") != OSU_MANIA_MODE) {
                error = "not an osu!mania beatmap";
                return std::nullopt;
            }

            int32_t key_count = (int32_t)number(value_or(difficulty, "CircleSize"), "CircleSize");

            if (key_count <= 0) {
                error = std::format("key count {} is invalid", key_count);
                return std::nullopt;
            }

            std::string background;

            for (std::string_view line : sections["Events"]) {
                std::vector<std::string> row = split_row(line);

                if (row.size() > 2 && trim(row[0]) == "0") {
                    background = std::string(trim(row[2]));
                    break;
                }
            }

            std::vector<Hit> hits;

            for (std::string_view line : sections["HitObjects"]) {
                std::vector<std::string> row = split_row(line);

                if (row.size() < 5) {
                    throw std::runtime_error(std::format("hit object '{}' has too few fields", line));
                }

                bool hold = ((int32_t)number(row[3], "hit object type") & OSU_HOLD_TYPE) != 0;
                int32_t lane = (int32_t)number(row[0], "hit object x") * key_count / OSU_PLAYFIELD_WIDTH;
                modified = modified || lane < 0 || lane >= key_count;
                Hit hit;
                hit.key = (uint8_t)std::clamp(lane, 0, key_count - 1);
                hit.time = (int32_t)number(row[2], "hit object time") + shift;
                hit.hit_type = hold ? HitType::HOLD : HitType::HIT;

                if (hold) {
                    if (row.size() < 6) {
                        throw std::runtime_error(std::format("hold '{}' has no end time", line));
                    }

                    hit.end_time = (int32_t)number(std::string_view(row[5]).substr(0, row[5].find(':')), "hold end time") + shift;

                    if (hit.end_time <= hit.time) {
                        hit.hit_type = HitType::HIT;
                        hit.end_time = hit.time;
                        modified = true;
                    }
                }

                hits.push_back(hit);
            }

            std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
                return std::tie(a.time, a.key) < std::tie(b.time, b.key);
            });

            validate_chart_hits(hits, allow_empty);
            nlohmann::json timing_points = nlohmann::json::array();

            for (std::string_view line : sections["TimingPoints"]) {
                std::vector<std::string> row = split_row(line);

                if (row.size() < 3) {
                    continue;
                }

                double beat_length = number(row[1], "beat length");
                bool uninherited = row.size() > 6 ? (int32_t)number(row[6], "timing point uninherited") != 0 : beat_length > 0;

                if (!uninherited) {
                    continue;
                }

                if (!valid_chart_bpm(60000.0 / beat_length)) {
                    throw std::runtime_error(std::string(CHART_INVALID_BPM));
                }

                timing_points.push_back({
                    {"time", (int32_t)number(row[0], "timing point time") + shift},
                    {"beat_length", beat_length},
                    {"meter", (int32_t)number(row[2], "meter")},
                });
            }

            std::string title = value_or(metadata, "Title");
            std::string artist = value_or(metadata, "Artist");
            int32_t overall_difficulty = (int32_t)std::lround(number(value_or(difficulty, "OverallDifficulty", "0"), "OverallDifficulty"));
            int32_t preview_time = (int32_t)number(value_or(general, "PreviewTime", "0"), "PreviewTime");

            return nlohmann::json{
                {"info",
                 {
                     {"title", title},
                     {"title_unicode", value_or(metadata, "TitleUnicode", title)},
                     {"artist", artist},
                     {"artist_unicode", value_or(metadata, "ArtistUnicode", artist)},
                     {"mapper", value_or(metadata, "Creator")},
                     {"difficulty_name", value_or(metadata, "Version")},
                     {"audio_file", value_or(general, "AudioFilename")},
                     {"image_file", background},
                     {"audio_lead_in", (int32_t)number(value_or(general, "AudioLeadIn", "0"), "AudioLeadIn")},
                     {"preview_time", preview_time == OSU_NO_PREVIEW ? preview_time : preview_time + shift},
                     {"hit_accuracy", std::max(overall_difficulty - OSU_OD_OFFSET, 0)},
                     {"key_count", key_count},
                     {"damage", (int32_t)std::lround(number(value_or(difficulty, "HPDrainRate", "0"), "HPDrainRate"))},
                 }},
                {"timing_points", timing_points},
                {"hit_objects", hits},
            };
        }
        catch (const std::exception& e) {
            error = e.what();
            return std::nullopt;
        }
    }
}

#include "PatternLibrary.h"

#include "Const.h"
#include "Utf8Path.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <tuple>

namespace nascent {
    namespace {
        constexpr int FORMAT_VERSION = 1;
        constexpr double BEAT_PRECISION = 1e6;

        double beats_at(const ChartDocument& document, double time) {
            return document.data().timing_points.empty() ? time * ChartDocument::DEFAULT_BPM / 60000.0 : document.beat_at(time);
        }

        double time_at(const ChartDocument& document, double beat) {
            return document.data().timing_points.empty() ? beat * 60000.0 / ChartDocument::DEFAULT_BPM : document.time_at_beat(beat);
        }

        double rounded(double beat) {
            return std::round(beat * BEAT_PRECISION) / BEAT_PRECISION;
        }
    }

    LibraryPattern capture_pattern(const ChartDocument& document, const std::vector<Hit>& hits, std::string name) {
        LibraryPattern pattern;
        pattern.name = std::move(name);
        pattern.key_count = document.data().info.key_count;

        if (hits.empty()) {
            return pattern;
        }

        int32_t earliest = std::min_element(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.time < b.time; })->time;
        double base = beats_at(document, earliest);

        for (const Hit& hit : hits) {
            double start = beats_at(document, hit.time);
            double length = hit.hit_type == HitType::HOLD ? std::max(0.0, rounded(beats_at(document, hit.end_time) - start)) : 0.0;
            pattern.notes.push_back({std::max(0.0, rounded(start - base)), hit.key, length});
        }

        std::sort(pattern.notes.begin(), pattern.notes.end(), [](const LibraryNote& a, const LibraryNote& b) { return std::tie(a.beat, a.key) < std::tie(b.beat, b.key); });
        return pattern;
    }

    std::vector<Hit> library_pattern_hits(const ChartDocument& document, const LibraryPattern& pattern, double at, int32_t lane_offset) {
        std::vector<Hit> hits;
        int32_t keys = document.data().info.key_count;
        double base = beats_at(document, at);

        for (const LibraryNote& note : pattern.notes) {
            int32_t key = note.key + lane_offset;

            if (key < 0 || key >= keys) {
                continue;
            }

            int32_t time = (int32_t)std::lround(time_at(document, base + note.beat));

            if (note.length > 0) {
                int32_t end = std::max(time + 1, (int32_t)std::lround(time_at(document, base + note.beat + note.length)));
                hits.push_back({(uint8_t)key, HitType::HOLD, time, end});
            }
            else {
                hits.push_back({(uint8_t)key, HitType::HIT, time, time});
            }
        }

        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return std::tie(a.time, a.key) < std::tie(b.time, b.key); });
        hits.erase(std::unique(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.time == b.time && a.key == b.key; }), hits.end());
        return hits;
    }

    double pattern_length_beats(const LibraryPattern& pattern) {
        double length = 0;

        for (const LibraryNote& note : pattern.notes) {
            length = std::max(length, note.beat + note.length);
        }

        return length;
    }

    std::pair<uint8_t, uint8_t> pattern_lanes(const LibraryPattern& pattern) {
        if (pattern.notes.empty()) {
            return {0, 0};
        }

        auto [low, high] = std::minmax_element(pattern.notes.begin(), pattern.notes.end(), [](const LibraryNote& a, const LibraryNote& b) { return a.key < b.key; });
        return {low->key, high->key};
    }

    std::vector<LibraryPattern> load_pattern_library(const std::filesystem::path& path, std::string& error) {
        std::vector<LibraryPattern> patterns;
        std::error_code ec;

        if (!std::filesystem::exists(path, ec)) {
            return patterns;
        }

        std::ifstream in(path, std::ios::binary);

        if (!in) {
            error = "could not read " + path_utf8(path);
            return patterns;
        }

        try {
            nlohmann::json data = nlohmann::json::parse(in);

            for (const nlohmann::json& entry : data.at("patterns")) {
                LibraryPattern pattern;
                pattern.name = entry.value("name", std::string());
                int keys = entry.value("keys", 0);

                if (keys < 1 || keys > MAX_LANES) {
                    continue;
                }

                pattern.key_count = (uint8_t)keys;

                for (const nlohmann::json& note : entry.value("notes", nlohmann::json::array())) {
                    if (!note.is_array() || note.size() < 3) {
                        continue;
                    }

                    double beat = note[0].get<double>();
                    int key = note[1].get<int>();
                    double length = note[2].get<double>();

                    if (std::isfinite(beat) && std::isfinite(length) && beat >= 0 && length >= 0 && key >= 0 && key < keys) {
                        pattern.notes.push_back({beat, (uint8_t)key, length});
                    }
                }

                if (!pattern.notes.empty()) {
                    patterns.push_back(std::move(pattern));
                }
            }
        }
        catch (const nlohmann::json::exception& exception) {
            error = std::string("the pattern library is damaged: ") + exception.what();
            patterns.clear();
        }

        return patterns;
    }

    bool save_pattern_library(const std::filesystem::path& path, const std::vector<LibraryPattern>& patterns, std::string& error) {
        nlohmann::json list = nlohmann::json::array();

        for (const LibraryPattern& pattern : patterns) {
            nlohmann::json notes = nlohmann::json::array();

            for (const LibraryNote& note : pattern.notes) {
                notes.push_back({note.beat, note.key, note.length});
            }

            list.push_back({{"name", pattern.name}, {"keys", pattern.key_count}, {"notes", notes}});
        }

        std::filesystem::path temporary = path;
        temporary += ".tmp";

        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);

            if (!out) {
                error = "could not write " + path_utf8(temporary);
                return false;
            }

            out << nlohmann::json{{"version", FORMAT_VERSION}, {"patterns", list}}.dump();

            if (!out) {
                error = "could not write " + path_utf8(temporary);
                return false;
            }
        }

        std::error_code ec;
        std::filesystem::rename(temporary, path, ec);

        if (ec) {
            std::filesystem::remove(temporary, ec);
            error = "could not replace " + path_utf8(path);
            return false;
        }

        return true;
    }
}

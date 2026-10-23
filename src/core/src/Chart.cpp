#include "Chart.h"
#include "Hit.h"
#include "Accuracy.h"
#include "Const.h"
#include "OsuFormat.h"
#include "Sha256.h"
#include "Utf8Path.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <tuple>

namespace nascent {
    void from_json(const nlohmann::json& j, ChartInfo& p) {
        j.at("title").get_to(p.title);
        j.at("title_unicode").get_to(p.title_unicode);
        j.at("artist").get_to(p.artist);
        j.at("artist_unicode").get_to(p.artist_unicode);
        j.at("mapper").get_to(p.mapper);
        j.at("difficulty_name").get_to(p.difficulty_name);
        j.at("audio_file").get_to(p.audio_file);
        j.at("image_file").get_to(p.image_file);
        j.at("audio_lead_in").get_to(p.audio_lead_in);
        j.at("preview_time").get_to(p.preview_time);
        j.at("hit_accuracy").get_to(p.hit_accuracy);
        j.at("key_count").get_to(p.key_count);
        j.at("damage").get_to(p.damage);
    }

    void to_json(nlohmann::json& j, const ChartInfo& p) {
        j = nlohmann::json{
            {"title", p.title},
            {"title_unicode", p.title_unicode},
            {"artist", p.artist},
            {"artist_unicode", p.artist_unicode},
            {"mapper", p.mapper},
            {"difficulty_name", p.difficulty_name},
            {"audio_file", p.audio_file},
            {"image_file", p.image_file},
            {"audio_lead_in", p.audio_lead_in},
            {"preview_time", p.preview_time},
            {"hit_accuracy", p.hit_accuracy},
            {"key_count", p.key_count},
            {"damage", p.damage}
        };
    }

    void from_json(const nlohmann::json& j, ChartTimingPoint& p) {
        j.at("time").get_to(p.time);
        const nlohmann::json& beat_length = j.at("beat_length");
        p.beat_length = beat_length.is_null() ? std::numeric_limits<float>::infinity() : beat_length.get<float>();
        j.at("meter").get_to(p.meter);
    }

    void to_json(nlohmann::json& j, const ChartTimingPoint& p) {
        j = nlohmann::json{
            {"time", p.time},
            {"beat_length", p.beat_length},
            {"meter", p.meter}
        };
    }

    std::unique_ptr<Chart> Chart::load(const std::filesystem::path& path, float pitch, std::string& error) {
        std::ifstream ifs(path, std::ios::binary);

        if (!ifs) {
            error = "could not open file";
            return nullptr;
        }

        std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        return load_text(text, path, pitch, error);
    }

    std::unique_ptr<Chart> Chart::load_text(std::string_view text, const std::filesystem::path& path, float pitch, std::string& error) {
        nlohmann::json jdata;

        if (path.extension() == OSU_FILE_EXTENSION) {
            std::optional<nlohmann::json> converted = osu_to_chart_json(text, error);

            if (!converted) {
                return nullptr;
            }

            jdata = std::move(*converted);
        }
        else {
            try {
                jdata = nlohmann::json::parse(text);
            }
            catch (const nlohmann::json::exception& e) {
                error = e.what();
                return nullptr;
            }
        }

        std::unique_ptr<Chart> chart = parse(jdata, pitch, error);

        if (chart == nullptr || !chart->assign_paths(path, error)) {
            return nullptr;
        }

        return chart;
    }

    bool Chart::assign_paths(const std::filesystem::path& path, std::string& error) {
        try {
            file_path = path;
            audio_path = path.parent_path() / utf8_path(unpitched_info.audio_file);
            image_path = path.parent_path() / utf8_path(unpitched_info.image_file);
            (void)audio_path.string();
            (void)image_path.string();
        }
        catch (const std::exception& e) {
            error = std::format("invalid audio or image path: {}", e.what());
            return false;
        }

        return true;
    }

    std::unique_ptr<Chart> Chart::restore(const std::filesystem::path& path, ChartInfo info, std::vector<Hit> hits, std::vector<ChartTimingPoint> timing_points, std::string gameplay_hash, std::string difficulty_key, float pitch) {
        std::unique_ptr<Chart> chart(new Chart());
        chart->unpitched_info = std::move(info);
        chart->unpitched_hits = std::move(hits);
        chart->unpitched_timing_points = std::move(timing_points);
        chart->cached_gameplay_hash = std::move(gameplay_hash);
        chart->cached_difficulty_key = std::move(difficulty_key);
        std::string error;

        if (!chart->assign_paths(path, error)) {
            return nullptr;
        }

        chart->set_pitch(pitch);
        return chart;
    }

    std::unique_ptr<Chart> Chart::parse(const nlohmann::json& data, float pitch, std::string& error) {
        std::unique_ptr<Chart> chart(new Chart());

        try {
            data.at("info").get_to(chart->unpitched_info);
            data.at("hit_objects").get_to(chart->unpitched_hits);
            data.at("timing_points").get_to(chart->unpitched_timing_points);
        }
        catch (const nlohmann::json::exception& e) {
            error = e.what();
            return nullptr;
        }

        if (!chart->validate_and_prepare(error)) {
            return nullptr;
        }

        chart->set_pitch(pitch);

        return chart;
    }

    nlohmann::json Chart::serialize() const {
        return nlohmann::json{
            {"info", unpitched_info},
            {"hit_objects", unpitched_hits},
            {"timing_points", unpitched_timing_points}
        };
    }

    const std::string& Chart::gameplay_hash() const {
        return cached_gameplay_hash;
    }

    std::string Chart::compute_gameplay_hash() const {
        std::vector<Hit> sorted_hits = unpitched_hits;

        std::sort(sorted_hits.begin(), sorted_hits.end(), [](const Hit& a, const Hit& b) {
            int32_t a_end = a.hit_type == HitType::HOLD ? a.end_time : a.time;
            int32_t b_end = b.hit_type == HitType::HOLD ? b.end_time : b.time;
            return std::tie(a.time, a.key, a.hit_type, a_end) < std::tie(b.time, b.key, b.hit_type, b_end);
        });

        std::string canonical = std::format(
            "{}\nkeys {}\nhits {}\n",
            CHART_HASH_FORMAT,
            unpitched_info.key_count,
            sorted_hits.size()
        );

        for (const Hit& hit : sorted_hits) {
            if (hit.hit_type == HitType::HOLD) {
                canonical += std::format("{} {} hold {}\n", hit.time, hit.key, hit.end_time);
            }
            else {
                canonical += std::format("{} {} tap\n", hit.time, hit.key);
            }
        }

        return sha256_hex(canonical);
    }

    const std::string& Chart::difficulty_key() const {
        return cached_difficulty_key;
    }

    std::string Chart::compute_difficulty_key() const {
        std::string canonical = std::format("keys {}\nhits {}\n", unpitched_info.key_count, unpitched_hits.size());

        for (const Hit& hit : unpitched_hits) {
            canonical += std::format("{} {} {} {}\n", hit.time, hit.key, (int)hit.hit_type, hit.end_time);
        }

        canonical += std::format("timing {}\n", unpitched_timing_points.size());

        for (const ChartTimingPoint& tp : unpitched_timing_points) {
            canonical += std::format("{} {:a} {}\n", tp.time, tp.beat_length, tp.meter);
        }

        return sha256_hex(canonical);
    }

    std::unique_ptr<Chart> Chart::create(const ChartInfo& info, std::vector<Hit> hits, std::vector<ChartTimingPoint> timing_points, std::string& error) {
        std::unique_ptr<Chart> chart(new Chart());
        chart->unpitched_info = info;
        chart->unpitched_hits = std::move(hits);
        chart->unpitched_timing_points = std::move(timing_points);

        if (!chart->validate_and_prepare(error)) {
            return nullptr;
        }

        chart->set_pitch(1.0f);

        return chart;
    }

    bool Chart::validate_and_prepare(std::string& error) {
        ChartInfo& info = unpitched_info;

        if (info.key_count == 0 || info.key_count > MAX_LANES) {
            error = std::format("key_count {} is outside 1-{}", info.key_count, MAX_LANES);
            return false;
        }

        if (unpitched_hits.empty()) {
            error = "chart has no hit objects";
            return false;
        }

        for (const Hit& hit : unpitched_hits) {
            if (hit.key >= info.key_count) {
                error = std::format("hit at {}ms is in lane {} but key_count is {}", hit.time, hit.key, info.key_count);
                return false;
            }

            if (hit.hit_type == HitType::HOLD && hit.end_time < hit.time) {
                error = std::format("hold at {}ms ends before it starts", hit.time);
                return false;
            }
        }

        for (const ChartTimingPoint& tp : unpitched_timing_points) {
            if (!(tp.beat_length > 0) || tp.meter == 0) {
                error = std::format("timing point at {}ms has an invalid beat_length or meter", tp.time);
                return false;
            }
        }

        info.hit_accuracy = std::min(info.hit_accuracy, MAX_HIT_ACC_DIFFICULTY);

        std::stable_sort(unpitched_hits.begin(), unpitched_hits.end(), [](const Hit& a, const Hit& b) {
            return a.time < b.time;
        });

        std::stable_sort(unpitched_timing_points.begin(), unpitched_timing_points.end(), [](const ChartTimingPoint& a, const ChartTimingPoint& b) {
            return a.time < b.time;
        });

        cached_gameplay_hash = compute_gameplay_hash();
        cached_difficulty_key = compute_difficulty_key();
        return true;
    }

    void Chart::set_pitch(float pitch) {
        this->pitch = pitch;

        info = unpitched_info;
        info.audio_lead_in = (int32_t)std::lround(unpitched_info.audio_lead_in / pitch);
        pitched_timing(pitch, hits, timing_points);
    }

    void Chart::release_notes() {
        unpitched_hits = {};
        hits = {};
        notes_loaded = false;
    }

    void Chart::restore_notes(std::vector<Hit> notes) {
        unpitched_hits = std::move(notes);
        notes_loaded = true;
        set_pitch(pitch);
    }

    bool Chart::has_notes() const {
        return notes_loaded;
    }

    void Chart::pitched_timing(float pitch, std::vector<Hit>& out_hits, std::vector<ChartTimingPoint>& out_timing_points) const {
        out_hits = unpitched_hits;
        out_timing_points = unpitched_timing_points;

        for (Hit& hit : out_hits) {
            hit.time = (int32_t)std::lround(hit.time / pitch);
            hit.end_time = (int32_t)std::lround(hit.end_time / pitch);
        }

        for (ChartTimingPoint& tp : out_timing_points) {
            tp.beat_length /= pitch;
            tp.time = (int32_t)std::lround(tp.time / pitch);
        }
    }

    uint8_t Chart::unpitched_key_count() const {
        return unpitched_info.key_count;
    }

    const ChartInfo& Chart::unpitched_chart_info() const {
        return unpitched_info;
    }

    const std::vector<Hit>& Chart::unpitched_hit_objects() const {
        return unpitched_hits;
    }

    const std::vector<ChartTimingPoint>& Chart::unpitched_timing() const {
        return unpitched_timing_points;
    }

    BpmRange Chart::bpm_range() const {
        BpmRange range;

        if (unpitched_timing_points.empty()) {
            return range;
        }

        int32_t start = INT32_MAX;
        int32_t end = INT32_MIN;

        for (const Hit& hit : unpitched_hits) {
            start = std::min(start, hit.time);
            end = std::max(end, hit.hit_type == HitType::HOLD ? hit.end_time : hit.time);
        }

        if (unpitched_hits.empty()) {
            start = end = unpitched_timing_points.front().time;
        }

        std::map<int64_t, double> durations;
        bool any = false;

        for (size_t index = 0; index < unpitched_timing_points.size(); index++) {
            const ChartTimingPoint& point = unpitched_timing_points[index];
            int32_t from = index == 0 ? std::min(point.time, start) : point.time;
            int32_t to = index + 1 < unpitched_timing_points.size() ? unpitched_timing_points[index + 1].time : std::max(end, point.time);
            double covered = (double)std::min(to, end) - std::max(from, start);
            double bpm = 60000.0 / point.beat_length;

            if (covered <= 0 && (any || index + 1 < unpitched_timing_points.size())) {
                continue;
            }

            range.min = any ? std::min(range.min, bpm) : bpm;
            range.max = any ? std::max(range.max, bpm) : bpm;
            durations[std::llround(bpm * BPM_PRECISION)] += std::max(0.0, covered);
            any = true;
        }

        auto longest = std::max_element(durations.begin(), durations.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
        range.main = longest != durations.end() ? longest->first / BPM_PRECISION : 0;
        return range;
    }

    int32_t Chart::get_duration_ms() const {
        return get_chart_end_ms() - get_chart_start_ms();
    }

    int32_t Chart::get_chart_end_ms() const {
        int32_t end = 0;

        for (const Hit& hit : hits) {
            end = std::max(end, hit.hit_type == HitType::HOLD ? hit.end_time : hit.time);
        }

        return end;
    }

    int32_t Chart::get_chart_start_ms() const {
        return hits.empty() ? 0 : hits.front().time;
    }

    int32_t Chart::intro_skip_ms() const {
        int32_t first_note = get_chart_start_ms();
        return first_note > INTRO_SKIP_MIN_GAP_MS ? first_note - INTRO_SKIP_LEAD_MS : 0;
    }

    const ChartTimingPoint* Chart::timing_point_at(double time) const {
        auto it = std::upper_bound(timing_points.begin(), timing_points.end(), time, [](double t, const ChartTimingPoint& tp) {
            return t < tp.time;
        });

        return it == timing_points.begin() ? nullptr : &*(it - 1);
    }

    uint32_t Chart::section_beat_at(double time) const {
        const ChartTimingPoint* tp = timing_point_at(time);
        return tp != nullptr ? (uint32_t)((time - tp->time) / tp->beat_length) : 0;
    }

    uint8_t Chart::meter_beat_at(double time) const {
        const ChartTimingPoint* tp = timing_point_at(time);
        return tp != nullptr ? (uint8_t)(section_beat_at(time) % tp->meter) : 0;
    }

    float Chart::beat_progress_at(double time) const {
        const ChartTimingPoint* tp = timing_point_at(time);

        if (tp == nullptr) {
            return 0.0f;
        }

        double beats = (time - tp->time) / tp->beat_length;
        return (float)(beats - std::floor(beats));
    }

    uint32_t Chart::held_lanes_at(double time) const {
        uint32_t lanes = 0;

        for (const Hit& hit : hits) {
            if (hit.time > time) {
                break;
            }

            if (hit.hit_type == HitType::HOLD && hit.end_time > time) {
                lanes |= (1u << hit.key);
            }
        }

        return lanes;
    }
}

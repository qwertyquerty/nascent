#include "ChartDocument.h"

#include "Accuracy.h"
#include "ChartSource.h"
#include "StepManiaFormat.h"
#include "Utf8Path.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
#include <set>
#include <tuple>

namespace nascent {
    namespace {
        std::filesystem::path relative_to(const std::filesystem::path& file, const std::filesystem::path& folder) {
            std::error_code ec;
            std::filesystem::path relative = std::filesystem::relative(file, folder, ec);
            return ec || relative.empty() ? file : relative;
        }

        std::filesystem::path absolute_from(const std::string& stored, const std::filesystem::path& folder) {
            if (stored.empty()) {
                return {};
            }

            std::filesystem::path path = utf8_path(stored);
            return path.is_absolute() ? path : folder / path;
        }

        std::string generic_utf8(const std::filesystem::path& path) {
            std::u8string text = path.generic_u8string();
            return std::string(text.begin(), text.end());
        }

        bool same_file(const std::filesystem::path& a, const std::filesystem::path& b) {
            std::error_code ec;
            bool equivalent = std::filesystem::equivalent(a, b, ec);
            return ec ? a.lexically_normal() == b.lexically_normal() : equivalent;
        }

        std::string sanitized(std::string text) {
            constexpr std::string_view FORBIDDEN = "<>:\"/\\|?*";
            std::replace_if(text.begin(), text.end(), [&](char c) { return FORBIDDEN.find(c) != std::string_view::npos || (unsigned char)c < 32; }, '_');
            return text;
        }
    }

    bool same_hit(const Hit& a, const Hit& b) {
        return a.key == b.key && a.time == b.time && a.hit_type == b.hit_type && a.end_time == b.end_time;
    }

    std::vector<Hit> shifted_hits(const std::vector<Hit>& hits, int32_t time_offset, int32_t key_offset) {
        std::vector<Hit> moved = hits;

        for (Hit& hit : moved) {
            hit.key = (uint8_t)(hit.key + key_offset);
            hit.time += time_offset;
            hit.end_time += time_offset;
        }

        return moved;
    }

    std::vector<Hit> mirrored_hits(const std::vector<Hit>& hits, uint8_t key_count) {
        std::vector<Hit> mirrored = hits;

        for (Hit& hit : mirrored) {
            hit.key = (uint8_t)(key_count - 1 - hit.key);
        }

        return mirrored;
    }

    std::vector<Hit> rotated_hits(const std::vector<Hit>& hits, uint8_t key_count, int32_t columns) {
        std::vector<Hit> rotated = hits;
        int32_t count = std::max<int32_t>(key_count, 1);

        for (Hit& hit : rotated) {
            hit.key = (uint8_t)((((int32_t)hit.key + columns) % count + count) % count);
        }

        return rotated;
    }

    void replace_hits(ChartData& data, const std::vector<Hit>& removed, const std::vector<Hit>& added) {
        std::set<std::pair<int32_t, uint8_t>> gone;
        std::set<std::pair<int32_t, uint8_t>> taken;

        for (const Hit& hit : removed) {
            gone.insert({hit.time, hit.key});
        }

        for (const Hit& hit : added) {
            taken.insert({hit.time, hit.key});
        }

        std::erase_if(data.hits, [&](const Hit& hit) { return gone.contains({hit.time, hit.key}) || taken.contains({hit.time, hit.key}); });
        data.hits.insert(data.hits.end(), added.begin(), added.end());
    }

    std::string chart_to_json(const ChartData& data) {
        nlohmann::json document = {
            {"info", data.info},
            {"hit_objects", data.hits},
            {"timing_points", data.timing_points},
        };

        return document.dump(1, '\t');
    }

    bool ChartDocument::is_stepmania(const std::filesystem::path& path) {
        std::string extension = path_utf8(path.extension());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return extension == SM_FILE_EXTENSION || extension == SSC_FILE_EXTENSION || extension == DWI_FILE_EXTENSION;
    }

    std::vector<ChartDocument::ChartChoice> ChartDocument::stepmania_charts(const std::filesystem::path& path, std::string& error) {
        std::ifstream in(path, std::ios::binary);

        if (!in) {
            error = "could not open file";
            return {};
        }

        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::vector<ChartChoice> choices;

        for (StepManiaChart& chart : stepmania_to_chart_jsons(text, error, true)) {
            std::string label = std::format("{} {}", chart.steps_type, chart.difficulty);

            if (chart.meter > 0) {
                label += std::format(" {}", chart.meter);
            }

            if (!chart.description.empty()) {
                label += " - " + chart.description;
            }

            choices.push_back({std::move(label), std::move(chart.chart), std::move(chart.error), chart.modified});
        }

        return choices;
    }

    std::unique_ptr<ChartDocument> ChartDocument::from_json(const nlohmann::json& data, const std::filesystem::path& source, std::string& error) {
        std::unique_ptr<Chart> chart = Chart::parse(data, 1.0f, error, true);

        if (chart == nullptr) {
            return nullptr;
        }

        std::unique_ptr<ChartDocument> document(new ChartDocument());
        document->current.info = chart->unpitched_chart_info();
        document->current.hits = chart->unpitched_hit_objects();
        document->current.timing_points = chart->unpitched_timing();
        document->folder = source.parent_path();
        document->shift_to_native_timing(source);
        document->merged = document->normalize();
        document->saved_revision = ~0ull;
        return document;
    }

    void ChartDocument::shift_to_native_timing(const std::filesystem::path& source) {
        int32_t shift = (int32_t)std::lround(source_offset_ms(chart_source(source), audio_path()));

        if (shift == 0) {
            return;
        }

        for (Hit& hit : current.hits) {
            hit.time -= shift;
            hit.end_time -= shift;
        }

        for (ChartTimingPoint& point : current.timing_points) {
            point.time -= shift;
        }
    }

    std::unique_ptr<ChartDocument> ChartDocument::open(const std::filesystem::path& path, std::string& error) {
        if (is_stepmania(path)) {
            std::vector<ChartChoice> choices = stepmania_charts(path, error);

            for (const ChartChoice& choice : choices) {
                if (choice.error.empty()) {
                    return from_json(choice.chart, path, error);
                }
            }

            if (!choices.empty() && error.empty()) {
                error = choices.front().error;
            }

            if (error.empty()) {
                error = "no charts in this file";
            }

            return nullptr;
        }

        std::unique_ptr<Chart> chart = Chart::load(path, 1.0f, error, true);

        if (chart == nullptr) {
            return nullptr;
        }

        std::unique_ptr<ChartDocument> document(new ChartDocument());
        document->current.info = chart->unpitched_chart_info();
        document->current.hits = chart->unpitched_hit_objects();
        document->current.timing_points = chart->unpitched_timing();
        document->folder = path.parent_path();
        document->file = path.extension() == ".json" ? path : std::filesystem::path();
        document->shift_to_native_timing(path);
        document->merged = document->normalize();
        document->saved_revision = document->file.empty() ? ~0ull : document->revision_count;
        return document;
    }

    std::unique_ptr<ChartDocument> ChartDocument::from_audio(const std::filesystem::path& audio) {
        std::unique_ptr<ChartDocument> document(new ChartDocument());
        ChartInfo& info = document->current.info;
        info.title = path_utf8(audio.stem());
        info.title_unicode = info.title;
        info.difficulty_name = "Normal";
        info.audio_file = path_utf8(audio.filename());
        info.key_count = DEFAULT_KEY_COUNT;
        info.hit_accuracy = DEFAULT_HIT_ACCURACY;
        document->current.timing_points.push_back({0, 60000.0f / DEFAULT_BPM, 4});
        document->folder = audio.parent_path();
        document->saved_revision = ~0ull;
        return document;
    }

    std::unique_ptr<ChartDocument> ChartDocument::copy_of(const ChartDocument& source, bool keep_notes) {
        std::unique_ptr<ChartDocument> document(new ChartDocument());
        document->current = source.current;
        document->folder = source.folder;
        document->saved_revision = ~0ull;

        if (!keep_notes) {
            document->current.hits.clear();
        }

        return document;
    }

    std::vector<std::unique_ptr<ChartDocument>> ChartDocument::open_siblings(const std::filesystem::path& opened, const ChartDocument& main) {
        std::vector<std::unique_ptr<ChartDocument>> found;
        std::filesystem::path audio = main.audio_path();
        std::error_code ec;

        if (audio.empty()) {
            return found;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(opened.parent_path(), ec)) {
            std::filesystem::path path = entry.path();
            std::string extension = path_utf8(path.extension());
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });

            if (!entry.is_regular_file(ec) || (extension != ".json" && extension != ".osu") || same_file(path, opened)) {
                continue;
            }

            std::string error;
            std::unique_ptr<ChartDocument> sibling = open(path, error);

            if (sibling != nullptr && same_file(sibling->audio_path(), audio)) {
                found.push_back(std::move(sibling));
            }
        }

        auto difficulty = [](const ChartDocument& document) {
            return document.current.info.difficulty_name;
        };

        std::erase_if(found, [&](const std::unique_ptr<ChartDocument>& candidate) {
            if (!candidate->file.empty()) {
                return false;
            }

            if (difficulty(*candidate) == difficulty(main)) {
                return true;
            }

            return std::any_of(found.begin(), found.end(), [&](const std::unique_ptr<ChartDocument>& other) {
                return !other->file.empty() && difficulty(*other) == difficulty(*candidate);
            });
        });

        std::sort(found.begin(), found.end(), [](const std::unique_ptr<ChartDocument>& a, const std::unique_ptr<ChartDocument>& b) {
            return std::make_pair(a->current.info.key_count, a->current.hits.size()) < std::make_pair(b->current.info.key_count, b->current.hits.size());
        });

        return found;
    }

    const ChartData& ChartDocument::data() const {
        return current;
    }

    const std::filesystem::path& ChartDocument::path() const {
        return file;
    }

    const std::filesystem::path& ChartDocument::directory() const {
        return folder;
    }

    std::filesystem::path ChartDocument::audio_path() const {
        return absolute_from(current.info.audio_file, folder);
    }

    std::filesystem::path ChartDocument::suggested_file_name() const {
        const ChartInfo& info = current.info;
        std::string name = info.artist.empty() ? info.title : info.artist + " - " + info.title;

        if (!info.difficulty_name.empty()) {
            name += " [" + info.difficulty_name + "]";
        }

        return utf8_path(sanitized(name.empty() ? "chart" : name) + ".json");
    }

    uint64_t ChartDocument::revision() const {
        return revision_count;
    }

    bool ChartDocument::dirty() const {
        return revision_count != saved_revision;
    }

    size_t ChartDocument::normalize() {
        uint8_t keys = std::max<uint8_t>(current.info.key_count, 1);
        std::vector<Hit>& hits = current.hits;

        hits.erase(std::remove_if(hits.begin(), hits.end(), [&](const Hit& hit) { return hit.key >= keys; }), hits.end());

        for (Hit& hit : hits) {
            if (hit.hit_type == HitType::HOLD && hit.end_time <= hit.time) {
                hit.hit_type = HitType::HIT;
            }

            if (hit.hit_type == HitType::HIT) {
                hit.end_time = hit.time;
            }
        }

        std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
            return std::tie(a.time, a.key) < std::tie(b.time, b.key);
        });
        size_t before = hits.size();
        hits.erase(std::unique(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.time == b.time && a.key == b.key; }), hits.end());
        size_t removed = before - hits.size();

        std::vector<ChartTimingPoint>& points = current.timing_points;
        points.erase(std::remove_if(points.begin(), points.end(), [](const ChartTimingPoint& point) { return !(point.beat_length > 0) || point.meter == 0; }), points.end());
        std::stable_sort(points.begin(), points.end(), [](const ChartTimingPoint& a, const ChartTimingPoint& b) { return a.time < b.time; });
        current.info.hit_accuracy = std::min(current.info.hit_accuracy, MAX_HIT_ACC_DIFFICULTY);
        return removed;
    }

    size_t ChartDocument::merged_on_open() const {
        return merged;
    }

    void ChartDocument::snapshot() {
        undo_stack.push_back(current);
        redo_stack.clear();

        if (undo_stack.size() > UNDO_LIMIT) {
            undo_stack.erase(undo_stack.begin());
        }
    }

    void ChartDocument::change(const std::function<void(ChartData&)>& edit) {
        snapshot();
        change_without_snapshot(edit);
    }

    void ChartDocument::change_without_snapshot(const std::function<void(ChartData&)>& edit) {
        edit(current);
        normalize();
        revision_count++;
    }

    bool ChartDocument::undo() {
        if (undo_stack.empty()) {
            return false;
        }

        redo_stack.push_back(std::move(current));
        current = std::move(undo_stack.back());
        undo_stack.pop_back();
        revision_count++;
        return true;
    }

    bool ChartDocument::redo() {
        if (redo_stack.empty()) {
            return false;
        }

        undo_stack.push_back(std::move(current));
        current = std::move(redo_stack.back());
        redo_stack.pop_back();
        revision_count++;
        return true;
    }

    bool ChartDocument::can_undo() const {
        return !undo_stack.empty();
    }

    bool ChartDocument::can_redo() const {
        return !redo_stack.empty();
    }

    bool ChartDocument::save(std::string& error) {
        if (file.empty()) {
            error = "choose where to save the chart first";
            return false;
        }

        return save_as(file, error);
    }

    bool ChartDocument::save_as(const std::filesystem::path& path, std::string& error) {
        ChartInfo info = current.info;
        std::filesystem::path target_folder = path.parent_path();

        if (target_folder != folder) {
            if (std::filesystem::path audio = absolute_from(info.audio_file, folder); !audio.empty()) {
                info.audio_file = generic_utf8(relative_to(audio, target_folder));
            }

            if (std::filesystem::path image = absolute_from(info.image_file, folder); !image.empty()) {
                info.image_file = generic_utf8(relative_to(image, target_folder));
            }
        }

        ChartData saved = current;
        saved.info = info;
        std::filesystem::path temporary = path;
        temporary += ".tmp";

        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);

            if (!out) {
                error = "could not write " + path_utf8(temporary);
                return false;
            }

            out << chart_to_json(saved);

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

        current.info = info;
        file = path;
        folder = target_folder;
        saved_revision = revision_count;
        return true;
    }

    void ChartDocument::set_audio(const std::filesystem::path& audio) {
        change([&](ChartData& data) {
            data.info.audio_file = generic_utf8(relative_to(audio, folder));
        });
    }

    void ChartDocument::set_image(const std::filesystem::path& image) {
        change([&](ChartData& data) {
            data.info.image_file = generic_utf8(relative_to(image, folder));
        });
    }

    std::unique_ptr<Chart> ChartDocument::preview() const {
        return Chart::restore(folder / "editor-preview.json", current.info, current.hits, current.timing_points, std::string(), std::string(), 1.0f);
    }

    std::unique_ptr<Chart> ChartDocument::validated(std::string& error) const {
        return Chart::create(current.info, current.hits, current.timing_points, error);
    }

    const ChartTimingPoint* ChartDocument::timing_point_at(double time) const {
        const std::vector<ChartTimingPoint>& points = current.timing_points;

        if (points.empty()) {
            return nullptr;
        }

        auto after = std::upper_bound(points.begin(), points.end(), time + 0.5, [](double value, const ChartTimingPoint& point) {
            return value < point.time;
        });

        return after == points.begin() ? &points.front() : &*(after - 1);
    }

    double ChartDocument::snap_length(double time, uint32_t divisor) const {
        const ChartTimingPoint* point = timing_point_at(time);
        double beat = point != nullptr ? point->beat_length : 60000.0 / DEFAULT_BPM;
        return beat / std::max<uint32_t>(divisor, 1);
    }

    double ChartDocument::snap(double time, uint32_t divisor) const {
        const ChartTimingPoint* point = timing_point_at(time);
        double origin = point != nullptr ? point->time : 0.0;
        double length = snap_length(time, divisor);
        return origin + std::round((time - origin) / length) * length;
    }

    double ChartDocument::step(double time, uint32_t divisor, int32_t steps) const {
        double position = snap(time, divisor);

        if (steps > 0 && position > time + 0.5) {
            steps--;
        }
        else if (steps < 0 && position < time - 0.5) {
            steps++;
        }

        for (int32_t index = 0; index < std::abs(steps); index++) {
            double direction = steps > 0 ? 1.0 : -1.0;
            double length = snap_length(position + direction, divisor);
            position = snap(position + direction * length, divisor);
        }

        return position;
    }

    std::vector<Hit> ChartDocument::notes_in(uint8_t first_key, uint8_t last_key, double from, double to) const {
        std::vector<Hit> found;

        for (const Hit& hit : current.hits) {
            int32_t end = hit.hit_type == HitType::HOLD ? hit.end_time : hit.time;

            if (hit.key >= first_key && hit.key <= last_key && hit.time <= to && end >= from) {
                found.push_back(hit);
            }
        }

        return found;
    }

    bool ChartDocument::contains(const Hit& hit) const {
        auto found = std::lower_bound(current.hits.begin(), current.hits.end(), hit, [](const Hit& a, const Hit& b) { return std::tie(a.time, a.key) < std::tie(b.time, b.key); });
        return found != current.hits.end() && same_hit(*found, hit);
    }

    double ChartDocument::beat_at(double time) const {
        const std::vector<ChartTimingPoint>& points = current.timing_points;

        if (points.empty()) {
            return time;
        }

        if (time < points.front().time) {
            return (time - points.front().time) / points.front().beat_length;
        }

        double beats = 0;

        for (size_t index = 0; index < points.size(); index++) {
            const ChartTimingPoint& point = points[index];

            if (index + 1 == points.size() || time < points[index + 1].time) {
                return beats + (time - point.time) / point.beat_length;
            }

            beats += (points[index + 1].time - point.time) / point.beat_length;
        }

        return beats;
    }

    double ChartDocument::time_at_beat(double beat) const {
        const std::vector<ChartTimingPoint>& points = current.timing_points;

        if (points.empty()) {
            return beat;
        }

        if (beat < 0) {
            return points.front().time + beat * points.front().beat_length;
        }

        double beats = 0;

        for (size_t index = 0; index < points.size(); index++) {
            const ChartTimingPoint& point = points[index];
            bool last = index + 1 == points.size();
            double span = last ? 0.0 : (points[index + 1].time - point.time) / point.beat_length;

            if (last || beat < beats + span) {
                return point.time + (beat - beats) * point.beat_length;
            }

            beats += span;
        }

        return beat;
    }

    std::vector<Hit> ChartDocument::quantized(const std::vector<Hit>& hits, uint32_t divisor, bool ends) const {
        std::vector<Hit> result = hits;

        for (Hit& hit : result) {
            int32_t start = (int32_t)std::lround(std::max(0.0, snap(hit.time, divisor)));

            if (hit.hit_type != HitType::HOLD) {
                hit.time = hit.end_time = start;
                continue;
            }

            int32_t end = ends ? (int32_t)std::lround(snap(hit.end_time, divisor)) : hit.end_time + (start - hit.time);

            if (end <= start) {
                end = (int32_t)std::lround(step(start, divisor, 1));
            }

            hit.time = start;
            hit.end_time = end;
        }

        return result;
    }

    std::vector<Hit> ChartDocument::inverted(const std::vector<Hit>& hits, uint32_t gap_divisor) const {
        std::vector<Hit> result = hits;

        for (Hit& hit : result) {
            auto next = std::find_if(current.hits.begin(), current.hits.end(), [&](const Hit& other) {
                return other.key == hit.key && other.time > hit.time;
            });

            if (next == current.hits.end()) {
                continue;
            }

            double space = next->time - hit.time;
            int32_t length = (int32_t)std::lround(std::max(space / 2.0, space - snap_length(next->time, gap_divisor)));

            if (length > 0) {
                hit.hit_type = HitType::HOLD;
                hit.end_time = hit.time + length;
            }
        }

        return result;
    }

    std::vector<Hit> ChartDocument::stretched(const std::vector<Hit>& hits, double factor, double anchor) const {
        double anchor_beat = beat_at(anchor);
        auto scale = [&](int32_t time) {
            return (int32_t)std::lround(time_at_beat(anchor_beat + (beat_at(time) - anchor_beat) * factor));
        };
        std::vector<Hit> result = hits;

        for (Hit& hit : result) {
            int32_t start = scale(hit.time);
            hit.end_time = hit.hit_type == HitType::HOLD ? scale(hit.end_time) : start;
            hit.time = start;
        }

        return result;
    }

    std::optional<size_t> ChartDocument::note_at(uint8_t key, double time, double tolerance) const {
        std::optional<size_t> best;
        double best_distance = tolerance;

        for (size_t index = 0; index < current.hits.size(); index++) {
            const Hit& hit = current.hits[index];

            if (hit.key != key) {
                continue;
            }

            double distance = std::abs(hit.time - time);

            if (hit.hit_type == HitType::HOLD && time >= hit.time && time <= hit.end_time) {
                distance = std::min(distance, tolerance * 0.5);
            }

            if (distance <= best_distance) {
                best_distance = distance;
                best = index;
            }
        }

        return best;
    }
}

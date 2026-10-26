#pragma once

#include "Accuracy.h"
#include "Chart.h"
#include "Hit.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    struct ChartData {
        ChartInfo info;
        std::vector<Hit> hits;
        std::vector<ChartTimingPoint> timing_points;
    };

    bool same_hit(const Hit& a, const Hit& b);
    std::vector<Hit> shifted_hits(const std::vector<Hit>& hits, int32_t time_offset, int32_t key_offset);
    std::vector<Hit> mirrored_hits(const std::vector<Hit>& hits, uint8_t key_count);
    std::vector<Hit> rotated_hits(const std::vector<Hit>& hits, uint8_t key_count, int32_t columns);
    void replace_hits(ChartData& data, const std::vector<Hit>& removed, const std::vector<Hit>& added);
    std::string chart_to_json(const ChartData& data);

    class ChartDocument {
        protected:
        ChartData current;
        std::filesystem::path file;
        std::filesystem::path folder;
        std::vector<ChartData> undo_stack;
        std::vector<ChartData> redo_stack;
        uint64_t revision_count = 0;
        uint64_t saved_revision = 0;

        void normalize();

        public:
        static constexpr size_t UNDO_LIMIT = 300;
        static constexpr float DEFAULT_BPM = 120.0f;
        static constexpr uint8_t DEFAULT_KEY_COUNT = 4;
        static constexpr uint8_t DEFAULT_HIT_ACCURACY = RANKED_HIT_ACC_DIFFICULTY;

        struct ChartChoice {
            std::string label;
            nlohmann::json chart;
            std::string error;
            bool modified = false;
        };

        static bool is_stepmania(const std::filesystem::path& path);
        static std::vector<ChartChoice> stepmania_charts(const std::filesystem::path& path, std::string& error);
        static std::unique_ptr<ChartDocument> from_json(const nlohmann::json& chart, const std::filesystem::path& source, std::string& error);
        static std::unique_ptr<ChartDocument> open(const std::filesystem::path& path, std::string& error);
        static std::unique_ptr<ChartDocument> from_audio(const std::filesystem::path& audio);

        const ChartData& data() const;
        const std::filesystem::path& path() const;
        const std::filesystem::path& directory() const;
        std::filesystem::path audio_path() const;
        std::filesystem::path suggested_file_name() const;
        uint64_t revision() const;
        bool dirty() const;

        void snapshot();
        void change(const std::function<void(ChartData&)>& edit);
        void change_without_snapshot(const std::function<void(ChartData&)>& edit);
        bool undo();
        bool redo();
        bool can_undo() const;
        bool can_redo() const;

        bool save(std::string& error);
        bool save_as(const std::filesystem::path& path, std::string& error);
        void set_audio(const std::filesystem::path& audio);
        void set_image(const std::filesystem::path& image);

        std::unique_ptr<Chart> preview() const;
        std::unique_ptr<Chart> validated(std::string& error) const;

        const ChartTimingPoint* timing_point_at(double time) const;
        double snap_length(double time, uint32_t divisor) const;
        double snap(double time, uint32_t divisor) const;
        double step(double time, uint32_t divisor, int32_t steps) const;
        std::optional<size_t> note_at(uint8_t key, double time, double tolerance) const;
        std::vector<Hit> notes_in(uint8_t first_key, uint8_t last_key, double from, double to) const;
        bool contains(const Hit& hit) const;
        double beat_at(double time) const;
        double time_at_beat(double beat) const;
        std::vector<Hit> stretched(const std::vector<Hit>& hits, double factor, double anchor) const;
        std::vector<Hit> quantized(const std::vector<Hit>& hits, uint32_t divisor, bool ends) const;
    };
}

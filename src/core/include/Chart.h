#pragma once

#include "json.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Hit.h"

namespace nascent {
    struct ChartInfo {
        std::string title;
        std::string title_unicode;
        std::string artist;
        std::string artist_unicode;
        std::string mapper;
        std::string difficulty_name;
        std::string audio_file;
        std::string image_file;
        int32_t audio_lead_in = 0;
        int32_t preview_time = 0;
        uint8_t hit_accuracy = 0;
        uint8_t key_count = 0;
        uint8_t damage = 0;
    };

    void to_json(nlohmann::json& j, const ChartInfo& p);
    void from_json(const nlohmann::json& j, ChartInfo& p);

    struct ChartTimingPoint {
        int32_t time = 0;
        float beat_length = 0;
        uint8_t meter = 4;
    };

    void to_json(nlohmann::json& j, const ChartTimingPoint& p);
    void from_json(const nlohmann::json& j, ChartTimingPoint& p);

    class Chart {
        private:
        ChartInfo unpitched_info;
        std::vector<Hit> unpitched_hits;
        std::vector<ChartTimingPoint> unpitched_timing_points;

        Chart() = default;

        bool validate_and_prepare(std::string& error);

        public:
        std::filesystem::path file_path;
        std::filesystem::path audio_path;
        std::filesystem::path image_path;

        ChartInfo info;
        std::vector<Hit> hits;
        std::vector<ChartTimingPoint> timing_points;

        float pitch = 1.0;

        static constexpr std::string_view CHART_HASH_FORMAT = "nascent-chart-v2";

        static std::unique_ptr<Chart> load(const std::filesystem::path& path, float pitch, std::string& error);
        static std::unique_ptr<Chart> parse(const nlohmann::json& data, float pitch, std::string& error);
        static std::unique_ptr<Chart> create(const ChartInfo& info, std::vector<Hit> hits, std::vector<ChartTimingPoint> timing_points, std::string& error);

        /**
         * Set the pitch of the chart and recaculate the time of all the notes
         */
        void set_pitch(float);
        void pitched_timing(float pitch, std::vector<Hit>& out_hits, std::vector<ChartTimingPoint>& out_timing_points) const;
        uint8_t unpitched_key_count() const;

        nlohmann::json serialize() const;
        std::string gameplay_hash() const;

        /**
         * Get the length of the Chart in ms from the start of the first hit to the end of the last hit
         */
        int32_t get_duration_ms() const;

        /**
         * Get the start of the played part of the chart (hit time of first hit)
         */
        int32_t get_chart_start_ms() const;

        /**
         * Get the end of the played part of the chart (release time of last hit)
         */
        int32_t get_chart_end_ms() const;

        const ChartTimingPoint* timing_point_at(double time) const;
        uint32_t section_beat_at(double time) const;
        uint8_t meter_beat_at(double time) const;
        float beat_progress_at(double time) const;
        uint32_t held_lanes_at(double time) const;
    };
}

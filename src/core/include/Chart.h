#pragma once

#include "json.h"
using json = nlohmann::json;

#include "boost/filesystem.hpp"

#include <vector>
#include <string>
#include <cstdint>

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
        int32_t audio_lead_in;
        int32_t preview_time;
        uint8_t hit_accuracy;
        uint8_t key_count;
        uint8_t damage;
    };

    void to_json(json& j, const ChartInfo& p);
    void from_json(const json& j, ChartInfo& p);

    struct ChartTimingPoint {
        int32_t time;
        float beat_length;
        uint8_t meter;
    };

    void to_json(json& j, const ChartTimingPoint& p);
    void from_json(const json& j, ChartTimingPoint& p);

    class Chart {
        private:
        ChartInfo unpitched_info;
        std::vector<Hit> unpitched_hits;
        std::vector<ChartTimingPoint> unpitched_timing_points;

        public:
        boost::filesystem::path json_path;
        boost::filesystem::path audio_path;
        boost::filesystem::path image_path;

        ChartInfo info;
        std::vector<Hit> hits;
        std::vector<ChartTimingPoint> timing_points;

        float pitch = 1.0;

        Chart(const std::string& path, float pitch);

        /**
         * Set the pitch of the chart and recaculate the time of all the notes
         */
        void set_pitch(float);

        /**
         * Get the length of the Chart in ms from the start of the first hit to the end of the last hit
         */
        uint32_t get_duration_ms();

        /**
         * Get the start of the played part of the chart (hit time of first hit)
         */
        uint32_t get_chart_start_ms();

        /**
         * Get the end of the played part of the chart (release time of last hit)
         */
        uint32_t get_chart_end_ms();
    };
}

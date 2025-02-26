#pragma once

#include "json.h"
using json = nlohmann::json;

#include "boost/filesystem.hpp"

#include <vector>
#include <string>
#include <cstdint>

namespace nascent {
    enum HitType {
        HIT,
        HOLD
    };

    struct ChartInfo {
        std::string title;
        std::string title_unicode;
        std::string artist;
        std::string artist_unicode;
        std::string mapper;
        std::string difficulty_name;
        std::string audio_file;
        uint32_t audio_lead_in;
        uint32_t preview_time;
        uint8_t hit_accuracy;
        uint8_t key_count;
        uint8_t damage;
    };

    void to_json(json& j, const ChartInfo& p);

    void from_json(const json& j, ChartInfo& p);

    struct ChartHit {
        uint8_t key;
        HitType type;
        uint32_t time;
        uint32_t end_time;
    };

    void to_json(json& j, const ChartHit& p);

    void from_json(const json& j, ChartHit& p);
    class Chart {
        public:
        boost::filesystem::path json_path;
        boost::filesystem::path audio_path;

        ChartInfo info;

        std::vector<ChartHit> hits;

        Chart(const std::string& path);

        /**
         * Get the length of the Chart in ms from the start of the first hit to the end of the last hit
         */
        uint32_t get_duration_ms();
    };
}

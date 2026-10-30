#pragma once

#include "Attempt.h"
#include "Chart.h"
#include "Results.h"
#include "json.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    struct ReplayChart {
        std::string path;
        std::string gameplay_hash;
        std::string title;
        std::string artist;
        std::string difficulty_name;
        uint8_t key_count = 0;
    };

    struct ReplaySettings {
        int32_t audio_input_offset = 0;
        int32_t audio_visual_offset = 0;
        uint32_t scroll_speed = 0;
        bool mirror = false;
        int32_t chart_offset = 0;
    };

    struct ReplayResult {
        double accuracy = 0.0;
        std::string grade;
        uint32_t max_combo = 0;
        std::map<std::string, uint32_t> score_counts;
        double average_error = 0.0;
        double difficulty = 0.0;
        double performance = 0.0;
    };

    struct Replay {
        static constexpr uint32_t FORMAT_VERSION = 1;
        static constexpr size_t FOLDER_HASH_LENGTH = 16;

        uint32_t version = FORMAT_VERSION;
        std::string played_at;
        ReplayChart chart;
        float rate = 1.0f;
        ReplaySettings settings;
        std::vector<AttemptInput> inputs;
        std::vector<double> input_times;
        std::vector<std::array<double, 2>> clock_samples;
        std::vector<std::array<double, 3>> pauses;
        int32_t end_time = 0;
        ReplayResult result;
        nlohmann::json diagnostics;
    };

    void to_json(nlohmann::json& j, const Replay& replay);
    void from_json(const nlohmann::json& j, Replay& replay);

    std::string current_utc_timestamp();
    ReplayResult replay_result_from(const AttemptSummary& summary);
    Replay make_replay(const Attempt& attempt, const AttemptSummary& summary, const ReplayChart& chart, float rate, const ReplaySettings& settings, const std::string& played_at);
    Attempt simulate_replay(const Chart& chart, const Replay& replay);

    std::filesystem::path replay_folder(const std::filesystem::path& directory, const std::string& gameplay_hash);
    std::optional<std::filesystem::path> save_replay(const Replay& replay, const std::filesystem::path& directory, std::string& error);
    std::optional<Replay> load_replay(const std::filesystem::path& path, std::string& error);
}

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

namespace nascent {
    struct AttemptSummary;

    struct LocalBest {
        double accuracy = 0;
        std::string grade;
        uint32_t max_combo = 0;
        double performance = 0;
        std::string played_at;
    };

    class LocalBests {
        public:
        static constexpr const char* FILE_NAME = "bests.json";
        static constexpr uint32_t FILE_VERSION = 1;

        static void load();
        static bool is_backfilled();
        static void backfill(const std::unordered_map<std::string, std::string>& chart_hash_by_folder, const std::filesystem::path& replay_directory);
        static void record(const std::string& chart_hash, float rate, const AttemptSummary& summary, const std::string& played_at);
        static const LocalBest* lookup(const std::string& chart_hash, float rate);
        static std::string last_played(const std::string& chart_hash);
        static void flush();
    };
}

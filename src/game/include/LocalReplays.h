#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nascent {
    struct LocalScore {
        double accuracy = 0;
        uint32_t max_combo = 0;
        std::string played_at;
        float rate = 1.0f;
        std::filesystem::path path;
    };

    class LocalReplays {
        public:
        static constexpr const char* MIGRATED_MARKER = ".gameplay-hash-folders";

        static std::filesystem::path directory();
        static bool folders_migrated();
        static void migrate_folders(const std::unordered_map<std::string, std::string>& gameplay_hash_by_file_folder);
        static std::unordered_map<std::string, std::string> gameplay_hash_by_folder(const std::vector<std::string>& gameplay_hashes);
        static std::vector<LocalScore> scores(const std::string& gameplay_hash, long rate);
        static std::vector<LocalScore> all_scores(const std::string& gameplay_hash);
    };
}

#pragma once

#include "Chart.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nascent {
    class DifficultyCache {
        using Rates = std::vector<std::pair<int32_t, double>>;

        std::filesystem::path path;
        std::filesystem::path legacy_path;
        mutable std::shared_mutex mutex;
        std::mutex save_mutex;
        std::unordered_map<std::string, Rates> entries;
        bool dirty = false;

        void merge(const std::string& key, int32_t rate, double value);
        bool load_binary();
        bool load_legacy(const std::filesystem::path& source);

        public:
        static constexpr uint32_t FILE_VERSION = 2;
        static constexpr uint32_t LEGACY_FILE_VERSION = 1;

        explicit DifficultyCache(std::filesystem::path path);

        static std::string fingerprint();
        static int32_t rate_key(float pitch);

        void load();
        bool save(std::string& error);
        bool is_dirty() const;

        std::optional<double> lookup(const Chart& chart, float pitch) const;
        std::optional<double> lookup(const std::string& difficulty_key, float pitch) const;
        void store(const Chart& chart, float pitch, double difficulty);
        void store(const std::string& difficulty_key, float pitch, double difficulty);
        double difficulty(const Chart& chart, float pitch);
        void retain(const std::unordered_set<std::string>& difficulty_keys);
        void clear();
        size_t size() const;
    };
}

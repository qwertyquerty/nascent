#pragma once

#include "Chart.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace nascent {
    class DifficultyCache {
        std::filesystem::path path;
        mutable std::mutex mutex;
        std::unordered_map<std::string, std::unordered_map<int32_t, double>> entries;
        bool dirty = false;

        public:
        static constexpr uint32_t FILE_VERSION = 1;

        explicit DifficultyCache(std::filesystem::path path);

        static std::string fingerprint();
        static int32_t rate_key(float pitch);

        void load();
        bool save(std::string& error);

        std::optional<double> lookup(const Chart& chart, float pitch) const;
        void store(const Chart& chart, float pitch, double difficulty);
        double difficulty(const Chart& chart, float pitch);
        void retain(const std::unordered_set<std::string>& difficulty_keys);
        size_t size() const;
    };
}

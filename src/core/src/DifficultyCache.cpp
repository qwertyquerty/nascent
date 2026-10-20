#include "DifficultyCache.h"

#include "DifficultyCalculator.h"
#include "SettingsStore.h"
#include "Sha256.h"

#include <cmath>
#include <cstdlib>
#include <format>
#include <utility>

namespace nascent {
    DifficultyCache::DifficultyCache(std::filesystem::path path) : path(std::move(path)) {
    }

    std::string DifficultyCache::fingerprint() {
        return sha256_hex(std::format("{}\n{}\n{}", NASCENT_DIFFICULTY_SOURCE_HASH, nlohmann::json(DifficultyParameters{}).dump(), Chart::CHART_HASH_FORMAT));
    }

    int32_t DifficultyCache::rate_key(float pitch) {
        return (int32_t)std::lround(pitch * 100.0f);
    }

    void DifficultyCache::load() {
        nlohmann::json data;
        std::string error;
        VersionedJsonFile file(path, FILE_VERSION);
        SettingsLoadStatus status = file.load(data, error);

        std::lock_guard<std::mutex> lock(mutex);
        entries.clear();
        dirty = false;

        if (status != SettingsLoadStatus::LOADED || !data.is_object() || data.value("fingerprint", std::string()) != fingerprint()) {
            return;
        }

        const nlohmann::json& charts = data.value("charts", nlohmann::json::object());

        if (!charts.is_object()) {
            return;
        }

        for (const auto& [key, rates] : charts.items()) {
            if (!rates.is_object()) {
                continue;
            }

            for (const auto& [rate, value] : rates.items()) {
                if (value.is_number()) {
                    entries[key][std::atoi(rate.c_str())] = value.get<double>();
                }
            }
        }
    }

    bool DifficultyCache::save(std::string& error) {
        nlohmann::json data = {{"fingerprint", fingerprint()}, {"charts", nlohmann::json::object()}};

        {
            std::lock_guard<std::mutex> lock(mutex);

            if (!dirty) {
                return true;
            }

            for (const auto& [key, rates] : entries) {
                nlohmann::json& chart = data["charts"][key];

                for (const auto& [rate, value] : rates) {
                    chart[std::to_string(rate)] = value;
                }
            }

            dirty = false;
        }

        VersionedJsonFile file(path, FILE_VERSION);

        if (!file.save(data, error)) {
            std::lock_guard<std::mutex> lock(mutex);
            dirty = true;
            return false;
        }

        return true;
    }

    std::optional<double> DifficultyCache::lookup(const Chart& chart, float pitch) const {
        std::lock_guard<std::mutex> lock(mutex);
        auto rates = entries.find(chart.difficulty_key());

        if (rates == entries.end()) {
            return std::nullopt;
        }

        auto value = rates->second.find(rate_key(pitch));
        return value != rates->second.end() ? std::optional<double>(value->second) : std::nullopt;
    }

    void DifficultyCache::store(const Chart& chart, float pitch, double difficulty) {
        std::lock_guard<std::mutex> lock(mutex);
        entries[chart.difficulty_key()][rate_key(pitch)] = difficulty;
        dirty = true;
    }

    double DifficultyCache::difficulty(const Chart& chart, float pitch) {
        if (std::optional<double> cached = lookup(chart, pitch)) {
            return *cached;
        }

        double value = DifficultyCalculator::calculate_at_pitch(chart, pitch);
        store(chart, pitch, value);
        return value;
    }

    void DifficultyCache::retain(const std::unordered_set<std::string>& difficulty_keys) {
        std::lock_guard<std::mutex> lock(mutex);

        for (auto it = entries.begin(); it != entries.end();) {
            if (difficulty_keys.contains(it->first)) {
                ++it;
                continue;
            }

            it = entries.erase(it);
            dirty = true;
        }
    }

    void DifficultyCache::clear() {
        std::lock_guard<std::mutex> lock(mutex);
        dirty = dirty || !entries.empty();
        entries.clear();
    }

    size_t DifficultyCache::size() const {
        std::lock_guard<std::mutex> lock(mutex);
        size_t count = 0;

        for (const auto& [key, rates] : entries) {
            count += rates.size();
        }

        return count;
    }
}

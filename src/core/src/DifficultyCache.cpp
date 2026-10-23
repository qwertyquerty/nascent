#include "DifficultyCache.h"

#include "DifficultyCalculator.h"
#include "SettingsStore.h"
#include "Sha256.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <utility>

namespace nascent {
    namespace {
        constexpr size_t RATE_BYTES = sizeof(int32_t) + sizeof(double);

        std::optional<double> find_rate(const std::vector<std::pair<int32_t, double>>& rates, int32_t rate) {
            for (const auto& [key, value] : rates) {
                if (key == rate) {
                    return value;
                }
            }

            return std::nullopt;
        }

        bool starts_with_magic(const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::binary);
            std::string magic(BinaryJsonFile::MAGIC.size(), ' ');
            return file.read(magic.data(), (std::streamsize)magic.size()) && magic == BinaryJsonFile::MAGIC;
        }
    }

    DifficultyCache::DifficultyCache(std::filesystem::path path) : path(std::move(path)) {
    }

    std::string DifficultyCache::fingerprint() {
        return sha256_hex(std::format("{}\n{}", NASCENT_DIFFICULTY_SOURCE_HASH, Chart::CHART_HASH_FORMAT));
    }

    int32_t DifficultyCache::rate_key(float pitch) {
        return (int32_t)std::lround(pitch * 100.0f);
    }

    void DifficultyCache::merge(const std::string& key, int32_t rate, double value) {
        Rates& rates = entries[key];

        if (!find_rate(rates, rate)) {
            rates.emplace_back(rate, value);
        }
    }

    bool DifficultyCache::load_binary() {
        nlohmann::json data;

        if (!BinaryJsonFile(path, FILE_VERSION).load(data) || !data.is_object() || data.value("fingerprint", std::string()) != fingerprint()) {
            return false;
        }

        auto charts = data.find("charts");

        if (charts == data.end() || !charts->is_array()) {
            return false;
        }

        std::unique_lock lock(mutex);

        for (const nlohmann::json& chart : *charts) {
            if (!chart.is_array() || chart.size() != 2 || !chart[0].is_string() || !chart[1].is_binary()) {
                continue;
            }

            const std::string& key = chart[0].get_ref<const std::string&>();
            const std::vector<uint8_t>& bytes = chart[1].get_binary();

            for (size_t offset = 0; offset + RATE_BYTES <= bytes.size(); offset += RATE_BYTES) {
                int32_t rate = 0;
                double value = 0;
                std::memcpy(&rate, bytes.data() + offset, sizeof(rate));
                std::memcpy(&value, bytes.data() + offset + sizeof(rate), sizeof(value));
                merge(key, rate, value);
            }
        }

        return true;
    }

    bool DifficultyCache::load_legacy(const std::filesystem::path& source) {
        std::error_code ec;

        if (!std::filesystem::is_regular_file(source, ec)) {
            return false;
        }

        nlohmann::json data;
        std::string error;
        SettingsLoadStatus status = VersionedJsonFile(source, LEGACY_FILE_VERSION).load(data, error);

        if ((status != SettingsLoadStatus::LOADED && status != SettingsLoadStatus::MIGRATED) || !data.is_object() || data.value("fingerprint", std::string()) != fingerprint()) {
            return false;
        }

        const nlohmann::json& charts = data.value("charts", nlohmann::json::object());

        if (!charts.is_object()) {
            return false;
        }

        std::unique_lock lock(mutex);

        for (const auto& [key, rates] : charts.items()) {
            if (!rates.is_object()) {
                continue;
            }

            for (const auto& [rate, value] : rates.items()) {
                if (value.is_number()) {
                    merge(key, std::atoi(rate.c_str()), value.get<double>());
                }
            }
        }

        dirty = true;
        legacy_path = source;
        return true;
    }

    void DifficultyCache::load() {
        if (load_binary() || starts_with_magic(path)) {
            return;
        }

        if (load_legacy(path)) {
            return;
        }

        std::filesystem::path legacy = path;
        legacy.replace_extension(".json");

        if (legacy != path) {
            load_legacy(legacy);
        }
    }

    bool DifficultyCache::save(std::string& error) {
        std::lock_guard<std::mutex> saving(save_mutex);

        {
            std::unique_lock lock(mutex);

            if (!dirty) {
                return true;
            }

            dirty = false;
        }

        nlohmann::json charts = nlohmann::json::array();

        {
            std::shared_lock lock(mutex);
            charts.get_ref<nlohmann::json::array_t&>().reserve(entries.size());

            for (const auto& [key, rates] : entries) {
                std::vector<uint8_t> bytes(rates.size() * RATE_BYTES);

                for (size_t index = 0; index < rates.size(); index++) {
                    std::memcpy(bytes.data() + index * RATE_BYTES, &rates[index].first, sizeof(int32_t));
                    std::memcpy(bytes.data() + index * RATE_BYTES + sizeof(int32_t), &rates[index].second, sizeof(double));
                }

                charts.push_back(nlohmann::json::array({key, nlohmann::json::binary(std::move(bytes))}));
            }
        }

        nlohmann::json data = {{"fingerprint", fingerprint()}, {"charts", std::move(charts)}};

        if (!BinaryJsonFile(path, FILE_VERSION).save(data, error)) {
            std::unique_lock lock(mutex);
            dirty = true;
            return false;
        }

        if (!legacy_path.empty() && legacy_path != path) {
            std::error_code ec;
            std::filesystem::remove(legacy_path, ec);
        }

        legacy_path.clear();
        return true;
    }

    bool DifficultyCache::is_dirty() const {
        std::shared_lock lock(mutex);
        return dirty;
    }

    std::optional<double> DifficultyCache::lookup(const Chart& chart, float pitch) const {
        return lookup(chart.difficulty_key(), pitch);
    }

    std::optional<double> DifficultyCache::lookup(const std::string& difficulty_key, float pitch) const {
        std::shared_lock lock(mutex);
        auto rates = entries.find(difficulty_key);
        return rates != entries.end() ? find_rate(rates->second, rate_key(pitch)) : std::nullopt;
    }

    void DifficultyCache::store(const Chart& chart, float pitch, double difficulty) {
        store(chart.difficulty_key(), pitch, difficulty);
    }

    void DifficultyCache::store(const std::string& difficulty_key, float pitch, double difficulty) {
        std::unique_lock lock(mutex);
        Rates& rates = entries[difficulty_key];
        int32_t rate = rate_key(pitch);
        auto existing = std::find_if(rates.begin(), rates.end(), [rate](const auto& entry) { return entry.first == rate; });

        if (existing != rates.end()) {
            existing->second = difficulty;
        }
        else {
            rates.emplace_back(rate, difficulty);
        }

        dirty = true;
    }

    double DifficultyCache::difficulty(const Chart& chart, float pitch) {
        if (std::optional<double> cached = lookup(chart, pitch)) {
            return *cached;
        }

        if (!chart.has_notes()) {
            return 0.0;
        }

        double value = DifficultyCalculator::calculate_at_pitch(chart, pitch);
        store(chart, pitch, value);
        return value;
    }

    void DifficultyCache::retain(const std::unordered_set<std::string>& difficulty_keys) {
        std::unique_lock lock(mutex);

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
        std::unique_lock lock(mutex);
        dirty = dirty || !entries.empty();
        entries.clear();
    }

    size_t DifficultyCache::size() const {
        std::shared_lock lock(mutex);
        size_t count = 0;

        for (const auto& [key, rates] : entries) {
            count += rates.size();
        }

        return count;
    }
}

#include "LocalBests.h"

#include "gse/Platform.h"
#include "Replay.h"
#include "Results.h"
#include "SettingsManager.h"
#include "SettingsStore.h"
#include "Utf8Path.h"

#include <cmath>
#include <format>
#include <iostream>
#include <map>
#include <memory>

namespace nascent {
    struct LocalBestsFile {
        std::map<std::string, std::map<std::string, LocalBest>> charts;
        bool backfilled = false;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LocalBest, accuracy, grade, max_combo, performance, played_at)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LocalBestsFile, charts, backfilled)

    static std::unique_ptr<SettingsStore<LocalBestsFile>> store;

    static std::string rate_key(float rate) {
        return std::to_string(std::lround(rate * 100.0f));
    }

    static bool is_better(const LocalBest& candidate, const LocalBest& current) {
        return candidate.accuracy > current.accuracy || (candidate.accuracy == current.accuracy && candidate.max_combo > current.max_combo);
    }

    static bool offer(const std::string& chart_hash, float rate, const LocalBest& play) {
        std::map<std::string, LocalBest>& rates = store->value.charts[chart_hash];
        auto existing = rates.find(rate_key(rate));

        if (existing != rates.end() && !is_better(play, existing->second)) {
            return false;
        }

        rates[rate_key(rate)] = play;
        return true;
    }

    static void save() {
        std::string error;

        if (!store->save(error)) {
            std::cerr << "Best plays: " << error << std::endl;
        }
    }

    void LocalBests::load() {
        store = std::make_unique<SettingsStore<LocalBestsFile>>(user_config_directory(SettingsManager::APP_NAME) / FILE_NAME, FILE_VERSION);
        std::string error;
        store->load(error);

        if (!error.empty()) {
            std::cerr << "Best plays: " << error << std::endl;
        }
    }

    bool LocalBests::is_backfilled() {
        return store == nullptr || store->value.backfilled;
    }

    void LocalBests::backfill(const std::unordered_map<std::string, std::string>& chart_hash_by_folder, const std::filesystem::path& replay_directory) {
        if (store == nullptr) {
            return;
        }

        std::error_code ec;

        for (const auto& folder : std::filesystem::directory_iterator(replay_directory, ec)) {
            auto chart_hash = chart_hash_by_folder.find(path_utf8(folder.path().filename()));

            if (chart_hash == chart_hash_by_folder.end()) {
                continue;
            }

            for (const auto& file : std::filesystem::directory_iterator(folder.path(), ec)) {
                std::string error;
                std::optional<Replay> replay = load_replay(file.path(), error);

                if (replay) {
                    offer(chart_hash->second, replay->rate, {replay->result.accuracy, replay->result.grade, replay->result.max_combo, replay->result.performance, replay->played_at});
                }
            }
        }

        store->value.backfilled = true;
        save();
    }

    void LocalBests::record(const std::string& chart_hash, float rate, const AttemptSummary& summary, const std::string& played_at) {
        if (store == nullptr) {
            return;
        }

        if (offer(chart_hash, rate, {summary.accuracy, std::string(grade_name(summary.grade)), summary.max_combo, summary.performance, played_at})) {
            save();
        }
    }

    const LocalBest* LocalBests::lookup(const std::string& chart_hash, float rate) {
        if (store == nullptr) {
            return nullptr;
        }

        auto chart = store->value.charts.find(chart_hash);

        if (chart == store->value.charts.end()) {
            return nullptr;
        }

        auto best = chart->second.find(rate_key(rate));
        return best != chart->second.end() ? &best->second : nullptr;
    }
}

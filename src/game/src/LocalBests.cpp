#include "LocalBests.h"

#include "gse/Platform.h"
#include "Replay.h"
#include "Results.h"
#include "SettingsManager.h"
#include "SettingsStore.h"
#include "Utf8Path.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace nascent {
    struct LocalBestsFile {
        std::unordered_map<std::string, std::map<std::string, LocalBest>> charts;
        std::unordered_map<std::string, std::string> last_played;
        bool backfilled = false;
        bool last_played_backfilled = false;
    };

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LocalBest, accuracy, grade, max_combo, performance, played_at)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LocalBestsFile, charts, last_played, backfilled, last_played_backfilled)

    static std::unique_ptr<SettingsStore<LocalBestsFile>> store;
    static std::mutex save_mutex;
    static std::atomic<uint64_t> latest_save = 0;
    static std::atomic<int32_t> pending_saves = 0;

    static std::string rate_key(float rate) {
        return std::to_string(std::lround(rate * 100.0f));
    }

    static std::string compact_time(const std::string& played_at) {
        std::string compact;

        for (char c : played_at) {
            if (c != '-' && c != ':') {
                compact.push_back(c);
            }
        }

        return compact;
    }

    static void note_played(const std::string& chart_hash, const std::string& compact) {
        std::string& last = store->value.last_played[chart_hash];
        last = std::max(last, compact);
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
        nlohmann::json snapshot = store->value;
        VersionedJsonFile file = store->get_file();
        uint64_t generation = ++latest_save;
        pending_saves++;

        std::thread([snapshot = std::move(snapshot), file = std::move(file), generation]() mutable {
            {
                std::lock_guard<std::mutex> lock(save_mutex);
                std::string error;

                if (generation == latest_save && !file.save(snapshot, error)) {
                    std::cerr << "Best plays: " << error << std::endl;
                }
            }

            pending_saves--;
        }).detach();
    }

    void LocalBests::flush() {
        while (pending_saves > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
        return store == nullptr || (store->value.backfilled && store->value.last_played_backfilled);
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
                if (!store->value.last_played_backfilled) {
                    std::string stem = path_utf8(file.path().stem());
                    note_played(chart_hash->second, stem.substr(0, stem.find('-')));
                }

                if (store->value.backfilled) {
                    continue;
                }

                std::string error;
                std::optional<Replay> replay = load_replay(file.path(), error);

                if (replay) {
                    offer(chart_hash->second, replay->rate, {replay->result.accuracy, replay->result.grade, replay->result.max_combo, replay->result.performance, replay->played_at});
                }
            }
        }

        store->value.backfilled = true;
        store->value.last_played_backfilled = true;
        save();
    }

    void LocalBests::record(const std::string& chart_hash, float rate, const AttemptSummary& summary, const std::string& played_at) {
        if (store == nullptr) {
            return;
        }

        offer(chart_hash, rate, {summary.accuracy, std::string(grade_name(summary.grade)), summary.max_combo, summary.performance, played_at});
        note_played(chart_hash, compact_time(played_at));
        save();
    }

    std::string LocalBests::last_played(const std::string& chart_hash) {
        if (store == nullptr) {
            return std::string();
        }

        auto found = store->value.last_played.find(chart_hash);
        return found != store->value.last_played.end() ? found->second : std::string();
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

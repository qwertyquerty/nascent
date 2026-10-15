#include "LocalReplays.h"

#include "gse/Platform.h"
#include "Replay.h"
#include "SettingsManager.h"
#include "Utf8Path.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <system_error>

namespace nascent {
    static std::filesystem::path unused_path(const std::filesystem::path& folder, const std::filesystem::path& name) {
        std::error_code ec;
        std::filesystem::path target = folder / name;

        for (int32_t copy = 1; std::filesystem::exists(target, ec); copy++) {
            target = folder / utf8_path(std::format("{}-{}{}", path_utf8(name.stem()), copy, path_utf8(name.extension())));
        }

        return target;
    }

    std::filesystem::path LocalReplays::directory() {
        return user_config_directory(SettingsManager::APP_NAME) / "replays";
    }

    bool LocalReplays::folders_migrated() {
        std::error_code ec;
        return !std::filesystem::is_directory(directory(), ec) || std::filesystem::exists(directory() / MIGRATED_MARKER, ec);
    }

    void LocalReplays::migrate_folders(const std::unordered_map<std::string, std::string>& gameplay_hash_by_file_folder) {
        std::error_code ec;
        std::filesystem::path root = directory();
        std::vector<std::filesystem::path> folders;

        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (entry.is_directory(ec)) {
                folders.push_back(entry.path());
            }
        }

        for (const std::filesystem::path& folder : folders) {
            auto found = gameplay_hash_by_file_folder.find(path_utf8(folder.filename()));

            if (found == gameplay_hash_by_file_folder.end()) {
                continue;
            }

            std::filesystem::path target = replay_folder(root, found->second);

            if (target == folder) {
                continue;
            }

            std::filesystem::create_directories(target, ec);
            std::vector<std::filesystem::path> files;

            for (const auto& file : std::filesystem::directory_iterator(folder, ec)) {
                files.push_back(file.path());
            }

            for (const std::filesystem::path& file : files) {
                std::filesystem::rename(file, unused_path(target, file.filename()), ec);

                if (ec) {
                    std::cerr << std::format("Replays: could not move {}: {}", path_utf8(file), ec.message()) << std::endl;
                }
            }

            std::filesystem::remove(folder, ec);
        }

        std::filesystem::create_directories(root, ec);
        std::ofstream(root / MIGRATED_MARKER) << "replay folders are named after the chart's gameplay hash\n";
    }

    std::unordered_map<std::string, std::string> LocalReplays::gameplay_hash_by_folder(const std::vector<std::string>& gameplay_hashes) {
        std::unordered_map<std::string, std::string> folders;

        for (const std::string& hash : gameplay_hashes) {
            folders[path_utf8(replay_folder({}, hash))] = hash;
        }

        return folders;
    }

    static std::vector<LocalScore> load_scores(const std::string& gameplay_hash, std::optional<long> rate) {
        std::vector<LocalScore> scores;
        std::error_code ec;

        for (const auto& file : std::filesystem::directory_iterator(replay_folder(LocalReplays::directory(), gameplay_hash), ec)) {
            std::string error;
            std::optional<Replay> replay = load_replay(file.path(), error);

            if (!replay || (rate && std::lround(replay->rate * 100.0f) != *rate)) {
                continue;
            }

            if (!replay->chart.gameplay_hash.empty() && replay->chart.gameplay_hash != gameplay_hash) {
                continue;
            }

            scores.push_back({replay->result.accuracy, replay->result.max_combo, replay->played_at, replay->rate, file.path()});
        }

        std::sort(scores.begin(), scores.end(), [](const LocalScore& a, const LocalScore& b) {
            return a.accuracy != b.accuracy ? a.accuracy > b.accuracy : a.max_combo > b.max_combo;
        });

        return scores;
    }

    std::vector<LocalScore> LocalReplays::scores(const std::string& gameplay_hash, long rate) {
        return load_scores(gameplay_hash, rate);
    }

    std::vector<LocalScore> LocalReplays::all_scores(const std::string& gameplay_hash) {
        return load_scores(gameplay_hash, std::nullopt);
    }
}

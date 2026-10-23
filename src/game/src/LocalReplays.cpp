#include "LocalReplays.h"

#include "gse/Platform.h"
#include "Replay.h"
#include "SettingsManager.h"
#include "SettingsStore.h"
#include "Utf8Path.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <system_error>
#include <unordered_map>

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

    namespace {
        struct IndexedScore {
            uint64_t size = 0;
            int64_t modified = 0;
            bool valid = false;
            std::string gameplay_hash;
            LocalScore score;
        };

        std::mutex score_index_mutex;

        std::unordered_map<std::string, IndexedScore> read_score_index(const std::filesystem::path& path) {
            std::unordered_map<std::string, IndexedScore> index;
            std::optional<std::string> bytes = BinaryJsonFile(path, LocalReplays::SCORE_INDEX_VERSION).read();

            if (!bytes) {
                return index;
            }

            BinaryJsonReader reader{*bytes, BinaryJsonFile::header_size()};

            try {
                while (reader.ok && reader.position < bytes->size()) {
                    std::optional<std::string_view> record = reader.next();
                    nlohmann::json entry;

                    if (!record || !BinaryJsonReader::decode(*record, entry) || !entry.is_array() || entry.size() != 10) {
                        return {};
                    }

                    IndexedScore score;
                    score.size = entry[1].get<uint64_t>();
                    score.modified = entry[2].get<int64_t>();
                    score.valid = entry[3].get<bool>();
                    score.gameplay_hash = entry[4].get<std::string>();
                    score.score.accuracy = entry[5].get<double>();
                    score.score.max_combo = entry[6].get<uint32_t>();
                    score.score.played_at = entry[7].get<std::string>();
                    score.score.rate = entry[8].get<float>();
                    score.score.performance = entry[9].get<double>();
                    index.emplace(entry[0].get<std::string>(), std::move(score));
                }
            }
            catch (const nlohmann::json::exception&) {
                return {};
            }

            return index;
        }

        void write_score_index(const std::filesystem::path& path, const std::unordered_map<std::string, IndexedScore>& index) {
            std::string bytes = BinaryJsonFile::header(LocalReplays::SCORE_INDEX_VERSION);
            BinaryJsonWriter writer{bytes};

            for (const auto& [name, score] : index) {
                writer.write(nlohmann::json::array({name, score.size, score.modified, score.valid, score.gameplay_hash, score.score.accuracy, score.score.max_combo, score.score.played_at, score.score.rate, score.score.performance}));
            }

            std::string error;

            if (!BinaryJsonFile(path, LocalReplays::SCORE_INDEX_VERSION).save_bytes(bytes, error)) {
                std::cerr << "Replays: " << error << std::endl;
            }
        }
    }

    static std::vector<LocalScore> load_scores(const std::string& gameplay_hash, std::optional<long> rate) {
        std::vector<LocalScore> scores;
        std::error_code ec;
        std::filesystem::path folder = replay_folder(LocalReplays::directory(), gameplay_hash);

        if (!std::filesystem::is_directory(folder, ec)) {
            return scores;
        }

        std::lock_guard<std::mutex> lock(score_index_mutex);
        std::filesystem::path index_path = folder / LocalReplays::SCORE_INDEX_FILE;
        std::unordered_map<std::string, IndexedScore> previous = read_score_index(index_path);
        std::unordered_map<std::string, IndexedScore> current;
        bool changed = false;

        for (const auto& file : std::filesystem::directory_iterator(folder, ec)) {
            std::error_code file_ec;

            if (!file.is_regular_file(file_ec) || file.path().extension() != ".json") {
                continue;
            }

            std::string name = path_utf8(file.path().filename());
            uint64_t size = file.file_size(file_ec);
            int64_t modified = (int64_t)file.last_write_time(file_ec).time_since_epoch().count();
            auto known = previous.find(name);

            if (known != previous.end() && known->second.size == size && known->second.modified == modified) {
                current.emplace(name, std::move(known->second));
                continue;
            }

            IndexedScore indexed;
            indexed.size = size;
            indexed.modified = modified;
            std::string error;
            std::optional<Replay> replay = load_replay(file.path(), error);

            if (replay) {
                indexed.valid = true;
                indexed.gameplay_hash = replay->chart.gameplay_hash;
                indexed.score = {replay->result.accuracy, replay->result.max_combo, replay->played_at, replay->rate, {}, replay->result.performance};
            }

            current.emplace(name, std::move(indexed));
            changed = true;
        }

        if (changed || current.size() != previous.size()) {
            write_score_index(index_path, current);
        }

        for (const auto& [name, indexed] : current) {
            if (!indexed.valid || (rate && std::lround(indexed.score.rate * 100.0f) != *rate)) {
                continue;
            }

            if (!indexed.gameplay_hash.empty() && indexed.gameplay_hash != gameplay_hash) {
                continue;
            }

            LocalScore score = indexed.score;
            score.path = folder / utf8_path(name);
            scores.push_back(std::move(score));
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

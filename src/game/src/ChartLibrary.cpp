#include "ChartLibrary.h"

#include "ChartSearch.h"
#include "Const.h"
#include "DifficultyCache.h"
#include "LocalReplays.h"
#include "Replay.h"
#include "Sha256.h"
#include "Utf8Path.h"

#include <format>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace nascent {
    namespace {
        struct LoadedChart {
            std::unique_ptr<Chart> chart;
            std::u8string folder;
            float difficulty = 0;
        };

        std::u8string folder_key(const std::filesystem::path& folder_path) {
            std::u8string folder = folder_path.filename().u8string();

            for (char8_t& c : folder) {
                if (c >= u8'A' && c <= u8'Z') {
                    c = (char8_t)(c - u8'A' + u8'a');
                }
            }

            return folder;
        }

        ChartLibrary build_library(std::vector<LoadedChart> loaded, float pitch, DifficultyCache& cache) {
            parallel_for(loaded.size(), [&](size_t index) {
                loaded[index].difficulty = (float)cache.difficulty(*loaded[index].chart, pitch);
            });

            std::unordered_map<std::u8string, std::string> folder_titles;

            for (const LoadedChart& entry : loaded) {
                folder_titles.emplace(entry.folder, ascii_lowercase(entry.chart->info.title));
            }

            std::stable_sort(loaded.begin(), loaded.end(), [&](const LoadedChart& a, const LoadedChart& b) {
                const std::string& a_title = folder_titles.at(a.folder);
                const std::string& b_title = folder_titles.at(b.folder);

                if (a_title != b_title) {
                    return a_title < b_title;
                }

                if (a.folder != b.folder) {
                    return a.folder < b.folder;
                }

                if (a.difficulty != b.difficulty) {
                    return a.difficulty < b.difficulty;
                }

                return a.chart->info.difficulty_name < b.chart->info.difficulty_name;
            });

            ChartLibrary library;
            library.pitch = pitch;

            for (LoadedChart& entry : loaded) {
                const ChartInfo& info = entry.chart->info;
                library.hashes.push_back(entry.chart->gameplay_hash());
                library.difficulties.push_back(entry.difficulty);
                library.search_texts.push_back(ascii_lowercase(std::format("{} {} {} {} {} {}", info.title, info.title_unicode, info.artist, info.artist_unicode, info.mapper, info.difficulty_name)));
                library.details.push_back(std::format("[{}] - {} // {}", info.difficulty_name, info.artist, info.mapper));
                library.lengths_ms.push_back((double)entry.chart->get_duration_ms() * entry.chart->pitch);
                library.charts.push_back(std::move(entry.chart));
            }

            return library;
        }

        void migrate_replay_folders(const ChartLibrary& library) {
            if (LocalReplays::folders_migrated()) {
                return;
            }

            std::vector<std::optional<std::string>> file_hashes(library.size());

            parallel_for(library.size(), [&](size_t index) {
                file_hashes[index] = sha256_file_hex(library.charts[index]->file_path);
            });

            std::unordered_map<std::string, std::string> gameplay_hash_by_file_folder;

            for (size_t index = 0; index < file_hashes.size(); index++) {
                if (file_hashes[index]) {
                    gameplay_hash_by_file_folder[file_hashes[index]->substr(0, Replay::FOLDER_HASH_LENGTH)] = library.hashes[index];
                }
            }

            LocalReplays::migrate_folders(gameplay_hash_by_file_folder);
        }
    }

    size_t ChartLibrary::size() const {
        return charts.size();
    }

    ChartLibrary ChartLibrary::without(const std::vector<bool>& removed) {
        ChartLibrary library;
        library.pitch = pitch;

        for (size_t index = 0; index < charts.size(); index++) {
            if (index < removed.size() && removed[index]) {
                continue;
            }

            library.charts.push_back(std::move(charts[index]));
            library.hashes.push_back(hashes[index]);
            library.difficulties.push_back(index < difficulties.size() ? difficulties[index] : std::nullopt);
            library.search_texts.push_back(search_texts[index]);
            library.details.push_back(details[index]);
            library.lengths_ms.push_back(lengths_ms[index]);
        }

        return library;
    }

    ChartLibrary load_chart_library(const std::filesystem::path& song_directory, float pitch, DifficultyCache& cache) {
        std::error_code ec;
        std::vector<LoadedChart> loaded;

        if (std::filesystem::is_directory(song_directory, ec)) {
            struct ChartFile {
                std::filesystem::path path;
                std::u8string folder;
            };

            std::vector<ChartFile> chart_files;

            auto extension_rank = [](const std::filesystem::path& path) {
                auto found = std::find(CHART_FILE_EXTENSIONS.begin(), CHART_FILE_EXTENSIONS.end(), path.extension().string());
                return (size_t)(found - CHART_FILE_EXTENSIONS.begin());
            };

            for (const auto& chart_folder : std::filesystem::directory_iterator(song_directory, ec)) {
                if (!chart_folder.is_directory(ec)) {
                    continue;
                }

                std::u8string folder = folder_key(chart_folder.path());
                std::vector<std::filesystem::path> paths;

                for (const auto& chart_file : std::filesystem::directory_iterator(chart_folder.path(), ec)) {
                    if (extension_rank(chart_file.path()) < CHART_FILE_EXTENSIONS.size()) {
                        paths.push_back(chart_file.path());
                    }
                }

                std::sort(paths.begin(), paths.end(), [&](const std::filesystem::path& a, const std::filesystem::path& b) {
                    return std::make_pair(extension_rank(a), a) < std::make_pair(extension_rank(b), b);
                });

                for (std::filesystem::path& path : paths) {
                    chart_files.push_back({std::move(path), folder});
                }
            }

            std::vector<LoadedChart> parsed(chart_files.size());
            std::mutex log_mutex;

            parallel_for(chart_files.size(), [&](size_t index) {
                std::string error;
                std::unique_ptr<Chart> chart = Chart::load(chart_files[index].path, pitch, error);
                std::error_code audio_ec;

                if (chart != nullptr && !std::filesystem::is_regular_file(chart->audio_path, audio_ec)) {
                    error = std::format("audio file {} is missing", path_utf8(chart->audio_path));
                    chart.reset();
                }

                if (chart == nullptr) {
                    std::lock_guard<std::mutex> lock(log_mutex);
                    std::cerr << "Skipping chart " << path_utf8(chart_files[index].path) << ": " << error << std::endl;
                    return;
                }

                parsed[index] = {std::move(chart), chart_files[index].folder};
            });

            std::unordered_set<std::string> loaded_hashes;

            for (LoadedChart& entry : parsed) {
                if (entry.chart != nullptr && loaded_hashes.insert(entry.chart->gameplay_hash()).second) {
                    loaded.push_back(std::move(entry));
                }
            }
        }

        std::unordered_set<std::string> difficulty_keys;

        for (const LoadedChart& entry : loaded) {
            difficulty_keys.insert(entry.chart->difficulty_key());
        }

        ChartLibrary library = build_library(std::move(loaded), pitch, cache);
        cache.retain(difficulty_keys);
        migrate_replay_folders(library);

        std::string error;

        if (!cache.save(error)) {
            std::cerr << "Difficulty cache: " << error << std::endl;
        }

        return library;
    }

    ChartLibrary single_chart_library(std::unique_ptr<Chart> chart, float pitch, DifficultyCache& cache) {
        std::u8string folder = folder_key(chart->file_path.parent_path());
        std::vector<LoadedChart> loaded;
        loaded.push_back({std::move(chart), std::move(folder)});
        return build_library(std::move(loaded), pitch, cache);
    }
}

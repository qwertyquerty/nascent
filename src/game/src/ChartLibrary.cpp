#include "ChartLibrary.h"

#include "ChartSearch.h"
#include "Const.h"
#include "DifficultyCache.h"
#include "LocalReplays.h"
#include "OsuFormat.h"
#include "Replay.h"
#include "Sha256.h"
#include "Utf8Path.h"

#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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

        constexpr int32_t OSU_HEADER_LINES = 64;
        constexpr std::string_view OSU_MANIA_MODE_VALUE = "3";

        std::string_view header_trim(std::string_view text) {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
                text.remove_prefix(1);
            }

            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
                text.remove_suffix(1);
            }

            return text;
        }

        bool may_be_mania(std::string_view contents) {
            bool general = false;
            int32_t count = 0;

            while (!contents.empty() && count++ < OSU_HEADER_LINES) {
                size_t end = contents.find('\n');
                std::string_view text = header_trim(contents.substr(0, end));
                contents = end == std::string_view::npos ? std::string_view() : contents.substr(end + 1);

                if (text.starts_with('[')) {
                    if (general) {
                        return false;
                    }

                    general = text == "[General]";
                    continue;
                }

                if (general && text.starts_with("Mode") && text.find(':') != std::string_view::npos) {
                    return header_trim(text.substr(text.find(':') + 1)) == OSU_MANIA_MODE_VALUE;
                }
            }

            return true;
        }

        std::u8string file_key(const std::filesystem::path& name) {
            std::u8string key = name.u8string();
#if defined(_WIN32) || defined(__APPLE__)
            for (char8_t& c : key) {
                if (c >= u8'A' && c <= u8'Z') {
                    c = (char8_t)(c - u8'A' + u8'a');
                }
            }
#endif
            return key;
        }

        bool audio_listed(const Chart& chart, const std::unordered_set<std::u8string>& folder_files) {
            std::filesystem::path relative = utf8_path(chart.info.audio_file).lexically_normal();
            std::error_code ec;

            if (relative.has_parent_path()) {
                return std::filesystem::is_regular_file(chart.audio_path, ec);
            }

            return folder_files.contains(file_key(relative));
        }

        int64_t folder_time(const std::filesystem::path& folder) {
            std::error_code ec;
            std::filesystem::file_time_type time = std::filesystem::last_write_time(folder, ec);
            return ec ? 0 : (int64_t)time.time_since_epoch().count();
        }

        ChartLibrary build_library(std::vector<LoadedChart> loaded, float pitch, DifficultyCache& cache, LibraryLoadProgress* progress = nullptr) {
            parallel_for(loaded.size(), [&](size_t index) {
                loaded[index].difficulty = (float)cache.difficulty(*loaded[index].chart, pitch);

                if (progress != nullptr) {
                    progress->done++;
                }
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
                library.bpms.push_back(entry.chart->bpm_range());
                library.added.push_back(folder_time(entry.chart->file_path.parent_path()));
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

    float LibraryLoadProgress::fraction() const {
        size_t all = total;
        return all == 0 ? 0.0f : std::min(1.0f, (float)done / (float)all);
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
            library.bpms.push_back(bpms[index]);
            library.added.push_back(index < added.size() ? added[index] : 0);
        }

        return library;
    }

    ChartLibrary load_chart_library(const std::vector<std::filesystem::path>& song_directories, float pitch, DifficultyCache& cache, LibraryLoadProgress* progress) {
        std::error_code ec;
        std::vector<LoadedChart> loaded;

        {
            struct ChartFile {
                std::filesystem::path path;
                std::u8string folder;
                std::shared_ptr<const std::unordered_set<std::u8string>> folder_files;
            };

            std::vector<ChartFile> chart_files;

            auto extension_rank = [](const std::filesystem::path& path) {
                auto found = std::find(CHART_FILE_EXTENSIONS.begin(), CHART_FILE_EXTENSIONS.end(), path.extension().string());
                return (size_t)(found - CHART_FILE_EXTENSIONS.begin());
            };

            std::vector<std::filesystem::path> chart_folders;

            for (const std::filesystem::path& song_directory : song_directories) {
                for (const auto& chart_folder : std::filesystem::directory_iterator(song_directory, ec)) {
                    if (chart_folder.is_directory(ec) && chart_folder.path().extension() != ".part") {
                        chart_folders.push_back(chart_folder.path());
                    }
                }
            }

            for (const std::filesystem::path& chart_folder : chart_folders) {
                std::u8string folder = folder_key(chart_folder);
                std::vector<std::filesystem::path> paths;
                auto files = std::make_shared<std::unordered_set<std::u8string>>();

                for (const auto& chart_file : std::filesystem::directory_iterator(chart_folder, ec)) {
                    files->insert(file_key(chart_file.path().filename()));

                    if (extension_rank(chart_file.path()) < CHART_FILE_EXTENSIONS.size()) {
                        paths.push_back(chart_file.path());
                    }
                }

                std::sort(paths.begin(), paths.end(), [&](const std::filesystem::path& a, const std::filesystem::path& b) {
                    return std::make_pair(extension_rank(a), a) < std::make_pair(extension_rank(b), b);
                });

                for (std::filesystem::path& path : paths) {
                    chart_files.push_back({std::move(path), folder, files});
                }
            }

            std::vector<LoadedChart> parsed(chart_files.size());
            std::mutex log_mutex;

            if (progress != nullptr) {
                progress->total = chart_files.size() * 2;
            }

            parallel_for(chart_files.size(), [&](size_t index) {
                const ChartFile& source = chart_files[index];
                std::string error;
                std::unique_ptr<Chart> chart;
                std::ifstream file(source.path, std::ios::binary);
                std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

                if (progress != nullptr) {
                    progress->done++;
                }

                if (source.path.extension() == OSU_FILE_EXTENSION && !may_be_mania(contents)) {
                    return;
                }

                if (!file && contents.empty()) {
                    error = "could not open file";
                }
                else {
                    chart = Chart::load_text(contents, source.path, pitch, error);
                }

                if (chart != nullptr && !audio_listed(*chart, *source.folder_files)) {
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

        if (progress != nullptr) {
            progress->total = progress->done + loaded.size();
        }

        ChartLibrary library = build_library(std::move(loaded), pitch, cache, progress);
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

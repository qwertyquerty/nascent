#include "ChartLibrary.h"

#include "ChartSearch.h"
#include "Const.h"
#include "DifficultyCache.h"
#include "LibraryCache.h"
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
            int64_t added = 0;
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

        bool audio_listed(const std::string& audio_file, const std::filesystem::path& chart_path, const std::unordered_set<std::u8string>& folder_files) {
            std::filesystem::path relative = utf8_path(audio_file).lexically_normal();
            std::error_code ec;

            if (relative.has_parent_path()) {
                return std::filesystem::is_regular_file(chart_path.parent_path() / relative, ec);
            }

            return folder_files.contains(file_key(relative));
        }

        ChartLibrary build_library(std::vector<LoadedChart> loaded, float pitch, DifficultyCache& cache, LibraryLoadProgress* progress = nullptr) {
            if (progress != nullptr) {
                progress->begin(LibraryLoadStage::BUILDING, loaded.size());
            }

            parallel_for(loaded.size(), [&](size_t index) {
                loaded[index].difficulty = (float)cache.difficulty(*loaded[index].chart, pitch);

                if (progress != nullptr) {
                    progress->step();
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
                library.added.push_back(entry.added);
                library.charts.push_back(std::move(entry.chart));
            }

            return library;
        }

        void migrate_replay_folders(const ChartLibrary& library, LibraryLoadProgress* progress) {
            if (LocalReplays::folders_migrated()) {
                return;
            }

            std::vector<std::optional<std::string>> file_hashes(library.size());

            if (progress != nullptr) {
                progress->begin(LibraryLoadStage::FINISHING, library.size());
            }

            parallel_for(library.size(), [&](size_t index) {
                file_hashes[index] = sha256_file_hex(library.charts[index]->file_path);

                if (progress != nullptr) {
                    progress->step();
                }
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

    void LibraryLoadProgress::reset() {
        begin(LibraryLoadStage::LISTING, 0);
    }

    void LibraryLoadProgress::begin(LibraryLoadStage next, size_t count) {
        done = 0;
        total = count;
        stage = (uint8_t)next;
    }

    void LibraryLoadProgress::step() {
        done++;
    }

    float LibraryLoadProgress::fraction() const {
        size_t current = std::min<size_t>(stage, STAGE_WEIGHTS.size());
        float sum = 0;
        float reached = 0;

        for (size_t index = 0; index < STAGE_WEIGHTS.size(); index++) {
            sum += STAGE_WEIGHTS[index];
            reached += index < current ? STAGE_WEIGHTS[index] : 0.0f;
        }

        if (current < STAGE_WEIGHTS.size()) {
            size_t all = total;
            reached += all == 0 ? 0.0f : STAGE_WEIGHTS[current] * std::min(1.0f, (float)done / (float)all);
        }

        return sum > 0 ? std::min(1.0f, reached / sum) : 1.0f;
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

    std::string chart_key(const std::filesystem::path& path) {
        return path_utf8(path);
    }

    ChartLibrary cached_chart_library(LibraryCache& library_cache, const std::vector<std::filesystem::path>& song_directories, float pitch, DifficultyCache& cache) {
        std::vector<std::string> roots;

        for (const std::filesystem::path& directory : song_directories) {
            std::string root = chart_key(directory);

            if (!root.empty() && root.back() != '/' && root.back() != '\\') {
                root += (char)std::filesystem::path::preferred_separator;
            }

            roots.push_back(std::move(root));
        }

        std::vector<LoadedChart> loaded;
        std::unordered_set<std::string> hashes;

        for (LibraryCache::Restored& restored : library_cache.load(pitch)) {
            bool listed = std::any_of(roots.begin(), roots.end(), [&](const std::string& root) {
                return restored.key.starts_with(root);
            });

            if (listed && hashes.insert(restored.chart->gameplay_hash()).second) {
                std::u8string folder = folder_key(restored.chart->file_path.parent_path());
                loaded.push_back({std::move(restored.chart), std::move(folder), 0, restored.added});
            }
        }

        return build_library(std::move(loaded), pitch, cache);
    }

    LibraryUpdate scan_chart_library(const std::vector<std::filesystem::path>& song_directories, float pitch, DifficultyCache& cache, LibraryCache& library_cache, LibraryLoadProgress* progress) {
        enum class Outcome : uint8_t {
            KEPT_CHART,
            KEPT_OTHER,
            NEW_CHART,
            NEW_OTHER
        };

        struct ChartFile {
            std::filesystem::path path;
            std::string key;
            std::shared_ptr<const std::unordered_set<std::u8string>> folder_files;
            uint64_t size = 0;
            int64_t modified = 0;
            int64_t added = 0;
        };

        struct Scanned {
            Outcome outcome = Outcome::NEW_OTHER;
            bool playable = false;
            std::unique_ptr<Chart> chart;
        };

        std::error_code ec;
        std::vector<ChartFile> chart_files;

        auto extension_rank = [](const std::filesystem::path& path) {
            auto found = std::find(CHART_FILE_EXTENSIONS.begin(), CHART_FILE_EXTENSIONS.end(), path.extension().string());
            return (size_t)(found - CHART_FILE_EXTENSIONS.begin());
        };

        auto ticks = [](std::filesystem::file_time_type time) {
            return (int64_t)time.time_since_epoch().count();
        };

        std::vector<std::pair<std::filesystem::path, int64_t>> chart_folders;

        for (const std::filesystem::path& song_directory : song_directories) {
            for (const auto& chart_folder : std::filesystem::directory_iterator(song_directory, ec)) {
                if (chart_folder.is_directory(ec) && chart_folder.path().extension() != ".part") {
                    std::filesystem::file_time_type time = chart_folder.last_write_time(ec);
                    chart_folders.emplace_back(chart_folder.path(), ec ? 0 : ticks(time));
                }
            }
        }

        if (progress != nullptr) {
            progress->begin(LibraryLoadStage::LISTING, chart_folders.size());
        }

        for (const auto& [chart_folder, added] : chart_folders) {
            if (progress != nullptr) {
                progress->step();
            }

            auto files = std::make_shared<std::unordered_set<std::u8string>>();
            std::vector<ChartFile> found;

            for (const auto& chart_file : std::filesystem::directory_iterator(chart_folder, ec)) {
                files->insert(file_key(chart_file.path().filename()));

                if (extension_rank(chart_file.path()) >= CHART_FILE_EXTENSIONS.size()) {
                    continue;
                }

                std::error_code stat_ec;
                ChartFile file{chart_file.path(), chart_key(chart_file.path()), nullptr, 0, 0, added};
                file.size = chart_file.file_size(stat_ec);
                std::filesystem::file_time_type time = chart_file.last_write_time(stat_ec);
                file.modified = stat_ec ? 0 : ticks(time);
                found.push_back(std::move(file));
            }

            std::sort(found.begin(), found.end(), [&](const ChartFile& a, const ChartFile& b) {
                return std::make_pair(extension_rank(a.path), a.path) < std::make_pair(extension_rank(b.path), b.path);
            });

            for (ChartFile& file : found) {
                file.folder_files = files;
                chart_files.push_back(std::move(file));
            }
        }

        std::vector<Scanned> scanned(chart_files.size());
        std::mutex log_mutex;

        if (progress != nullptr) {
            progress->begin(LibraryLoadStage::READING, chart_files.size());
        }

        parallel_for(chart_files.size(), [&](size_t index) {
            const ChartFile& source = chart_files[index];
            Scanned& result = scanned[index];
            const LibraryCache::Record* record = library_cache.find(source.key);

            if (progress != nullptr) {
                progress->step();
            }

            if (record != nullptr && record->size == source.size && record->modified == source.modified && record->added == source.added) {
                result.outcome = record->chart ? Outcome::KEPT_CHART : Outcome::KEPT_OTHER;
                result.playable = record->chart && audio_listed(record->audio_file, source.path, *source.folder_files);
                return;
            }

            std::ifstream file(source.path, std::ios::binary);
            std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            std::string error;

            if (source.path.extension() == OSU_FILE_EXTENSION && !may_be_mania(contents)) {
                return;
            }

            if (!file && contents.empty()) {
                error = "could not open file";
            }
            else {
                result.chart = Chart::load_text(contents, source.path, pitch, error);
            }

            if (result.chart != nullptr) {
                result.outcome = Outcome::NEW_CHART;
                result.playable = audio_listed(result.chart->info.audio_file, source.path, *source.folder_files);

                if (!result.playable) {
                    error = std::format("audio file {} is missing", path_utf8(result.chart->audio_path));
                }
            }

            if (!error.empty()) {
                std::lock_guard<std::mutex> lock(log_mutex);
                std::cerr << "Skipping chart " << path_utf8(source.path) << ": " << error << std::endl;
            }
        });

        LibraryUpdate update;
        std::vector<LibraryCache::Entry> entries;
        bool rewrite = !library_cache.exists();

        for (size_t index = 0; index < chart_files.size(); index++) {
            const ChartFile& source = chart_files[index];
            Scanned& result = scanned[index];
            LibraryCache::Entry entry{source.key, source.size, source.modified, source.added, result.chart.get(), false};
            entry.keep = result.outcome == Outcome::KEPT_CHART || result.outcome == Outcome::KEPT_OTHER;
            rewrite = rewrite || !entry.keep;
            entries.push_back(entry);

            if (result.outcome == Outcome::KEPT_CHART && result.playable) {
                update.kept.insert(source.key);
            }
        }

        rewrite = rewrite || entries.size() != library_cache.size();
        update.changed = rewrite || update.kept.size() != library_cache.chart_count();
        std::vector<Chart*> fresh;

        for (Scanned& result : scanned) {
            if (result.outcome == Outcome::NEW_CHART && result.playable) {
                fresh.push_back(result.chart.get());
            }
        }

        if (progress != nullptr) {
            progress->begin(LibraryLoadStage::DIFFICULTY, fresh.size());
        }

        parallel_for(fresh.size(), [&](size_t index) {
            cache.difficulty(*fresh[index], pitch);

            if (progress != nullptr) {
                progress->step();
            }
        });

        if (progress != nullptr) {
            progress->begin(LibraryLoadStage::SAVING, 1);
        }

        if (rewrite) {
            std::string error;

            if (!library_cache.rewrite(entries, error)) {
                std::cerr << "Library cache: " << error << std::endl;
            }
        }

        for (size_t index = 0; index < scanned.size(); index++) {
            if (scanned[index].outcome == Outcome::NEW_CHART && scanned[index].playable) {
                update.added.push_back(std::move(scanned[index].chart));
                update.added_times.push_back(chart_files[index].added);
            }
        }

        std::string error;

        if (!cache.save(error)) {
            std::cerr << "Difficulty cache: " << error << std::endl;
        }

        if (progress != nullptr) {
            progress->step();
        }

        return update;
    }

    ChartLibrary merge_chart_library(ChartLibrary current, LibraryUpdate update, float pitch, DifficultyCache& cache, std::vector<std::unique_ptr<Chart>>& removed, LibraryLoadProgress* progress) {
        std::vector<LoadedChart> loaded;
        std::unordered_set<std::string> hashes;

        for (size_t index = 0; index < current.charts.size(); index++) {
            std::unique_ptr<Chart>& chart = current.charts[index];

            if (update.kept.contains(chart_key(chart->file_path)) && hashes.insert(chart->gameplay_hash()).second) {
                std::u8string folder = folder_key(chart->file_path.parent_path());
                loaded.push_back({std::move(chart), std::move(folder), 0, index < current.added.size() ? current.added[index] : 0});
            }
            else {
                removed.push_back(std::move(chart));
            }
        }

        for (size_t index = 0; index < update.added.size(); index++) {
            std::unique_ptr<Chart>& chart = update.added[index];

            if (chart->pitch != pitch) {
                chart->set_pitch(pitch);
            }

            if (hashes.insert(chart->gameplay_hash()).second) {
                std::u8string folder = folder_key(chart->file_path.parent_path());
                loaded.push_back({std::move(chart), std::move(folder), 0, update.added_times[index]});
            }
        }

        return build_library(std::move(loaded), pitch, cache, progress);
    }

    void prepare_replay_folders(const ChartLibrary& library, LibraryLoadProgress* progress) {
        migrate_replay_folders(library, progress);
    }

    ChartLibrary single_chart_library(std::unique_ptr<Chart> chart, float pitch, DifficultyCache& cache) {
        std::u8string folder = folder_key(chart->file_path.parent_path());
        std::vector<LoadedChart> loaded;
        loaded.push_back({std::move(chart), std::move(folder)});
        return build_library(std::move(loaded), pitch, cache);
    }
}

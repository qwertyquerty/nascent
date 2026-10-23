#include "ChartLibrary.h"

#include "ChartSearch.h"
#include "Const.h"
#include "DifficultyCache.h"
#include "DifficultyCalculator.h"
#include "LibraryCache.h"
#include "LocalReplays.h"
#include "OsuFormat.h"
#include "Replay.h"
#include "Sha256.h"
#include "StepManiaFormat.h"
#include "Utf8Path.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <tuple>
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
            ChartSummary summary;
        };

        double pitched_length_ms(const ChartSummary& summary, float pitch) {
            int32_t start = summary.notes == 0 ? 0 : (int32_t)std::lround(summary.start_ms / pitch);
            int32_t end = (int32_t)std::lround(std::max(0, summary.end_ms) / pitch);
            return (double)(end - start) * pitch;
        }

        bool library_order(const std::string& a_title, const std::u8string& a_folder, float a_difficulty, const std::string& a_name, const std::string& b_title, const std::u8string& b_folder, float b_difficulty, const std::string& b_name) {
            if (a_title != b_title) {
                return a_title < b_title;
            }

            if (a_folder != b_folder) {
                return a_folder < b_folder;
            }

            if (a_difficulty != b_difficulty) {
                return a_difficulty < b_difficulty;
            }

            return a_name < b_name;
        }

        std::string lowercase_extension(const std::filesystem::path& path) {
            std::string extension = path_utf8(path.extension());

            for (char& c : extension) {
                c = (char)std::tolower((unsigned char)c);
            }

            return extension;
        }

        bool is_stepmania_file(const std::filesystem::path& path) {
            std::string extension = lowercase_extension(path);
            return extension == SM_FILE_EXTENSION || extension == SSC_FILE_EXTENSION || extension == DWI_FILE_EXTENSION;
        }

        int32_t stepmania_preference(const std::filesystem::path& path) {
            std::string extension = lowercase_extension(path);
            return extension == SSC_FILE_EXTENSION ? 3 : (extension == SM_FILE_EXTENSION ? 2 : (extension == DWI_FILE_EXTENSION ? 1 : 0));
        }

        size_t extension_rank(const std::filesystem::path& path) {
            std::string extension = path_utf8(path.extension());
            auto found = std::find(LIBRARY_CHART_EXTENSIONS.begin(), LIBRARY_CHART_EXTENSIONS.end(), extension);

            if (found == LIBRARY_CHART_EXTENSIONS.end() && is_stepmania_file(path)) {
                found = std::find(LIBRARY_CHART_EXTENSIONS.begin(), LIBRARY_CHART_EXTENSIONS.end(), lowercase_extension(path));
            }

            return (size_t)(found - LIBRARY_CHART_EXTENSIONS.begin());
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

        ChartLibrary build_library(std::vector<LoadedChart> loaded, float pitch, DifficultyCache& cache, const LibraryCache* library_cache, LibraryLoadProgress* progress = nullptr) {
            if (progress != nullptr) {
                progress->begin(LibraryLoadStage::BUILDING, loaded.size());
            }

            parallel_for(loaded.size(), [&](size_t index) {
                loaded[index].difficulty = (float)library_difficulty(cache, library_cache, *loaded[index].chart, pitch, true).value_or(0.0);

                if (progress != nullptr) {
                    progress->step();
                }
            });

            std::unordered_map<std::u8string, std::string> folder_titles;

            for (const LoadedChart& entry : loaded) {
                folder_titles.emplace(entry.folder, ascii_lowercase(entry.chart->info.title));
            }

            std::vector<const std::string*> titles(loaded.size());

            for (size_t index = 0; index < loaded.size(); index++) {
                titles[index] = &folder_titles.at(loaded[index].folder);
            }

            std::vector<size_t> order(loaded.size());

            for (size_t index = 0; index < order.size(); index++) {
                order[index] = index;
            }

            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return library_order(*titles[a], loaded[a].folder, loaded[a].difficulty, loaded[a].chart->info.difficulty_name, *titles[b], loaded[b].folder, loaded[b].difficulty, loaded[b].chart->info.difficulty_name);
            });

            ChartLibrary library;
            library.pitch = pitch;

            for (size_t index : order) {
                LoadedChart& entry = loaded[index];
                const ChartInfo& info = entry.chart->info;
                library.hashes.push_back(entry.chart->gameplay_hash());
                library.keys.push_back(chart_key(entry.chart->file_path));
                library.sort_titles.push_back(*titles[index]);
                library.sort_difficulties.push_back(entry.difficulty);
                library.difficulties.push_back(entry.difficulty);
                library.search_texts.push_back(ascii_lowercase(std::format("{} {} {} {} {} {}", info.title, info.title_unicode, info.artist, info.artist_unicode, info.mapper, info.difficulty_name)));
                library.details.push_back(std::format("[{}] - {} // {}", info.difficulty_name, info.artist, info.mapper));
                library.lengths_ms.push_back(pitched_length_ms(entry.summary, entry.chart->pitch));
                library.bpms.push_back(entry.summary.bpm);
                library.long_note_shares.push_back(entry.summary.notes == 0 ? 0.0f : (float)entry.summary.holds / (float)entry.summary.notes);
                library.note_counts.push_back(entry.summary.notes);
                library.added.push_back(entry.added);
                library.folders.push_back(std::move(entry.folder));
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

    std::u8string folder_key(const std::filesystem::path& folder_path) {
        std::u8string folder = folder_path.lexically_normal().u8string();

        for (char8_t& c : folder) {
            if (c >= u8'A' && c <= u8'Z') {
                c = (char8_t)(c - u8'A' + u8'a');
            }
        }

        return folder;
    }

    std::unique_ptr<Chart> load_chart_notes(const LibraryCache* library_cache, const Chart& chart) {
        if (library_cache != nullptr) {
            if (std::optional<std::vector<Hit>> notes = library_cache->load_notes(chart)) {
                return Chart::restore(chart.file_path, chart.unpitched_chart_info(), std::move(*notes), chart.unpitched_timing(), chart.gameplay_hash(), chart.difficulty_key(), 1.0f);
            }
        }

        std::string error;
        std::unique_ptr<Chart> loaded = load_chart_file(chart.file_path, 1.0f, error, chart.gameplay_hash());
        return loaded != nullptr && loaded->gameplay_hash() == chart.gameplay_hash() ? std::move(loaded) : nullptr;
    }

    std::optional<double> library_difficulty(DifficultyCache& cache, const LibraryCache* library_cache, const Chart& chart, float pitch, bool use_resident) {
        if (std::optional<double> cached = cache.lookup(chart, pitch)) {
            return cached;
        }

        if (use_resident && chart.has_notes()) {
            return cache.difficulty(chart, pitch);
        }

        std::unique_ptr<Chart> loaded = load_chart_notes(library_cache, chart);

        if (loaded == nullptr) {
            return std::nullopt;
        }

        double value = DifficultyCalculator::calculate_at_pitch(*loaded, pitch);
        cache.store(chart, pitch, value);
        return value;
    }

    float long_note_share(const Chart& chart) {
        if (chart.hits.empty()) {
            return 0.0f;
        }

        size_t holds = (size_t)std::count_if(chart.hits.begin(), chart.hits.end(), [](const Hit& hit) {
            return hit.hit_type == HitType::HOLD;
        });
        return (float)holds / (float)chart.hits.size();
    }

    size_t ChartLibrary::size() const {
        return charts.size();
    }

    void ChartLibrary::append(ChartLibrary& from, size_t index) {
        charts.push_back(std::move(from.charts[index]));
        hashes.push_back(std::move(from.hashes[index]));
        keys.push_back(std::move(from.keys[index]));
        folders.push_back(std::move(from.folders[index]));
        sort_titles.push_back(std::move(from.sort_titles[index]));
        sort_difficulties.push_back(from.sort_difficulties[index]);
        difficulties.push_back(index < from.difficulties.size() ? from.difficulties[index] : std::nullopt);
        search_texts.push_back(std::move(from.search_texts[index]));
        details.push_back(std::move(from.details[index]));
        lengths_ms.push_back(from.lengths_ms[index]);
        bpms.push_back(from.bpms[index]);
        long_note_shares.push_back(from.long_note_shares[index]);
        note_counts.push_back(from.note_counts[index]);
        added.push_back(from.added[index]);
    }

    ChartLibrary ChartLibrary::without(const std::vector<bool>& removed, std::vector<std::unique_ptr<Chart>>& dropped) {
        ChartLibrary library;
        library.pitch = pitch;

        for (size_t index = 0; index < charts.size(); index++) {
            if (index < removed.size() && removed[index]) {
                dropped.push_back(std::move(charts[index]));
                continue;
            }

            library.append(*this, index);
        }

        return library;
    }

    bool is_library_chart_file(const std::filesystem::path& path) {
        return extension_rank(path) < LIBRARY_CHART_EXTENSIONS.size();
    }

    bool holds_several_charts(const std::filesystem::path& path) {
        return is_stepmania_file(path);
    }

    std::vector<std::unique_ptr<Chart>> load_chart_text(std::string_view text, const std::filesystem::path& path, float pitch, std::string& error) {
        std::vector<std::unique_ptr<Chart>> charts;

        if (!is_stepmania_file(path)) {
            std::unique_ptr<Chart> chart = Chart::load_text(text, path, pitch, error);

            if (chart != nullptr) {
                charts.push_back(std::move(chart));
            }

            return charts;
        }

        std::vector<std::string> skipped;

        for (const StepManiaChart& converted : stepmania_to_chart_jsons(text, error)) {
            std::string chart_error = converted.error;
            std::unique_ptr<Chart> chart = converted.chart.is_null() ? nullptr : Chart::load_text(converted.chart.dump(), path, pitch, chart_error);

            if (chart != nullptr) {
                charts.push_back(std::move(chart));
            }
            else if (std::find(skipped.begin(), skipped.end(), chart_error) == skipped.end()) {
                skipped.push_back(std::move(chart_error));
            }
        }

        if (charts.empty() && error.empty()) {
            error = "no playable charts";

            for (size_t index = 0; index < skipped.size(); index++) {
                error += (index == 0 ? ": " : "; ") + skipped[index];
            }
        }

        return charts;
    }

    std::unique_ptr<Chart> load_chart_file(const std::filesystem::path& path, float pitch, std::string& error, std::string_view gameplay_hash) {
        std::ifstream file(path, std::ios::binary);

        if (!file) {
            error = "could not open file";
            return nullptr;
        }

        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::vector<std::unique_ptr<Chart>> charts = load_chart_text(text, path, pitch, error);

        if (charts.empty()) {
            return nullptr;
        }

        auto found = std::find_if(charts.begin(), charts.end(), [&](const std::unique_ptr<Chart>& chart) {
            return chart->gameplay_hash() == gameplay_hash;
        });

        return std::move(found != charts.end() ? *found : charts.front());
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
                loaded.push_back({std::move(restored.chart), std::move(folder), 0, restored.added, restored.summary});
            }
        }

        return build_library(std::move(loaded), pitch, cache, &library_cache);
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
            std::vector<std::unique_ptr<Chart>> charts;
        };

        std::error_code ec;
        std::vector<ChartFile> chart_files;

        auto ticks = [](std::filesystem::file_time_type time) {
            return (int64_t)time.time_since_epoch().count();
        };

        std::vector<std::tuple<std::filesystem::path, int64_t, bool>> chart_folders;

        for (const std::filesystem::path& song_directory : song_directories) {
            for (const auto& chart_folder : std::filesystem::directory_iterator(song_directory, ec)) {
                if (chart_folder.is_directory(ec) && chart_folder.path().extension() != ".part") {
                    std::filesystem::file_time_type time = chart_folder.last_write_time(ec);
                    chart_folders.emplace_back(chart_folder.path(), ec ? 0 : ticks(time), true);
                }
            }
        }

        if (progress != nullptr) {
            progress->begin(LibraryLoadStage::LISTING, chart_folders.size());
        }

        for (size_t folder_index = 0; folder_index < chart_folders.size(); folder_index++) {
            auto [chart_folder, added, may_hold_songs] = chart_folders[folder_index];

            if (progress != nullptr) {
                progress->step();
            }

            auto files = std::make_shared<std::unordered_set<std::u8string>>();
            std::vector<ChartFile> found;
            std::vector<std::filesystem::path> subfolders;
            int32_t best_stepmania = 0;

            for (const auto& chart_file : std::filesystem::directory_iterator(chart_folder, ec)) {
                files->insert(file_key(chart_file.path().filename()));
                std::error_code type_ec;

                if (may_hold_songs && chart_file.is_directory(type_ec)) {
                    subfolders.push_back(chart_file.path());
                    continue;
                }

                if (!is_library_chart_file(chart_file.path())) {
                    continue;
                }

                best_stepmania = std::max(best_stepmania, stepmania_preference(chart_file.path()));
                std::error_code stat_ec;
                ChartFile file{chart_file.path(), chart_key(chart_file.path()), nullptr, 0, 0, added};
                file.size = chart_file.file_size(stat_ec);
                std::filesystem::file_time_type time = chart_file.last_write_time(stat_ec);
                file.modified = stat_ec ? 0 : ticks(time);
                found.push_back(std::move(file));
            }

            std::erase_if(found, [&](const ChartFile& file) {
                int32_t preference = stepmania_preference(file.path);
                return preference > 0 && preference < best_stepmania;
            });

            if (found.empty() && may_hold_songs) {
                for (const std::filesystem::path& subfolder : subfolders) {
                    if (subfolder.extension() != ".part") {
                        std::error_code time_ec;
                        std::filesystem::file_time_type time = std::filesystem::last_write_time(subfolder, time_ec);
                        chart_folders.emplace_back(subfolder, time_ec ? 0 : ticks(time), false);
                    }
                }

                if (progress != nullptr) {
                    progress->total += subfolders.size();
                }

                continue;
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
                result.outcome = record->charts > 0 ? Outcome::KEPT_CHART : Outcome::KEPT_OTHER;
                result.playable = record->charts > 0 && audio_listed(record->audio_file, source.path, *source.folder_files);
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
                result.charts = load_chart_text(contents, source.path, pitch, error);
            }

            if (!result.charts.empty()) {
                const Chart& first = *result.charts.front();
                result.outcome = Outcome::NEW_CHART;
                result.playable = audio_listed(first.info.audio_file, source.path, *source.folder_files);

                if (!result.playable) {
                    error = std::format("audio file {} is missing", path_utf8(first.audio_path));
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
            LibraryCache::Entry entry{source.key, source.size, source.modified, source.added, {}, false};

            for (const std::unique_ptr<Chart>& chart : result.charts) {
                entry.charts.push_back(chart.get());
            }

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
                for (const std::unique_ptr<Chart>& chart : result.charts) {
                    fresh.push_back(chart.get());
                }
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
                for (std::unique_ptr<Chart>& chart : scanned[index].charts) {
                    update.added.push_back(std::move(chart));
                    update.added_times.push_back(chart_files[index].added);
                }
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

    void prepare_library_update(LibraryUpdate& update, float pitch, DifficultyCache& cache, const LibraryCache& library_cache, LibraryLoadProgress* progress) {
        std::vector<LoadedChart> loaded;
        std::unordered_set<std::string> hashes;

        for (size_t index = 0; index < update.added.size(); index++) {
            std::unique_ptr<Chart>& chart = update.added[index];

            if (chart->pitch != pitch) {
                chart->set_pitch(pitch);
            }

            if (hashes.insert(chart->gameplay_hash()).second) {
                std::u8string folder = folder_key(chart->file_path.parent_path());
                ChartSummary summary = summarize_chart(*chart);
                loaded.push_back({std::move(chart), std::move(folder), 0, update.added_times[index], summary});
            }
        }

        update.added.clear();
        update.added_times.clear();
        update.library = build_library(std::move(loaded), pitch, cache, &library_cache, progress);

        for (std::unique_ptr<Chart>& chart : update.library.charts) {
            chart->release_notes();
        }
    }

    ChartLibrary merge_chart_library(ChartLibrary current, LibraryUpdate update, float pitch, DifficultyCache& cache, std::vector<std::unique_ptr<Chart>>& removed) {
        ChartLibrary& fresh = update.library;
        std::vector<bool> kept(current.size(), false);
        std::unordered_map<std::string, size_t> fresh_hashes;
        std::unordered_map<std::u8string, std::vector<size_t>> fresh_folders;
        std::vector<bool> dropped(fresh.size(), false);

        for (size_t index = 0; index < fresh.size(); index++) {
            fresh_hashes.emplace(fresh.hashes[index], index);
            fresh_folders[fresh.folders[index]].push_back(index);
        }

        for (size_t index = 0; index < current.size(); index++) {
            if (!update.kept.contains(current.keys[index])) {
                continue;
            }

            kept[index] = true;

            if (auto duplicate = fresh_hashes.find(current.hashes[index]); duplicate != fresh_hashes.end()) {
                dropped[duplicate->second] = true;
            }

            if (auto folder = fresh_folders.find(current.folders[index]); folder != fresh_folders.end()) {
                for (size_t fresh_index : folder->second) {
                    fresh.sort_titles[fresh_index] = current.sort_titles[index];
                }

                fresh_folders.erase(folder);
            }
        }

        std::vector<size_t> additions;

        for (size_t index = 0; index < fresh.size(); index++) {
            if (!dropped[index]) {
                additions.push_back(index);
            }
            else {
                removed.push_back(std::move(fresh.charts[index]));
            }
        }

        std::stable_sort(additions.begin(), additions.end(), [&](size_t a, size_t b) {
            return library_order(fresh.sort_titles[a], fresh.folders[a], fresh.sort_difficulties[a], fresh.charts[a]->info.difficulty_name, fresh.sort_titles[b], fresh.folders[b], fresh.sort_difficulties[b], fresh.charts[b]->info.difficulty_name);
        });

        if (fresh.pitch != pitch) {
            for (size_t index : additions) {
                std::optional<double> difficulty = cache.lookup(*fresh.charts[index], pitch);
                fresh.difficulties[index] = difficulty ? std::optional<float>((float)*difficulty) : std::nullopt;
            }
        }

        ChartLibrary merged;
        merged.pitch = pitch;
        size_t next_addition = 0;

        auto take_additions_before = [&](size_t index) {
            while (next_addition < additions.size()) {
                size_t candidate = additions[next_addition];
                bool before = library_order(fresh.sort_titles[candidate], fresh.folders[candidate], fresh.sort_difficulties[candidate], fresh.charts[candidate]->info.difficulty_name, current.sort_titles[index], current.folders[index], current.sort_difficulties[index], current.charts[index]->info.difficulty_name);

                if (!before) {
                    return;
                }

                merged.append(fresh, candidate);
                next_addition++;
            }
        };

        for (size_t index = 0; index < current.size(); index++) {
            if (!kept[index]) {
                removed.push_back(std::move(current.charts[index]));
                continue;
            }

            take_additions_before(index);
            merged.append(current, index);
        }

        while (next_addition < additions.size()) {
            merged.append(fresh, additions[next_addition++]);
        }

        return merged;
    }

    void prepare_replay_folders(const ChartLibrary& library, LibraryLoadProgress* progress) {
        migrate_replay_folders(library, progress);
    }

    ChartLibrary single_chart_library(std::unique_ptr<Chart> chart, float pitch, DifficultyCache& cache) {
        std::u8string folder = folder_key(chart->file_path.parent_path());
        ChartSummary summary = summarize_chart(*chart);
        std::vector<LoadedChart> loaded;
        loaded.push_back({std::move(chart), std::move(folder), 0, 0, summary});
        return build_library(std::move(loaded), pitch, cache, nullptr);
    }
}

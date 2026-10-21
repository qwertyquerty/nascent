#pragma once

#include "Chart.h"
#include "gse/concurrency/Async.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace nascent {
    class DifficultyCache;
    class LibraryCache;

    struct ChartLibrary {
        std::vector<std::unique_ptr<Chart>> charts;
        std::vector<std::string> hashes;
        std::vector<std::optional<float>> difficulties;
        std::vector<std::string> search_texts;
        std::vector<std::string> details;
        std::vector<double> lengths_ms;
        std::vector<BpmRange> bpms;
        std::vector<int64_t> added;
        float pitch = 1.0f;

        size_t size() const;
        ChartLibrary without(const std::vector<bool>& removed);
    };

    enum class LibraryLoadStage : uint8_t {
        LISTING,
        READING,
        DIFFICULTY,
        SAVING,
        BUILDING,
        FINISHING,
        DONE
    };

    struct LibraryLoadProgress {
        std::atomic<uint8_t> stage{0};
        std::atomic<size_t> done{0};
        std::atomic<size_t> total{0};

        static constexpr std::array<float, (size_t)LibraryLoadStage::DONE> STAGE_WEIGHTS = {1.0f, 4.0f, 3.0f, 0.5f, 1.0f, 0.5f};

        void reset();
        void begin(LibraryLoadStage next, size_t count);
        void step();
        float fraction() const;
    };

    struct LibraryUpdate {
        std::unordered_set<std::string> kept;
        std::vector<std::unique_ptr<Chart>> added;
        std::vector<int64_t> added_times;
        bool changed = false;
    };

    inline constexpr std::array<std::string_view, 5> LIBRARY_CHART_EXTENSIONS = {".osu", ".json", ".ssc", ".sm", ".dwi"};

    bool is_library_chart_file(const std::filesystem::path& path);
    bool holds_several_charts(const std::filesystem::path& path);
    std::vector<std::unique_ptr<Chart>> load_chart_text(std::string_view text, const std::filesystem::path& path, float pitch, std::string& error);
    std::unique_ptr<Chart> load_chart_file(const std::filesystem::path& path, float pitch, std::string& error, std::string_view gameplay_hash = {});
    std::string chart_key(const std::filesystem::path& path);
    ChartLibrary cached_chart_library(LibraryCache& library_cache, const std::vector<std::filesystem::path>& song_directories, float pitch, DifficultyCache& cache);
    LibraryUpdate scan_chart_library(const std::vector<std::filesystem::path>& song_directories, float pitch, DifficultyCache& cache, LibraryCache& library_cache, LibraryLoadProgress* progress = nullptr);
    ChartLibrary merge_chart_library(ChartLibrary current, LibraryUpdate update, float pitch, DifficultyCache& cache, std::vector<std::unique_ptr<Chart>>& removed, LibraryLoadProgress* progress = nullptr);
    void prepare_replay_folders(const ChartLibrary& library, LibraryLoadProgress* progress = nullptr);
    ChartLibrary single_chart_library(std::unique_ptr<Chart> chart, float pitch, DifficultyCache& cache);
}

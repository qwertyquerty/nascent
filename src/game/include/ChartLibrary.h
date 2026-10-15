#pragma once

#include "Chart.h"
#include "gse/concurrency/Async.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace nascent {
    class DifficultyCache;

    struct ChartLibrary {
        std::vector<std::unique_ptr<Chart>> charts;
        std::vector<std::string> hashes;
        std::vector<std::optional<float>> difficulties;
        std::vector<std::string> search_texts;
        std::vector<std::string> details;
        std::vector<double> lengths_ms;
        std::vector<BpmRange> bpms;
        float pitch = 1.0f;

        size_t size() const;
        ChartLibrary without(const std::vector<bool>& removed);
    };

    ChartLibrary load_chart_library(const std::filesystem::path& song_directory, float pitch, DifficultyCache& cache);
    ChartLibrary single_chart_library(std::unique_ptr<Chart> chart, float pitch, DifficultyCache& cache);
}

#pragma once

#include "Chart.h"

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

    template <typename F>
    void parallel_for(size_t count, F work) {
        size_t thread_count = std::min<size_t>(std::max(1u, std::thread::hardware_concurrency()), std::max<size_t>(count, 1));
        std::atomic<size_t> next = 0;
        std::vector<std::thread> threads;

        for (size_t t = 0; t < thread_count; t++) {
            threads.emplace_back([&] {
                for (size_t index = next++; index < count; index = next++) {
                    work(index);
                }
            });
        }

        for (std::thread& thread : threads) {
            thread.join();
        }
    }

    struct ChartLibrary {
        std::vector<std::unique_ptr<Chart>> charts;
        std::vector<std::string> hashes;
        std::vector<std::optional<float>> difficulties;
        std::vector<std::string> search_texts;
        std::vector<std::string> details;
        std::vector<double> lengths_ms;
        float pitch = 1.0f;

        size_t size() const;
        ChartLibrary without(const std::vector<bool>& removed);
    };

    ChartLibrary load_chart_library(const std::filesystem::path& song_directory, float pitch, DifficultyCache& cache);
    ChartLibrary single_chart_library(std::unique_ptr<Chart> chart, float pitch, DifficultyCache& cache);
}

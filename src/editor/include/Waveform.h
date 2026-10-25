#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <future>
#include <memory>
#include <vector>

namespace nascent {
    struct WaveformBands {
        static constexpr size_t BAND_COUNT = 3;
        static constexpr double BIN_MS = 1.0;

        double bin_ms = BIN_MS;
        std::vector<std::array<float, BAND_COUNT>> bins;
    };

    std::shared_ptr<WaveformBands> analyse_waveform(const std::filesystem::path& file, const std::atomic<bool>* cancelled = nullptr);

    class Waveform {
        protected:
        std::future<std::shared_ptr<WaveformBands>> job;
        std::shared_ptr<WaveformBands> bands;
        std::shared_ptr<std::atomic<bool>> cancel;

        public:
        void load(const std::filesystem::path& file);
        void clear();
        void update();
        bool ready() const;
        bool loading() const;
        std::array<float, WaveformBands::BAND_COUNT> peak(double from_ms, double to_ms) const;
    };
}

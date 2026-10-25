#include "Waveform.h"

#include "Mp3Padding.h"
#include "Utf8Path.h"

#include "miniaudio.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>

namespace nascent {
    namespace {
        constexpr ma_uint32 SAMPLE_RATE = 24000;
        constexpr double LOW_CUTOFF_HZ = 220.0;
        constexpr double HIGH_CUTOFF_HZ = 2200.0;
        constexpr double NORMALIZE_PERCENTILE = 0.985;
        constexpr float MAX_LEVEL = 1.25f;
        constexpr ma_uint64 CHUNK_FRAMES = 8192;
        constexpr std::array<double, 3> SMOOTHING_MS = {5.0, 3.0, 0.0};

        double pole(double cutoff) {
            return 1.0 - std::exp(-2.0 * std::numbers::pi * cutoff / SAMPLE_RATE);
        }

        bool open_decoder(const std::filesystem::path& file, ma_decoder& decoder) {
            ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, SAMPLE_RATE);
#ifdef _WIN32
            return ma_decoder_init_file_w(file.c_str(), &config, &decoder) == MA_SUCCESS;
#else
            return ma_decoder_init_file(file.c_str(), &config, &decoder) == MA_SUCCESS;
#endif
        }

        bool is_mp3(const std::filesystem::path& file) {
            std::string extension = path_utf8(file.extension());
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return extension == ".mp3";
        }
    }

    std::shared_ptr<WaveformBands> analyse_waveform(const std::filesystem::path& file, const std::atomic<bool>* cancelled) {
        ma_decoder decoder;

        if (!open_decoder(file, decoder)) {
            return nullptr;
        }

        ma_uint64 skip = 0;

        if (is_mp3(file)) {
            if (std::optional<Mp3Padding> padding = read_mp3_padding(file)) {
                skip = (ma_uint64)std::llround(padding->milliseconds() * SAMPLE_RATE / 1000.0);
            }
        }

        auto bands = std::make_shared<WaveformBands>();
        size_t bin_frames = (size_t)std::lround(WaveformBands::BIN_MS * SAMPLE_RATE / 1000.0);
        bands->bin_ms = bin_frames * 1000.0 / SAMPLE_RATE;
        std::vector<float> chunk(CHUNK_FRAMES);
        std::array<double, WaveformBands::BAND_COUNT> energy = {};
        size_t in_bin = 0;
        double low = 0;
        double below_high = 0;
        double low_pole = pole(LOW_CUTOFF_HZ);
        double high_pole = pole(HIGH_CUTOFF_HZ);

        while (cancelled == nullptr || !cancelled->load()) {
            ma_uint64 read = 0;

            if (ma_decoder_read_pcm_frames(&decoder, chunk.data(), CHUNK_FRAMES, &read) != MA_SUCCESS && read == 0) {
                break;
            }

            if (read == 0) {
                break;
            }

            for (ma_uint64 frame = 0; frame < read; frame++) {
                double sample = chunk[(size_t)frame];
                low += low_pole * (sample - low);
                below_high += high_pole * (sample - below_high);

                if (skip > 0) {
                    skip--;
                    continue;
                }

                double mid = below_high - low;
                double high = sample - below_high;
                energy[0] += low * low;
                energy[1] += mid * mid;
                energy[2] += high * high;

                if (++in_bin == bin_frames) {
                    std::array<float, WaveformBands::BAND_COUNT> bin;

                    for (size_t band = 0; band < WaveformBands::BAND_COUNT; band++) {
                        bin[band] = (float)(energy[band] / (double)bin_frames);
                    }

                    bands->bins.push_back(bin);
                    energy = {};
                    in_bin = 0;
                }
            }
        }

        ma_decoder_uninit(&decoder);

        if (bands->bins.empty() || (cancelled != nullptr && cancelled->load())) {
            return nullptr;
        }

        std::vector<std::array<float, WaveformBands::BAND_COUNT>> energies = bands->bins;
        size_t count = energies.size();

        for (size_t band = 0; band < WaveformBands::BAND_COUNT; band++) {
            size_t radius = (size_t)std::lround(SMOOTHING_MS[band] / bands->bin_ms);
            double sum = 0;
            size_t window = 0;

            for (size_t index = 0; index < std::min(radius, count); index++) {
                sum += energies[index][band];
                window++;
            }

            for (size_t index = 0; index < count; index++) {
                if (index + radius < count) {
                    sum += energies[index + radius][band];
                    window++;
                }

                if (index > radius) {
                    sum -= energies[index - radius - 1][band];
                    window--;
                }

                bands->bins[index][band] = (float)std::sqrt(std::max(0.0, sum / (double)window));
            }
        }

        for (size_t band = 0; band < WaveformBands::BAND_COUNT; band++) {
            std::vector<float> levels;
            levels.reserve(bands->bins.size());

            for (const auto& bin : bands->bins) {
                levels.push_back(bin[band]);
            }

            size_t index = (size_t)(NORMALIZE_PERCENTILE * (double)(levels.size() - 1));
            std::nth_element(levels.begin(), levels.begin() + (ptrdiff_t)index, levels.end());
            float scale = levels[index] > 1e-6f ? 1.0f / levels[index] : 0.0f;

            for (auto& bin : bands->bins) {
                bin[band] = std::min(bin[band] * scale, MAX_LEVEL);
            }
        }

        return bands;
    }

    void Waveform::load(const std::filesystem::path& file) {
        clear();
        cancel = std::make_shared<std::atomic<bool>>(false);
        job = std::async(std::launch::async, [file, flag = cancel] { return analyse_waveform(file, flag.get()); });
    }

    void Waveform::clear() {
        if (cancel != nullptr) {
            cancel->store(true);
        }

        if (job.valid()) {
            job.wait();
            job = {};
        }

        bands.reset();
    }

    void Waveform::update() {
        if (job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            bands = job.get();
        }
    }

    bool Waveform::ready() const {
        return bands != nullptr;
    }

    bool Waveform::loading() const {
        return job.valid();
    }

    std::array<float, WaveformBands::BAND_COUNT> Waveform::peak(double from_ms, double to_ms) const {
        std::array<float, WaveformBands::BAND_COUNT> result = {};

        if (bands == nullptr || to_ms < 0) {
            return result;
        }

        int64_t count = (int64_t)bands->bins.size();
        int64_t first = std::clamp((int64_t)std::floor(std::max(0.0, from_ms) / bands->bin_ms), (int64_t)0, count);
        int64_t last = std::clamp((int64_t)std::floor(to_ms / bands->bin_ms), first, count - 1);

        for (int64_t index = first; index <= last && index < count; index++) {
            for (size_t band = 0; band < WaveformBands::BAND_COUNT; band++) {
                result[band] = std::max(result[band], bands->bins[(size_t)index][band]);
            }
        }

        return result;
    }
}

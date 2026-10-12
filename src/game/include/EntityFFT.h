#pragma once

#include <cstdint>
#include <vector>

#include "Entity.h"
#include "olcPixelGameEngine.h"
#include "kiss_fftr.h"

namespace nascent {
    class Skin;

    inline constexpr int FFT_WINDOW_SIZE = 4096;
    inline constexpr int FFT_REFERENCE_WINDOW_SIZE = 480;
    inline constexpr int FFT_SAMPLE_RATE = 48000;
    inline constexpr int FFT_CHANNELS = 2;
    inline constexpr double FFT_BIN_HZ = (double)FFT_SAMPLE_RATE / FFT_WINDOW_SIZE;
    inline constexpr double FFT_TILT_HZ = 100.0;
    inline constexpr double FFT_MIN_FREQ = 30.0;
    inline constexpr double FFT_MAX_FREQ = 16000.0;
    inline constexpr int FFT_BAR_COUNT = 128;
    inline constexpr double FFT_SPACING_RATIO = 0.25;
    inline constexpr double FFT_AVG_SMOOTHING_RATIO = 0.05;
    inline constexpr uint8_t FFT_BAR_ALPHA = 64;

    class EntityFFT : public Entity {
        protected:
        Skin* skin;
        kiss_fftr_cfg fft_cfg;
        std::vector<kiss_fft_cpx> output;
        std::vector<kiss_fft_scalar> input;
        std::vector<float> bins;
        std::vector<float> magnitudes;
        std::vector<double> bar_edges;
        uint8_t channel;

        public:
        olc::vd2d pos = {0, 0};
        olc::vd2d size = {0, 0};
        bool flipped = false;
        bool mirrored = false;
        float opacity = 1.0f;

        EntityFFT(Skin*, olc::vd2d, olc::vd2d, uint8_t);
        ~EntityFFT();
        EntityFFT(const EntityFFT&) = delete;
        EntityFFT& operator=(const EntityFFT&) = delete;

        void init(Scene*) override;
        void update(Scene*, float) override;
        void draw(olc::PixelGameEngine*) override;

        void update_song_buffer(const std::vector<float>& buffer, float elasped_time);
    };
}

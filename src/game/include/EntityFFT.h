#pragma once

#include <cstdint>

#include "Entity.h"
#include "Skin.h"
#include "olcPixelGameEngine.h"
#include "kiss_fftr.h"

#define FFT_WINDOW_SIZE 480
#define FFT_SAMPLE_RATE 48000
#define FFT_BIN_SIZE (FFT_SAMPLE_RATE / FFT_WINDOW_SIZE)
#define FFT_BAR_COUNT ((FFT_MAX_FREQ/FFT_BIN_SIZE) - (FFT_MIN_FREQ/FFT_BIN_SIZE))
#define FFT_MIN_FREQ 100
#define FFT_MAX_FREQ 20100
#define FFT_BAR_FREQ_INTERVAL 1.01469811603
#define FFT_SPACING_RATIO 0.25
#define FFT_AVG_SMOOTHING_RATIO 0.05
#define FFT_BAR_ALPHA 64

namespace nascent {
    class EntityFFT : public Entity {
        protected:
        Skin* skin;
        kiss_fftr_cfg fft_cfg;
        std::vector<kiss_fft_cpx> output;
        std::vector<kiss_fft_scalar> input;
        std::vector<float> bins;
        uint8_t channel;

        public:
        olc::vd2d pos = {0,0};
        olc::vd2d size = {0,0};
        bool flipped = false;

        EntityFFT(Skin*, olc::vd2d, olc::vd2d, uint8_t);
        void init(Scene*) override;
        void update(Scene*, float) override;
        void draw(olc::PixelGameEngine*) override;

        void update_song_buffer(const std::vector<float>& buffer, float elasped_time);
    };
}

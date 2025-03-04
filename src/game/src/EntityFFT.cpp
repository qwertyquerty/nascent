#include "EntityFFT.h"

#include "olcPixelGameEngine.h"

namespace nascent {
    EntityFFT::EntityFFT(Skin* skin, olc::vd2d pos, olc::vd2d size, uint8_t channel) {
        this->channel = channel;
        this->skin = skin;
        this->pos = pos;
        this->size = size;
        fft_cfg = kiss_fftr_alloc(FFT_WINDOW_SIZE, 0, 0, 0);
    };

    void EntityFFT::init(Scene* scene) {
        input.resize(FFT_WINDOW_SIZE);
        output.resize((FFT_WINDOW_SIZE / 2) + 1);
        bins.resize(FFT_BAR_COUNT);
    };

    void EntityFFT::update(Scene* game, float elapsed_time) {
        
    };

    void EntityFFT::draw(olc::PixelGameEngine* window) {
        float w = size.x / ((double)FFT_BAR_COUNT + (double)FFT_BAR_COUNT * FFT_SPACING_RATIO - FFT_SPACING_RATIO);

        for (int i = 0; i <= FFT_BAR_COUNT; i++) {
            float x = (float)(i) * w * (1+FFT_SPACING_RATIO);
            float h = std::min(bins[i] / 4 * size.y, size.y);
            olc::Pixel c = skin->title_gradient_at((x-pos.x)/size.x,0);
            c.a = FFT_BAR_ALPHA;

            if (flipped) {
                window->FillRectDecal({pos.x+size.x-x, pos.y+size.y-h}, {w, h}, c);
            } else {
                window->FillRectDecal({pos.x+x, pos.y}, {w, h}, c);
            }
        }
    };

    void EntityFFT::update_song_buffer(const std::vector<float>& buffer, float elapsed_time) {
        for (uint16_t i = 0; i < buffer.size() / 2; i++) {
            float mono_sample = buffer[i*2+channel];
            input[i] = mono_sample * 0.5 * (1 - std::cos(2 * 3.141592653 * i / (buffer.size()/2-1)));
        }

        kiss_fftr(fft_cfg, input.data(), output.data());

        for (int i = (FFT_MIN_FREQ/FFT_BIN_SIZE); i <= (FFT_MAX_FREQ/FFT_BIN_SIZE); i++) {
            float amplitude = std::log10(
                std::sqrt(output[i].r*output[i].r + output[i].i*output[i].i) * i + 1
            );

            float ratio = std::min(FFT_AVG_SMOOTHING_RATIO * (elapsed_time * 1000), 1.0);

            bins[i] = bins[i] * (1-ratio) + amplitude * ratio;
        }
    }
}

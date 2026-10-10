#include "EntityFFT.h"
#include "Skin.h"

#include "olcPixelGameEngine.h"

#include <algorithm>
#include <cmath>

namespace nascent {
    EntityFFT::EntityFFT(Skin* skin, olc::vd2d pos, olc::vd2d size, uint8_t channel) {
        this->channel = channel;
        this->skin = skin;
        this->pos = pos;
        this->size = size;
        fft_cfg = kiss_fftr_alloc(FFT_WINDOW_SIZE, 0, 0, 0);
    };

    EntityFFT::~EntityFFT() {
        kiss_fftr_free(fft_cfg);
    }

    void EntityFFT::init(Scene*) {
        input.resize(FFT_WINDOW_SIZE, 0);
        output.resize((FFT_WINDOW_SIZE / 2) + 1, kiss_fft_cpx{0, 0});
        bins.resize(FFT_BAR_COUNT, 0);
    };

    void EntityFFT::update(Scene*, float) {

    };

    void EntityFFT::draw(olc::PixelGameEngine* window) {
        float w = size.x / ((double)FFT_BAR_COUNT + (double)FFT_BAR_COUNT * FFT_SPACING_RATIO - FFT_SPACING_RATIO);

        for (int bar = 0; bar < FFT_BAR_COUNT; bar++) {
            float x = (float)bar * w * (1 + FFT_SPACING_RATIO);
            float h = std::min(bins[bar] / 4 * size.y, size.y);
            olc::Pixel c = skin->title_gradient_at(x / size.x, 0);
            c.a = FFT_BAR_ALPHA;

            if (flipped) {
                window->FillRectDecal({(float)(pos.x + size.x - x - w), (float)(pos.y + size.y - h)}, {w, h}, c);
            }
            else {
                window->FillRectDecal({(float)(pos.x + x), (float)pos.y}, {w, h}, c);
            }
        }
    };

    void EntityFFT::update_song_buffer(const std::vector<float>& buffer, float elapsed_time) {
        size_t frames = std::min(buffer.size() / FFT_CHANNELS, (size_t)FFT_WINDOW_SIZE);

        if (frames < 2) {
            return;
        }

        for (size_t i = 0; i < frames; i++) {
            float mono_sample = buffer[i * FFT_CHANNELS + channel];
            input[i] = mono_sample * 0.5 * (1 - std::cos(2 * 3.141592653 * i / (frames - 1)));
        }

        std::fill(input.begin() + frames, input.end(), 0);

        kiss_fftr(fft_cfg, input.data(), output.data());

        float ratio = std::min(FFT_AVG_SMOOTHING_RATIO * (elapsed_time * 1000), 1.0);

        for (int bar = 0; bar < FFT_BAR_COUNT; bar++) {
            int i = FFT_MIN_BIN + bar;

            float amplitude = std::log10(
                std::sqrt(output[i].r * output[i].r + output[i].i * output[i].i) * i + 1
            );

            bins[bar] = bins[bar] * (1 - ratio) + amplitude * ratio;
        }
    }
}

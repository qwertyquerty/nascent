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
        magnitudes.resize(output.size(), 0);
        bar_edges.resize(FFT_BAR_COUNT + 1);

        for (int edge = 0; edge <= FFT_BAR_COUNT; edge++) {
            bar_edges[edge] = FFT_MIN_FREQ * std::pow(FFT_MAX_FREQ / FFT_MIN_FREQ, (double)edge / FFT_BAR_COUNT);
        }
    };

    void EntityFFT::update(Scene*, float) {

    };

    void EntityFFT::draw(olc::PixelGameEngine* window) {
        float w = size.x / ((double)FFT_BAR_COUNT + (double)FFT_BAR_COUNT * FFT_SPACING_RATIO - FFT_SPACING_RATIO);

        for (int bar = 0; bar < FFT_BAR_COUNT; bar++) {
            float x = (float)bar * w * (1 + FFT_SPACING_RATIO);
            float h = std::min(bins[bar] / 4 * size.y, size.y);
            olc::Pixel c = skin->title_gradient_at(x / size.x, 0);
            c.a = (uint8_t)std::lround(FFT_BAR_ALPHA * std::clamp(opacity, 0.0f, 1.0f));

            float bar_x = (float)(mirrored ? pos.x + size.x - x - w : pos.x + x);
            float bar_y = (float)(flipped ? pos.y + size.y - h : pos.y);
            window->FillRectDecal({bar_x, bar_y}, {w, h}, c);
        }
    };

    void EntityFFT::update_song_buffer(const std::vector<float>& buffer, float elapsed_time) {
        size_t available = buffer.size() / FFT_CHANNELS;
        size_t frames = std::min(available, (size_t)FFT_WINDOW_SIZE);

        if (frames < 2) {
            return;
        }

        size_t first = available - frames;

        for (size_t i = 0; i < frames; i++) {
            float mono_sample = buffer[(first + i) * FFT_CHANNELS + channel];
            input[i] = mono_sample * 0.5 * (1 - std::cos(2 * 3.141592653 * i / (frames - 1)));
        }

        std::fill(input.begin() + frames, input.end(), 0);

        kiss_fftr(fft_cfg, input.data(), output.data());

        float ratio = std::min(FFT_AVG_SMOOTHING_RATIO * (elapsed_time * 1000), 1.0);

        for (size_t i = 0; i < output.size(); i++) {
            magnitudes[i] = std::sqrt(output[i].r * output[i].r + output[i].i * output[i].i);
        }

        for (int bar = 0; bar < FFT_BAR_COUNT; bar++) {
            double low = bar_edges[bar];
            double high = bar_edges[bar + 1];
            double center = std::sqrt(low * high);
            size_t first_bin = (size_t)std::ceil(low / FFT_BIN_HZ);
            size_t last_bin = std::min((size_t)std::ceil(high / FFT_BIN_HZ), magnitudes.size());
            float peak = 0;

            if (first_bin < last_bin) {
                peak = *std::max_element(magnitudes.begin() + first_bin, magnitudes.begin() + last_bin);
            }
            else {
                double position = std::min(center / FFT_BIN_HZ, (double)magnitudes.size() - 1.001);
                size_t lower = (size_t)position;
                float fraction = (float)(position - (double)lower);
                peak = magnitudes[lower] * (1 - fraction) + magnitudes[lower + 1] * fraction;
            }

            double weight = center / FFT_TILT_HZ * FFT_REFERENCE_WINDOW_SIZE / FFT_WINDOW_SIZE;
            float amplitude = std::log10(peak * weight + 1);

            bins[bar] = bins[bar] * (1 - ratio) + amplitude * ratio;
        }
    }
}

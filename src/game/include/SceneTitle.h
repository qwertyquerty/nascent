#pragma once

#include "Scene.h"
#include "SongClock.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace nascent {
    class Attempt;
    class Chart;
    class EntityFFT;
    class EntityField;
    class Skin;

    class SceneTitle : public Scene {
        protected:
        std::unique_ptr<Chart> logo_chart;
        std::unique_ptr<Attempt> logo_attempt;
        std::unique_ptr<EntityField> logo_field;
        SongClock logo_clock;
        double logo_position = 0;

        std::unique_ptr<Chart> bg_chart;
        std::vector<std::unique_ptr<EntityField>> bg_field;
        SongClock bg_clock;
        double bg_position = 0;

        std::unique_ptr<EntityFFT> fft_l;
        std::unique_ptr<EntityFFT> fft_r;

        std::vector<float> audio_buffer;

        int logo_audio_id = -1;
        int bg_audio_id = -1;

        std::unique_ptr<Skin> logo_skin;
        Skin* bg_skin;

        double timer = 0;

        bool loaded = false;
        bool logo_started = false;
        bool bg_started = false;

        uint8_t get_field_count();

        public:
        SceneTitle(Skin* bg_skin);
        ~SceneTitle();
        void init(Game*) override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}

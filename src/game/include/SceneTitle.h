#pragma once

#include "Scene.h"
#include "SongClock.h"
#include "Util.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace nascent {
    class Attempt;
    class Chart;
    class EntityFFT;
    class EntityField;
    class SceneList;
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
        SpriteDecal start_hint;

        int logo_audio_id = -1;
        int bg_audio_id = -1;

        std::unique_ptr<Skin> logo_skin;
        Skin* bg_skin;
        SceneList* list;

        double timer = 0;
        float bg_fade = 0;

        bool loaded = false;
        bool logo_started = false;
        bool bg_started = false;

        uint8_t get_field_count();
        std::unique_ptr<Chart> load_random_chart();
        bool load_background();

        public:
        static constexpr const char* LOGO_CHART_PATH = "assets/logo/logo.json";
        static constexpr int32_t RANDOM_CHART_ATTEMPTS = 10;
        static constexpr float BACKGROUND_FADE_IN_TIME = 1.0f;
        static constexpr const char* START_HINT = "Press Enter to start";
        static constexpr int32_t START_HINT_GAP = 24;
        static constexpr float START_HINT_PULSE_SPEED = 2.5f;

        SceneTitle(Skin* bg_skin, SceneList* list);
        ~SceneTitle();
        void init(Game*) override;
        void on_exit() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}

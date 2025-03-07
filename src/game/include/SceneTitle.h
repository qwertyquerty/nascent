#pragma once

#include "Scene.h"
#include "Chart.h"
#include "Game.h"
#include "EntityField.h"
#include "EntityFFT.h"
#include "Skin.h"

#define SCENE_TITLE_FIELD_COUNT 9
#define SCENE_TITLE_SCROLL_RATE_PX_PER_S 250

#define LOGO_START_DELAY_S 1

namespace nascent {
    class SceneTitle : public Scene {
        protected:
        Chart* logo_chart;
        EntityField* logo_field;

        Chart* bg_chart;
        EntityField* bg_field[SCENE_TITLE_FIELD_COUNT];

        EntityFFT* fft_l;
        EntityFFT* fft_r;

        int logo_audio_id;
        int bg_audio_id;

        Skin* logo_skin;
        Skin* bg_skin;

        double timer;

        bool logo_started = false;
        bool bg_started = false;

        public:
        SceneTitle();
        ~SceneTitle();
        void init(Game*) override;
        void update(Game*, float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}

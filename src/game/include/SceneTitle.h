#pragma once

#include "Scene.h"
#include "Chart.h"
#include "Game.h"
#include "EntityField.h"
#include "EntityFFT.h"
#include "Skin.h"
#include "SettingsManager.h"

namespace nascent {
    class SceneTitle : public Scene {
        protected:
        Chart* logo_chart;
        EntityField* logo_field;

        Chart* bg_chart;
        std::vector<EntityField*> bg_field;

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
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}

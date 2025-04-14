#pragma once

#include "Scene.h"
#include "Chart.h"
#include "Game.h"
#include "EntityField.h"
#include "EntityFFT.h"
#include "Skin.h"

#define PLAY_START_DELAY_S 10
#define FIELD_AUDIO_VISUAL_OFFSET_MS -30
#define FIELD_AUDIO_INPUT_OFFSET_MS -40

namespace nascent {
    class ScenePlay : public Scene {
        protected:
        Chart* chart;
        EntityField* field;

        int chart_audio_id;

        Skin* skin;

        double timer;

        bool chart_started = false;

        public:
        ScenePlay();
        ~ScenePlay();
        void init(Game*) override;
        void update(Game*, float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}

#pragma once

#include "Scene.h"
#include "Chart.h"
#include "Game.h"
#include "EntityField.h"
#include "EntityFFT.h"
#include "Skin.h"

namespace nascent {
    class ScenePlay : public Scene {
        protected:
        Chart* chart;
        EntityField* field;

        int chart_audio_id;

        Skin* skin;

        double timer = 0;

        bool chart_started = false;
        bool paused = false;

        uint64_t frame_pos = 0;

        float chart_pitch = 1;

        public:
        ScenePlay(Chart* chart, Skin* skin);
        ~ScenePlay();
        void init(Game*) override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}

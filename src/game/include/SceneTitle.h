#pragma once

#include "Scene.h"
#include "Chart.h"
#include "Game.h"
#include "EntityField.h"

namespace nascent {
    class SceneTitle : public Scene {
        protected:
        Chart* logo_chart;
        EntityField* logo_field;
        int logo_audio_id;

        public:
        SceneTitle();
        void init(Game*) override;
        void update(Game*, float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}

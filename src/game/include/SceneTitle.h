#pragma once

#include "Scene.h"
#include "Chart.h"
#include "Game.h"
#include "EntityField.h"
#include "Skin.h"

#define SCENE_TITLE_FIELD_COUNT 11
#define SCENE_TITLE_SCROLL_RATE_PX_PER_S 250

namespace nascent {
    class SceneTitle : public Scene {
        protected:
        Chart* logo_chart;
        EntityField* logo_field[SCENE_TITLE_FIELD_COUNT];
        int logo_audio_id;
        Skin* title_skin;

        public:
        SceneTitle();
        ~SceneTitle();
        void init(Game*) override;
        void update(Game*, float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}

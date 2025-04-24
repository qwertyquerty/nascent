#pragma once

#include "Scene.h"
#include "Chart.h"
#include "Game.h"
#include "EntityField.h"
#include "EntityFFT.h"
#include "Skin.h"

#define LIST_SONG_DIRECTORY "assets/songs"

namespace nascent {
    class SceneList : public Scene {
        protected:
        std::vector<Chart*> charts;
        Chart* selected_chart = nullptr;
        int32_t selected_chart_position = 0;

        EntityField* field;

        int selected_chart_audio_id = -1;

        Skin* skin;

        double timer = 0;

        void load_charts();
        void select_chart(Chart*);

        public:
        SceneList();
        ~SceneList();
        void init(Game*) override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}

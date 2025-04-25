#pragma once

#include "Scene.h"
#include "Chart.h"
#include "Game.h"
#include "EntityField.h"
#include "EntityFFT.h"
#include "Skin.h"

namespace nascent {
    class SceneList : public Scene {
        protected:
        std::vector<Chart*> charts;
        Chart* selected_chart = nullptr;
        int32_t selected_chart_index = 0;
        int32_t selected_chart_position = 0;

        EntityField* field;

        int selected_chart_audio_id = -1;

        Skin* skin;

        double timer = 0;

        void load_charts();
        void select_chart(int32_t);

        public:
        SceneList();
        ~SceneList();
        void init(Game*) override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}

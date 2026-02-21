#pragma once

#include "Scene.h"
#include "Chart.h"
#include "Game.h"
#include "EntityField.h"
#include "EntityFFT.h"
#include "Skin.h"
#include "Util.h"

#include <mutex>

namespace nascent {
    class SceneList : public Scene {
        protected:
        std::vector<Chart*> charts;
        Chart* selected_chart = nullptr;
        int32_t selected_chart_index = 0;
        int32_t selected_chart_position = 0;

        EntityField* field = nullptr;

        int selected_chart_audio_id = -1;

        const uint8_t list_chart_height = 100;

        float list_scroll_offset = 0;

        Skin* skin;

        olc::Decal* background_image = nullptr;
        olc::Decal* old_background_image = nullptr;
        olc::Sprite* background_sprite = nullptr;

        LatestWinsMutex background_load_mutex;
        bool load_done;

        double timer = 0;

        bool crossfade = false;
        float crossfade_timer = 0;
        float audio_fadein_timer = 0;

        float pitch = 1.0;

        void load_charts();
        void select_chart(int32_t);
        void set_pitch(float);

        public:
        SceneList();
        ~SceneList();
        void init(Game*) override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
        void load_chart_image_song();
    };
}

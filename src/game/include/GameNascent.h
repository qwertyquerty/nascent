#pragma once

#include "Game.h"
#include "SceneTitle.h"
#include "ScenePlay.h"

#define DISCORD_APPLICATION_ID "1360933253070061718"

namespace nascent {
    class GameNascent : public Game {
        protected:
        Scene* scene_title;
        float elapsed_time;
        int64_t start_time;
        Skin* skin;

        int foreground_layer = 0;
        int background_layer;

        public:
        bool debug = true;
        GameNascent(olc::PixelGameEngine* window);
        ~GameNascent();
        void init() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;

        void discord_init();
        void discord_update();
    };
}

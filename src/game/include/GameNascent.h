#pragma once

#include "Game.h"

#include <cstdint>
#include <memory>

namespace nascent {
    class SceneList;
    class Skin;

    class GameNascent : public Game {
        protected:
        std::unique_ptr<Skin> skin;
        std::unique_ptr<SceneList> scene_list;
        float elapsed_time = 0;
        int64_t start_time;

        public:
        GameNascent(olc::PixelGameEngine* window);
        ~GameNascent();
        void init() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;

        void discord_init();
        void discord_update();
    };
}

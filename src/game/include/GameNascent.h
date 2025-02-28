#pragma once

#include "Game.h"
#include "SceneTitle.h"

namespace nascent {
    class GameNascent : public Game {
        protected:
        Scene* scene_title;
        float elapsed_time;
        
        public:
        GameNascent();
        ~GameNascent();
        void init() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}

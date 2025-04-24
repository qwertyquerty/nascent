#pragma once

#include "Entity.h"
#include "olcPixelGameEngine.h"

#include <vector>

namespace nascent {
    class Game;

    class Scene {
        protected:
        std::vector<Entity> entities;
        Game* game;

        public:
        Scene();
        virtual void init(Game*) = 0;
        virtual void update(float) = 0;
        virtual void draw(olc::PixelGameEngine*) = 0;

        void set_game(Game*);
    };
}

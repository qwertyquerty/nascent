#pragma once

#include "Entity.h"
#include "olcPixelGameEngine.h"

#include <vector>

namespace nascent {
    class Game;

    class Scene {
        protected:
        std::vector<Entity> entities;

        public:
        Scene();
        virtual void init(Game*) = 0;
        virtual void update(Game*, float) = 0;
        virtual void draw(olc::PixelGameEngine*) = 0;
    };
}

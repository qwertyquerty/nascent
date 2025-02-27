#pragma once

#include "olcPixelGameEngine.h"

namespace nascent {
    class Scene;

    class Entity {
        public:
        Entity();
        virtual void init(Scene*) = 0;
        virtual void update(Scene*, float) = 0;
        virtual void draw(olc::PixelGameEngine*) = 0;
    };
}

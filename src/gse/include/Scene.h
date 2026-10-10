#pragma once

#include "olcPixelGameEngine.h"

namespace nascent {
    class Game;

    class Scene {
        protected:
        Game* game = nullptr;

        public:
        bool initialized = false;

        Scene();
        virtual ~Scene() = default;
        virtual void init(Game*) = 0;
        virtual void on_enter() {}
        virtual void on_exit() {}
        virtual void update(float) = 0;
        virtual void draw(olc::PixelGameEngine*) = 0;

        void set_game(Game*);
    };
}

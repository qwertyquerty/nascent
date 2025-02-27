#pragma once

#include "Scene.h"
#include "olcPixelGameEngine.h"
#include "olcPGEX_MiniAudio.h"

namespace nascent {
    class Game {
        protected:
        Scene* scene;
        olc::MiniAudio audio;

        public:
        Game();
        virtual void init() = 0;
        virtual void update(float) = 0;
        virtual void draw(olc::PixelGameEngine*) = 0;

        void set_scene(Scene* scene);
        Scene* get_scene();
        olc::MiniAudio& get_audio();
    };
}

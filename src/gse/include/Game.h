#pragma once

#include "Scene.h"
#include "olcPixelGameEngine.h"
#include "olcPGEX_MiniAudio.h"

namespace nascent {
    class Game {
        protected:
        Scene* scene = nullptr;
        Scene* queued_scene = nullptr;

        olc::MiniAudio audio;

        public:
        olc::PixelGameEngine* window;

        Game(olc::PixelGameEngine* window, Scene* scene);
        virtual void init() = 0;
        virtual void update(float) = 0;
        virtual void draw(olc::PixelGameEngine*) = 0;

        void set_scene(Scene* scene);
        void try_scene_swap();
        Scene* get_scene();
        olc::MiniAudio& get_audio();
    };
}

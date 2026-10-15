#pragma once

#include "gse/FrameClock.h"
#include "gse/Overlay.h"
#include "gse/Scene.h"
#include "gse/SceneStack.h"
#include "gse/Transition.h"
#include "gse/audio/AudioMixer.h"
#include "gse/input/Input.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <memory>
#include <string>

namespace nascent {
    class Game {
        protected:
        Input input_state;
        AudioMixer mixer;
        FrameClock frame_clock;
        SceneStack scene_stack;
        OverlayStack global_overlays;
        std::unique_ptr<Transition> transition;
        bool quit_requested = false;
        olc::vi2d screen_size = {0, 0};
        olc::Pixel clear_color = olc::BLACK;

        void use_blank_canvas();
        void begin_frame(float elapsed_time);
        void update_scene(float elapsed_time);
        void draw_scenes(olc::PixelGameEngine* window);
        void release_scenes();
        void track_screen_size();
        virtual void on_screen_resize() {}
        virtual void on_back() {}

        public:
        olc::PixelGameEngine* window;

        Game(olc::PixelGameEngine* window);
        virtual ~Game();
        virtual void init() = 0;
        virtual void update(float elapsed_time) = 0;
        virtual void draw(olc::PixelGameEngine* window) = 0;

        SceneStack& scenes();
        Input& input();
        AudioMixer& audio();
        OverlayStack& overlays();
        void set_transition(std::unique_ptr<Transition> transition);
        void limit_frame_rate(uint16_t frames_per_second);

        virtual void notify(const std::string& message, bool is_error = false);
        void quit();
        bool should_quit() const;
    };
}

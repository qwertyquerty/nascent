#pragma once

#include "KeyEvents.h"
#include "Scene.h"
#include "olcPixelGameEngine.h"
#include "olcPGEX_MiniAudio.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace nascent {
    class Game {
        protected:
        Scene* scene = nullptr;
        Scene* pending_scene = nullptr;
        olc::MiniAudio audio;
        std::unique_ptr<Scene> owned_scene;
        std::unique_ptr<Scene> pending_owned_scene;
        Scene* outgoing_scene = nullptr;
        std::unique_ptr<Scene> outgoing_owned_scene;
        float transition_time = 0;
        bool skip_transition_step = false;
        std::optional<std::chrono::steady_clock::time_point> next_frame;
        float music_volume = 1.0f;
        float effects_volume = 1.0f;
        int32_t transition_layer = -1;
        KeyEventQueue key_event_queue;
        std::vector<KeyEvent> frame_key_events;
        std::string frame_text;
        bool key_event_hook_active = false;
        bool quit_requested = false;

        void init_input();
        void use_blank_canvas();
        void make_layer_blank(uint32_t layer);
        void poll_key_events();
        void begin_frame(float elapsed_time);
        void apply_pending_scene();
        void update_transition(float elapsed_time);
        void draw_scenes(olc::PixelGameEngine* window);
        void release_scenes();

        public:
        olc::PixelGameEngine* window;
        float scene_transition_time = 0.35f;
        static constexpr float MAX_TRANSITION_STEP = 1.0f / 30.0f;
        static constexpr std::chrono::duration<double> FRAME_SPIN_TIME = std::chrono::milliseconds(1);

        Game(olc::PixelGameEngine* window);
        virtual ~Game();
        virtual void init() = 0;
        virtual void update(float) = 0;
        virtual void draw(olc::PixelGameEngine*) = 0;

        void set_scene(Scene* scene);
        void set_scene(std::unique_ptr<Scene> scene);
        bool is_transitioning() const;
        void limit_frame_rate(uint16_t limit);
        olc::MiniAudio& get_audio();

        void play_sound(int id, bool looping = false);
        void play_music(int id, bool looping = false);
        void apply_music_volume(int id, float fade = 1.0f);
        void apply_effects_volume(int id);
        void set_volumes(float master, float music, float effects);
        void stop_sound(int id);

        const std::vector<KeyEvent>& get_frame_key_events();
        const std::string& get_frame_text();

        void quit();
        bool should_quit();
    };
}

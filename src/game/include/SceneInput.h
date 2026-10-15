#pragma once

#include "gse/Scene.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nascent {
    class Skin;

    class SceneInput : public Scene {
        protected:
        static constexpr uint8_t MIN_KEY_COUNT = 1;
        static constexpr uint8_t MAX_KEY_COUNT = 10;

        Skin* skin;

        uint8_t key_count = 4;
        uint8_t selected_lane = 0;
        std::vector<olc::Key> lane_keys;

        bool capturing = false;
        bool capture_all = false;
        double capture_time = 0;
        float timer = 0;

        std::string status;
        bool status_is_error = false;

        void load_mode(uint8_t key_count);
        void bind(olc::Key key);
        void reset();
        void play_menu_sound();
        void draw_lanes(olc::PixelGameEngine* window, int32_t top) const;

        public:
        SceneInput(Skin* skin, uint8_t key_count);
        void on_create() override;
        void on_enter() override;
        void update(float) override;
        void draw(olc::PixelGameEngine*) override;
    };
}

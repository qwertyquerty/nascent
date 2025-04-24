#pragma once

#include "olcPixelGameEngine.h"

namespace nascent {
    class InputManager {
        public:
        static const olc::Key pause_key = olc::Key::TAB;
        
        static const olc::Key random_chart_key = olc::Key::F2;

        static const olc::Key menu_up_key = olc::Key::UP;
        static const olc::Key menu_down_key = olc::Key::DOWN;
        static const olc::Key menu_left_key = olc::Key::LEFT;
        static const olc::Key menu_right_key = olc::Key::RIGHT;

        static const olc::Key menu_start_key = olc::Key::ENTER;

        static uint32_t get_lane_keys_pressed(olc::PixelGameEngine* window);
    };
}
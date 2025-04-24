#pragma once

#include "olcPixelGameEngine.h"

namespace nascent {
    class InputManager {
        public:
        static const olc::Key pause_key = olc::Key::TAB;
        static const olc::Key random_chart_key = olc::Key::F2;
        static uint32_t get_lane_keys_pressed(olc::PixelGameEngine* window);
    };
}
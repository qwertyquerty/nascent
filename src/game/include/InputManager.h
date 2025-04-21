#pragma once

#include "olcPixelGameEngine.h"

namespace nascent {
    class InputManager {
        public:
        static const olc::Key pause_key = olc::Key::TAB;
        static uint32_t get_lane_keys_pressed(olc::PixelGameEngine* window);
    };
}
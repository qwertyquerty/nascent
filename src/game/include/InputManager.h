#pragma once

#include "olcPixelGameEngine.h"

namespace nascent {
    class InputManager {
        public:
        static uint32_t get_lane_keys_pressed(olc::PixelGameEngine* window);
    };
}
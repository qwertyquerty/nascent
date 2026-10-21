#pragma once

#include "olcPixelGameEngine.h"

#include <cstdint>

namespace nascent {
    class Attempt;
    class Skin;

    inline constexpr uint8_t TIMING_BAR_MAX_LENGTH = 50;

    void draw_timing_bar(olc::PixelGameEngine* window, const Skin* skin, const Attempt& attempt, float center_x, float length, float thickness, float height, bool show_average);
}

#pragma once

#include "olcPixelGameEngine.h"

#include <cstdint>

namespace nascent {
    class Attempt;
    class MipDecal;
    class Skin;

    inline constexpr uint8_t TIMING_BAR_MAX_LENGTH = 50;

    void draw_timing_bar(olc::PixelGameEngine* window, Skin* skin, const Attempt& attempt, float center_x, float length, float thickness, float height, bool show_average);
    void draw_skin_image(olc::PixelGameEngine* window, const MipDecal* image, olc::vf2d position, olc::vf2d size, olc::Pixel tint, float fraction = 1.0f);
}

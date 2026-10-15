#pragma once

#include "olcPixelGameEngine.h"
#include "olcPGEX_TTF.h"

#include <string_view>

namespace nascent {
    struct Rect {
        olc::vf2d position;
        olc::vf2d size;

        bool contains(olc::vf2d point) const;
        olc::vf2d center() const;
        Rect inflated(float amount) const;
    };

    olc::FontRect draw_text_centered(olc::Font& font, std::string_view text, float center_x, float baseline, olc::Pixel color, float scale = 1.0f);
    Rect text_rect(olc::Font& font, std::string_view text, olc::vf2d baseline, float scale = 1.0f);
}

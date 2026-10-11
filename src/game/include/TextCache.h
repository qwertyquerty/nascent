#pragma once

#include "olcPGEX_TTF.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <string_view>

namespace nascent {
    inline constexpr uint64_t TEXT_CACHE_EVICT_FRAMES = 300;
    inline constexpr uint64_t TEXT_CACHE_SWEEP_FRAMES = 60;

    void draw_text(olc::Font& font, std::string_view text, olc::vi2d baseline, olc::Pixel color);
    void draw_text(olc::Font& font, std::string_view text, int32_t x, int32_t y, olc::Pixel color);
    olc::FontRect text_bounds(olc::Font& font, std::string_view text);

    void text_cache_begin_frame();
    void text_cache_forget(const olc::Font& font);
    void text_cache_clear();
}

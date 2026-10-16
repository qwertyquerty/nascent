#pragma once

#include "gse/input/Input.h"
#include "olcPixelGameEngine.h"
#include "olcPGEX_TTF.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nascent {
    struct Rect {
        olc::vf2d position;
        olc::vf2d size;

        bool contains(olc::vf2d point) const;
        olc::vf2d center() const;
        Rect inflated(float amount) const;
    };

    class Hotspots {
        protected:
        std::vector<std::pair<Rect, int32_t>> spots;

        public:
        void clear();
        void add(const Rect& rect, int32_t id);
        std::optional<int32_t> at(olc::vf2d point) const;
        bool hovered(int32_t id, olc::vf2d point) const;
        std::optional<int32_t> take_click(Input& input, uint32_t button = 0) const;
    };

    struct Hint {
        std::string text;
        int32_t id = -1;
    };

    float hints_width(olc::Font& font, const std::vector<Hint>& hints, float gap);
    void draw_hints(olc::PixelGameEngine* window, olc::Font& font, const std::vector<Hint>& hints, olc::vf2d baseline, float gap, olc::Pixel color, olc::Pixel hover_color, Hotspots& hotspots);

    olc::FontRect draw_text_centered(olc::Font& font, std::string_view text, float center_x, float baseline, olc::Pixel color, float scale = 1.0f);
    Rect text_rect(olc::Font& font, std::string_view text, olc::vf2d baseline, float scale = 1.0f);
}

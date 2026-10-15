#include "gse/ui/Layout.h"

#include "gse/render/TextCache.h"

namespace nascent {
    bool Rect::contains(olc::vf2d point) const {
        return point.x >= position.x && point.x <= position.x + size.x && point.y >= position.y && point.y <= position.y + size.y;
    }

    olc::vf2d Rect::center() const {
        return position + size / 2.0f;
    }

    Rect Rect::inflated(float amount) const {
        return {position - olc::vf2d{amount, amount}, size + olc::vf2d{amount * 2.0f, amount * 2.0f}};
    }

    olc::FontRect draw_text_centered(olc::Font& font, std::string_view text, float center_x, float baseline, olc::Pixel color, float scale) {
        olc::FontRect bounds = text_bounds(font, text);
        draw_text(font, text, {center_x - (bounds.offset.x + bounds.size.x / 2.0f) * scale, baseline}, color, scale);
        return bounds;
    }

    Rect text_rect(olc::Font& font, std::string_view text, olc::vf2d baseline, float scale) {
        olc::FontRect bounds = text_bounds(font, text);
        return {{baseline.x + bounds.offset.x * scale, baseline.y + bounds.offset.y * scale}, {bounds.size.x * scale, bounds.size.y * scale}};
    }
}

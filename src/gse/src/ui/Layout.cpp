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

    void Hotspots::clear() {
        spots.clear();
    }

    void Hotspots::add(const Rect& rect, int32_t id) {
        spots.emplace_back(rect, id);
    }

    std::optional<int32_t> Hotspots::at(olc::vf2d point) const {
        for (auto spot = spots.rbegin(); spot != spots.rend(); ++spot) {
            if (spot->first.contains(point)) {
                return spot->second;
            }
        }

        return std::nullopt;
    }

    bool Hotspots::hovered(int32_t id, olc::vf2d point) const {
        std::optional<int32_t> found = at(point);
        return found && *found == id;
    }

    std::optional<int32_t> Hotspots::take_click(Input& input, uint32_t button) const {
        if (!input.clicked(button)) {
            return std::nullopt;
        }

        std::optional<int32_t> found = at(input.mouse_position());

        if (found) {
            input.take_click(button);
        }

        return found;
    }

    float hints_width(olc::Font& font, const std::vector<Hint>& hints, float gap) {
        float width = 0;

        for (size_t index = 0; index < hints.size(); index++) {
            width += (index > 0 ? gap : 0.0f) + (hints[index].text.empty() ? 0.0f : (float)text_bounds(font, hints[index].text).size.x);
        }

        return width;
    }

    void draw_hints(olc::PixelGameEngine* window, olc::Font& font, const std::vector<Hint>& hints, olc::vf2d baseline, float gap, olc::Pixel color, olc::Pixel hover_color, Hotspots& hotspots) {
        olc::FontRect metrics = text_bounds(font, "Hgjy");
        olc::vf2d mouse = window->GetMousePos();
        float x = baseline.x;

        for (const Hint& hint : hints) {
            if (hint.text.empty()) {
                continue;
            }

            float width = (float)text_bounds(font, hint.text).size.x;
            Rect area = Rect{{x, baseline.y + metrics.offset.y}, {width, (float)metrics.size.y}}.inflated(gap / 4.0f);
            bool clickable = hint.id >= 0;

            if (clickable) {
                hotspots.add(area, hint.id);
            }

            draw_text(font, hint.text, olc::vf2d{x, baseline.y}, clickable && area.contains(mouse) ? hover_color : color, 1.0f);
            x += width + gap;
        }
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

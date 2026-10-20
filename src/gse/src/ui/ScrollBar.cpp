#include "gse/ui/ScrollBar.h"

#include "gse/ui/Ui.h"

#include <algorithm>

namespace nascent {
    void ScrollBar::layout(Rect track, float visible_fraction) {
        this->track = track;
        this->visible_fraction = std::clamp(visible_fraction, 0.0f, 1.0f);
    }

    void ScrollBar::hide() {
        track = {};
    }

    bool ScrollBar::is_dragging() const {
        return grab.has_value();
    }

    float ScrollBar::thumb_height() const {
        return std::min(track.size.y, std::max(Ui::layout_pxf("scrollbar.min_thumb"), track.size.y * visible_fraction));
    }

    Rect ScrollBar::thumb(float position) const {
        float height = thumb_height();
        return {{track.position.x, track.position.y + (track.size.y - height) * std::clamp(position, 0.0f, 1.0f)}, {track.size.x, height}};
    }

    Rect ScrollBar::hit_area() const {
        float slop = Ui::layout_pxf("scrollbar.hit_slop");
        return {{track.position.x - slop, track.position.y}, {track.size.x + slop * 2.0f, track.size.y}};
    }

    std::optional<float> ScrollBar::update(Input& input, float position) {
        olc::vf2d mouse = input.mouse_position();

        if (grab && (!input.mouse(0).bHeld || track.size.y <= 0.0f)) {
            grab.reset();
        }

        if (!grab && track.size.y > 0.0f && input.clicked(0) && hit_area().contains(mouse)) {
            input.take_click(0);
            Rect current = thumb(position);
            bool on_thumb = mouse.y >= current.position.y && mouse.y <= current.position.y + current.size.y;
            grab = on_thumb ? mouse.y - current.position.y : current.size.y / 2.0f;
        }

        if (!grab) {
            return std::nullopt;
        }

        float travel = track.size.y - thumb_height();

        if (travel <= 0.0f) {
            return 0.0f;
        }

        return std::clamp((mouse.y - *grab - track.position.y) / travel, 0.0f, 1.0f);
    }

    void ScrollBar::draw(olc::PixelGameEngine* window, float position, olc::Pixel track_color, olc::Pixel thumb_color) const {
        if (track.size.y <= 0.0f) {
            return;
        }

        window->FillRectDecal(track.position, track.size, track_color);
        Rect bar = thumb(position);

        if (grab || hit_area().contains(window->GetMousePos())) {
            float extra = bar.size.x * ((float)Ui::layout("scrollbar.active_width_scale") - 1.0f);
            bar = {{bar.position.x - extra / 2.0f, bar.position.y}, {bar.size.x + extra, bar.size.y}};
        }

        window->FillRectDecal(bar.position, bar.size, thumb_color);
    }
}

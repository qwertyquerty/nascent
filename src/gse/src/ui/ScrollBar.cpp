#include "gse/ui/ScrollBar.h"

#include "gse/ui/Ui.h"

#include <algorithm>

namespace nascent {
    void ScrollBar::layout(Rect track, float visible_fraction, ScrollAxis axis) {
        this->track = track;
        this->visible_fraction = std::clamp(visible_fraction, 0.0f, 1.0f);
        this->axis = axis;
    }

    void ScrollBar::hide() {
        track = {};
    }

    bool ScrollBar::is_dragging() const {
        return grab.has_value();
    }

    float olc::vf2d::* ScrollBar::along() const {
        return axis == ScrollAxis::HORIZONTAL ? &olc::vf2d::x : &olc::vf2d::y;
    }

    float olc::vf2d::* ScrollBar::across() const {
        return axis == ScrollAxis::HORIZONTAL ? &olc::vf2d::y : &olc::vf2d::x;
    }

    float ScrollBar::thumb_length() const {
        float length = track.size.*along();
        return std::min(length, std::max(Ui::layout_pxf("scrollbar.min_thumb"), length * visible_fraction));
    }

    Rect ScrollBar::thumb(float position) const {
        float length = thumb_length();
        Rect bar = track;
        bar.position.*along() += (track.size.*along() - length) * std::clamp(position, 0.0f, 1.0f);
        bar.size.*along() = length;
        return bar;
    }

    Rect ScrollBar::hit_area() const {
        float slop = Ui::layout_pxf("scrollbar.hit_slop");
        Rect area = track;
        area.position.*across() -= slop;
        area.size.*across() += slop * 2.0f;
        return area;
    }

    std::optional<float> ScrollBar::update(Input& input, float position) {
        olc::vf2d mouse = input.mouse_position();
        float length = track.size.*along();

        if (grab && (!input.mouse(0).bHeld || length <= 0.0f)) {
            grab.reset();
        }

        if (!grab && length > 0.0f && input.clicked(0) && hit_area().contains(mouse)) {
            input.take_click(0);
            Rect current = thumb(position);
            float start = current.position.*along();
            bool on_thumb = mouse.*along() >= start && mouse.*along() <= start + current.size.*along();
            grab = on_thumb ? mouse.*along() - start : current.size.*along() / 2.0f;
        }

        if (!grab) {
            return std::nullopt;
        }

        float travel = length - thumb_length();

        if (travel <= 0.0f) {
            return 0.0f;
        }

        return std::clamp((mouse.*along() - *grab - track.position.*along()) / travel, 0.0f, 1.0f);
    }

    void ScrollBar::draw(olc::PixelGameEngine* window, float position, olc::Pixel track_color, olc::Pixel thumb_color) const {
        if (track.size.*along() <= 0.0f) {
            return;
        }

        window->FillRectDecal(track.position, track.size, track_color);
        Rect bar = thumb(position);

        if (grab || hit_area().contains(window->GetMousePos())) {
            float extra = bar.size.*across() * ((float)Ui::layout("scrollbar.active_width_scale") - 1.0f);
            bar.position.*across() -= extra / 2.0f;
            bar.size.*across() += extra;
        }

        window->FillRectDecal(bar.position, bar.size, thumb_color);
    }
}

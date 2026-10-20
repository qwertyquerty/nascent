#pragma once

#include "gse/input/Input.h"
#include "gse/ui/Layout.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <optional>

namespace nascent {
    enum class ScrollAxis : uint8_t {
        VERTICAL,
        HORIZONTAL
    };

    class ScrollBar {
        protected:
        Rect track;
        float visible_fraction = 1.0f;
        ScrollAxis axis = ScrollAxis::VERTICAL;
        std::optional<float> grab;

        float olc::vf2d::* along() const;
        float olc::vf2d::* across() const;
        float thumb_length() const;
        Rect thumb(float position) const;
        Rect hit_area() const;

        public:
        void layout(Rect track, float visible_fraction, ScrollAxis axis = ScrollAxis::VERTICAL);
        void hide();
        bool is_dragging() const;
        std::optional<float> update(Input& input, float position);
        void draw(olc::PixelGameEngine* window, float position, olc::Pixel track_color, olc::Pixel thumb_color) const;
    };
}

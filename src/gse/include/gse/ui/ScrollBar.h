#pragma once

#include "gse/input/Input.h"
#include "gse/ui/Layout.h"
#include "olcPixelGameEngine.h"

#include <optional>

namespace nascent {
    class ScrollBar {
        protected:
        Rect track;
        float visible_fraction = 1.0f;
        std::optional<float> grab;

        float thumb_height() const;
        Rect thumb(float position) const;
        Rect hit_area() const;

        public:
        void layout(Rect track, float visible_fraction);
        void hide();
        bool is_dragging() const;
        std::optional<float> update(Input& input, float position);
        void draw(olc::PixelGameEngine* window, float position, olc::Pixel track_color, olc::Pixel thumb_color) const;
    };
}

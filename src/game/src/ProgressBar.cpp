#include "ProgressBar.h"

#include <algorithm>

namespace nascent {
    ProgressBar::ProgressBar(olc::vf2d size, olc::Pixel track_color) : size(size), track_color(track_color) {}

    void ProgressBar::draw(olc::PixelGameEngine* window, olc::vf2d position, float progress, olc::Pixel fill_color) const {
        window->FillRectDecal(position, size, track_color);

        float fill = std::clamp(progress, 0.0f, 1.0f);

        if (fill > 0.0f) {
            window->FillRectDecal(position, {size.x * fill, size.y}, fill_color);
        }
    }
}

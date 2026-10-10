#pragma once

#include "olcPixelGameEngine.h"

namespace nascent {
    class ProgressBar {
        public:
        olc::vf2d size;
        olc::Pixel track_color = olc::Pixel(255, 255, 255, 32);

        ProgressBar(olc::vf2d size);

        void draw(olc::PixelGameEngine* window, olc::vf2d position, float progress, olc::Pixel fill_color) const;
    };
}

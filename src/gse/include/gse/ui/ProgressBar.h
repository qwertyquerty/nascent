#pragma once

#include "olcPixelGameEngine.h"

namespace nascent {
    class ProgressBar {
        public:
        olc::vf2d size;
        olc::Pixel track_color;

        ProgressBar(olc::vf2d size, olc::Pixel track_color);

        void draw(olc::PixelGameEngine* window, olc::vf2d position, float progress, olc::Pixel fill_color) const;
    };
}

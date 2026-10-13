#pragma once

#include "olcPixelGameEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nascent {
    class Ui {
        static inline double current_scale = 1.0;
        static inline olc::vi2d current_screen = {1920, 1080};

        public:
        static constexpr double REFERENCE_WIDTH = 1920.0;
        static constexpr double REFERENCE_HEIGHT = 1080.0;
        static constexpr double MINIMUM_SCALE = 0.25;

        static bool set_screen(olc::vi2d screen) {
            double scale = std::max(MINIMUM_SCALE, std::min(screen.x / REFERENCE_WIDTH, screen.y / REFERENCE_HEIGHT));
            bool changed = screen != current_screen || scale != current_scale;
            current_screen = screen;
            current_scale = scale;
            return changed;
        }

        static double scale() {
            return current_scale;
        }

        static olc::vi2d screen() {
            return current_screen;
        }

        static int32_t px(double reference) {
            return (int32_t)std::lround(reference * current_scale);
        }

        static float pxf(double reference) {
            return (float)(reference * current_scale);
        }

        static olc::vi2d px(olc::vi2d reference) {
            return {px((double)reference.x), px((double)reference.y)};
        }

        static olc::vf2d pxf(olc::vf2d reference) {
            return {pxf(reference.x), pxf(reference.y)};
        }
    };
}

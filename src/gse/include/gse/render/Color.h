#pragma once

#include "olcPixelGameEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nascent {
    inline olc::Pixel fade(olc::Pixel color, double opacity) {
        color.a = (uint8_t)std::lround(color.a * std::clamp(opacity, 0.0, 1.0));
        return color;
    }

    inline olc::Pixel mix(olc::Pixel from, olc::Pixel to, double t) {
        t = std::clamp(t, 0.0, 1.0);
        auto channel = [t](uint8_t a, uint8_t b) { return (uint8_t)std::lround(a + (b - a) * t); };
        return olc::Pixel(channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b), channel(from.a, to.a));
    }

    inline olc::Pixel blend(olc::Pixel below, olc::Pixel above) {
        float a = above.a / 255.0f;
        return olc::Pixel(
            (uint8_t)(below.r * (1 - a) + above.r * a),
            (uint8_t)(below.g * (1 - a) + above.g * a),
            (uint8_t)(below.b * (1 - a) + above.b * a),
            std::max(below.a, above.a)
        );
    }
}

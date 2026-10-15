#pragma once

#include <algorithm>
#include <cmath>

namespace nascent {
    inline float progress(float elapsed, float start, float duration) {
        return duration > 0 ? std::clamp((elapsed - start) / duration, 0.0f, 1.0f) : (elapsed >= start ? 1.0f : 0.0f);
    }

    inline double smooth(double t) {
        t = std::clamp(t, 0.0, 1.0);
        return t * t * (3 - 2 * t);
    }

    inline float smoothstep(double begin, double end, double t) {
        return (float)std::lerp(begin, end, smooth(t));
    }

    inline float ease_out(float t) {
        float inverse = 1.0f - std::clamp(t, 0.0f, 1.0f);
        return 1.0f - inverse * inverse * inverse;
    }

    inline float ease_in(float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * t;
    }
}

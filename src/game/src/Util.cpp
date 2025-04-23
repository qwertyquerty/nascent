#include "Util.h"
#include <cmath>
#include <algorithm>

namespace nascent {
    float smoothstep(double begin, double end, double t) {
        t = std::clamp(t, 0.0, 1.0);
        return std::lerp(begin, end, t * t * (3 - 2 * t));
    }
}

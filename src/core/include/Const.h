#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace nascent {
    inline constexpr std::string_view CHART_FILE_EXTENSION = ".json";

    inline constexpr int32_t PITCH_STEPS_PER_UNIT = 20;
    inline constexpr float PITCH_INTERVAL = 1.0f / PITCH_STEPS_PER_UNIT;
    inline constexpr float PITCH_MINIMUM = 0.75f;
    inline constexpr float PITCH_MAXIMUM = 1.5f;

    inline float snap_pitch(float pitch) {
        float clamped = std::clamp(pitch, PITCH_MINIMUM, PITCH_MAXIMUM);
        return (float)std::lround(clamped * PITCH_STEPS_PER_UNIT) / (float)PITCH_STEPS_PER_UNIT;
    }

    inline constexpr uint8_t MAX_LANES = 32;
}

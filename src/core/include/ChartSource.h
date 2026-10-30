#pragma once

#include <cstdint>
#include <filesystem>

namespace nascent {
    enum class ChartSource : uint8_t {
        NASCENT,
        OSU,
        STEPMANIA,
    };

    inline constexpr double OSU_SOURCE_OFFSET_MS = -24.0;

    ChartSource chart_source(const std::filesystem::path& chart_path);
    double source_offset_ms(ChartSource source, const std::filesystem::path& audio_path);
}

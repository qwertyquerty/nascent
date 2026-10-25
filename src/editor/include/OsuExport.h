#pragma once

#include <filesystem>
#include <string>

namespace nascent {
    struct ChartData;

    struct OsuDifficulty {
        float overall_difficulty = 8.0f;
        float hp_drain = 8.0f;
    };

    std::string chart_to_osu(const ChartData& data, const OsuDifficulty& difficulty);
    bool export_osu(const ChartData& data, const OsuDifficulty& difficulty, const std::filesystem::path& path, std::string& error);
}

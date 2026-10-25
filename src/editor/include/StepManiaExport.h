#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace nascent {
    struct ChartData;

    std::optional<std::string> chart_to_sm(const ChartData& data, int32_t meter, std::string& error);
    std::optional<std::string> chart_to_ssc(const ChartData& data, int32_t meter, std::string& error);
    std::optional<std::string> chart_to_dwi(const ChartData& data, int32_t meter, std::string& error);
    bool export_stepmania(const ChartData& data, const std::filesystem::path& path, int32_t meter, std::string& error);
}

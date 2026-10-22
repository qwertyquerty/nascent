#pragma once

#include "json.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace nascent {
    inline constexpr std::string_view SM_FILE_EXTENSION = ".sm";
    inline constexpr std::string_view SSC_FILE_EXTENSION = ".ssc";
    inline constexpr std::string_view DWI_FILE_EXTENSION = ".dwi";

    struct StepManiaChart {
        size_t index = 0;
        std::string steps_type;
        std::string difficulty;
        std::string description;
        int32_t meter = 0;
        std::string chart_key;
        bool modified = false;
        nlohmann::json chart;
        std::string error;
    };

    std::vector<StepManiaChart> stepmania_to_chart_jsons(std::string_view text, std::string& error);
}

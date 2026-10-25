#pragma once

#include "json.h"

#include <optional>
#include <string>
#include <string_view>

namespace nascent {
    inline constexpr std::string_view OSU_FILE_EXTENSION = ".osu";

    std::optional<nlohmann::json> osu_to_chart_json(std::string_view text, std::string& error);
    std::optional<nlohmann::json> osu_to_chart_json(std::string_view text, std::string& error, bool& modified, bool allow_empty = false);
}

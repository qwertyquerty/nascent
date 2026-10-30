#pragma once

#include "olcPixelGameEngine.h"

#include <optional>
#include <string_view>

namespace nascent {
    std::optional<olc::Key> key_from_name(std::string_view name);
    std::string_view key_name(olc::Key key);
}

#pragma once

#include <filesystem>
#include <optional>

namespace nascent {
    void run_game_window(std::optional<std::filesystem::path> test_play = std::nullopt);
}

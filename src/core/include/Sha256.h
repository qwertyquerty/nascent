#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace nascent {
    std::string sha256_hex(std::string_view data);
    std::optional<std::string> sha256_file_hex(const std::filesystem::path& path);
}

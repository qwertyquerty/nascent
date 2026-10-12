#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace nascent {
    inline std::filesystem::path utf8_path(std::string_view text) {
        return std::filesystem::path(std::u8string(text.begin(), text.end()));
    }

    inline std::string path_utf8(const std::filesystem::path& path) {
        std::u8string text = path.u8string();
        return std::string(text.begin(), text.end());
    }
}

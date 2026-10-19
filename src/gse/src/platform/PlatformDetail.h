#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace nascent::platform_detail {
    std::string os_name();
    std::filesystem::path config_home();
    std::string config_folder_name(const std::string& app_name);
    std::filesystem::path bundled_assets_directory(const std::filesystem::path& executable_directory);
    std::vector<const char*> clipboard_commands();
    const char* url_opener();
    void install_crash_signals();
}

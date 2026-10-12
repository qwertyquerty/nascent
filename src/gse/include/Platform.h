#pragma once

#include <filesystem>
#include <string>

namespace nascent {
    std::filesystem::path executable_directory();
    void use_executable_directory_for_assets();
    std::filesystem::path user_config_directory(const std::string& app_name);
    void precise_sleep(double seconds);
    bool copy_to_clipboard(const std::string& text);
    std::string platform_description();
}

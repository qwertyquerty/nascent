#pragma once

#include <filesystem>
#include <string>

namespace nascent {
    std::filesystem::path executable_directory();
    void use_executable_directory_for_assets();
    void attach_parent_console();
    std::filesystem::path user_config_directory(const std::string& app_name);
    void precise_sleep(double seconds);
    bool copy_to_clipboard(const std::string& text);
    bool open_url(const std::string& url);
    std::string platform_description();
}

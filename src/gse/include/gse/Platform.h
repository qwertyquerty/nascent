#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace nascent {
    std::filesystem::path executable_path();
    std::filesystem::path executable_directory();
    bool spawn_detached(const std::filesystem::path& program, const std::vector<std::string>& arguments);
    void use_executable_directory_for_assets();
    void attach_parent_console();
    std::filesystem::path user_config_directory(const std::string& app_name);
    void precise_sleep(double seconds);
    bool copy_to_clipboard(const std::string& text);
    bool open_url(const std::string& url);
    bool open_folder(const std::filesystem::path& folder);
    bool open_file(const std::filesystem::path& file);
    std::string platform_description();
    void set_cursor_hidden(bool hidden);
}

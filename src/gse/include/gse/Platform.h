#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace nascent {
    std::filesystem::path executable_path();
    std::filesystem::path executable_directory();
    bool spawn_detached(const std::filesystem::path& program, const std::vector<std::string>& arguments);
    void use_executable_directory_for_assets();
    bool attach_parent_console();
    void redirect_output_to_log(const std::filesystem::path& file);
    void install_crash_handler();
    void install_thread_crash_handler();
    std::filesystem::path user_config_directory(const std::string& app_name);
    bool move_into_place(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec);
    void precise_sleep(double seconds);
    bool copy_to_clipboard(const std::string& text);
    bool open_url(const std::string& url);
    bool open_folder(const std::filesystem::path& folder);
    bool open_file(const std::filesystem::path& file);
    std::string platform_description();
    void set_cursor_hidden(bool hidden);
    bool fit_window_to_screen(int32_t& width, int32_t& height);
    void allow_app_switching_in_fullscreen();
    std::optional<bool> application_focused();
}

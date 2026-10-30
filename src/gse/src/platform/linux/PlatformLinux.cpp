#include "gse/Platform.h"
#include "platform/PlatformDetail.h"

#include <sys/wait.h>

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>
#include <vector>

namespace nascent {
    std::filesystem::path executable_path() {
        std::error_code ec;
        std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", ec);
        return ec ? std::filesystem::path() : path;
    }

    bool fit_window_to_screen(int32_t&, int32_t&) {
        return false;
    }

    void allow_app_switching_in_fullscreen() {
    }

    void allow_window_maximize() {
    }

    static constexpr int COMMAND_NOT_FOUND = 127;

    static std::string shell_quote(const std::string& text) {
        std::string quoted = "'";

        for (char c : text) {
            quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
        }

        return quoted + "'";
    }

    std::optional<std::filesystem::path> choose_folder(const std::string& title) {
        std::string quoted = shell_quote(title);
        std::array<std::string, 2> commands = {
            "zenity --file-selection --directory --title=" + quoted + " 2>/dev/null",
            "kdialog --title " + quoted + " --getexistingdirectory \"$HOME\" 2>/dev/null",
        };

        for (const std::string& command : commands) {
            FILE* pipe = popen(command.c_str(), "r");

            if (pipe == nullptr) {
                continue;
            }

            std::string output;
            std::array<char, 512> buffer = {};

            while (size_t count = fread(buffer.data(), 1, buffer.size(), pipe)) {
                output.append(buffer.data(), count);
            }

            int status = pclose(pipe);

            if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) == COMMAND_NOT_FOUND) {
                continue;
            }

            while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) {
                output.pop_back();
            }

            return WEXITSTATUS(status) == 0 ? std::filesystem::path(output) : std::filesystem::path();
        }

        return std::nullopt;
    }

    std::optional<bool> application_focused() {
        return std::nullopt;
    }
}

namespace nascent::platform_detail {
    std::string os_name() {
        return "Linux";
    }

    std::vector<std::filesystem::path> font_fallback_candidates() {
        return {
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/OTF/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        };
    }

    std::filesystem::path config_home() {
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
            return xdg;
        }

        const char* home = std::getenv("HOME");
        return home != nullptr ? std::filesystem::path(home) / ".config" : std::filesystem::path();
    }

    std::string config_folder_name(const std::string& app_name) {
        std::string lower_name = app_name;

        for (char& c : lower_name) {
            c = (char)std::tolower((unsigned char)c);
        }

        return lower_name;
    }

    std::filesystem::path bundled_assets_directory(const std::filesystem::path& executable_directory) {
        return executable_directory;
    }

    std::vector<const char*> clipboard_commands() {
        std::vector<const char*> commands;

        if (std::getenv("WAYLAND_DISPLAY") != nullptr) {
            commands.push_back("command -v wl-copy >/dev/null 2>&1 && wl-copy");
        }

        commands.push_back("command -v xclip >/dev/null 2>&1 && xclip -selection clipboard");
        commands.push_back("command -v xsel >/dev/null 2>&1 && xsel --clipboard --input");
        return commands;
    }

    std::vector<const char*> clipboard_read_commands() {
        std::vector<const char*> commands;

        if (std::getenv("WAYLAND_DISPLAY") != nullptr) {
            commands.push_back("command -v wl-paste >/dev/null 2>&1 && wl-paste --no-newline");
        }

        commands.push_back("command -v xclip >/dev/null 2>&1 && xclip -selection clipboard -o");
        commands.push_back("command -v xsel >/dev/null 2>&1 && xsel --clipboard --output");
        return commands;
    }

    const char* url_opener() {
        return "xdg-open";
    }
}

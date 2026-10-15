#include "gse/Platform.h"
#include "platform/PlatformDetail.h"

#include <cctype>
#include <cstdlib>
#include <system_error>
#include <vector>

namespace nascent {
    std::filesystem::path executable_path() {
        std::error_code ec;
        std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", ec);
        return ec ? std::filesystem::path() : path;
    }
}

namespace nascent::platform_detail {
    std::string os_name() {
        return "Linux";
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

    const char* url_opener() {
        return "xdg-open";
    }
}

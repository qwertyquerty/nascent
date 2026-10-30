#include "gse/Platform.h"
#include "platform/PlatformDetail.h"

#include <cstdint>
#include <cstdlib>
#include <system_error>
#include <vector>

#include <mach-o/dyld.h>

namespace nascent {
    std::filesystem::path executable_path() {
        std::error_code ec;
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::vector<char> buffer(size + 1, '\0');

        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            return {};
        }

        std::filesystem::path path = std::filesystem::canonical(buffer.data(), ec);
        return ec ? std::filesystem::path() : path;
    }
}

namespace nascent::platform_detail {
    std::string os_name() {
        return "macOS";
    }

    std::vector<std::filesystem::path> font_fallback_candidates() {
        std::filesystem::path fonts = "/System/Library/Fonts";
        return {fonts / "ヒラギノ角ゴシック W3.ttc", fonts / "AppleSDGothicNeo.ttc", fonts / "PingFang.ttc", fonts / "Hiragino Sans GB.ttc"};
    }

    std::filesystem::path config_home() {
        const char* home = std::getenv("HOME");
        return home != nullptr ? std::filesystem::path(home) / "Library" / "Application Support" : std::filesystem::path();
    }

    std::string config_folder_name(const std::string& app_name) {
        return app_name;
    }

    std::filesystem::path bundled_assets_directory(const std::filesystem::path& executable_directory) {
        std::error_code ec;
        std::filesystem::path resources = executable_directory.parent_path() / "Resources";

        if (!std::filesystem::is_directory(executable_directory / "assets", ec) && std::filesystem::is_directory(resources / "assets", ec)) {
            return resources;
        }

        return executable_directory;
    }

    std::vector<const char*> clipboard_commands() {
        return {"pbcopy"};
    }

    std::vector<const char*> clipboard_read_commands() {
        return {"pbpaste"};
    }

    const char* url_opener() {
        return "open";
    }
}

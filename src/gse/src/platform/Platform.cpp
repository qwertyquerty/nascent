#include "gse/Platform.h"
#include "platform/PlatformDetail.h"

#include <format>
#include <system_error>

namespace nascent {
    std::filesystem::path executable_directory() {
        return executable_path().parent_path();
    }

    void use_executable_directory_for_assets() {
        std::error_code ec;
        std::filesystem::path directory = executable_directory();

        if (directory.empty()) {
            return;
        }

        directory = platform_detail::bundled_assets_directory(directory);

        if (std::filesystem::is_directory(directory / "assets", ec)) {
            std::filesystem::current_path(directory, ec);
        }
    }

    std::filesystem::path user_config_directory(const std::string& app_name) {
        std::filesystem::path base = platform_detail::config_home();

        if (base.empty()) {
            base = executable_directory();
        }

        return base / platform_detail::config_folder_name(app_name);
    }

    std::string platform_description() {
#if defined(__x86_64__) || defined(_M_X64)
        const char* arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
        const char* arch = "arm64";
#else
        const char* arch = "unknown arch";
#endif

#if defined(__clang__)
        std::string compiler = std::format("Clang {}.{}", __clang_major__, __clang_minor__);
#elif defined(_MSC_VER)
        std::string compiler = std::format("MSVC {}", _MSC_VER);
#elif defined(__GNUC__)
        std::string compiler = std::format("GCC {}.{}", __GNUC__, __GNUC_MINOR__);
#else
        std::string compiler = "unknown compiler";
#endif

        return std::format("{} {} {}", platform_detail::os_name(), arch, compiler);
    }
}

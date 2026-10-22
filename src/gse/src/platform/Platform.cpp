#include "gse/Platform.h"
#include "platform/PlatformDetail.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <string>
#include <system_error>
#include <thread>

namespace nascent {
    static void log_terminate() {
        std::string reason = "terminate was called";

        if (std::exception_ptr error = std::current_exception()) {
            try {
                std::rethrow_exception(error);
            }
            catch (const std::exception& exception) {
                reason = std::string("unhandled exception: ") + exception.what();
            }
            catch (...) {
                reason = "unhandled exception of an unknown type";
            }
        }

        std::fprintf(stderr, "Crash: %s\n", reason.c_str());
        std::fflush(stderr);
        std::abort();
    }

    void install_thread_crash_handler() {
        std::set_terminate(log_terminate);
    }

    void install_crash_handler() {
        install_thread_crash_handler();
        platform_detail::install_crash_signals();
    }

    static constexpr int32_t MOVE_ATTEMPTS = 8;
    static constexpr int32_t MOVE_RETRY_MS = 50;

    bool move_into_place(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec) {
        for (int32_t attempt = 1; attempt <= MOVE_ATTEMPTS; attempt++) {
            std::filesystem::rename(from, to, ec);

            if (!ec) {
                return true;
            }

            if (attempt < MOVE_ATTEMPTS) {
                std::this_thread::sleep_for(std::chrono::milliseconds(MOVE_RETRY_MS * attempt));
            }
        }

        std::error_code copy_ec;
        std::filesystem::copy(from, to, std::filesystem::copy_options::recursive, copy_ec);

        if (copy_ec) {
            std::error_code cleanup_ec;
            std::filesystem::remove_all(to, cleanup_ec);
            return false;
        }

        std::error_code cleanup_ec;
        std::filesystem::remove_all(from, cleanup_ec);
        ec.clear();
        return true;
    }

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

    std::vector<std::filesystem::path> system_fallback_fonts() {
        std::vector<std::filesystem::path> fonts;

        for (const std::filesystem::path& candidate : platform_detail::font_fallback_candidates()) {
            std::error_code ec;

            if (std::filesystem::is_regular_file(candidate, ec)) {
                fonts.push_back(candidate);
            }
        }

        return fonts;
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

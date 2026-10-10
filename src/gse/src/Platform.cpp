#include "Platform.h"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace nascent {
    std::filesystem::path executable_directory() {
        std::error_code ec;

#if defined(_WIN32)
        std::vector<wchar_t> buffer(MAX_PATH);

        while (true) {
            DWORD length = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());

            if (length == 0) {
                return {};
            }

            if (length < buffer.size()) {
                return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
            }

            buffer.resize(buffer.size() * 2);
        }
#elif defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        std::vector<char> buffer(size + 1, '\0');

        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            return {};
        }

        std::filesystem::path path = std::filesystem::canonical(buffer.data(), ec);
        return ec ? std::filesystem::path() : path.parent_path();
#else
        std::filesystem::path path = std::filesystem::read_symlink("/proc/self/exe", ec);
        return ec ? std::filesystem::path() : path.parent_path();
#endif
    }

    void use_executable_directory_for_assets() {
        std::error_code ec;
        std::filesystem::path directory = executable_directory();

        if (!directory.empty() && std::filesystem::is_directory(directory / "assets", ec)) {
            std::filesystem::current_path(directory, ec);
        }
    }

    std::filesystem::path user_config_directory(const std::string& app_name) {
        std::filesystem::path base;

#if defined(_WIN32)
        PWSTR roaming = nullptr;

        if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &roaming) == S_OK) {
            base = std::filesystem::path(roaming);
        }

        CoTaskMemFree(roaming);
#elif defined(__APPLE__)
        if (const char* home = std::getenv("HOME")) {
            base = std::filesystem::path(home) / "Library" / "Application Support";
        }
#else
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
            base = xdg;
        }
        else if (const char* home = std::getenv("HOME")) {
            base = std::filesystem::path(home) / ".config";
        }
#endif

        if (base.empty()) {
            base = executable_directory();
        }

#if defined(_WIN32) || defined(__APPLE__)
        return base / app_name;
#else
        std::string lower_name = app_name;

        for (char& c : lower_name) {
            c = (char)std::tolower((unsigned char)c);
        }

        return base / lower_name;
#endif
    }
}

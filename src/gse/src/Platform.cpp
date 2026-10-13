#include "Platform.h"

#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#if !defined(_WIN32)
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;
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

    void precise_sleep(double seconds) {
        if (seconds <= 0) {
            return;
        }

#if defined(_WIN32)
        static thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

        if (timer != nullptr) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)(seconds * 10000000.0);

            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, INFINITE);
                return;
            }
        }

        Sleep((DWORD)(seconds * 1000.0));
#else
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
#endif
    }

    bool copy_to_clipboard(const std::string& text) {
#if defined(_WIN32)
        int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, ((size_t)length + 1) * sizeof(wchar_t));

        if (memory == nullptr) {
            return false;
        }

        wchar_t* wide = (wchar_t*)GlobalLock(memory);
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wide, length);
        wide[length] = L'\0';
        GlobalUnlock(memory);

        if (!OpenClipboard(nullptr)) {
            GlobalFree(memory);
            return false;
        }

        EmptyClipboard();
        bool copied = SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
        CloseClipboard();

        if (!copied) {
            GlobalFree(memory);
        }

        return copied;
#else
#if defined(__APPLE__)
        std::vector<const char*> commands = {"pbcopy"};
#else
        std::vector<const char*> commands;

        if (std::getenv("WAYLAND_DISPLAY") != nullptr) {
            commands.push_back("command -v wl-copy >/dev/null 2>&1 && wl-copy");
        }

        commands.push_back("command -v xclip >/dev/null 2>&1 && xclip -selection clipboard");
        commands.push_back("command -v xsel >/dev/null 2>&1 && xsel --clipboard --input");
#endif
        auto previous_handler = std::signal(SIGPIPE, SIG_IGN);
        bool copied = false;

        for (const char* command : commands) {
            FILE* pipe = popen(command, "w");

            if (pipe == nullptr) {
                continue;
            }

            bool written = std::fwrite(text.data(), 1, text.size(), pipe) == text.size();

            if (pclose(pipe) == 0 && written) {
                copied = true;
                break;
            }
        }

        std::signal(SIGPIPE, previous_handler);
        return copied;
#endif
    }

    bool open_url(const std::string& url) {
        if (!url.starts_with("https://")) {
            return false;
        }

#if defined(_WIN32)
        int length = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), (int)url.size(), nullptr, 0);
        std::wstring wide(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), (int)url.size(), wide.data(), length);
        return (INT_PTR)ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
#else
#if defined(__APPLE__)
        std::string program = "open";
#else
        std::string program = "xdg-open";
#endif
        std::string argument = url;
        char* arguments[] = {program.data(), argument.data(), nullptr};
        pid_t pid = 0;

        if (posix_spawnp(&pid, program.c_str(), nullptr, nullptr, arguments, environ) != 0) {
            return false;
        }

        std::thread([pid] {
            waitpid(pid, nullptr, 0);
        }).detach();
        return true;
#endif
    }

    std::string platform_description() {
#if defined(_WIN32)
        const char* os = "Windows";
#elif defined(__APPLE__)
        const char* os = "macOS";
#elif defined(__linux__)
        const char* os = "Linux";
#else
        const char* os = "unknown OS";
#endif

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

        return std::format("{} {} {}", os, arch, compiler);
    }
}

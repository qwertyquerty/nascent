#include "gse/Platform.h"
#include "platform/PlatformDetail.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

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

namespace nascent {
    static std::wstring widen(const std::string& text) {
        int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
        std::wstring wide(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wide.data(), length);
        return wide;
    }

    std::filesystem::path executable_path() {
        std::vector<wchar_t> buffer(MAX_PATH);

        while (true) {
            DWORD length = GetModuleFileNameW(nullptr, buffer.data(), (DWORD)buffer.size());

            if (length == 0) {
                return {};
            }

            if (length < buffer.size()) {
                return std::filesystem::path(std::wstring(buffer.data(), length));
            }

            buffer.resize(buffer.size() * 2);
        }
    }

    bool spawn_detached(const std::filesystem::path& program, const std::vector<std::string>& arguments) {
        std::wstring command = L"\"" + program.wstring() + L"\"";

        for (const std::string& argument : arguments) {
            command += L" \"" + widen(argument) + L"\"";
        }

        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process = {};
        std::wstring directory = program.parent_path().wstring();

        if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, directory.empty() ? nullptr : directory.c_str(), &startup, &process)) {
            return false;
        }

        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
    }

    void attach_parent_console() {
        bool missing_out = _fileno(stdout) < 0;
        bool missing_err = _fileno(stderr) < 0;

        if (!(missing_out || missing_err) || !AttachConsole(ATTACH_PARENT_PROCESS)) {
            return;
        }

        if (missing_out) {
            std::freopen("CONOUT$", "w", stdout);
        }

        if (missing_err) {
            std::freopen("CONOUT$", "w", stderr);
        }
    }

    void precise_sleep(double seconds) {
        if (seconds <= 0) {
            return;
        }

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
    }

    bool copy_to_clipboard(const std::string& text) {
        std::wstring wide = widen(text);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (wide.size() + 1) * sizeof(wchar_t));

        if (memory == nullptr) {
            return false;
        }

        wchar_t* target = (wchar_t*)GlobalLock(memory);
        std::copy(wide.begin(), wide.end(), target);
        target[wide.size()] = L'\0';
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
    }

    bool open_url(const std::string& url) {
        if (!url.starts_with("https://")) {
            return false;
        }

        return (INT_PTR)ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
    }
}

namespace nascent::platform_detail {
    std::string os_name() {
        return "Windows";
    }

    std::filesystem::path config_home() {
        std::filesystem::path base;
        PWSTR roaming = nullptr;

        if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, nullptr, &roaming) == S_OK) {
            base = std::filesystem::path(roaming);
        }

        CoTaskMemFree(roaming);
        return base;
    }

    std::string config_folder_name(const std::string& app_name) {
        return app_name;
    }

    std::filesystem::path bundled_assets_directory(const std::filesystem::path& executable_directory) {
        return executable_directory;
    }

    std::vector<const char*> clipboard_commands() {
        return {};
    }

    const char* url_opener() {
        return nullptr;
    }
}

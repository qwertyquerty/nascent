#include "gse/Platform.h"
#include "platform/PlatformDetail.h"

#include <algorithm>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
    bool alt_held() {
        return (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    }

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

    bool attach_parent_console() {
        bool missing_out = _fileno(stdout) < 0;
        bool missing_err = _fileno(stderr) < 0;

        if (!(missing_out || missing_err)) {
            return true;
        }

        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            return false;
        }

        if (missing_out) {
            std::freopen("CONOUT$", "w", stdout);
        }

        if (missing_err) {
            std::freopen("CONOUT$", "w", stderr);
        }

        return true;
    }

    void redirect_output_to_log(const std::filesystem::path& file) {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        std::filesystem::path previous = file;
        previous.replace_extension(".previous.log");
        std::filesystem::rename(file, previous, ec);

        if (_wfreopen(file.wstring().c_str(), L"w", stderr) == nullptr) {
            return;
        }

        std::setvbuf(stderr, nullptr, _IONBF, 0);

        if (_wfreopen(file.wstring().c_str(), L"a", stdout) != nullptr) {
            std::setvbuf(stdout, nullptr, _IONBF, 0);
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

    bool open_folder(const std::filesystem::path& folder) {
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        return (INT_PTR)ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
    }

    bool open_file(const std::filesystem::path& file) {
        std::error_code ec;
        return std::filesystem::is_regular_file(file, ec) && (INT_PTR)ShellExecuteW(nullptr, L"open", file.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32;
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

    static constexpr ULONG CRASH_STACK_FRAMES = 62;

    static void write_frame(void* address) {
        HMODULE module = nullptr;
        char name[MAX_PATH] = "";

        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)address, &module) || GetModuleFileNameA(module, name, MAX_PATH) == 0) {
            std::fprintf(stderr, "  %p\n", address);
            return;
        }

        const char* base = std::strrchr(name, '\\');
        std::fprintf(stderr, "  %s+0x%llx\n", base != nullptr ? base + 1 : name, (unsigned long long)((uintptr_t)address - (uintptr_t)module));
    }

    static void write_stack(ULONG skip) {
        void* frames[CRASH_STACK_FRAMES] = {};
        USHORT count = RtlCaptureStackBackTrace(skip, CRASH_STACK_FRAMES, frames, nullptr);
        std::fprintf(stderr, "Stack:\n");

        for (USHORT index = 0; index < count; index++) {
            write_frame(frames[index]);
        }

        std::fflush(stderr);
    }

    static void write_crash_stack(CONTEXT context) {
#if defined(_M_X64) || defined(_M_ARM64)
        std::fprintf(stderr, "Stack:\n");

        for (ULONG frame = 0; frame < CRASH_STACK_FRAMES; frame++) {
#if defined(_M_X64)
            DWORD64 pc = context.Rip;
#else
            DWORD64 pc = context.Pc;
#endif
            if (pc == 0) {
                break;
            }

            write_frame((void*)pc);
            DWORD64 image_base = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(pc, &image_base, nullptr);

            if (function == nullptr) {
#if defined(_M_X64)
                context.Rip = *(DWORD64*)context.Rsp;
                context.Rsp += sizeof(DWORD64);
#else
                context.Pc = context.Lr;
#endif
                continue;
            }

            void* handler_data = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, pc, function, &context, &handler_data, &establisher, nullptr);
        }

        std::fflush(stderr);
#else
        (void)context;
        write_stack(0);
#endif
    }

    static const char* exception_name(DWORD code) {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:
                return "access violation";
            case EXCEPTION_STACK_OVERFLOW:
                return "stack overflow";
            case EXCEPTION_INT_DIVIDE_BY_ZERO:
                return "integer divide by zero";
            case EXCEPTION_ILLEGAL_INSTRUCTION:
                return "illegal instruction";
            case 0xE06D7363:
                return "unhandled C++ exception";
            default:
                return "exception";
        }
    }

    static LONG WINAPI crash_filter(EXCEPTION_POINTERS* info) {
        const EXCEPTION_RECORD* record = info->ExceptionRecord;
        std::fprintf(stderr, "Crash: %s (0x%08lX)", exception_name(record->ExceptionCode), record->ExceptionCode);

        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            std::fprintf(stderr, " %s address %p", record->ExceptionInformation[0] == 1 ? "writing" : "reading", (void*)record->ExceptionInformation[1]);
        }

        std::fprintf(stderr, " at\n");
        write_frame(record->ExceptionAddress);
        write_crash_stack(*info->ContextRecord);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    static void crash_abort(int) {
        std::fprintf(stderr, "Crash: aborted\n");
        write_stack(1);
    }

    void install_crash_signals() {
        SetUnhandledExceptionFilter(crash_filter);
        std::signal(SIGABRT, crash_abort);
    }
}

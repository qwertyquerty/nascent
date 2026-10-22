#include "gse/input/WindowHook.h"
#include "gse/Platform.h"
#include "gse/input/KeySource.h"

#include <algorithm>
#include <atomic>
#include <future>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <cwchar>

namespace nascent {
    static constexpr olc::Key offset_key(olc::Key first, WPARAM offset) {
        return (olc::Key)((size_t)first + offset);
    }

    static constexpr olc::Key key_from_virtual_key(WPARAM vk) {
        if (vk >= 'A' && vk <= 'Z') {
            return offset_key(olc::Key::A, vk - 'A');
        }

        if (vk >= '0' && vk <= '9') {
            return offset_key(olc::Key::K0, vk - '0');
        }

        if (vk >= VK_F1 && vk <= VK_F12) {
            return offset_key(olc::Key::F1, vk - VK_F1);
        }

        if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
            return offset_key(olc::Key::NP0, vk - VK_NUMPAD0);
        }

        switch (vk) {
            case VK_UP:
                return olc::Key::UP;
            case VK_DOWN:
                return olc::Key::DOWN;
            case VK_LEFT:
                return olc::Key::LEFT;
            case VK_RIGHT:
                return olc::Key::RIGHT;
            case VK_BACK:
                return olc::Key::BACK;
            case VK_ESCAPE:
                return olc::Key::ESCAPE;
            case VK_RETURN:
                return olc::Key::ENTER;
            case VK_PAUSE:
                return olc::Key::PAUSE;
            case VK_SCROLL:
                return olc::Key::SCROLL;
            case VK_TAB:
                return olc::Key::TAB;
            case VK_DELETE:
                return olc::Key::DEL;
            case VK_HOME:
                return olc::Key::HOME;
            case VK_END:
                return olc::Key::END;
            case VK_PRIOR:
                return olc::Key::PGUP;
            case VK_NEXT:
                return olc::Key::PGDN;
            case VK_INSERT:
                return olc::Key::INS;
            case VK_SHIFT:
                return olc::Key::SHIFT;
            case VK_CONTROL:
                return olc::Key::CTRL;
            case VK_SPACE:
                return olc::Key::SPACE;
            case VK_MULTIPLY:
                return olc::Key::NP_MUL;
            case VK_ADD:
                return olc::Key::NP_ADD;
            case VK_DIVIDE:
                return olc::Key::NP_DIV;
            case VK_SUBTRACT:
                return olc::Key::NP_SUB;
            case VK_DECIMAL:
                return olc::Key::NP_DECIMAL;
            case VK_OEM_1:
                return olc::Key::OEM_1;
            case VK_OEM_2:
                return olc::Key::OEM_2;
            case VK_OEM_3:
                return olc::Key::OEM_3;
            case VK_OEM_4:
                return olc::Key::OEM_4;
            case VK_OEM_5:
                return olc::Key::OEM_5;
            case VK_OEM_6:
                return olc::Key::OEM_6;
            case VK_OEM_7:
                return olc::Key::OEM_7;
            case VK_OEM_8:
                return olc::Key::OEM_8;
            case VK_OEM_PLUS:
                return olc::Key::EQUALS;
            case VK_OEM_COMMA:
                return olc::Key::COMMA;
            case VK_OEM_MINUS:
                return olc::Key::MINUS;
            case VK_OEM_PERIOD:
                return olc::Key::PERIOD;
            case VK_CAPITAL:
                return olc::Key::CAPS_LOCK;
            default:
                return olc::Key::NONE;
        }
    }

    static constexpr LPARAM ALT_HELD_FLAG = 1 << 29;
    static constexpr LPARAM KEY_REPEAT_FLAG = 1 << 30;
    static std::atomic<KeyEventQueue*> hooked_queue = nullptr;
    static std::atomic<bool> hook_key_events = true;
    static std::atomic<bool> cursor_hidden = false;
    static std::atomic<WNDPROC> original_window_proc = nullptr;
    static HWND hooked_window = nullptr;
    static std::wstring window_title;
    static wchar_t pending_surrogate = 0;

    static std::string wide_to_utf8(const std::wstring& text) {
        int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
        std::string narrow(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), narrow.data(), length, nullptr, nullptr);
        return narrow;
    }

    static std::vector<std::string> take_dropped_files(HDROP drop) {
        std::vector<std::string> files;
        UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);

        for (UINT index = 0; index < count; index++) {
            UINT length = DragQueryFileW(drop, index, nullptr, 0);
            std::wstring path(length + 1, L'\0');
            DragQueryFileW(drop, index, path.data(), length + 1);
            path.resize(length);
            files.push_back(wide_to_utf8(path));
        }

        DragFinish(drop);
        return files;
    }

    static std::wstring utf8_to_wide(const std::string& text) {
        int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
        std::wstring wide(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wide.data(), length);
        return wide;
    }

    static LRESULT CALLBACK nascent_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        if (msg == WM_SETCURSOR && cursor_hidden && LOWORD(lparam) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }

        if (msg == WM_SETTEXT && !window_title.empty()) {
            lparam = (LPARAM)window_title.c_str();
        }

        if (msg == WM_SYSKEYDOWN && wparam == VK_F4 && (lparam & ALT_HELD_FLAG) != 0) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }

        KeyEventQueue* queue = hooked_queue.load();

        if (queue != nullptr) {
            switch (msg) {
                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYUP: {
                    if (!hook_key_events) {
                        break;
                    }

                    bool pressed = msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN;

                    if (queue->push(key_from_virtual_key(wparam), pressed, now_ms()) && pressed && (lparam & KEY_REPEAT_FLAG) == 0) {
                        queue->record_delay((double)(DWORD)(GetTickCount() - (DWORD)GetMessageTime()));
                    }
                    break;
                }
                case WM_CHAR: {
                    wchar_t unit = (wchar_t)wparam;

                    if (unit >= 0xd800 && unit <= 0xdbff) {
                        pending_surrogate = unit;
                    }
                    else if (unit >= 0xdc00 && unit <= 0xdfff) {
                        if (pending_surrogate != 0) {
                            queue->push_text(0x10000 + (((char32_t)pending_surrogate - 0xd800) << 10) + ((char32_t)unit - 0xdc00));
                        }

                        pending_surrogate = 0;
                    }
                    else if (unit >= 0x20 && unit != 0x7f) {
                        queue->push_text((char32_t)unit);
                    }

                    break;
                }
                case WM_KILLFOCUS:
                    queue->release_all(now_ms());
                    break;
                case WM_DROPFILES:
                    queue->push_dropped_files(take_dropped_files((HDROP)wparam));
                    return 0;
                case WM_XBUTTONDOWN:
                    if (GET_XBUTTON_WPARAM(wparam) == XBUTTON1) {
                        queue->push_back_click();
                    }

                    return TRUE;
            }
        }

        return CallWindowProcW(original_window_proc.load(), hwnd, msg, wparam, lparam);
    }

    static BOOL CALLBACK find_olc_window(HWND hwnd, LPARAM lparam) {
        DWORD process_id = 0;
        GetWindowThreadProcessId(hwnd, &process_id);

        if (process_id != GetCurrentProcessId()) {
            return TRUE;
        }

        wchar_t class_name[64] = {};

        if (GetClassNameW(hwnd, class_name, 64) && std::wcscmp(class_name, L"OLC_PIXEL_GAME_ENGINE") == 0) {
            *(HWND*)lparam = hwnd;
            return FALSE;
        }

        return TRUE;
    }

    bool install_window_hook(KeyEventQueue* queue, const std::string& title) {
        if (hooked_window != nullptr) {
            hooked_queue = queue;
            return true;
        }

        HWND window = nullptr;
        EnumWindows(find_olc_window, (LPARAM)&window);

        if (window == nullptr) {
            return false;
        }

        window_title = utf8_to_wide(title);
        hooked_queue = queue;
        original_window_proc = (WNDPROC)GetWindowLongPtrW(window, GWLP_WNDPROC);

        if (SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)nascent_window_proc) == 0) {
            hooked_queue = nullptr;
            return false;
        }

        hooked_window = window;

        if (!window_title.empty()) {
            SetWindowTextW(window, window_title.c_str());
        }

        return true;
    }

    void uninstall_window_hook() {
        hooked_queue = nullptr;

        if (hooked_window != nullptr && IsWindow(hooked_window)) {
            SetWindowLongPtrW(hooked_window, GWLP_WNDPROC, (LONG_PTR)original_window_proc.load());
        }

        hooked_window = nullptr;
    }

    void set_cursor_hidden(bool hidden) {
        if (cursor_hidden.exchange(hidden) == hidden || hooked_window == nullptr) {
            return;
        }

        POINT point;

        if (GetCursorPos(&point) && WindowFromPoint(point) == hooked_window) {
            PostMessageW(hooked_window, WM_SETCURSOR, (WPARAM)hooked_window, MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        }
    }

    void set_window_hook_key_events(bool enabled) {
        hook_key_events = enabled;
    }

    bool fit_window_to_screen(int32_t&, int32_t&) {
        return false;
    }

    void allow_app_switching_in_fullscreen() {
    }

    static constexpr DWORD WINDOWED_STYLE = WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_THICKFRAME;
    static constexpr DWORD WINDOWED_EX_STYLE = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
    static constexpr DWORD COVERING_STYLE = WS_VISIBLE | WS_POPUP;
    static constexpr int32_t BORDERLESS_OVERHANG = 1;

    static std::wstring monitor_friendly_name(const std::wstring& device) {
        UINT32 path_count = 0;
        UINT32 mode_count = 0;

        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) {
            return {};
        }

        std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);

        if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr) != ERROR_SUCCESS) {
            return {};
        }

        for (UINT32 index = 0; index < path_count; index++) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
            source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
            source.header.size = sizeof(source);
            source.header.adapterId = paths[index].sourceInfo.adapterId;
            source.header.id = paths[index].sourceInfo.id;

            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || device != source.viewGdiDeviceName) {
                continue;
            }

            DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
            target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
            target.header.size = sizeof(target);
            target.header.adapterId = paths[index].targetInfo.adapterId;
            target.header.id = paths[index].targetInfo.id;

            if (DisplayConfigGetDeviceInfo(&target.header) == ERROR_SUCCESS) {
                return target.monitorFriendlyDeviceName;
            }
        }

        return {};
    }

    static BOOL CALLBACK collect_display(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
        MONITORINFOEXW info = {};
        info.cbSize = sizeof(info);

        if (!GetMonitorInfoW(monitor, &info)) {
            return TRUE;
        }

        std::wstring device = info.szDevice;
        std::wstring name = monitor_friendly_name(device);
        DisplayInfo display;
        display.id = wide_to_utf8(device);
        display.name = wide_to_utf8(name);
        display.x = info.rcMonitor.left;
        display.y = info.rcMonitor.top;
        display.width = info.rcMonitor.right - info.rcMonitor.left;
        display.height = info.rcMonitor.bottom - info.rcMonitor.top;
        display.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        ((std::vector<DisplayInfo>*)data)->push_back(std::move(display));
        return TRUE;
    }

    static HWND game_window() {
        if (hooked_window != nullptr) {
            return hooked_window;
        }

        HWND window = nullptr;
        EnumWindows(find_olc_window, (LPARAM)&window);
        return window;
    }

    bool window_placement_supported() {
        return true;
    }

    std::vector<DisplayInfo> list_displays() {
        std::vector<DisplayInfo> displays;
        EnumDisplayMonitors(nullptr, nullptr, collect_display, (LPARAM)&displays);
        return displays;
    }

    bool place_window(WindowMode mode, const std::string& display_id, int32_t width, int32_t height) {
        HWND window = game_window();

        if (window == nullptr) {
            return false;
        }

        std::vector<DisplayInfo> displays = list_displays();
        const DisplayInfo* target = nullptr;

        for (const DisplayInfo& display : displays) {
            if ((!display_id.empty() && display.id == display_id) || (target == nullptr && display.primary)) {
                target = &display;
            }
        }

        if (target == nullptr && !displays.empty()) {
            target = &displays.front();
        }

        if (target == nullptr) {
            return false;
        }

        RECT area = {target->x, target->y, target->x + target->width, target->y + target->height};
        DWORD style = COVERING_STYLE;
        DWORD ex_style = 0;

        if (mode == WindowMode::WINDOWED) {
            MONITORINFO info = {};
            info.cbSize = sizeof(info);

            if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) {
                area = info.rcWork;
            }

            style = WINDOWED_STYLE;
            ex_style = WINDOWED_EX_STYLE;
            RECT frame = {0, 0, width, height};
            AdjustWindowRectEx(&frame, style, FALSE, ex_style);
            int32_t outer_width = std::min<int32_t>(frame.right - frame.left, area.right - area.left);
            int32_t outer_height = std::min<int32_t>(frame.bottom - frame.top, area.bottom - area.top);
            int32_t left = area.left + (area.right - area.left - outer_width) / 2;
            int32_t top = area.top + (area.bottom - area.top - outer_height) / 2;
            area = {left, top, left + outer_width, top + outer_height};
        }
        else if (mode == WindowMode::BORDERLESS) {
            area.bottom += BORDERLESS_OVERHANG;
        }

        SetWindowLongPtrW(window, GWL_STYLE, (LONG_PTR)style);
        SetWindowLongPtrW(window, GWL_EXSTYLE, (LONG_PTR)ex_style);
        return SetWindowPos(window, HWND_TOP, area.left, area.top, area.right - area.left, area.bottom - area.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW) != 0;
    }

    std::optional<std::filesystem::path> choose_folder(const std::string& title) {
        HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

        if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
            return std::nullopt;
        }

        std::optional<std::filesystem::path> chosen;
        IFileOpenDialog* dialog = nullptr;

        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
            FILEOPENDIALOGOPTIONS options = 0;
            dialog->GetOptions(&options);
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            std::wstring wide_title = utf8_to_wide(title);
            dialog->SetTitle(wide_title.c_str());
            HRESULT shown = dialog->Show(hooked_window);

            if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
                chosen = std::filesystem::path();
            }
            else if (SUCCEEDED(shown)) {
                IShellItem* item = nullptr;
                PWSTR path = nullptr;

                if (SUCCEEDED(dialog->GetResult(&item)) && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    chosen = std::filesystem::path(path);
                    CoTaskMemFree(path);
                }

                if (item != nullptr) {
                    item->Release();
                }
            }

            dialog->Release();
        }

        if (SUCCEEDED(initialized)) {
            CoUninitialize();
        }

        return chosen;
    }

    std::optional<bool> application_focused() {
        return std::nullopt;
    }

    static constexpr USHORT GENERIC_DESKTOP_USAGE_PAGE = 0x01;
    static constexpr USHORT KEYBOARD_USAGE = 0x06;
    static constexpr USHORT UNMAPPED_VIRTUAL_KEY = 0xFF;
    static constexpr const wchar_t* RAW_INPUT_WINDOW_CLASS = L"NascentRawInput";
    static std::thread raw_input_thread;
    static std::atomic<DWORD> raw_input_thread_id = 0;
    static std::atomic<KeyEventQueue*> raw_input_queue = nullptr;
    static std::atomic<HWND> raw_input_target = nullptr;

    static void handle_raw_input(HRAWINPUT handle) {
        double time = now_ms();
        RAWINPUT input = {};
        UINT size = sizeof(input);

        if (GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) == (UINT)-1 || input.header.dwType != RIM_TYPEKEYBOARD) {
            return;
        }

        const RAWKEYBOARD& keyboard = input.data.keyboard;
        KeyEventQueue* queue = raw_input_queue.load();

        if (queue == nullptr || keyboard.VKey == UNMAPPED_VIRTUAL_KEY || GetForegroundWindow() != raw_input_target.load()) {
            return;
        }

        bool pressed = (keyboard.Flags & RI_KEY_BREAK) == 0;

        if (queue->push(key_from_virtual_key(keyboard.VKey), pressed, time) && pressed) {
            queue->record_delay((double)(DWORD)(GetTickCount() - (DWORD)GetMessageTime()));
        }
    }

    static LRESULT CALLBACK raw_input_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        if (msg == WM_INPUT) {
            handle_raw_input((HRAWINPUT)lparam);
        }

        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    static void run_raw_input(std::promise<bool> started) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        raw_input_thread_id = GetCurrentThreadId();

        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW window_class = {};
        window_class.cbSize = sizeof(window_class);
        window_class.lpfnWndProc = raw_input_window_proc;
        window_class.hInstance = instance;
        window_class.lpszClassName = RAW_INPUT_WINDOW_CLASS;
        RegisterClassExW(&window_class);

        HWND window = CreateWindowExW(0, RAW_INPUT_WINDOW_CLASS, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
        RAWINPUTDEVICE device = {GENERIC_DESKTOP_USAGE_PAGE, KEYBOARD_USAGE, RIDEV_INPUTSINK, window};

        if (window == nullptr || !RegisterRawInputDevices(&device, 1, sizeof(device))) {
            if (window != nullptr) {
                DestroyWindow(window);
            }

            started.set_value(false);
            return;
        }

        started.set_value(true);
        MSG message;

        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            DispatchMessageW(&message);
        }

        device.dwFlags = RIDEV_REMOVE;
        device.hwndTarget = nullptr;
        RegisterRawInputDevices(&device, 1, sizeof(device));
        DestroyWindow(window);
    }

    std::optional<InputMode> start_key_source(KeyEventQueue* queue, const std::string&) {
        if (raw_input_thread.joinable()) {
            raw_input_queue = queue;
            return InputMode::RAW_INPUT;
        }

        HWND window = hooked_window;

        if (window == nullptr) {
            EnumWindows(find_olc_window, (LPARAM)&window);
        }

        if (window == nullptr) {
            return std::nullopt;
        }

        raw_input_target = window;
        raw_input_queue = queue;
        std::promise<bool> started;
        std::future<bool> result = started.get_future();
        raw_input_thread = std::thread(run_raw_input, std::move(started));

        if (!result.get()) {
            raw_input_thread.join();
            raw_input_queue = nullptr;
            return std::nullopt;
        }

        return InputMode::RAW_INPUT;
    }

    void stop_key_source() {
        raw_input_queue = nullptr;

        if (!raw_input_thread.joinable()) {
            return;
        }

        PostThreadMessageW(raw_input_thread_id.load(), WM_QUIT, 0, 0);
        raw_input_thread.join();
    }
}

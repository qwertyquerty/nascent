#include "WindowHook.h"

#include <atomic>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cwchar>
#endif

namespace nascent {
#ifdef _WIN32
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

    static std::atomic<KeyEventQueue*> hooked_queue = nullptr;
    static std::atomic<WNDPROC> original_window_proc = nullptr;
    static HWND hooked_window = nullptr;
    static std::wstring window_title;

    static std::wstring utf8_to_wide(const std::string& text) {
        int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
        std::wstring wide(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), wide.data(), length);
        return wide;
    }

    static LRESULT CALLBACK nascent_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        if (msg == WM_SETTEXT && !window_title.empty()) {
            lparam = (LPARAM)window_title.c_str();
        }

        KeyEventQueue* queue = hooked_queue.load();

        if (queue != nullptr) {
            switch (msg) {
                case WM_KEYDOWN:
                case WM_SYSKEYDOWN:
                case WM_KEYUP:
                case WM_SYSKEYUP: {
                    queue->push(key_from_virtual_key(wparam), msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN, now_ms());
                    break;
                }
                case WM_KILLFOCUS:
                    queue->release_all(now_ms());
                    break;
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
#else
    bool install_window_hook(KeyEventQueue*, const std::string&) {
        return false;
    }

    void uninstall_window_hook() {
    }
#endif
}

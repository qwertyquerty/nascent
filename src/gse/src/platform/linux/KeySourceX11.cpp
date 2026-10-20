#include "gse/Platform.h"
#include "gse/input/KeySource.h"

#include <poll.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

namespace X11 {
#include <X11/XKBlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
}

namespace nascent {
    using namespace X11;

    static constexpr const char* ENGINE_WINDOW_TITLE = "OneLoneCoder.com - Pixel Game Engine";
    static constexpr int WINDOW_SEARCH_ATTEMPTS = 40;
    static constexpr std::chrono::milliseconds WINDOW_SEARCH_INTERVAL(50);
    static constexpr int WINDOW_SEARCH_DEPTH = 4;
    static constexpr size_t KEYCODE_COUNT = 256;

    static std::thread key_thread;
    static std::array<int, 2> wake_pipe = {-1, -1};
    static std::atomic<KeyEventQueue*> key_queue = nullptr;
    static Display* key_display = nullptr;
    static Display* modifier_display = nullptr;
    static bool modifier_display_opened = false;
    static constexpr size_t KEYMAP_BYTES = 32;

    bool alt_held() {
        if (!modifier_display_opened) {
            modifier_display_opened = true;
            modifier_display = XOpenDisplay(nullptr);
        }

        if (modifier_display == nullptr) {
            return false;
        }

        std::array<char, KEYMAP_BYTES> keys = {};
        XQueryKeymap(modifier_display, keys.data());

        for (KeySym symbol : {XK_Alt_L, XK_Alt_R}) {
            KeyCode code = XKeysymToKeycode(modifier_display, symbol);

            if (code != 0 && (keys[code / 8] & (1 << (code % 8))) != 0) {
                return true;
            }
        }

        return false;
    }

    static olc::Key key_from_keysym(KeySym symbol) {
        if (symbol >= XK_a && symbol <= XK_z) {
            return (olc::Key)((size_t)olc::Key::A + (symbol - XK_a));
        }

        if (symbol >= XK_A && symbol <= XK_Z) {
            return (olc::Key)((size_t)olc::Key::A + (symbol - XK_A));
        }

        if (symbol >= XK_0 && symbol <= XK_9) {
            return (olc::Key)((size_t)olc::Key::K0 + (symbol - XK_0));
        }

        if (symbol >= XK_F1 && symbol <= XK_F12) {
            return (olc::Key)((size_t)olc::Key::F1 + (symbol - XK_F1));
        }

        if (symbol >= XK_KP_0 && symbol <= XK_KP_9) {
            return (olc::Key)((size_t)olc::Key::NP0 + (symbol - XK_KP_0));
        }

        switch (symbol) {
            case XK_Up:
            case XK_KP_Up:
                return olc::Key::UP;
            case XK_Down:
            case XK_KP_Down:
                return olc::Key::DOWN;
            case XK_Left:
            case XK_KP_Left:
                return olc::Key::LEFT;
            case XK_Right:
            case XK_KP_Right:
                return olc::Key::RIGHT;
            case XK_Return:
            case XK_KP_Enter:
            case XK_Linefeed:
                return olc::Key::ENTER;
            case XK_BackSpace:
                return olc::Key::BACK;
            case XK_Escape:
                return olc::Key::ESCAPE;
            case XK_Pause:
                return olc::Key::PAUSE;
            case XK_Scroll_Lock:
                return olc::Key::SCROLL;
            case XK_Tab:
                return olc::Key::TAB;
            case XK_Delete:
            case XK_KP_Delete:
                return olc::Key::DEL;
            case XK_Home:
            case XK_KP_Home:
                return olc::Key::HOME;
            case XK_End:
            case XK_KP_End:
                return olc::Key::END;
            case XK_Page_Up:
            case XK_KP_Page_Up:
                return olc::Key::PGUP;
            case XK_Page_Down:
            case XK_KP_Page_Down:
                return olc::Key::PGDN;
            case XK_Insert:
            case XK_KP_Insert:
                return olc::Key::INS;
            case XK_Shift_L:
            case XK_Shift_R:
                return olc::Key::SHIFT;
            case XK_Control_L:
            case XK_Control_R:
                return olc::Key::CTRL;
            case XK_space:
                return olc::Key::SPACE;
            case XK_period:
                return olc::Key::PERIOD;
            case XK_KP_Multiply:
                return olc::Key::NP_MUL;
            case XK_KP_Add:
                return olc::Key::NP_ADD;
            case XK_KP_Divide:
                return olc::Key::NP_DIV;
            case XK_KP_Subtract:
                return olc::Key::NP_SUB;
            case XK_KP_Decimal:
                return olc::Key::NP_DECIMAL;
            case XK_semicolon:
                return olc::Key::OEM_1;
            case XK_slash:
                return olc::Key::OEM_2;
            case XK_asciitilde:
            case XK_grave:
                return olc::Key::OEM_3;
            case XK_bracketleft:
                return olc::Key::OEM_4;
            case XK_backslash:
                return olc::Key::OEM_5;
            case XK_bracketright:
                return olc::Key::OEM_6;
            case XK_apostrophe:
                return olc::Key::OEM_7;
            case XK_numbersign:
                return olc::Key::OEM_8;
            case XK_equal:
                return olc::Key::EQUALS;
            case XK_comma:
                return olc::Key::COMMA;
            case XK_minus:
                return olc::Key::MINUS;
            case XK_Caps_Lock:
                return olc::Key::CAPS_LOCK;
            default:
                return olc::Key::NONE;
        }
    }

    static bool is_engine_window(Display* display, Window window) {
        char* name = nullptr;

        if (XFetchName(display, window, &name) == 0 || name == nullptr) {
            return false;
        }

        bool matches = std::strncmp(name, ENGINE_WINDOW_TITLE, std::strlen(ENGINE_WINDOW_TITLE)) == 0;
        XFree(name);
        return matches;
    }

    static Window find_engine_window(Display* display, Window parent, int depth) {
        Window root = 0;
        Window parent_of_parent = 0;
        Window* children = nullptr;
        unsigned int count = 0;

        if (XQueryTree(display, parent, &root, &parent_of_parent, &children, &count) == 0) {
            return 0;
        }

        Window found = 0;

        for (unsigned int index = 0; index < count && found == 0; index++) {
            if (is_engine_window(display, children[index])) {
                found = children[index];
            }
            else if (depth > 1) {
                found = find_engine_window(display, children[index], depth - 1);
            }
        }

        if (children != nullptr) {
            XFree(children);
        }

        return found;
    }

    static olc::Key key_for_event(XKeyEvent& event, KeyEventQueue* queue) {
        std::array<char, 16> text = {};
        KeySym symbol = NoSymbol;
        int length = XLookupString(&event, text.data(), (int)text.size(), &symbol, nullptr);

        if ((event.state & (ControlMask | Mod4Mask)) == 0) {
            for (int index = 0; index < length; index++) {
                unsigned char character = (unsigned char)text[index];

                if (character >= 0x20 && character != 0x7f) {
                    queue->push_text(character);
                }
            }
        }

        olc::Key key = key_from_keysym(symbol);
        return key != olc::Key::NONE ? key : key_from_keysym(XLookupKeysym(&event, 0));
    }

    static void name_window(Display* display, Window window, const std::string& title) {
        std::string instance = title;

        for (char& character : instance) {
            character = (char)std::tolower((unsigned char)character);
        }

        XClassHint hint = {instance.data(), const_cast<char*>(title.c_str())};
        XSetClassHint(display, window, &hint);
        Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
        Atom net_name = XInternAtom(display, "_NET_WM_NAME", False);
        XChangeProperty(display, window, net_name, utf8, 8, PropModeReplace, (const unsigned char*)title.c_str(), (int)title.size());
    }

    static bool is_auto_repeat_release(Display* display, const XEvent& event) {
        if (XEventsQueued(display, QueuedAfterReading) == 0) {
            return false;
        }

        XEvent next;
        XPeekEvent(display, &next);
        return next.type == KeyPress && next.xkey.keycode == event.xkey.keycode && next.xkey.time == event.xkey.time;
    }

    static void run_key_thread(Display* display, bool detectable_repeat) {
        std::array<olc::Key, KEYCODE_COUNT> pressed_keys = {};
        pressed_keys.fill(olc::Key::NONE);

        while (true) {
            while (XPending(display) > 0) {
                XEvent event;
                XNextEvent(display, &event);
                double time = now_ms();
                KeyEventQueue* queue = key_queue.load();

                if (queue == nullptr) {
                    continue;
                }

                if (event.type == FocusOut) {
                    queue->release_all(time);
                    pressed_keys.fill(olc::Key::NONE);
                    continue;
                }

                if (event.type != KeyPress && event.type != KeyRelease) {
                    continue;
                }

                size_t code = event.xkey.keycode % KEYCODE_COUNT;

                if (event.type == KeyPress) {
                    olc::Key key = key_for_event(event.xkey, queue);

                    if (pressed_keys[code] == olc::Key::NONE) {
                        pressed_keys[code] = key;
                    }

                    queue->push(pressed_keys[code], true, time);
                    continue;
                }

                if (!detectable_repeat && is_auto_repeat_release(display, event)) {
                    XEvent repeat;
                    XNextEvent(display, &repeat);
                    key_for_event(repeat.xkey, queue);
                    continue;
                }

                queue->push(pressed_keys[code], false, time);
                pressed_keys[code] = olc::Key::NONE;
            }

            std::array<pollfd, 2> descriptors = {{{ConnectionNumber(display), POLLIN, 0}, {wake_pipe[0], POLLIN, 0}}};

            if (poll(descriptors.data(), descriptors.size(), -1) < 0 && errno != EINTR) {
                break;
            }

            if ((descriptors[1].revents & POLLIN) != 0) {
                break;
            }
        }
    }

    std::optional<InputMode> start_key_source(KeyEventQueue* queue, const std::string& title) {
        if (key_thread.joinable()) {
            key_queue = queue;
            return InputMode::X11_THREAD;
        }

        Display* display = XOpenDisplay(nullptr);

        if (display == nullptr) {
            return std::nullopt;
        }

        Window window = 0;

        for (int attempt = 0; attempt < WINDOW_SEARCH_ATTEMPTS && window == 0; attempt++) {
            window = find_engine_window(display, DefaultRootWindow(display), WINDOW_SEARCH_DEPTH);

            if (window == 0) {
                std::this_thread::sleep_for(WINDOW_SEARCH_INTERVAL);
            }
        }

        if (window == 0 || pipe(wake_pipe.data()) != 0) {
            XCloseDisplay(display);
            return std::nullopt;
        }

        Bool detectable_repeat = False;
        XkbSetDetectableAutoRepeat(display, True, &detectable_repeat);
        XSelectInput(display, window, KeyPressMask | KeyReleaseMask | FocusChangeMask);

        if (!title.empty()) {
            name_window(display, window, title);
        }

        XFlush(display);

        key_display = display;
        key_queue = queue;
        key_thread = std::thread(run_key_thread, display, detectable_repeat == True);
        return InputMode::X11_THREAD;
    }

    void stop_key_source() {
        key_queue = nullptr;

        if (!key_thread.joinable()) {
            return;
        }

        char wake = 0;
        ssize_t written = write(wake_pipe[1], &wake, 1);
        (void)written;
        key_thread.join();

        close(wake_pipe[0]);
        close(wake_pipe[1]);
        wake_pipe = {-1, -1};
        XCloseDisplay(key_display);
        key_display = nullptr;
    }
}

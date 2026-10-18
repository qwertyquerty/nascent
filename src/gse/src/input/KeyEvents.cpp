#include "gse/input/KeyEvents.h"

#include <algorithm>
#include <chrono>
#include <iterator>
#include <utility>

namespace nascent {
    double now_ms() {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    const char* input_mode_name(InputMode mode) {
        switch (mode) {
            case InputMode::WINDOW_MESSAGES:
                return "window messages";
            case InputMode::RAW_INPUT:
                return "raw input";
            case InputMode::X11_THREAD:
                return "X11 thread";
            case InputMode::COCOA_EVENTS:
                return "Cocoa events";
            default:
                return "polling";
        }
    }

    bool KeyEventQueue::push(olc::Key key, bool pressed, double time) {
        size_t index = (size_t)key;

        if (key == olc::Key::NONE || index >= KEY_COUNT) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex);

        if (held[index] == pressed) {
            return false;
        }

        held[index] = pressed;
        events.push_back({key, pressed, time});
        return true;
    }

    void KeyEventQueue::release_all(double time) {
        std::lock_guard<std::mutex> lock(mutex);

        for (size_t index = 0; index < KEY_COUNT; index++) {
            if (held[index]) {
                held[index] = false;
                events.push_back({(olc::Key)index, false, time});
            }
        }
    }

    std::vector<KeyEvent> KeyEventQueue::drain() {
        std::lock_guard<std::mutex> lock(mutex);
        return std::exchange(events, {});
    }

    void KeyEventQueue::push_text(char32_t code_point) {
        std::lock_guard<std::mutex> lock(mutex);

        if (code_point < 0x80) {
            text.push_back((char)code_point);
        }
        else if (code_point < 0x800) {
            text.push_back((char)(0xc0 | (code_point >> 6)));
            text.push_back((char)(0x80 | (code_point & 0x3f)));
        }
        else if (code_point < 0x10000) {
            text.push_back((char)(0xe0 | (code_point >> 12)));
            text.push_back((char)(0x80 | ((code_point >> 6) & 0x3f)));
            text.push_back((char)(0x80 | (code_point & 0x3f)));
        }
        else {
            text.push_back((char)(0xf0 | (code_point >> 18)));
            text.push_back((char)(0x80 | ((code_point >> 12) & 0x3f)));
            text.push_back((char)(0x80 | ((code_point >> 6) & 0x3f)));
            text.push_back((char)(0x80 | (code_point & 0x3f)));
        }
    }

    std::string KeyEventQueue::drain_text() {
        std::lock_guard<std::mutex> lock(mutex);
        return std::exchange(text, {});
    }

    void KeyEventQueue::push_repeat(olc::Key key) {
        size_t index = (size_t)key;

        if (key == olc::Key::NONE || index >= KEY_COUNT) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex);

        if (held[index]) {
            repeats.push_back(key);
        }
    }

    std::vector<olc::Key> KeyEventQueue::drain_repeats() {
        std::lock_guard<std::mutex> lock(mutex);
        return std::exchange(repeats, {});
    }

    void KeyEventQueue::push_dropped_files(std::vector<std::string> files) {
        std::lock_guard<std::mutex> lock(mutex);
        dropped_files.insert(dropped_files.end(), std::make_move_iterator(files.begin()), std::make_move_iterator(files.end()));
    }

    void KeyEventQueue::record_delay(double delay_ms) {
        std::lock_guard<std::mutex> lock(mutex);
        delay_stats.count++;
        delay_stats.total_ms += delay_ms;
        delay_stats.max_ms = std::max(delay_stats.max_ms, delay_ms);
    }

    InputDelayStats KeyEventQueue::input_delay() const {
        std::lock_guard<std::mutex> lock(mutex);
        return delay_stats;
    }

    void KeyEventQueue::reset_input_delay() {
        std::lock_guard<std::mutex> lock(mutex);
        delay_stats = InputDelayStats();
    }

    std::vector<std::string> KeyEventQueue::drain_dropped_files() {
        std::lock_guard<std::mutex> lock(mutex);
        return std::exchange(dropped_files, {});
    }

    void KeyEventQueue::push_back_click() {
        std::lock_guard<std::mutex> lock(mutex);
        back_clicks++;
    }

    bool KeyEventQueue::drain_back_click() {
        std::lock_guard<std::mutex> lock(mutex);
        return std::exchange(back_clicks, 0) > 0;
    }
}

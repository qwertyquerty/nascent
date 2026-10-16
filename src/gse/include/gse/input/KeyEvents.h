#pragma once

#include "olcPixelGameEngine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace nascent {
    double now_ms();

    inline constexpr size_t KEY_COUNT = (size_t)olc::Key::ENUM_END;

    enum class InputMode {
        POLLING,
        WINDOW_MESSAGES,
        RAW_INPUT,
        X11_THREAD,
        COCOA_EVENTS,
    };

    const char* input_mode_name(InputMode mode);

    struct KeyEvent {
        olc::Key key;
        bool pressed;
        double time;
    };

    struct InputDelayStats {
        uint32_t count = 0;
        double total_ms = 0;
        double max_ms = 0;

        double average_ms() const {
            return count > 0 ? total_ms / count : 0.0;
        }
    };

    class KeyEventQueue {
        protected:
        mutable std::mutex mutex;
        std::vector<KeyEvent> events;
        std::array<bool, KEY_COUNT> held = {};
        std::string text;
        std::vector<std::string> dropped_files;
        int32_t back_clicks = 0;
        InputDelayStats delay_stats;

        public:
        bool push(olc::Key key, bool pressed, double time);
        void push_text(char32_t code_point);
        void release_all(double time);
        std::vector<KeyEvent> drain();
        std::string drain_text();
        void push_dropped_files(std::vector<std::string> files);
        std::vector<std::string> drain_dropped_files();
        void push_back_click();
        bool drain_back_click();
        void record_delay(double delay_ms);
        InputDelayStats input_delay() const;
        void reset_input_delay();
    };

}

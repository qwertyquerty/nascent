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

    struct KeyEvent {
        olc::Key key;
        bool pressed;
        double time;
    };

    class KeyEventQueue {
        protected:
        std::mutex mutex;
        std::vector<KeyEvent> events;
        std::array<bool, KEY_COUNT> held = {};
        std::string text;
        std::vector<std::string> dropped_files;

        public:
        void push(olc::Key key, bool pressed, double time);
        void push_text(char32_t code_point);
        void release_all(double time);
        std::vector<KeyEvent> drain();
        std::string drain_text();
        void push_dropped_files(std::vector<std::string> files);
        std::vector<std::string> drain_dropped_files();
    };

}

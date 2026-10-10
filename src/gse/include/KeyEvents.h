#pragma once

#include "olcPixelGameEngine.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
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

        public:
        void push(olc::Key key, bool pressed, double time);
        void release_all(double time);
        std::vector<KeyEvent> drain();
    };

}

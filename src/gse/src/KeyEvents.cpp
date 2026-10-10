#include "KeyEvents.h"

#include <chrono>
#include <utility>

namespace nascent {
    double now_ms() {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void KeyEventQueue::push(olc::Key key, bool pressed, double time) {
        size_t index = (size_t)key;

        if (key == olc::Key::NONE || index >= KEY_COUNT) {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex);

        if (held[index] == pressed) {
            return;
        }

        held[index] = pressed;
        events.push_back({key, pressed, time});
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
}

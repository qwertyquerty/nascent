#pragma once

#include "olcPixelGameEngine.h"

namespace nascent {
    class KeyRepeat {
        protected:
        float held_time = 0;
        float next_repeat = 0;

        public:
        static constexpr float DELAY = 0.35f;
        static constexpr float INTERVAL = 0.04f;

        bool update(const olc::HWButton& button, float elapsed_time) {
            if (button.bPressed) {
                held_time = 0;
                next_repeat = DELAY;
                return true;
            }

            if (!button.bHeld) {
                held_time = 0;
                return false;
            }

            held_time += elapsed_time;

            if (held_time < next_repeat) {
                return false;
            }

            next_repeat += INTERVAL;
            return true;
        }
    };
}

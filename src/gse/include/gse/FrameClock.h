#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace nascent {
    class FrameClock {
        protected:
        std::optional<std::chrono::steady_clock::time_point> next_frame;

        public:
        static constexpr std::chrono::duration<double> SPIN_TIME = std::chrono::milliseconds(1);

        void limit(uint16_t frames_per_second);
    };
}

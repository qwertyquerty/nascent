#include "gse/FrameClock.h"

#include "gse/Platform.h"

#include <thread>

namespace nascent {
    void FrameClock::limit(uint16_t frames_per_second) {
        if (frames_per_second == 0) {
            next_frame.reset();
            return;
        }

        using clock = std::chrono::steady_clock;
        clock::duration period = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / frames_per_second));
        clock::time_point now = clock::now();

        if (!next_frame || now - *next_frame > period) {
            next_frame = now + period;
            return;
        }

        std::chrono::duration<double> remaining = *next_frame - now;

        if (remaining > SPIN_TIME) {
            precise_sleep((remaining - SPIN_TIME).count());
        }

        while (clock::now() < *next_frame) {
            std::this_thread::yield();
        }

        *next_frame += period;
    }
}

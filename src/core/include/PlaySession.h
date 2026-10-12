#pragma once

#include <cstdint>

#include "Attempt.h"
#include "Chart.h"
#include "SongClock.h"

namespace nascent {
    class PlaySession {
        protected:
        const Chart* chart;
        Attempt attempt;
        SongClock clock;

        int32_t to_input_time(double now) const;

        public:
        int32_t audio_visual_offset = 0;
        int32_t audio_input_offset = 0;

        PlaySession(const Chart* chart, double max_sync_error);

        void reset_clock(double song_position, double now);
        void sync_clock(double song_position, double now);

        void press(uint8_t key, double event_time);
        void release(uint8_t key, double event_time);
        void set_keys(uint32_t keys, double now);
        void update(double now);

        double song_position(double now) const;
        double visual_position(double now) const;

        const Attempt& get_attempt() const;
        uint32_t get_held_keys() const;
        unsigned int clock_snaps() const;
        double last_clock_drift() const;
    };
}

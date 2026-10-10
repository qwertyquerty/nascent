#include "SongClock.h"

#include <cmath>

namespace nascent {
    void SongClock::reset(double position, double now) {
        anchor_position = position;
        anchor_time = now;
        last_reported_position = position;
        has_report = true;
    }

    void SongClock::sync(double reported_position, double now) {
        double predicted = position_at(now);
        double drift = reported_position - predicted;

        if (!has_report || std::abs(drift) > max_error) {
            reset(reported_position, now);
            return;
        }

        if (reported_position != last_reported_position) {
            anchor_position = predicted + drift * correction_rate;
            anchor_time = now;
            last_reported_position = reported_position;
        }
    }

    double SongClock::position_at(double now) const {
        return anchor_position + (now - anchor_time);
    }
}

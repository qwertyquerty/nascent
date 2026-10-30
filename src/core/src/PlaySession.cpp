#include "PlaySession.h"

#include <cmath>

namespace nascent {
    PlaySession::PlaySession(const Chart* chart, double max_sync_error) : chart(chart), attempt(chart) {
        clock.max_error = max_sync_error;
    }

    int32_t PlaySession::to_input_time(double now) const {
        return (int32_t)std::lround(precise_input_time(now));
    }

    double PlaySession::precise_input_time(double now) const {
        return clock.position_at(now) + audio_input_offset + chart_offset + source_offset;
    }

    void PlaySession::record_input_times(double time) {
        while (input_times.size() < attempt.inputs.size()) {
            input_times.push_back(time);
        }
    }

    void PlaySession::reset_clock(double song_position, double now) {
        clock.reset(song_position, now);
    }

    void PlaySession::sync_clock(double song_position, double now) {
        clock.sync(song_position, now);
    }

    void PlaySession::set_clock_speed(double speed, double now) {
        clock.set_speed(speed, now);
    }

    void PlaySession::press(uint8_t key, double event_time) {
        attempt.press(key, to_input_time(event_time));
        record_input_times(precise_input_time(event_time));
    }

    void PlaySession::release(uint8_t key, double event_time) {
        attempt.release(key, to_input_time(event_time));
        record_input_times(precise_input_time(event_time));
    }

    void PlaySession::set_keys(uint32_t keys, double now) {
        attempt.set_keys(keys, to_input_time(now));
        record_input_times(precise_input_time(now));
    }

    void PlaySession::apply(const AttemptInput& input) {
        if (input.pressed) {
            attempt.press(input.key, input.time);
        }
        else {
            attempt.release(input.key, input.time);
        }

        record_input_times(input.time);
    }

    void PlaySession::update(double now) {
        attempt.update(to_input_time(now));
        record_input_times(precise_input_time(now));
    }

    double PlaySession::song_position(double now) const {
        return clock.position_at(now);
    }

    double PlaySession::visual_position(double now) const {
        return clock.position_at(now) - audio_visual_offset + chart_offset + source_offset;
    }

    const Attempt& PlaySession::get_attempt() const {
        return attempt;
    }

    const std::vector<double>& PlaySession::get_input_times() const {
        return input_times;
    }

    unsigned int PlaySession::clock_snaps() const {
        return clock.snaps;
    }

    double PlaySession::last_clock_drift() const {
        return clock.last_drift;
    }

    uint32_t PlaySession::get_held_keys() const {
        return attempt.held_keys;
    }
}

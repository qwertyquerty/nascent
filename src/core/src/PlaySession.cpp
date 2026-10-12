#include "PlaySession.h"

#include <cmath>

namespace nascent {
    PlaySession::PlaySession(const Chart* chart, double max_sync_error) : chart(chart), attempt(chart) {
        clock.max_error = max_sync_error;
    }

    int32_t PlaySession::to_input_time(double now) const {
        return (int32_t)std::lround(clock.position_at(now) + audio_input_offset);
    }

    void PlaySession::reset_clock(double song_position, double now) {
        clock.reset(song_position, now);
    }

    void PlaySession::sync_clock(double song_position, double now) {
        clock.sync(song_position, now);
    }

    void PlaySession::press(uint8_t key, double event_time) {
        attempt.press(key, to_input_time(event_time));
    }

    void PlaySession::release(uint8_t key, double event_time) {
        attempt.release(key, to_input_time(event_time));
    }

    void PlaySession::set_keys(uint32_t keys, double now) {
        attempt.set_keys(keys, to_input_time(now));
    }

    void PlaySession::update(double now) {
        attempt.update(to_input_time(now));
    }

    double PlaySession::song_position(double now) const {
        return clock.position_at(now);
    }

    double PlaySession::visual_position(double now) const {
        return clock.position_at(now) - audio_visual_offset;
    }

    const Attempt& PlaySession::get_attempt() const {
        return attempt;
    }

    unsigned int PlaySession::clock_snaps() const {
        return clock.snaps;
    }

    uint32_t PlaySession::get_held_keys() const {
        return attempt.held_keys;
    }
}

#include "Playback.h"

#include "Util.h"
#include "Utf8Path.h"
#include "gse/audio/AudioMixer.h"

#include <algorithm>
#include <cmath>
#include <exception>

namespace nascent {
    Playback::Playback(AudioMixer& mixer) : mixer(&mixer) {}

    Playback::~Playback() {
        unload();
    }

    bool Playback::load(const std::filesystem::path& file, std::string& error) {
        unload();
        std::error_code ec;

        if (!std::filesystem::is_regular_file(file, ec)) {
            error = "audio file " + path_utf8(file) + " is missing";
            return false;
        }

        try {
            sound = load_song_audio(mixer->engine(), file);
        }
        catch (const std::exception& exception) {
            error = "could not load " + path_utf8(file) + ": " + exception.what();
            sound = -1;
            return false;
        }

        if (sound < 0) {
            error = "could not load " + path_utf8(file);
            return false;
        }

        float seconds = 0.0f;

        if (ma_sound* handle = mixer->engine().GetSound(sound); handle != nullptr && ma_sound_get_length_in_seconds(handle, &seconds) == MA_SUCCESS) {
            length_ms = std::max(0.0, seconds * 1000.0 - mixer->engine().GetStartOffsetMilliseconds(sound));
        }

        mixer->engine().SetVolume(sound, gain);
        mixer->engine().SetPitch(sound, speed);
        paused_at = 0;
        last_position = 0;
        return true;
    }

    void Playback::unload() {
        if (sound < 0) {
            return;
        }

        mixer->engine().Stop(sound);
        mixer->engine().UnloadSound(sound);
        sound = -1;
        length_ms = 0;
        playing = false;
    }

    bool Playback::loaded() const {
        return sound >= 0;
    }

    void Playback::play() {
        if (sound < 0 || playing) {
            return;
        }

        if (length_ms > 0 && paused_at >= length_ms) {
            paused_at = 0;
        }

        last_position = paused_at;

        mixer->engine().Seek(sound, (ma_uint64)std::lround(std::max(0.0, paused_at)));
        mixer->engine().Play(sound);
        playing = true;
    }

    void Playback::pause() {
        if (!playing) {
            return;
        }

        paused_at = position();
        mixer->engine().Pause(sound);
        playing = false;
    }

    void Playback::toggle() {
        if (playing) {
            pause();
        }
        else {
            play();
        }
    }

    void Playback::seek(double target) {
        paused_at = std::max(0.0, length_ms > 0 ? std::min(target, length_ms) : target);
        last_position = paused_at;

        if (playing && sound >= 0) {
            mixer->engine().Seek(sound, (ma_uint64)std::lround(paused_at));
        }
    }

    void Playback::set_rate(float rate) {
        speed = std::clamp(rate, 0.25f, 2.0f);

        if (sound >= 0) {
            mixer->engine().SetPitch(sound, speed);
        }
    }

    void Playback::set_volume(float volume) {
        gain = std::clamp(volume, 0.0f, 1.0f);

        if (sound >= 0) {
            mixer->engine().SetVolume(sound, gain);
        }
    }

    float Playback::rate() const {
        return speed;
    }

    bool Playback::is_playing() const {
        return playing;
    }

    double Playback::position() {
        if (!playing || sound < 0) {
            return paused_at;
        }

        double reported = mixer->engine().GetPositionMilliseconds(sound);
        last_position = std::max(last_position, reported);
        return last_position;
    }

    double Playback::length() const {
        return length_ms;
    }

    void Playback::update() {
        if (playing && sound >= 0 && !mixer->engine().IsPlaying(sound)) {
            paused_at = length_ms;
            playing = false;
        }
    }
}

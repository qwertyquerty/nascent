#pragma once

#include <filesystem>
#include <string>

namespace nascent {
    class AudioMixer;

    class Playback {
        protected:
        AudioMixer* mixer = nullptr;
        int sound = -1;
        double length_ms = 0;
        double paused_at = 0;
        double last_position = 0;
        float speed = 1.0f;
        float gain = 1.0f;
        bool playing = false;

        public:
        explicit Playback(AudioMixer& mixer);
        ~Playback();
        Playback(const Playback&) = delete;
        Playback& operator=(const Playback&) = delete;

        bool load(const std::filesystem::path& file, std::string& error);
        void unload();
        bool loaded() const;

        void play();
        void pause();
        void toggle();
        void seek(double position);
        void set_rate(float rate);
        void set_volume(float volume);
        float rate() const;
        bool is_playing() const;
        double position();
        double length() const;
        void update();
    };
}

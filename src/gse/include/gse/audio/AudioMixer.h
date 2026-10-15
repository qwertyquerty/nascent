#pragma once

#include "olcPixelGameEngine.h"
#include "olcPGEX_MiniAudio.h"

#include <vector>

namespace nascent {
    class AudioMixer {
        protected:
        struct Fade {
            int id = -1;
            float from = 0;
            float to = 1;
            float duration = 0;
            float elapsed = 0;
            bool stop_at_end = false;
            float curve = 1.0f;

            float progress() const;
            float volume() const;
        };

        olc::MiniAudio audio;
        float music_volume = 1.0f;
        float effects_volume = 1.0f;
        std::vector<Fade> fades;

        public:
        olc::MiniAudio& engine();

        void play_effect(int id, bool looping = false);
        void play_music(int id, bool looping = false);
        void stop(int id);
        void apply_music_volume(int id, float fade = 1.0f);
        void apply_effects_volume(int id);
        void set_volumes(float master, float music, float effects);
        void fade_music(int id, float from, float to, float seconds, bool stop_at_end = false, float curve = 1.0f);
        float music_fade(int id) const;
        float fade_progress(int id) const;
        void cancel_fade(int id);
        void update(float elapsed_time);
    };
}

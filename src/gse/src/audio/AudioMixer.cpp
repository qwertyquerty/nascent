#include "gse/audio/AudioMixer.h"

#include <algorithm>
#include <cmath>

namespace nascent {
    float AudioMixer::Fade::progress() const {
        return duration > 0 ? std::clamp(elapsed / duration, 0.0f, 1.0f) : 1.0f;
    }

    float AudioMixer::Fade::volume() const {
        return std::lerp(from, to, std::pow(progress(), curve));
    }

    olc::MiniAudio& AudioMixer::engine() {
        return audio;
    }

    void AudioMixer::play_effect(int id, bool looping) {
        if (id == -1) {
            return;
        }

        if (audio.IsPlaying(id)) {
            audio.Stop(id);
        }

        audio.SetVolume(id, effects_volume);
        audio.Play(id, looping);
    }

    void AudioMixer::play_music(int id, bool looping) {
        if (id == -1) {
            return;
        }

        if (audio.IsPlaying(id)) {
            audio.Stop(id);
        }

        audio.SetVolume(id, music_volume);
        audio.Play(id, looping);
    }

    void AudioMixer::stop(int id) {
        if (id != -1) {
            cancel_fade(id);
            audio.Stop(id);
        }
    }

    void AudioMixer::apply_music_volume(int id, float fade) {
        audio.SetVolume(id, music_volume * fade);
    }

    void AudioMixer::apply_effects_volume(int id) {
        audio.SetVolume(id, effects_volume);
    }

    void AudioMixer::set_volumes(float master, float music, float effects) {
        music_volume = std::clamp(music, 0.0f, 1.0f);
        effects_volume = std::clamp(effects, 0.0f, 1.0f);
        ma_engine_set_volume(audio.GetEngine(), std::clamp(master, 0.0f, 1.0f));
    }

    void AudioMixer::fade_music(int id, float from, float to, float seconds, bool stop_at_end, float curve) {
        if (id == -1) {
            return;
        }

        cancel_fade(id);

        if (seconds <= 0) {
            apply_music_volume(id, to);

            if (stop_at_end) {
                audio.Stop(id);
            }

            return;
        }

        fades.push_back({id, from, to, seconds, 0.0f, stop_at_end, curve});
        apply_music_volume(id, from);
    }

    float AudioMixer::music_fade(int id) const {
        for (const Fade& fade : fades) {
            if (fade.id == id) {
                return fade.volume();
            }
        }

        return 1.0f;
    }

    float AudioMixer::fade_progress(int id) const {
        for (const Fade& fade : fades) {
            if (fade.id == id) {
                return fade.progress();
            }
        }

        return 1.0f;
    }

    void AudioMixer::cancel_fade(int id) {
        std::erase_if(fades, [id](const Fade& fade) { return fade.id == id; });
    }

    void AudioMixer::update(float elapsed_time) {
        for (Fade& fade : fades) {
            fade.elapsed += elapsed_time;
            apply_music_volume(fade.id, fade.volume());

            if (fade.progress() >= 1.0f && fade.stop_at_end) {
                audio.Stop(fade.id);
            }
        }

        std::erase_if(fades, [](const Fade& fade) { return fade.elapsed >= fade.duration; });
    }
}

#include "Util.h"
#include "Mp3Padding.h"
#include "olcPGEX_MiniAudio.h"
#include <cmath>
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace nascent {
    float smoothstep(double begin, double end, double t) {
        t = std::clamp(t, 0.0, 1.0);
        return std::lerp(begin, end, t * t * (3 - 2 * t));
    }

    int load_song_audio(olc::MiniAudio& audio, const std::filesystem::path& path) {
        int id = audio.LoadSound(path.string());
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return (char)std::tolower(c); });

        if (id < 0 || extension != ".mp3") {
            return id;
        }

        if (std::optional<Mp3Padding> padding = read_mp3_padding(path)) {
            audio.SetStartOffsetMilliseconds(id, padding->milliseconds());
        }

        return id;
    }

    SpriteDecal::SpriteDecal(std::unique_ptr<olc::Sprite> sprite, bool filter, bool clamp) {
        this->sprite = std::move(sprite);

        if (this->sprite != nullptr) {
            decal = std::make_unique<olc::Decal>(this->sprite.get(), filter, clamp);
        }
    }

    SpriteDecal SpriteDecal::adopt(olc::Decal* decal) {
        SpriteDecal result;

        if (decal != nullptr) {
            result.sprite.reset(decal->sprite);
            result.decal.reset(decal);
        }

        return result;
    }

    olc::Decal* SpriteDecal::get() const {
        return decal.get();
    }

    olc::Sprite* SpriteDecal::get_sprite() const {
        return sprite.get();
    }

    SpriteDecal::operator bool() const {
        return decal != nullptr;
    }

    void SpriteDecal::reset() {
        decal.reset();
        sprite.reset();
    }
}

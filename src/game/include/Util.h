#pragma once

#include "olcPixelGameEngine.h"

#include <filesystem>
#include <memory>

namespace olc {
    class MiniAudio;
}

namespace nascent {
    float smoothstep(double begin, double end, double t);
    int load_song_audio(olc::MiniAudio& audio, const std::filesystem::path& path);

    inline int positive_modulo(int i, int n) {
        return (i % n + n) % n;
    }

    class SpriteDecal {
        protected:
        std::unique_ptr<olc::Sprite> sprite;
        std::unique_ptr<olc::Decal> decal;

        public:
        SpriteDecal() = default;
        explicit SpriteDecal(std::unique_ptr<olc::Sprite> sprite, bool filter = false);

        static SpriteDecal adopt(olc::Decal* decal);

        olc::Decal* get() const;
        olc::Sprite* get_sprite() const;
        explicit operator bool() const;
        void reset();
    };
}

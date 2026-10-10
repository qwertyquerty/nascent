#pragma once

#include "olcPixelGameEngine.h"

#include <memory>

namespace nascent {
    float smoothstep(double begin, double end, double t);

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

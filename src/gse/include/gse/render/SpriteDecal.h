#pragma once

#include "olcPixelGameEngine.h"

#include <memory>

namespace nascent {
    class SpriteDecal {
        protected:
        std::unique_ptr<olc::Sprite> sprite;
        std::unique_ptr<olc::Decal> decal;

        public:
        SpriteDecal() = default;
        explicit SpriteDecal(std::unique_ptr<olc::Sprite> sprite, bool filter = false, bool clamp = true);

        static SpriteDecal adopt(olc::Decal* decal);

        olc::Decal* get() const;
        olc::Sprite* get_sprite() const;
        explicit operator bool() const;
        void reset();
    };
}

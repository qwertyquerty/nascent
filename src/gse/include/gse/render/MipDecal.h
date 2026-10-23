#pragma once

#include "gse/render/SpriteDecal.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace nascent {
    std::unique_ptr<olc::Sprite> downsample_half(const olc::Sprite& source);

    class MipDecal {
        protected:
        mutable std::vector<SpriteDecal> levels;
        mutable std::vector<std::unique_ptr<olc::Sprite>> pending;
        bool clamp = true;

        const olc::Sprite* sprite_at(size_t level) const;
        olc::Decal* decal_at(size_t level) const;

        public:
        struct Pick {
            olc::Decal* decal = nullptr;
            float factor = 1.0f;
        };

        static constexpr int32_t SMALLEST_LEVEL = 2;

        static std::vector<std::unique_ptr<olc::Sprite>> build_levels(std::unique_ptr<olc::Sprite> sprite);

        MipDecal() = default;
        explicit MipDecal(std::unique_ptr<olc::Sprite> sprite, bool clamp = true);
        explicit MipDecal(std::vector<std::unique_ptr<olc::Sprite>> sprites, bool clamp = true, bool lazy = false);

        Pick pick(float scale) const;
        olc::Decal* get() const;
        olc::Sprite* base() const;
        olc::vf2d size() const;
        explicit operator bool() const;
        void reset();
    };
}

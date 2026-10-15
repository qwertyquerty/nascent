#pragma once

#include "gse/render/SpriteDecal.h"
#include "olcPixelGameEngine.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace nascent {
    std::vector<olc::vf2d> star_points(olc::vf2d center, float outer_radius, float inner_radius, int points = 5);
    std::unique_ptr<olc::Sprite> rasterize_polygon(const std::vector<olc::vf2d>& points, olc::vi2d size, int32_t supersample = 4);
    SpriteDecal render_polygon(const std::vector<olc::vf2d>& points, olc::vi2d size, int32_t supersample = 4);

    class StarSprite {
        protected:
        SpriteDecal sprite;
        int32_t size = 0;
        float inner_ratio;

        public:
        explicit StarSprite(float inner_ratio = 0.45f);

        void draw(olc::PixelGameEngine* window, olc::vf2d center, float radius, olc::Pixel color);
    };
}

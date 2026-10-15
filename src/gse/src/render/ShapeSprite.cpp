#include "gse/render/ShapeSprite.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace nascent {
    std::vector<olc::vf2d> star_points(olc::vf2d center, float outer_radius, float inner_radius, int points) {
        std::vector<olc::vf2d> vertices;

        for (int index = 0; index < points * 2; index++) {
            double angle = -std::numbers::pi / 2 + index * std::numbers::pi / points;
            float distance = index % 2 == 0 ? outer_radius : inner_radius;
            vertices.push_back(center + olc::vf2d{(float)std::cos(angle), (float)std::sin(angle)} * distance);
        }

        return vertices;
    }

    static bool inside(const std::vector<olc::vf2d>& points, olc::vf2d point) {
        bool crossing = false;

        for (size_t index = 0, previous = points.size() - 1; index < points.size(); previous = index++) {
            const olc::vf2d& a = points[index];
            const olc::vf2d& b = points[previous];

            if ((a.y > point.y) != (b.y > point.y) && point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
                crossing = !crossing;
            }
        }

        return crossing;
    }

    std::unique_ptr<olc::Sprite> rasterize_polygon(const std::vector<olc::vf2d>& points, olc::vi2d size, int32_t supersample) {
        auto sprite = std::make_unique<olc::Sprite>(std::max(1, size.x), std::max(1, size.y));
        supersample = std::max(1, supersample);
        float step = 1.0f / supersample;

        for (int32_t y = 0; y < sprite->height; y++) {
            for (int32_t x = 0; x < sprite->width; x++) {
                int32_t covered = 0;

                for (int32_t sy = 0; sy < supersample; sy++) {
                    for (int32_t sx = 0; sx < supersample; sx++) {
                        covered += inside(points, {x + (sx + 0.5f) * step, y + (sy + 0.5f) * step}) ? 1 : 0;
                    }
                }

                sprite->SetPixel(x, y, olc::Pixel(255, 255, 255, (uint8_t)(covered * 255 / (supersample * supersample))));
            }
        }

        return sprite;
    }

    SpriteDecal render_polygon(const std::vector<olc::vf2d>& points, olc::vi2d size, int32_t supersample) {
        return SpriteDecal(rasterize_polygon(points, size, supersample));
    }

    StarSprite::StarSprite(float inner_ratio) : inner_ratio(inner_ratio) {}

    void StarSprite::draw(olc::PixelGameEngine* window, olc::vf2d center, float radius, olc::Pixel color) {
        int32_t wanted = std::max(1, (int32_t)std::ceil(radius * 2.0f));

        if (wanted != size || !sprite) {
            float half = wanted / 2.0f;
            sprite = render_polygon(star_points({half, half}, half, half * inner_ratio), {wanted, wanted});
            size = wanted;
        }

        olc::vf2d corner = {std::round(center.x - size / 2.0f), std::round(center.y - size / 2.0f)};
        window->DrawDecal(corner, sprite.get(), {1.0f, 1.0f}, color);
    }
}

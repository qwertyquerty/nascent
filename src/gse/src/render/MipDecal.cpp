#include "gse/render/MipDecal.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace nascent {
    std::unique_ptr<olc::Sprite> downsample_half(const olc::Sprite& source) {
        int32_t width = std::max(1, (source.width + 1) / 2);
        int32_t height = std::max(1, (source.height + 1) / 2);
        auto result = std::make_unique<olc::Sprite>(width, height);
        const olc::Pixel* pixels = source.pColData.data();
        olc::Pixel* output = result->GetData();

        for (int32_t y = 0; y < height; y++) {
            const olc::Pixel* rows[2] = {pixels + (size_t)std::min(source.height - 1, y * 2) * source.width, pixels + (size_t)std::min(source.height - 1, y * 2 + 1) * source.width};

            for (int32_t x = 0; x < width; x++) {
                int32_t columns[2] = {std::min(source.width - 1, x * 2), std::min(source.width - 1, x * 2 + 1)};
                uint32_t alpha = 0;
                uint32_t red = 0;
                uint32_t green = 0;
                uint32_t blue = 0;

                for (const olc::Pixel* row : rows) {
                    for (int32_t column : columns) {
                        olc::Pixel pixel = row[column];
                        alpha += pixel.a;
                        red += pixel.r * pixel.a;
                        green += pixel.g * pixel.a;
                        blue += pixel.b * pixel.a;
                    }
                }

                olc::Pixel averaged(0, 0, 0, (uint8_t)((alpha + 2) / 4));

                if (alpha > 0) {
                    averaged.r = (uint8_t)((red + alpha / 2) / alpha);
                    averaged.g = (uint8_t)((green + alpha / 2) / alpha);
                    averaged.b = (uint8_t)((blue + alpha / 2) / alpha);
                }

                output[(size_t)y * width + x] = averaged;
            }
        }

        return result;
    }

    std::vector<std::unique_ptr<olc::Sprite>> MipDecal::build_levels(std::unique_ptr<olc::Sprite> sprite) {
        std::vector<std::unique_ptr<olc::Sprite>> sprites;

        if (sprite == nullptr || sprite->width <= 0 || sprite->height <= 0) {
            return sprites;
        }

        sprites.push_back(std::move(sprite));

        while (sprites.back()->width > SMALLEST_LEVEL && sprites.back()->height > SMALLEST_LEVEL) {
            sprites.push_back(downsample_half(*sprites.back()));
        }

        return sprites;
    }

    MipDecal::MipDecal(std::unique_ptr<olc::Sprite> sprite, bool clamp) : MipDecal(build_levels(std::move(sprite)), clamp) {}

    MipDecal::MipDecal(std::vector<std::unique_ptr<olc::Sprite>> sprites, bool clamp, bool lazy) : clamp(clamp) {
        levels.resize(sprites.size());
        pending.resize(sprites.size());

        for (size_t level = 0; level < sprites.size(); level++) {
            if (lazy) {
                pending[level] = std::move(sprites[level]);
            }
            else {
                levels[level] = SpriteDecal(std::move(sprites[level]), true, clamp);
            }
        }
    }

    const olc::Sprite* MipDecal::sprite_at(size_t level) const {
        return levels[level] ? levels[level].get_sprite() : pending[level].get();
    }

    olc::Decal* MipDecal::decal_at(size_t level) const {
        if (!levels[level] && pending[level] != nullptr) {
            levels[level] = SpriteDecal(std::move(pending[level]), true, clamp);
        }

        return levels[level].get();
    }

    MipDecal::Pick MipDecal::pick(float scale) const {
        if (levels.empty()) {
            return {};
        }

        size_t level = 0;

        if (scale > 0) {
            level = (size_t)std::clamp((int)std::floor(std::log2(1.0f / scale)), 0, (int)levels.size() - 1);
        }

        const olc::Sprite* chosen = sprite_at(level);
        return {decal_at(level), (float)chosen->width / (float)sprite_at(0)->width};
    }

    olc::Decal* MipDecal::get() const {
        return levels.empty() ? nullptr : decal_at(0);
    }

    olc::Sprite* MipDecal::base() const {
        return levels.empty() ? nullptr : const_cast<olc::Sprite*>(sprite_at(0));
    }

    olc::vf2d MipDecal::size() const {
        olc::Sprite* sprite = base();
        return sprite == nullptr ? olc::vf2d{0.0f, 0.0f} : olc::vf2d{(float)sprite->width, (float)sprite->height};
    }

    MipDecal::operator bool() const {
        return !levels.empty();
    }

    void MipDecal::reset() {
        levels.clear();
        pending.clear();
    }
}

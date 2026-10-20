#include "gse/render/MipDecal.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace nascent {
    std::unique_ptr<olc::Sprite> downsample_half(const olc::Sprite& source) {
        int32_t width = std::max(1, (source.width + 1) / 2);
        int32_t height = std::max(1, (source.height + 1) / 2);
        auto result = std::make_unique<olc::Sprite>(width, height);

        for (int32_t y = 0; y < height; y++) {
            for (int32_t x = 0; x < width; x++) {
                uint32_t alpha = 0;
                uint32_t red = 0;
                uint32_t green = 0;
                uint32_t blue = 0;

                for (int32_t dy = 0; dy < 2; dy++) {
                    for (int32_t dx = 0; dx < 2; dx++) {
                        olc::Pixel pixel = source.GetPixel(std::min(source.width - 1, x * 2 + dx), std::min(source.height - 1, y * 2 + dy));
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

                result->SetPixel(x, y, averaged);
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

    MipDecal::MipDecal(std::vector<std::unique_ptr<olc::Sprite>> sprites, bool clamp) {
        for (std::unique_ptr<olc::Sprite>& sprite : sprites) {
            levels.emplace_back(std::move(sprite), true, clamp);
        }
    }

    MipDecal::Pick MipDecal::pick(float scale) const {
        if (levels.empty()) {
            return {};
        }

        size_t level = 0;

        if (scale > 0) {
            level = (size_t)std::clamp((int)std::floor(std::log2(1.0f / scale)), 0, (int)levels.size() - 1);
        }

        const olc::Sprite* chosen = levels[level].get_sprite();
        return {levels[level].get(), (float)chosen->width / (float)levels.front().get_sprite()->width};
    }

    olc::Decal* MipDecal::get() const {
        return levels.empty() ? nullptr : levels.front().get();
    }

    olc::Sprite* MipDecal::base() const {
        return levels.empty() ? nullptr : levels.front().get_sprite();
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
    }
}

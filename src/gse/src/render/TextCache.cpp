#include "gse/render/TextCache.h"

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace nascent {
    struct TextEngine : public olc::PGEX {
        static olc::PixelGameEngine* get() {
            return pge;
        }
    };

    struct TextEntry {
        std::unique_ptr<olc::Sprite> sprite;
        std::unique_ptr<olc::Decal> decal;
        olc::FontRect bounds;
        uint64_t last_used = 0;
        bool rendered = false;
    };

    struct TextHash {
        using is_transparent = void;

        size_t operator()(std::string_view text) const {
            return std::hash<std::string_view>{}(text);
        }
    };

    using TextEntries = std::unordered_map<std::string, TextEntry, TextHash, std::equal_to<>>;

    static std::unordered_map<const olc::Font*, TextEntries> text_cache;
    static uint64_t text_cache_frame = 0;

    static TextEntry& text_entry(olc::Font& font, std::string_view text) {
        TextEntries& entries = text_cache[&font];
        auto found = entries.find(text);

        if (found == entries.end()) {
            found = entries.emplace(std::string(text), TextEntry{}).first;
            found->second.bounds = font.GetStringBounds(found->first);
        }

        found->second.last_used = text_cache_frame;
        return found->second;
    }

    void draw_text(olc::Font& font, std::string_view text, olc::vi2d baseline, olc::Pixel color) {
        draw_text(font, text, olc::vf2d{(float)baseline.x, (float)baseline.y}, color, 1.0f);
    }

    void draw_text(olc::Font& font, std::string_view text, olc::vf2d baseline, olc::Pixel color, float scale) {
        if (text.empty() || scale <= 0.0f) {
            return;
        }

        TextEntry& entry = text_entry(font, text);

        if (!entry.rendered) {
            entry.rendered = true;

            if (entry.bounds.size.x > 0 && entry.bounds.size.y > 0) {
                entry.sprite.reset(font.RenderStringToSprite(std::string(text), olc::WHITE));

                if (entry.sprite != nullptr) {
                    entry.decal = std::make_unique<olc::Decal>(entry.sprite.get(), true);
                }
            }
        }

        if (entry.decal == nullptr) {
            return;
        }

        olc::vf2d corner = {baseline.x + entry.bounds.offset.x * scale, baseline.y + entry.bounds.offset.y * scale};

        if (scale == 1.0f) {
            corner = {std::round(corner.x), std::round(corner.y)};
        }

        TextEngine::get()->DrawDecal(corner, entry.decal.get(), {scale, scale}, color);
    }

    void draw_text(olc::Font& font, std::string_view text, int32_t x, int32_t y, olc::Pixel color) {
        draw_text(font, text, olc::vi2d{x, y}, color);
    }

    olc::FontRect text_bounds(olc::Font& font, std::string_view text) {
        if (text.empty()) {
            return olc::FontRect{{0, 0}, {0, 0}};
        }

        return text_entry(font, text).bounds;
    }

    SpriteDecal render_text_decal(olc::Font& font, std::string_view text, olc::Pixel color) {
        if (text.empty()) {
            return SpriteDecal();
        }

        return SpriteDecal(std::unique_ptr<olc::Sprite>(font.RenderStringToSprite(std::string(text), color)), true);
    }

    void text_cache_begin_frame() {
        text_cache_frame++;

        if (text_cache_frame % TEXT_CACHE_SWEEP_FRAMES != 0) {
            return;
        }

        for (auto& [font, entries] : text_cache) {
            std::erase_if(entries, [](const auto& entry) {
                return entry.second.last_used + TEXT_CACHE_EVICT_FRAMES < text_cache_frame;
            });
        }
    }

    void text_cache_forget(const olc::Font& font) {
        text_cache.erase(&font);
    }

    void text_cache_clear() {
        text_cache.clear();
    }
}

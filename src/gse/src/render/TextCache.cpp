#include "gse/render/TextCache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

    static constexpr std::u32string_view GLYPH_CHARACTERS = U"0123456789.,+-%:/± ";
    static constexpr int32_t GLYPH_PADDING = 1;
    static constexpr FT_Pos GLYPH_PROBE_X = 64 * 3;
    static constexpr FT_Pos GLYPH_PROBE_Y = 64 * 2;

    struct GlyphMetrics {
        bool usable = false;
        bool moves = true;
        FT_BBox box = {0, 0, 0, 0};
        FT_Pos advance = 0;
    };

    struct GlyphImage {
        bool rendered = false;
        std::unique_ptr<olc::Sprite> sprite;
        std::unique_ptr<olc::Decal> decal;
        olc::vi2d origin = {0, 0};
    };

    struct FontGlyphs {
        std::array<std::optional<GlyphMetrics>, 128> ascii;
        std::unordered_map<char32_t, GlyphMetrics> other;
        std::array<GlyphImage, GLYPH_CHARACTERS.size()> images;
    };

    struct TextPiece {
        size_t begin = 0;
        size_t end = 0;
        int32_t pen = 0;
        char32_t glyph = 0;
        size_t image = 0;
        bool single = false;
    };

    static std::unordered_map<const olc::Font*, TextEntries> text_cache;
    static std::unordered_map<const olc::Font*, FontGlyphs> glyph_cache;
    static std::vector<TextPiece> text_pieces;
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

    static void render_entry(olc::Font& font, std::string_view text, TextEntry& entry) {
        if (entry.rendered) {
            return;
        }

        entry.rendered = true;

        if (entry.bounds.size.x > 0 && entry.bounds.size.y > 0) {
            entry.sprite.reset(font.RenderStringToSprite(std::string(text), olc::WHITE));

            if (entry.sprite != nullptr) {
                entry.decal = std::make_unique<olc::Decal>(entry.sprite.get(), true);
            }
        }
    }

    static bool next_codepoint(std::string_view text, size_t& index, char32_t& codepoint) {
        static constexpr std::array<std::pair<unsigned char, unsigned char>, 4> LEADS = {{{0x80, 0x00}, {0xE0, 0xC0}, {0xF0, 0xE0}, {0xF8, 0xF0}}};
        unsigned char lead = (unsigned char)text[index];
        size_t length = 0;

        while (length < LEADS.size() && (lead & LEADS[length].first) != LEADS[length].second) {
            length++;
        }

        length++;

        if (length > LEADS.size() || index + length > text.size()) {
            return false;
        }

        codepoint = lead & (unsigned char)~LEADS[length - 1].first;

        for (size_t offset = 1; offset < length; offset++) {
            unsigned char continuation = (unsigned char)text[index + offset];

            if ((continuation & 0xC0) != 0x80) {
                return false;
            }

            codepoint = (codepoint << 6) | (continuation & 0x3F);
        }

        index += length;
        return true;
    }

    static GlyphMetrics measure_glyph(olc::Font& font, char32_t codepoint) {
        GlyphMetrics metrics;
        FT_BBox still;
        FT_BBox moved;
        FT_Vector advance;
        FT_Vector moved_advance;

        if (!font.GetGlyphMetrics(codepoint, {0, 0}, still, advance) || !font.GetGlyphMetrics(codepoint, {GLYPH_PROBE_X, GLYPH_PROBE_Y}, moved, moved_advance)) {
            return metrics;
        }

        if (advance.x != moved_advance.x || advance.y != 0 || moved_advance.y != 0 || advance.x % 64 != 0) {
            return metrics;
        }

        bool shifts = moved.xMin == still.xMin + GLYPH_PROBE_X && moved.xMax == still.xMax + GLYPH_PROBE_X && moved.yMin == still.yMin + GLYPH_PROBE_Y && moved.yMax == still.yMax + GLYPH_PROBE_Y;
        bool empty = still.xMin == 0 && still.xMax == 0 && still.yMin == 0 && still.yMax == 0 && moved.xMin == 0 && moved.xMax == 0 && moved.yMin == 0 && moved.yMax == 0;

        if (!shifts && !empty) {
            return metrics;
        }

        metrics.usable = true;
        metrics.moves = shifts;
        metrics.box = still;
        metrics.advance = advance.x;
        return metrics;
    }

    static const GlyphMetrics& glyph_metrics(olc::Font& font, FontGlyphs& glyphs, char32_t codepoint) {
        if (codepoint < glyphs.ascii.size()) {
            std::optional<GlyphMetrics>& known = glyphs.ascii[codepoint];

            if (!known) {
                known = measure_glyph(font, codepoint);
            }

            return *known;
        }

        auto found = glyphs.other.find(codepoint);

        if (found == glyphs.other.end()) {
            found = glyphs.other.emplace(codepoint, measure_glyph(font, codepoint)).first;
        }

        return found->second;
    }

    static GlyphImage& glyph_image(olc::Font& font, FontGlyphs& glyphs, const TextPiece& piece) {
        GlyphImage& image = glyphs.images[piece.image];

        if (!image.rendered) {
            image.rendered = true;
            image.sprite.reset(font.RenderGlyphToSprite(piece.glyph, GLYPH_PADDING, image.origin));

            if (image.sprite != nullptr) {
                image.decal = std::make_unique<olc::Decal>(image.sprite.get(), true);
            }
        }

        return image;
    }

    static FontGlyphs* layout_glyphs(olc::Font& font, std::string_view text, olc::FontRect& bounds) {
        if (std::none_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            return nullptr;
        }

        FontGlyphs& glyphs = glyph_cache[&font];
        FT_Pos pen = 0;
        FT_Pos min_x = std::numeric_limits<int>::max();
        FT_Pos min_y = std::numeric_limits<int>::max();
        FT_Pos max_x = std::numeric_limits<int>::min();
        FT_Pos max_y = std::numeric_limits<int>::min();
        std::optional<size_t> run;
        FT_Pos run_reach = 0;
        FT_Pos run_end = 0;
        size_t index = 0;
        text_pieces.clear();

        while (index < text.size()) {
            size_t begin = index;
            char32_t codepoint = 0;

            if (!next_codepoint(text, index, codepoint)) {
                return nullptr;
            }

            const GlyphMetrics& metrics = glyph_metrics(font, glyphs, codepoint);

            if (!metrics.usable) {
                return nullptr;
            }

            FT_BBox box = metrics.box;

            if (metrics.moves) {
                box.xMin += pen;
                box.xMax += pen;
            }

            FT_Pos start = pen;
            pen += metrics.advance;
            min_x = std::min(min_x, box.xMin);
            max_x = std::max(max_x, pen);
            min_y = std::min(min_y, box.yMin);
            max_y = std::max(max_y, box.yMax);

            size_t image = GLYPH_CHARACTERS.find(codepoint);

            if (image != std::u32string_view::npos) {
                if (run && run_reach > run_end) {
                    return nullptr;
                }

                run.reset();
                text_pieces.push_back({begin, index, (int32_t)(start / 64), codepoint, image, true});
                continue;
            }

            if (!run) {
                run = text_pieces.size();
                run_reach = box.xMax;
                run_end = pen;
                text_pieces.push_back({begin, index, (int32_t)(start / 64), 0, 0, false});
                continue;
            }

            run_reach = std::max(run_reach, box.xMax);
            run_end = std::max(run_end, pen);
            text_pieces[*run].end = index;
        }

        if (run && run_reach > run_end) {
            return nullptr;
        }

        min_x /= 64;
        min_y /= 64;
        max_x = (FT_Pos)std::ceil((double)max_x / 64);
        max_y = (FT_Pos)std::ceil((double)max_y / 64);
        bounds = olc::FontRect{{(int32_t)min_x, (int32_t)-max_y}, {(int32_t)(max_x - min_x), (int32_t)(max_y - min_y)}};
        return &glyphs;
    }

    static olc::vf2d text_corner(olc::vf2d baseline, olc::FontRect bounds, float scale, bool snap) {
        olc::vf2d corner = {baseline.x + bounds.offset.x * scale, baseline.y + bounds.offset.y * scale};

        if (snap && scale == 1.0f) {
            corner = {std::round(corner.x), std::round(corner.y)};
        }

        return corner;
    }

    static bool draw_glyphs(olc::Font& font, std::string_view text, olc::vf2d baseline, olc::Pixel color, float scale, bool snap) {
        olc::FontRect bounds;
        FontGlyphs* glyphs = layout_glyphs(font, text, bounds);

        if (glyphs == nullptr) {
            return false;
        }

        if (bounds.size.x <= 0 || bounds.size.y <= 0) {
            return true;
        }

        olc::vf2d corner = text_corner(baseline, bounds, scale, snap);

        for (const TextPiece& piece : text_pieces) {
            if (!piece.single) {
                std::string_view run = text.substr(piece.begin, piece.end - piece.begin);
                TextEntry& entry = text_entry(font, run);
                render_entry(font, run, entry);

                if (entry.decal != nullptr) {
                    olc::vf2d at = {(float)(piece.pen + entry.bounds.offset.x - bounds.offset.x), (float)(entry.bounds.offset.y - bounds.offset.y)};
                    TextEngine::get()->DrawDecal(corner + at * scale, entry.decal.get(), {scale, scale}, color);
                }

                continue;
            }

            GlyphImage& image = glyph_image(font, *glyphs, piece);

            if (image.decal == nullptr) {
                continue;
            }

            olc::vi2d at = {piece.pen + image.origin.x - bounds.offset.x, image.origin.y - bounds.offset.y};
            olc::vi2d inner = {image.sprite->width - GLYPH_PADDING * 2, image.sprite->height - GLYPH_PADDING * 2};
            int32_t left = at.x < 0 ? GLYPH_PADDING - at.x : 0;
            int32_t top = at.y < 0 ? GLYPH_PADDING - at.y : 0;
            int32_t right = at.x + inner.x > bounds.size.x ? GLYPH_PADDING + bounds.size.x - at.x : image.sprite->width;
            int32_t bottom = at.y + inner.y > bounds.size.y ? GLYPH_PADDING + bounds.size.y - at.y : image.sprite->height;

            if (right <= left || bottom <= top) {
                continue;
            }

            olc::vf2d position = corner + olc::vf2d{(float)(at.x - GLYPH_PADDING + left), (float)(at.y - GLYPH_PADDING + top)} * scale;
            TextEngine::get()->DrawPartialDecal(position, image.decal.get(), {(float)left, (float)top}, {(float)(right - left), (float)(bottom - top)}, {scale, scale}, color);
        }

        return true;
    }

    void draw_text(olc::Font& font, std::string_view text, olc::vi2d baseline, olc::Pixel color) {
        draw_text(font, text, olc::vf2d{(float)baseline.x, (float)baseline.y}, color, 1.0f);
    }

    void draw_text(olc::Font& font, std::string_view text, olc::vf2d baseline, olc::Pixel color, float scale) {
        draw_text(font, text, baseline, color, scale, true);
    }

    void draw_text(olc::Font& font, std::string_view text, olc::vf2d baseline, olc::Pixel color, float scale, bool snap) {
        if (text.empty() || scale <= 0.0f) {
            return;
        }

        if (draw_glyphs(font, text, baseline, color, scale, snap)) {
            return;
        }

        TextEntry& entry = text_entry(font, text);
        render_entry(font, text, entry);

        if (entry.decal == nullptr) {
            return;
        }

        TextEngine::get()->DrawDecal(text_corner(baseline, entry.bounds, scale, snap), entry.decal.get(), {scale, scale}, color);
    }

    void draw_text(olc::Font& font, std::string_view text, int32_t x, int32_t y, olc::Pixel color) {
        draw_text(font, text, olc::vi2d{x, y}, color);
    }

    olc::FontRect text_bounds(olc::Font& font, std::string_view text) {
        if (text.empty()) {
            return olc::FontRect{{0, 0}, {0, 0}};
        }

        olc::FontRect bounds;

        if (layout_glyphs(font, text, bounds) != nullptr) {
            return bounds;
        }

        return text_entry(font, text).bounds;
    }

    SpriteDecal render_text_decal(olc::Font& font, std::string_view text, olc::Pixel color) {
        if (text.empty()) {
            return SpriteDecal();
        }

        return SpriteDecal(std::unique_ptr<olc::Sprite>(font.RenderStringToSprite(std::string(text), color)), true);
    }

    std::string fit_text(olc::Font& font, std::string text, int32_t max_width) {
        auto width = [&font](const std::string& value) {
            return value.empty() ? 0 : text_bounds(font, value).size.x;
        };

        if (width(text) <= max_width) {
            return text;
        }

        while (!text.empty() && width(text + "...") > max_width) {
            text.pop_back();

            while (!text.empty() && ((unsigned char)text.back() & 0xc0) == 0x80) {
                text.pop_back();
            }

            if (!text.empty() && (unsigned char)text.back() >= 0xc0) {
                text.pop_back();
            }
        }

        return text + "...";
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
        glyph_cache.erase(&font);
    }

    void text_cache_clear() {
        text_cache.clear();
        glyph_cache.clear();
    }
}

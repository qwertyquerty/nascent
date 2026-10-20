#include "ChangelogOverlay.h"

#include "Skin.h"
#include "gse/Game.h"
#include "gse/render/Color.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace nascent {
    static std::string spaced_caps(std::string_view text) {
        std::string spaced;

        for (char c : text) {
            if (!spaced.empty()) {
                spaced += c == ' ' ? "  " : " ";
            }

            if (c != ' ') {
                spaced += (char)std::toupper((unsigned char)c);
            }
        }

        return spaced;
    }

    static float text_height(olc::Font& font) {
        return (float)text_bounds(font, "Hgjy").size.y;
    }

    ChangelogOverlay::ChangelogOverlay(Skin* skin, std::vector<ChangelogRelease> releases, std::string title, std::string subtitle) : skin(skin), releases(std::move(releases)), title(std::move(title)), subtitle(std::move(subtitle)) {}

    olc::Font& ChangelogOverlay::font_for(LineKind kind) const {
        return kind == LineKind::RELEASE ? skin->display_font_medium : skin->display_font_small;
    }

    void ChangelogOverlay::rebuild(float width) {
        wrapped_width = width;
        lines.clear();

        float release_gap = Ui::layout_pxf("changelog.release_gap");
        float release_spacing = Ui::layout_pxf("changelog.release_spacing");
        float section_gap = Ui::layout_pxf("changelog.section_gap");
        float section_spacing = Ui::layout_pxf("changelog.section_spacing");
        float line_gap = Ui::layout_pxf("changelog.line_gap");
        float item_gap = Ui::layout_pxf("changelog.item_gap");
        float item_width = width - Ui::layout_pxf("changelog.bullet_indent");
        float small_height = text_height(skin->display_font_small);
        float medium_height = text_height(skin->display_font_medium);
        float y = 0;

        for (size_t index = 0; index < releases.size(); index++) {
            const ChangelogRelease& release = releases[index];
            y += index > 0 ? release_spacing : 0.0f;
            lines.push_back({LineKind::RELEASE, release.version, y});
            y += medium_height + release_gap;

            for (const ChangelogSection& section : release.sections) {
                if (!section.title.empty()) {
                    y += section_spacing;
                    lines.push_back({LineKind::SECTION, spaced_caps(section.title), y});
                    y += small_height + section_gap;
                }

                for (const std::string& item : section.notes) {
                    std::vector<std::string> wrapped = wrap_text(skin->display_font_small, item, item_width);

                    for (size_t row = 0; row < wrapped.size(); row++) {
                        lines.push_back({row == 0 ? LineKind::ITEM : LineKind::CONTINUATION, std::move(wrapped[row]), y});
                        y += small_height + line_gap;
                    }

                    y += item_gap;
                }
            }
        }

        if (releases.empty()) {
            lines.push_back({LineKind::ITEM, EMPTY_TEXT, y});
            y += small_height;
        }

        content_height = y;
        scroll = std::clamp(scroll, 0.0f, max_scroll());
    }

    float ChangelogOverlay::max_scroll() const {
        return std::max(0.0f, content_height - view_height);
    }

    void ChangelogOverlay::update(float elapsed_time) {
        Input& input = game->input();

        if (spots.take_click(input) || input.take(actions::BACK) || input.take(actions::CONFIRM)) {
            game->audio().play_effect(skin->get_random_menu_pluck());
            close();
            return;
        }

        if (std::optional<float> position = scrollbar.update(input, max_scroll() > 0 ? scroll / max_scroll() : 0.0f)) {
            scroll = *position * max_scroll();
            return;
        }

        int32_t wheel = input.mouse_wheel();
        float speed = Ui::layout_pxf("changelog.scroll_speed");
        scroll -= (float)((wheel > 0) - (wheel < 0)) * Ui::layout_pxf("changelog.scroll_step");
        scroll += ((input.held(actions::DOWN) ? 1.0f : 0.0f) - (input.held(actions::UP) ? 1.0f : 0.0f)) * speed * elapsed_time;
        scroll = std::clamp(scroll, 0.0f, max_scroll());
    }

    olc::Pixel ChangelogOverlay::backdrop() const {
        return skin->color(SkinColor::PAUSE_OVERLAY);
    }

    void ChangelogOverlay::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        float padding = Ui::layout_pxf("changelog.padding");
        float width = std::min(Ui::layout_pxf("changelog.width"), (float)screen.x - padding * 2.0f);
        float top = (float)screen.y * (float)Ui::layout("changelog.top_fraction");
        float bottom = (float)screen.y * (1.0f - (float)Ui::layout("changelog.bottom_fraction"));
        float left = ((float)screen.x - width) / 2.0f;
        window->FillRectDecal({left, top}, {width, bottom - top}, skin->color(SkinColor::MENU_BACKGROUND));

        float text_left = left + padding;
        float text_width = width - padding * 2.0f;

        if (text_width != wrapped_width) {
            rebuild(text_width);
        }

        olc::FontRect large_metrics = text_bounds(skin->display_font_large, "Hgjy");
        olc::FontRect small_metrics = text_bounds(skin->display_font_small, "Hgjy");
        float title_top = top + padding;
        draw_text(skin->display_font_large, title, {(int32_t)text_left, (int32_t)(title_top - large_metrics.offset.y)}, skin->color(SkinColor::TEXT));
        float subtitle_top = title_top + (float)large_metrics.size.y + Ui::layout_pxf("changelog.subtitle_gap");
        draw_text(skin->display_font_small, subtitle, {(int32_t)text_left, (int32_t)(subtitle_top - small_metrics.offset.y)}, skin->color(SkinColor::TEXT_MUTED));

        float view_top = subtitle_top + (float)small_metrics.size.y + Ui::layout_pxf("changelog.header_gap");
        float footer_top = bottom - padding - (float)small_metrics.size.y;
        float view_bottom = footer_top - Ui::layout_pxf("changelog.footer_gap");
        view_height = std::max(0.0f, view_bottom - view_top);
        scroll = std::clamp(scroll, 0.0f, max_scroll());

        float edge = std::max(1.0f, Ui::layout_pxf("changelog.edge_fade"));
        float bullet = std::max(1.0f, Ui::layout_pxf("changelog.bullet_size"));
        float indent = Ui::layout_pxf("changelog.bullet_indent");

        for (size_t index = 0; index < lines.size(); index++) {
            const Line& line = lines[index];
            olc::Font& font = font_for(line.kind);
            olc::FontRect metrics = text_bounds(font, "Hgjy");
            float line_top = view_top + line.y - scroll;
            float line_bottom = line_top + (float)metrics.size.y;

            if (line_top < view_top || line_bottom > view_bottom) {
                continue;
            }

            float opacity = std::clamp(std::min(line_top - view_top, view_bottom - line_bottom) / edge, 0.0f, 1.0f);
            float x = line.kind == LineKind::ITEM || line.kind == LineKind::CONTINUATION ? text_left + indent : text_left;
            olc::Pixel color = skin->color(line.kind == LineKind::SECTION ? SkinColor::TEXT_MUTED : SkinColor::TEXT);

            if (line.kind == LineKind::RELEASE) {
                color = skin->accent(0.35);
            }

            if (line.kind == LineKind::ITEM && !releases.empty()) {
                window->FillRectDecal({text_left, line_top + ((float)metrics.size.y - bullet) / 2.0f}, {bullet, bullet}, fade(skin->accent(0.35), opacity));
            }

            draw_text(font, line.text, {(int32_t)x, (int32_t)(line_top - metrics.offset.y)}, fade(color, opacity));
        }

        if (max_scroll() > 0.0f) {
            float track_width = std::max(1.0f, Ui::layout_pxf("changelog.scrollbar_width"));
            float track_x = left + width - padding / 2.0f - track_width / 2.0f;
            scrollbar.layout({{track_x, view_top}, {track_width, view_height}}, content_height > 0 ? view_height / content_height : 1.0f);
            scrollbar.draw(window, scroll / max_scroll(), skin->color(SkinColor::SCROLLBAR_TRACK), skin->color(SkinColor::SCROLLBAR_THUMB));
        }
        else {
            scrollbar.hide();
        }

        std::vector<Hint> hints = {{CLOSE_HINT, 0}};
        float hint_gap = Ui::layout_pxf("changelog.hint_gap");
        float hint_x = left + width - padding - hints_width(skin->display_font_small, hints, hint_gap);
        spots.clear();
        draw_hints(window, skin->display_font_small, hints, {hint_x, footer_top - small_metrics.offset.y}, hint_gap, skin->color(SkinColor::TEXT_MUTED), skin->color(SkinColor::TEXT), spots);
    }
}

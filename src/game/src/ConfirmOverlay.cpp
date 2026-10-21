#include "ConfirmOverlay.h"

#include "Skin.h"
#include "gse/Game.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace nascent {
    ConfirmOverlay::ConfirmOverlay(Skin* skin, std::string title, std::vector<std::string> lines, std::string confirm_label, std::function<void()> on_confirm)
        : skin(skin), title(std::move(title)), lines(std::move(lines)), items{std::move(confirm_label), "Cancel"}, on_confirm(std::move(on_confirm)) {}

    olc::Pixel ConfirmOverlay::backdrop() const {
        return skin->color(SkinColor::MENU_DIM);
    }

    void ConfirmOverlay::on_open() {
        last_mouse = game->input().mouse_position();
    }

    void ConfirmOverlay::choose(size_t choice) {
        close();

        if (choice == CONFIRM) {
            game->audio().play_effect(skin->get_random_menu_pluck());

            if (on_confirm) {
                on_confirm();
            }
        }
        else {
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
        }
    }

    void ConfirmOverlay::update(float) {
        Input& input = game->input();
        olc::vi2d mouse = input.mouse_position();
        bool mouse_moved = mouse != last_mouse;
        last_mouse = mouse;

        if (input.take(actions::BACK) || input.take_click(1) || (input.clicked(0) && !panel.contains(mouse) && input.take_click(0))) {
            choose(CANCEL);
            return;
        }

        if (std::optional<int32_t> hovered = spots.at(mouse); hovered && mouse_moved && (size_t)*hovered != selected) {
            selected = (size_t)*hovered;
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
        }

        if (std::optional<int32_t> clicked = spots.take_click(input)) {
            choose((size_t)*clicked);
            return;
        }

        bool down = input.take(actions::DOWN);

        if (down || input.take(actions::UP)) {
            selected = (selected + (down ? 1 : items.size() - 1)) % items.size();
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
            return;
        }

        if (input.take(actions::CONFIRM)) {
            choose(selected);
        }
    }

    void ConfirmOverlay::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        olc::Font& title_font = skin->display_font_medium;
        olc::Font& row_font = skin->display_font_small;
        olc::FontRect title_metrics = text_bounds(title_font, "Hgjy");
        olc::FontRect row_metrics = text_bounds(row_font, "Hgjy");
        int32_t padding = Ui::layout_px("chart_menu.padding");
        int32_t row_height = Ui::layout_px("chart_menu.row_height");
        int32_t row_inset = Ui::layout_px("chart_menu.row_inset");
        int32_t gap = Ui::layout_px("chart_menu.gap");
        int32_t min_width = Ui::layout_px("chart_menu.min_width");
        int32_t max_width = std::max(min_width, screen.x - Ui::layout_px("chart_menu.screen_margin") * 2);
        int32_t content_width = text_bounds(title_font, title).size.x;

        for (const std::string& line : lines) {
            content_width = std::max(content_width, text_bounds(row_font, line).size.x);
        }

        for (const std::string& item : items) {
            content_width = std::max(content_width, text_bounds(row_font, item).size.x + row_inset * 2);
        }

        int32_t width = std::clamp(content_width + padding * 2, std::min(min_width, max_width), max_width);
        int32_t text_width = width - padding * 2;
        int32_t height = padding * 2 + title_metrics.size.y + gap + (row_metrics.size.y + gap) * (int32_t)lines.size() + gap + (int32_t)items.size() * row_height;
        int32_t left = (screen.x - width) / 2;
        int32_t top = (screen.y - height) / 2;
        int32_t x = left + padding;
        int32_t y = top + padding;

        window->FillRectDecal({(float)left, (float)top}, {(float)width, (float)height}, skin->color(SkinColor::MENU_BACKGROUND));
        panel = {{(float)left, (float)top}, {(float)width, (float)height}};
        spots.clear();

        draw_text(title_font, fit_text(title_font, title, text_width), {x, y - title_metrics.offset.y}, skin->color(SkinColor::TEXT));
        y += title_metrics.size.y + gap;

        for (size_t index = 0; index < lines.size(); index++) {
            draw_text(row_font, fit_text(row_font, lines[index], text_width), {x, y - row_metrics.offset.y}, skin->color(index == 0 ? SkinColor::TEXT_MUTED : SkinColor::TEXT_FAINT));
            y += row_metrics.size.y + gap;
        }

        y += gap;

        for (size_t index = 0; index < items.size(); index++) {
            bool is_selected = index == selected;
            spots.add({{(float)x, (float)y}, {(float)text_width, (float)row_height}}, (int32_t)index);

            if (is_selected) {
                window->FillRectDecal({(float)x, (float)y}, {(float)text_width, (float)row_height}, skin->color(SkinColor::SELECTION_HIGHLIGHT));
            }

            int32_t baseline = y + (row_height - row_metrics.size.y) / 2 - row_metrics.offset.y;
            draw_text(row_font, items[index], {x + row_inset, baseline}, skin->color(is_selected ? SkinColor::TEXT : SkinColor::TEXT_MUTED));
            y += row_height;
        }
    }
}

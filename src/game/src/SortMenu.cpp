#include "SortMenu.h"

#include "InputManager.h"
#include "Skin.h"
#include "gse/Game.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"

#include <utility>

namespace nascent {
    SortMenu::SortMenu(Skin* skin, ListSort current, std::function<void(ListSort)> on_choose) : skin(skin), current(current), on_choose(std::move(on_choose)), selected((size_t)current) {}

    olc::Pixel SortMenu::backdrop() const {
        return skin->color(SkinColor::MENU_DIM);
    }

    void SortMenu::on_open() {
        last_mouse = game->input().mouse_position();
    }

    void SortMenu::update(float) {
        Input& input = game->input();
        olc::vi2d mouse = input.mouse_position();
        bool mouse_moved = mouse != last_mouse;
        last_mouse = mouse;

        if (input.take(actions::BACK) || input.take(game_actions::SORT_MENU) || input.take_click(1) || (input.clicked(0) && !panel.contains(mouse) && input.take_click(0))) {
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
            close();
            return;
        }

        if (std::optional<int32_t> hovered = spots.at(mouse); hovered && mouse_moved && (size_t)*hovered != selected) {
            selected = (size_t)*hovered;
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
        }

        if (std::optional<int32_t> clicked = spots.take_click(input)) {
            selected = (size_t)*clicked;
            choose();
            return;
        }

        bool down = input.take(actions::DOWN);

        if (down || input.take(actions::UP)) {
            selected = (selected + (down ? 1 : LIST_SORT_NAMES.size() - 1)) % LIST_SORT_NAMES.size();
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
            return;
        }

        if (input.take(actions::CONFIRM)) {
            choose();
        }
    }

    void SortMenu::choose() {
        close();
        game->audio().play_effect(skin->get_random_menu_pluck());
        on_choose((ListSort)selected);
    }

    void SortMenu::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        olc::Font& title_font = skin->display_font_medium;
        olc::Font& row_font = skin->display_font_small;
        olc::FontRect title_metrics = text_bounds(title_font, "Hgjy");
        olc::FontRect row_metrics = text_bounds(row_font, "Hgjy");
        int32_t padding = Ui::px(PADDING);
        int32_t row_height = Ui::px(ROW_HEIGHT);
        int32_t row_inset = Ui::px(ROW_INSET);
        int32_t gap = Ui::px(GAP);
        int32_t width = Ui::px(WIDTH);
        int32_t height = padding * 2 + title_metrics.size.y + gap * 2 + (int32_t)LIST_SORT_NAMES.size() * row_height;
        int32_t left = (screen.x - width) / 2;
        int32_t top = (screen.y - height) / 2;
        int32_t x = left + padding;
        int32_t y = top + padding;

        window->FillRectDecal({(float)left, (float)top}, {(float)width, (float)height}, skin->color(SkinColor::MENU_BACKGROUND));
        panel = {{(float)left, (float)top}, {(float)width, (float)height}};
        spots.clear();

        draw_text(title_font, TITLE, {x, y - title_metrics.offset.y}, skin->color(SkinColor::TEXT));
        y += title_metrics.size.y + gap * 2;

        for (size_t index = 0; index < LIST_SORT_NAMES.size(); index++) {
            bool is_selected = index == selected;
            bool is_current = index == (size_t)current;
            spots.add({{(float)x, (float)y}, {(float)(width - padding * 2), (float)row_height}}, (int32_t)index);

            if (is_selected) {
                window->FillRectDecal({(float)x, (float)y}, {(float)(width - padding * 2), (float)row_height}, skin->color(SkinColor::SELECTION_HIGHLIGHT));
            }

            int32_t baseline = y + (row_height - row_metrics.size.y) / 2 - row_metrics.offset.y;
            draw_text(row_font, LIST_SORT_NAMES[index], {x + row_inset, baseline}, skin->color(is_selected ? SkinColor::TEXT : SkinColor::TEXT_MUTED));

            if (is_current) {
                int32_t marker_width = text_bounds(row_font, "Current").size.x;
                draw_text(row_font, "Current", {x + width - padding * 2 - row_inset - marker_width, baseline}, skin->color(SkinColor::POSITIVE));
            }

            y += row_height;
        }
    }
}

#include "QuitOverlay.h"

#include "Skin.h"
#include "gse/Game.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"

namespace nascent {
    QuitOverlay::QuitOverlay(Skin* skin) : skin(skin) {}

    void QuitOverlay::on_open() {
        last_mouse = game->input().mouse_position();
    }

    void QuitOverlay::choose(size_t choice) {
        if (choice == QUIT) {
            game->quit();
            return;
        }

        game->audio().play_effect(skin->get_random_menu_pluck());
        close();
    }

    void QuitOverlay::update(float) {
        Input& input = game->input();
        size_t previous = selection;

        if (input.take(actions::UP) || input.take(actions::LEFT)) {
            selection = (selection + ITEMS.size() - 1) % ITEMS.size();
        }

        if (input.take(actions::DOWN) || input.take(actions::RIGHT)) {
            selection = (selection + 1) % ITEMS.size();
        }

        olc::vi2d mouse = input.mouse_position();
        bool mouse_moved = mouse != last_mouse;
        last_mouse = mouse;

        for (size_t index = 0; index < item_bounds.size(); index++) {
            bool inside = item_bounds[index].contains(mouse);

            if (inside && mouse_moved) {
                selection = index;
            }

            if (inside && input.mouse(0).bPressed) {
                choose(index);
                return;
            }
        }

        if (selection != previous) {
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
        }

        if (input.take(actions::BACK)) {
            choose(CANCEL);
            return;
        }

        if (input.take(actions::CONFIRM)) {
            choose(selection);
        }
    }

    void QuitOverlay::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        float center_x = screen.x / 2.0f;
        window->FillRectDecal({0.0f, 0.0f}, screen, skin->color(SkinColor::PAUSE_OVERLAY));

        float y = screen.y * TITLE_TOP;
        draw_text_centered(skin->display_font_large, "Quit Nascent?", center_x, y, skin->color(SkinColor::TEXT));
        y += Ui::pxf(120);

        item_bounds.clear();
        float padding = Ui::pxf(ITEM_PADDING);

        for (size_t index = 0; index < ITEMS.size(); index++) {
            olc::Pixel color = skin->color(index == selection ? SkinColor::TEXT : SkinColor::TEXT_FAINT);
            olc::FontRect bounds = draw_text_centered(skin->display_font_medium, ITEMS[index], center_x, y, color);
            Rect item = {{center_x - bounds.size.x / 2.0f, y + bounds.offset.y}, {(float)bounds.size.x, (float)bounds.size.y}};
            item_bounds.push_back(item.inflated(padding));
            y += Ui::pxf(ITEM_SPACING);
        }

        draw_text_centered(skin->display_font_small, "↑/↓  choose        Enter  select        Esc  cancel", center_x, screen.y - Ui::pxf(HINT_BOTTOM), skin->color(SkinColor::TEXT_FAINT));
    }
}

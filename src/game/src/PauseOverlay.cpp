#include "PauseOverlay.h"

#include "Chart.h"
#include "InputManager.h"
#include "PlaySession.h"
#include "ScenePlay.h"
#include "Skin.h"
#include "gse/Game.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"

#include <algorithm>
#include <format>
#include <string>

namespace nascent {
    static std::string clock_text(double ms) {
        int64_t seconds = std::max<int64_t>(0, (int64_t)(ms / 1000.0));
        return std::format("{}:{:02}", seconds / 60, seconds % 60);
    }

    PauseOverlay::PauseOverlay(ScenePlay& play, Skin* skin) : play(play), skin(skin) {}

    void PauseOverlay::on_open() {
        last_mouse = game->input().mouse_position();
    }

    void PauseOverlay::choose(size_t choice) {
        switch (choice) {
            case RESTART:
                close();
                play.restart();
                return;
            case QUIT:
                close();
                game->audio().play_effect(skin->get_random_menu_pluck());
                game->scenes().pop();
                return;
            default:
                game->audio().play_effect(skin->get_random_menu_pluck());
                close();
                play.resume(now_ms());
                return;
        }
    }

    void PauseOverlay::update(float) {
        Input& input = game->input();
        bool pause_pressed = input.key_pressed(InputManager::get_key(KeyBinding::PAUSE));

        if (!input.focused()) {
            return;
        }

        size_t previous = selection;

        if (input.take(actions::UP)) {
            selection = (selection + ITEMS.size() - 1) % ITEMS.size();
        }

        if (input.take(actions::DOWN)) {
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

        if (pause_pressed || input.take(actions::BACK)) {
            choose(CONTINUE);
            return;
        }

        if (input.take(actions::CONFIRM)) {
            choose(selection);
        }
    }

    void PauseOverlay::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        float center_x = screen.x / 2.0f;
        const Chart& chart = *play.chart;
        window->FillRectDecal({0.0f, 0.0f}, screen, skin->color(SkinColor::PAUSE_OVERLAY));

        float y = screen.y * TITLE_TOP;
        draw_text_centered(skin->display_font_large, "PAUSED", center_x, y, skin->color(SkinColor::TEXT));
        y += Ui::pxf(96);
        draw_text_centered(skin->display_font_medium, std::format("{} - {}", chart.info.artist, chart.info.title), center_x, y, skin->color(SkinColor::TEXT_MUTED));
        y += Ui::pxf(44);

        int32_t chart_start = chart.get_chart_start_ms();
        std::string elapsed_text = clock_text(play.paused_position - chart_start);
        std::string total_text = clock_text(chart.get_chart_end_ms() - chart_start);
        std::string details = std::format("[{}]    {} / {}    {:.2f}%", chart.info.difficulty_name, elapsed_text, total_text, play.session->get_attempt().get_accuracy() * 100);
        draw_text_centered(skin->display_font_small, details, center_x, y, skin->color(SkinColor::TEXT_FAINT));
        y += Ui::pxf(80);

        item_bounds.clear();
        float padding = Ui::pxf(ITEM_PADDING);

        for (size_t index = 0; index < ITEMS.size(); index++) {
            bool selected = index == selection;
            olc::Pixel color = skin->color(selected ? SkinColor::TEXT : SkinColor::TEXT_FAINT);
            olc::FontRect bounds = draw_text_centered(skin->display_font_medium, ITEMS[index], center_x, y, color);
            Rect item = {{center_x - bounds.size.x / 2.0f, y + bounds.offset.y}, {(float)bounds.size.x, (float)bounds.size.y}};
            item_bounds.push_back(item.inflated(padding));
            y += Ui::pxf(ITEM_SPACING);
        }

        std::string hint = std::format("↑/↓  choose        Enter  select        {}/Esc  continue", InputManager::key_label(InputManager::get_key(KeyBinding::PAUSE)));
        draw_text_centered(skin->display_font_small, hint, center_x, screen.y - Ui::pxf(HINT_BOTTOM), skin->color(SkinColor::TEXT_FAINT));
    }
}

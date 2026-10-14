#include "ChartMenu.h"

#include "Chart.h"
#include "InputManager.h"
#include "Skin.h"
#include "TextCache.h"
#include "Ui.h"
#include "Utf8Path.h"

#include <algorithm>
#include <format>
#include <string>

namespace nascent {
    ChartMenu::Outcome ChartMenu::update(olc::PixelGameEngine* window) {
        Outcome outcome;

        if (window->GetKey(InputManager::back_key).bPressed || window->GetKey(InputManager::chart_menu_key).bPressed) {
            outcome.close = true;
            return outcome;
        }

        bool down = window->GetKey(InputManager::menu_down_key).bPressed;

        if (down || window->GetKey(InputManager::menu_up_key).bPressed) {
            selected = (selected + (down ? 1 : ACTIONS.size() - 1)) % ACTIONS.size();
            confirming = false;
            outcome.moved = true;
            return outcome;
        }

        if (!window->GetKey(InputManager::menu_start_key).bPressed) {
            return outcome;
        }

        ChartAction action = ACTIONS[selected];

        if (action != ChartAction::OPEN_WEBSITE && !confirming) {
            confirming = true;
            outcome.moved = true;
            return outcome;
        }

        outcome.close = true;
        outcome.action = action;
        return outcome;
    }

    void ChartMenu::draw(olc::PixelGameEngine* window, Skin* skin, const Chart& chart, size_t pack_count) const {
        olc::vi2d screen = window->GetScreenSize();
        olc::Font& title_font = skin->display_font_medium;
        olc::Font& row_font = skin->display_font_small;
        olc::FontRect title_metrics = text_bounds(title_font, "Hgjy");
        olc::FontRect row_metrics = text_bounds(row_font, "Hgjy");
        int32_t padding = Ui::px(PADDING);
        int32_t row_height = Ui::px(ROW_HEIGHT);
        int32_t row_inset = Ui::px(ROW_INSET);
        int32_t gap = Ui::px(GAP);

        std::string title = std::format("{} - {}", chart.info.artist, chart.info.title);
        std::string subtitle = std::format("[{}]  in  {}", chart.info.difficulty_name, path_utf8(chart.file_path.parent_path().filename()));
        std::string pack_charts = pack_count == 1 ? std::string("1 chart") : std::format("{} charts", pack_count);

        auto label = [&](ChartAction action, bool confirming_action) -> std::string {
            switch (action) {
                case ChartAction::OPEN_WEBSITE:
                    return "Open on website";
                case ChartAction::DELETE_CHART:
                    return confirming_action ? "Press Enter again to delete this chart" : "Delete chart";
                default:
                    return confirming_action ? std::format("Press Enter again to delete the pack and its {}", pack_charts) : std::format("Delete pack ({})", pack_charts);
            }
        };

        int32_t content_width = std::max(text_bounds(title_font, title).size.x, text_bounds(row_font, subtitle).size.x);

        for (ChartAction action : ACTIONS) {
            content_width = std::max(content_width, text_bounds(row_font, label(action, true)).size.x + row_inset * 2);
        }

        int32_t max_width = std::max(Ui::px(MIN_WIDTH), screen.x - Ui::px(SCREEN_MARGIN) * 2);
        int32_t width = std::clamp(content_width + padding * 2, std::min(Ui::px(MIN_WIDTH), max_width), max_width);
        int32_t height = padding * 2 + title_metrics.size.y + gap + row_metrics.size.y + gap * 2 + (int32_t)ACTIONS.size() * row_height;
        int32_t left = (screen.x - width) / 2;
        int32_t top = (screen.y - height) / 2;
        int32_t x = left + padding;
        int32_t y = top + padding;

        window->FillRectDecal({0.0f, 0.0f}, {(float)screen.x, (float)screen.y}, skin->color(SkinColor::MENU_DIM));
        window->FillRectDecal({(float)left, (float)top}, {(float)width, (float)height}, skin->color(SkinColor::MENU_BACKGROUND));

        draw_text(title_font, title, {x, y - title_metrics.offset.y}, skin->color(SkinColor::TEXT));
        y += title_metrics.size.y + gap;
        draw_text(row_font, subtitle, {x, y - row_metrics.offset.y}, skin->color(SkinColor::TEXT_MUTED));
        y += row_metrics.size.y + gap * 2;

        for (size_t index = 0; index < ACTIONS.size(); index++) {
            ChartAction action = ACTIONS[index];
            bool is_selected = index == selected;
            bool is_confirming = is_selected && confirming;
            bool danger = action != ChartAction::OPEN_WEBSITE;

            if (is_selected) {
                window->FillRectDecal({(float)x, (float)y}, {(float)(width - padding * 2), (float)row_height}, skin->color(SkinColor::SELECTION_HIGHLIGHT));
            }

            SkinColor role = is_confirming ? SkinColor::NEGATIVE : is_selected ? SkinColor::TEXT
                : danger                                                       ? SkinColor::MENU_DANGER_IDLE
                                                                               : SkinColor::TEXT_MUTED;
            olc::Pixel color = skin->color(role);
            int32_t baseline = y + (row_height - row_metrics.size.y) / 2 - row_metrics.offset.y;
            draw_text(row_font, label(action, is_confirming), {x + row_inset, baseline}, color);
            y += row_height;
        }
    }
}

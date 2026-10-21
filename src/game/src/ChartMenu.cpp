#include "ChartMenu.h"

#include "Chart.h"
#include "ChartFolders.h"
#include "ChartLibrary.h"
#include "InputManager.h"
#include "Skin.h"
#include "Utf8Path.h"
#include "gse/Game.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace nascent {
    static std::string format_label(const Chart& chart) {
        std::string extension = path_utf8(chart.file_path.extension());

        for (char& c : extension) {
            c = (char)std::tolower((unsigned char)c);
        }

        static constexpr std::array<std::pair<std::string_view, std::string_view>, 5> FORMATS = {{
            {".osu", "osu!mania beatmap"},
            {".sm", "StepMania chart"},
            {".ssc", "StepMania 5 chart"},
            {".dwi", "Dance With Intensity chart"},
            {".json", "Nascent chart"},
        }};

        auto found = std::find_if(FORMATS.begin(), FORMATS.end(), [&](const auto& entry) { return entry.first == extension; });
        std::string name(found != FORMATS.end() ? found->second : std::string_view("Chart"));
        return extension.empty() ? name : std::format("{} ({})", name, extension);
    }

    bool ChartMenu::enabled(ChartAction action) const {
        return !(linked && is_destructive(action)) && !(shared_file && action == ChartAction::DELETE_CHART) && !(pack_count == 0 && action == ChartAction::DELETE_PACK);
    }

    void ChartMenu::step_selection(bool down) {
        for (size_t step = 0; step < ACTIONS.size(); step++) {
            selected = (selected + (down ? 1 : ACTIONS.size() - 1)) % ACTIONS.size();

            if (enabled(ACTIONS[selected])) {
                return;
            }
        }
    }

    bool ChartMenu::is_destructive(ChartAction action) {
        return action == ChartAction::DELETE_CHART || action == ChartAction::DELETE_SONG || action == ChartAction::DELETE_PACK;
    }

    ChartMenu::ChartMenu(Skin* skin, const Chart& chart, size_t song_count, size_t pack_count, bool favorite, std::function<void(ChartAction)> on_action)
        : skin(skin),
          title(std::format("{} - {}", chart.info.artist, chart.info.title)),
          subtitle(std::format("[{}]  in  {}", chart.info.difficulty_name, path_utf8(chart.file_path.parent_path().filename()))),
          linked(ChartFolders::in_linked_folder(chart.file_path)),
          shared_file(holds_several_charts(chart.file_path)),
          details(std::format("{}  from {}", format_label(chart), linked ? "a linked folder" : "your Nascent songs folder")),
          song_count(song_count),
          pack_count(pack_count),
          favorite(favorite),
          on_action(std::move(on_action)) {}

    olc::Pixel ChartMenu::backdrop() const {
        return skin->color(SkinColor::MENU_DIM);
    }

    std::string ChartMenu::label(ChartAction action, bool confirming_action) const {
        std::string pack_charts = pack_count == 1 ? std::string("1 chart") : std::format("{} charts", pack_count);
        std::string song_charts = song_count == 1 ? std::string("its only difficulty") : std::format("all {} difficulties", song_count);

        switch (action) {
            case ChartAction::WATCH_REPLAY:
                return "Watch a replay";
            case ChartAction::TOGGLE_FAVORITE:
                return favorite ? "Remove from favorites" : "Add to favorites";
            case ChartAction::OPEN_WEBSITE:
                return "Open on website";
            case ChartAction::OPEN_FOLDER:
                return "Open chart folder";
            case ChartAction::DELETE_CHART:
                return confirming_action ? "Press Enter again to delete this difficulty" : "Delete this difficulty";
            case ChartAction::DELETE_SONG:
                return confirming_action ? std::format("Press Enter again to delete {}", song_charts) : std::format("Delete all difficulties ({})", song_count);
            default:
                if (pack_count == 0) {
                    return "Delete pack";
                }

                return confirming_action ? std::format("Press Enter again to delete the pack and its {}", pack_charts) : std::format("Delete pack ({})", pack_count);
        }
    }

    void ChartMenu::on_open() {
        last_mouse = game->input().mouse_position();
    }

    void ChartMenu::update(float) {
        Input& input = game->input();
        olc::vi2d mouse = input.mouse_position();
        bool mouse_moved = mouse != last_mouse;
        last_mouse = mouse;

        if (input.take(actions::BACK) || input.take(game_actions::CHART_MENU) || input.take_click(1) || (input.clicked(0) && !panel.contains(mouse) && input.take_click(0))) {
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
            close();
            return;
        }

        if (std::optional<int32_t> hovered = spots.at(mouse); hovered && mouse_moved && (size_t)*hovered != selected && enabled(ACTIONS[(size_t)*hovered])) {
            selected = (size_t)*hovered;
            confirming = false;
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
        }

        if (std::optional<int32_t> clicked = spots.take_click(input)) {
            if (!enabled(ACTIONS[(size_t)*clicked])) {
                return;
            }

            if ((size_t)*clicked != selected) {
                selected = (size_t)*clicked;
                confirming = false;
            }

            activate();
            return;
        }

        bool down = input.take(actions::DOWN);

        if (down || input.take(actions::UP)) {
            step_selection(down);
            confirming = false;
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
            return;
        }

        if (input.take(actions::CONFIRM)) {
            activate();
        }
    }

    void ChartMenu::activate() {
        ChartAction action = ACTIONS[selected];

        if (!enabled(action)) {
            return;
        }

        if (is_destructive(action) && !confirming) {
            confirming = true;
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
            return;
        }

        close();
        game->audio().play_effect(skin->get_random_menu_pluck());
        on_action(action);
    }

    void ChartMenu::draw(olc::PixelGameEngine* window) {
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

        int32_t content_width = std::max({text_bounds(title_font, title).size.x, text_bounds(row_font, subtitle).size.x, text_bounds(row_font, details).size.x});

        for (ChartAction action : ACTIONS) {
            content_width = std::max(content_width, text_bounds(row_font, label(action, true)).size.x + row_inset * 2);
        }

        int32_t max_width = std::max(min_width, screen.x - Ui::layout_px("chart_menu.screen_margin") * 2);
        int32_t width = std::clamp(content_width + padding * 2, std::min(min_width, max_width), max_width);
        int32_t height = padding * 2 + title_metrics.size.y + gap + (row_metrics.size.y + gap) * 2 + gap + (int32_t)ACTIONS.size() * row_height;
        int32_t left = (screen.x - width) / 2;
        int32_t top = (screen.y - height) / 2;
        int32_t x = left + padding;
        int32_t y = top + padding;

        window->FillRectDecal({(float)left, (float)top}, {(float)width, (float)height}, skin->color(SkinColor::MENU_BACKGROUND));
        panel = {{(float)left, (float)top}, {(float)width, (float)height}};
        spots.clear();

        draw_text(title_font, title, {x, y - title_metrics.offset.y}, skin->color(SkinColor::TEXT));
        y += title_metrics.size.y + gap;
        draw_text(row_font, subtitle, {x, y - row_metrics.offset.y}, skin->color(SkinColor::TEXT_MUTED));
        y += row_metrics.size.y + gap;
        draw_text(row_font, details, {x, y - row_metrics.offset.y}, skin->color(SkinColor::TEXT_FAINT));
        y += row_metrics.size.y + gap * 2;

        for (size_t index = 0; index < ACTIONS.size(); index++) {
            ChartAction action = ACTIONS[index];
            bool is_selected = index == selected;
            bool is_confirming = is_selected && confirming;
            bool danger = is_destructive(action);
            spots.add({{(float)x, (float)y}, {(float)(width - padding * 2), (float)row_height}}, (int32_t)index);

            if (is_selected) {
                window->FillRectDecal({(float)x, (float)y}, {(float)(width - padding * 2), (float)row_height}, skin->color(SkinColor::SELECTION_HIGHLIGHT));
            }

            SkinColor role = !enabled(action) ? SkinColor::TEXT_FAINT : is_confirming ? SkinColor::NEGATIVE
                : is_selected                                                         ? SkinColor::TEXT
                : danger                                                              ? SkinColor::MENU_DANGER_IDLE
                                                                                      : SkinColor::TEXT_MUTED;
            olc::Pixel color = skin->color(role);
            int32_t baseline = y + (row_height - row_metrics.size.y) / 2 - row_metrics.offset.y;
            draw_text(row_font, label(action, is_confirming), {x + row_inset, baseline}, color);
            y += row_height;
        }
    }
}

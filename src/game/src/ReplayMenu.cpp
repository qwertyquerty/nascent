#include "ReplayMenu.h"

#include "Chart.h"
#include "gse/Game.h"
#include "gse/concurrency/Async.h"
#include "InputManager.h"
#include "Results.h"
#include "Skin.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

namespace nascent {
    static std::string short_date(const std::string& timestamp) {
        return timestamp.size() >= 16 ? std::format("{} {}", timestamp.substr(0, 10), timestamp.substr(11, 5)) : timestamp;
    }

    ReplayMenu::ReplayMenu(Skin* skin, const Chart& chart, const std::string& gameplay_hash, float rate, WatchCallback on_watch)
        : skin(skin), chart_name(std::format("{} - {} [{}]", chart.info.artist, chart.info.title, chart.info.difficulty_name)), rate(rate), on_watch(std::move(on_watch)) {
        std::vector<LocalScore> scores = LocalReplays::all_scores(gameplay_hash);

        for (size_t index = 0; index < scores.size() && index < MAX_ROWS; index++) {
            const LocalScore& score = scores[index];
            local.push_back({score.path, 0, 0, std::string(), score.accuracy, score.max_combo, score.rate, score.played_at});
        }

        board = ServerApi::chart_leaderboard(gameplay_hash, rate, (int32_t)MAX_ROWS);
    }

    size_t ReplayMenu::count() const {
        return local.size() + online.size();
    }

    const ReplayMenu::Choice& ReplayMenu::choice(size_t index) const {
        return index < local.size() ? local[index] : online[index - local.size()];
    }

    olc::Pixel ReplayMenu::backdrop() const {
        return skin->color(SkinColor::MENU_DIM);
    }

    void ReplayMenu::watch(Replay replay, size_t index) {
        std::string watched = label(choice(index));
        close();
        game->audio().play_effect(skin->get_random_menu_pluck());
        on_watch(std::move(replay), std::move(watched));
    }

    std::string ReplayMenu::label(const Choice& choice) {
        if (choice.path) {
            return std::format("your play from {}", short_date(choice.played_at));
        }

        return std::format("{} (#{})", choice.player, choice.rank);
    }

    void ReplayMenu::on_open() {
        last_mouse = game->input().mouse_position();
    }

    void ReplayMenu::update(float) {
        Input& input = game->input();
        olc::vi2d mouse = input.mouse_position();
        bool mouse_moved = mouse != last_mouse;
        last_mouse = mouse;

        if (is_ready(board)) {
            ApiResult<ChartLeaderboard> result = board.get();

            if (!result.ok()) {
                online_status = std::format("Couldn't load the leaderboard: {}", result.error());
            }
            else {
                for (const RankedPlay& entry : result.value->entries) {
                    online.push_back({std::nullopt, entry.play.id, entry.rank, entry.play.user ? entry.play.user->username : std::string("unknown"), entry.play.accuracy, (uint32_t)entry.play.max_combo, entry.play.rate, entry.play.submitted_at});
                }

                online_status = online.empty() ? "Nobody has played this rate online yet" : std::string();
            }
        }

        if (downloading) {
            if (!is_ready(download)) {
                return;
            }

            ApiResult<Replay> result = download.get();
            size_t index = *std::exchange(downloading, std::nullopt);

            if (!result.ok()) {
                game->notify(std::format("Couldn't download the replay: {}", result.error()), true);
                return;
            }

            watch(std::move(*result.value), index);
            return;
        }

        if (input.take(actions::BACK) || input.take(game_actions::CHART_MENU) || input.take_click(1) || (input.clicked(0) && !panel.contains(mouse) && input.take_click(0))) {
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
            close();
            return;
        }

        if (count() == 0) {
            return;
        }

        selected = std::min(selected, count() - 1);

        if (std::optional<int32_t> hovered = spots.at(mouse); hovered && mouse_moved && (size_t)*hovered != selected && (size_t)*hovered < count()) {
            selected = (size_t)*hovered;
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
        }

        if (std::optional<int32_t> clicked = spots.take_click(input); clicked && (size_t)*clicked < count()) {
            selected = (size_t)*clicked;
            choose();
            return;
        }

        bool down = input.take(actions::DOWN);

        if (down || input.take(actions::UP)) {
            selected = (selected + (down ? 1 : count() - 1)) % count();
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
            return;
        }

        if (input.take(actions::CONFIRM)) {
            choose();
        }
    }

    void ReplayMenu::choose() {
        const Choice& picked = choice(selected);

        if (!picked.path) {
            download = ServerApi::replay(picked.play_id);
            downloading = selected;
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
            return;
        }

        std::string error;
        std::optional<Replay> replay = load_replay(*picked.path, error);

        if (!replay) {
            game->notify(std::format("Couldn't open the replay: {}", error), true);
            return;
        }

        watch(std::move(*replay), selected);
    }

    void ReplayMenu::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        olc::Font& title_font = skin->display_font_medium;
        olc::Font& row_font = skin->display_font_small;
        olc::FontRect title_metrics = text_bounds(title_font, "Hgjy");
        olc::FontRect row_metrics = text_bounds(row_font, "Hgjy");
        int32_t padding = Ui::layout_px("replay_menu.padding");
        int32_t row_height = Ui::layout_px("replay_menu.row_height");
        int32_t row_inset = Ui::layout_px("replay_menu.row_inset");
        int32_t gap = Ui::layout_px("replay_menu.gap");
        int32_t header_height = Ui::layout_px("replay_menu.header_height");
        int32_t header_text_lift = Ui::layout_px("replay_menu.header_text_lift");
        int32_t grade_offset = Ui::layout_px("replay_menu.grade_offset");
        int32_t accuracy_offset = Ui::layout_px("replay_menu.accuracy_offset");
        int32_t detail_offset = Ui::layout_px("replay_menu.detail_offset");

        size_t local_rows = std::max<size_t>(1, local.size());
        size_t online_rows = std::max<size_t>(1, online.size());
        int32_t width = std::min(screen.x - padding * 2, Ui::layout_px("replay_menu.width"));
        int32_t height = padding * 2 + title_metrics.size.y + gap + row_metrics.size.y + gap + header_height * 2 + (int32_t)(local_rows + online_rows) * row_height;
        int32_t left = (screen.x - width) / 2;
        int32_t top = std::max(padding, (screen.y - height) / 2);
        int32_t x = left + padding;
        int32_t y = top + padding;

        window->FillRectDecal({(float)left, (float)top}, {(float)width, (float)height}, skin->color(SkinColor::MENU_BACKGROUND));
        panel = {{(float)left, (float)top}, {(float)width, (float)height}};
        spots.clear();

        draw_text(title_font, "Watch a replay", {x, y - title_metrics.offset.y}, skin->color(SkinColor::TEXT));
        y += title_metrics.size.y + gap;
        draw_text(row_font, chart_name, {x, y - row_metrics.offset.y}, skin->color(SkinColor::TEXT_MUTED));
        y += row_metrics.size.y + gap;

        auto header = [&](const std::string& text) {
            draw_text(row_font, text, {x, y + header_height - row_metrics.size.y - header_text_lift - row_metrics.offset.y}, skin->color(SkinColor::TEXT_FAINT));
            y += header_height;
        };

        auto message = [&](const std::string& text) {
            draw_text(row_font, text, {x + row_inset, y + (row_height - row_metrics.size.y) / 2 - row_metrics.offset.y}, skin->color(SkinColor::TEXT_MUTED));
            y += row_height;
        };

        auto row = [&](size_t index) {
            const Choice& entry = choice(index);
            bool is_selected = index == selected;
            int32_t baseline = y + (row_height - row_metrics.size.y) / 2 - row_metrics.offset.y;
            olc::Pixel text_color = skin->color(is_selected ? SkinColor::TEXT : SkinColor::TEXT_MUTED);
            spots.add({{(float)x, (float)y}, {(float)(width - padding * 2), (float)row_height}}, (int32_t)index);

            if (is_selected) {
                window->FillRectDecal({(float)x, (float)y}, {(float)(width - padding * 2), (float)row_height}, skin->color(SkinColor::SELECTION_HIGHLIGHT));
            }

            std::string lead = entry.path ? std::format("{:.2f}x", entry.rate) : std::format("#{}", entry.rank);
            draw_text(row_font, lead, {x + row_inset, baseline}, entry.path ? text_color : skin->rank_color(entry.rank));
            skin->draw_grade(grade_for_accuracy(entry.accuracy), row_font, {(float)(x + row_inset + grade_offset), (float)baseline});
            draw_text(row_font, std::format("{:.2f}%", std::floor(entry.accuracy * 10000.0) / 100.0), {x + row_inset + accuracy_offset, baseline}, text_color);

            std::string detail = downloading && *downloading == index ? std::string("Downloading...")
                : entry.path                                          ? std::format("{}x    {}", entry.max_combo, short_date(entry.played_at))
                                                                      : std::format("{}    {}x", entry.player, entry.max_combo);
            draw_text(row_font, detail, {x + row_inset + detail_offset, baseline}, text_color);
            y += row_height;
        };

        header("Your replays, any rate");

        if (local.empty()) {
            message("You haven't played this chart yet");
        }

        for (size_t index = 0; index < local.size(); index++) {
            row(index);
        }

        header(std::format("Online leaderboard at {:.2f}x", rate));

        if (online.empty()) {
            message(online_status);
        }

        for (size_t index = 0; index < online.size(); index++) {
            row(local.size() + index);
        }
    }
}

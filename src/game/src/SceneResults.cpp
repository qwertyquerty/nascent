#include "SceneResults.h"
#include "RichPresence.h"
#include "GradeColor.h"
#include "Accuracy.h"
#include "Chart.h"
#include "Game.h"
#include "InputManager.h"
#include "Skin.h"
#include "StatusBar.h"
#include "TextCache.h"
#include "Ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace nascent {
    static const olc::Pixel BACKGROUND_TINT = olc::Pixel(240, 240, 240, 25);
    static const olc::Pixel POSITIVE_COLOR = olc::Pixel(90, 220, 120);
    static const olc::Pixel NEGATIVE_COLOR = olc::Pixel(235, 80, 80);
    static const olc::Pixel BEST_COLOR = olc::Pixel(240, 190, 70);
    static constexpr std::string_view BACK_HINT = "Enter / Esc  back to the song list";

    static olc::Pixel blend(olc::Pixel below, olc::Pixel above) {
        float a = above.a / 255.0f;
        return olc::Pixel(
            (uint8_t)(below.r * (1 - a) + above.r * a),
            (uint8_t)(below.g * (1 - a) + above.g * a),
            (uint8_t)(below.b * (1 - a) + above.b * a),
            std::max(below.a, above.a)
        );
    }

    SceneResults::SceneResults(Skin* skin, Scene* return_scene, const Chart* chart, AttemptSummary summary, std::future<ApiResult<Submission>> submission, int song_audio_id) {
        this->skin = skin;
        this->return_scene = return_scene;
        this->chart = chart;
        this->summary = std::move(summary);
        this->submission = std::move(submission);
        this->song_audio_id = song_audio_id;
    }

    void SceneResults::init(Game*) {
        RichPresence::viewing_results(*chart, summary.accuracy, grade_name(summary.grade));
        layout_error_graph();

        if (!chart->info.image_file.empty()) {
            auto sprite = std::make_unique<olc::Sprite>();

            if (sprite->LoadFromFile(chart->image_path.string()) == olc::OK) {
                background_image = SpriteDecal(std::move(sprite), true);
            }
        }
    }

    void SceneResults::layout_error_graph() {
        olc::vi2d screen = game->window->GetScreenSize();
        int32_t margin = screen.x / 24;

        error_graph_pos = {margin, (int32_t)(screen.y * 0.56)};
        build_error_graph({std::max(1, screen.x - margin * 2), std::max(1, (int32_t)(screen.y * 0.32))});
    }

    void SceneResults::on_resize() {
        layout_error_graph();
    }

    void SceneResults::build_error_graph(olc::vi2d size) {
        auto sprite = std::make_unique<olc::Sprite>(size.x, size.y);
        olc::Pixel background(20, 20, 24, 200);

        for (int32_t y = 0; y < size.y; y++) {
            for (int32_t x = 0; x < size.x; x++) {
                sprite->SetPixel(x, y, background);
            }
        }

        double range = get_window_from_hit_acc_diff(HitScore::BAD, summary.hit_accuracy);
        double half = size.y / 2.0 - 2;

        auto error_to_y = [&](double error) {
            double clamped = std::clamp(error, -range, range);
            return (int32_t)std::lround(size.y / 2.0 + clamped / range * half);
        };

        for (auto it = TIMED_HIT_SCORES.rbegin(); it != TIMED_HIT_SCORES.rend(); ++it) {
            double window = get_window_from_hit_acc_diff(*it, summary.hit_accuracy);
            olc::Pixel band = olc::Pixel(hit_score_color(*it));
            band.a = 28;

            for (int32_t y = error_to_y(-window); y <= error_to_y(window); y++) {
                for (int32_t x = 0; x < size.x; x++) {
                    sprite->SetPixel(x, y, blend(sprite->GetPixel(x, y), band));
                }
            }
        }

        for (int32_t x = 0; x < size.x; x++) {
            sprite->SetPixel(x, size.y / 2, blend(sprite->GetPixel(x, size.y / 2), olc::Pixel(255, 255, 255, 90)));
        }

        double span = std::max(1, summary.chart_end - summary.chart_start);

        auto time_to_x = [&](int32_t time) {
            return (int32_t)std::lround((time - summary.chart_start) / span * (size.x - 3));
        };

        olc::Pixel miss_color = olc::Pixel(hit_score_color(HitScore::MISS));
        miss_color.a = 110;

        for (const ErrorPoint& point : summary.errors) {
            if (point.score == HitScore::MISS && !point.played) {
                int32_t x = time_to_x(point.time);

                for (int32_t y = 0; y < size.y; y++) {
                    sprite->SetPixel(x, y, blend(sprite->GetPixel(x, y), miss_color));
                }
            }
        }

        for (const ErrorPoint& point : summary.errors) {
            if (point.score == HitScore::MISS && !point.played) {
                continue;
            }

            olc::Pixel color = olc::Pixel(hit_score_color(point.score));
            color.a = point.release ? 150 : 230;
            int32_t x = time_to_x(point.time);
            int32_t y = error_to_y(point.error);
            int32_t radius = std::max(1, Ui::px(1));
            int32_t dot_width = std::max(2, Ui::px(2));

            for (int32_t dy = -radius; dy <= radius; dy++) {
                for (int32_t dx = 0; dx < dot_width; dx++) {
                    if (x + dx >= 0 && x + dx < size.x && y + dy >= 0 && y + dy < size.y) {
                        sprite->SetPixel(x + dx, y + dy, blend(sprite->GetPixel(x + dx, y + dy), color));
                    }
                }
            }
        }

        error_graph = SpriteDecal(std::move(sprite));
    }

    void SceneResults::release_song() {
        if (song_audio_id != -1) {
            game->get_audio().Stop(song_audio_id);
            game->get_audio().UnloadSound(song_audio_id);
            song_audio_id = -1;
        }
    }

    void SceneResults::on_exit() {
        release_song();
    }

    void SceneResults::update(float) {
        if (song_audio_id != -1 && !game->get_audio().IsPlaying(song_audio_id)) {
            release_song();
        }

        if (submission.valid() && submission.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            submitted = submission.get();
            RichPresence::refresh_profile();
        }

        if (game->window->GetKey(InputManager::menu_start_key).bPressed) {
            game->play_sound(skin->sound(SkinSound::HITSOUND));
            game->set_scene(return_scene);
        }
    }

    using BarPart = std::pair<std::string, olc::Pixel>;

    static olc::Pixel change_color(double change) {
        return change > 0 ? POSITIVE_COLOR : change < 0 ? NEGATIVE_COLOR
                                                        : olc::GREY;
    }

    static std::vector<BarPart> performance_parts(const Standing& standing) {
        return {
            {std::format("{:.2f}", standing.performance), olc::WHITE},
            {std::format("{:+.2f}", standing.performance_change), change_color(standing.performance_change)},
        };
    }

    static std::vector<BarPart> rank_parts(std::optional<int64_t> rank, std::optional<int64_t> previous) {
        if (!rank) {
            return {{"Unranked", olc::WHITE}};
        }

        std::vector<BarPart> parts = {{std::format("#{}", *rank), olc::WHITE}};

        if (!previous) {
            parts.emplace_back("new", POSITIVE_COLOR);
        }
        else if (*previous != *rank) {
            parts.emplace_back(std::format("{:+}", *previous - *rank), change_color((double)(*previous - *rank)));
        }

        return parts;
    }

    void SceneResults::draw_bottom_bar(olc::PixelGameEngine* window) const {
        olc::vi2d screen = window->GetScreenSize();
        int32_t bar_height = StatusBar::height();
        int32_t padding = StatusBar::padding();
        int32_t segment_gap = Ui::px(StatusBar::SEGMENT_GAP);
        int32_t label_gap = Ui::px(StatusBar::LABEL_GAP);
        int32_t top = screen.y - bar_height;
        olc::Font& font = skin->display_font_small;

        window->FillRectDecal({0.0f, (float)top}, {(float)screen.x, (float)bar_height}, StatusBar::BACKGROUND);
        window->FillRectDecal({0.0f, (float)top}, {(float)screen.x, 1.0f}, StatusBar::EDGE);

        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t baseline = top + (bar_height - metrics.size.y) / 2 - metrics.offset.y;

        auto width = [&](std::string_view text) {
            return text.empty() ? 0 : text_bounds(font, text).size.x;
        };

        int32_t right = screen.x - padding - width(BACK_HINT);
        draw_text(font, BACK_HINT, {right, baseline}, olc::GREY);

        int32_t left_limit = right - segment_gap;
        int32_t x = padding;
        bool left_full = false;

        auto place = [&](int32_t needed) {
            int32_t gap = x > padding ? segment_gap : 0;
            left_full = left_full || x + gap + needed > left_limit;

            if (!left_full) {
                x += gap;
            }

            return !left_full;
        };

        auto message = [&](const std::string& text, olc::Pixel color) {
            if (place(width(text))) {
                draw_text(font, text, {x, baseline}, color);
                x += width(text);
            }
        };

        auto segment = [&](const std::string& label, const std::vector<BarPart>& parts) {
            int32_t needed = width(label);

            for (const BarPart& part : parts) {
                needed += label_gap + width(part.first);
            }

            if (!place(needed)) {
                return;
            }

            draw_text(font, label, {x, baseline}, olc::GREY);
            x += width(label);

            for (const BarPart& part : parts) {
                x += label_gap;
                draw_text(font, part.first, {x, baseline}, part.second);
                x += width(part.first);
            }
        };

        if (!submission.valid() && !submitted) {
            message("Not submitted: sign in to submit your plays", olc::DARK_GREY);
            return;
        }

        if (!submitted) {
            message("Submitting your play...", olc::GREY);
            return;
        }

        if (!submitted->ok() && ServerApi::is_retryable(submitted->response)) {
            message("Not submitted yet: it will be retried in the background", olc::GREY);
            return;
        }

        if (!submitted->ok()) {
            message(std::format("Not submitted: {}", submitted->error()), NEGATIVE_COLOR);
            return;
        }

        const Submission& result = *submitted->value;

        if (result.play.ranked) {
            message("Ranked", POSITIVE_COLOR);
        }
        else if (result.chart.status != "ranked") {
            message("Unranked chart", olc::GREY);
        }
        else {
            message("Not ranked: ticket expired", olc::GREY);
        }

        segment("Play", {{std::format("{:.2f}", result.play.performance), olc::WHITE}});

        if (result.chart_rank) {
            segment("Chart", {{std::format("#{}", *result.chart_rank), olc::WHITE}});
        }

        if (result.key_mode) {
            segment(std::format("{}K", result.key_mode->key_count), performance_parts(*result.key_mode));
            segment("Rank", rank_parts(result.key_mode->global_rank, result.key_mode->previous_global_rank));
        }

        if (result.play.is_chart_best) {
            message("New chart best", BEST_COLOR);
        }
    }

    void SceneResults::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();

        if (background_image) {
            olc::vf2d corners[4] = {{0.0f, 0.0f}, {0.0f, screen.y - 1.0f}, {screen.x - 1.0f, screen.y - 1.0f}, {screen.x - 1.0f, 0.0f}};
            window->DrawWarpedDecal(background_image.get(), corners, BACKGROUND_TINT);
        }

        int32_t x = screen.x / 24;
        int32_t y = screen.y / 10;

        draw_text(skin->display_font_medium, std::format("{} - {}", chart->info.artist, chart->info.title), {x, y}, olc::WHITE);
        y += Ui::px(36);
        draw_text(skin->display_font_small, std::format("[{}] // {}    {:.2f}x", chart->info.difficulty_name, chart->info.mapper, chart->pitch), {x, y}, olc::GREY);
        y += Ui::px(110);

        draw_text(skin->display_font_large, std::string(grade_name(summary.grade)), {x, y + Ui::px(20)}, grade_color(summary.grade));
        draw_text(skin->display_font_large, std::format("{:.2f}%", std::floor(summary.accuracy * 10000.0) / 100.0), {x + Ui::px(110), y + Ui::px(20)}, olc::WHITE);
        y += Ui::px(80);

        const std::pair<const char*, std::string> stats[] = {
            {"Max combo", std::format("{}", summary.max_combo)},
            {"Average error", std::format("{:+.1f} ms", summary.average_error)},
            {"Diff", std::format("{:.2f}", summary.difficulty)},
            {"Perf", std::format("{:.2f}", summary.performance)},
        };

        for (const auto& [label, value] : stats) {
            draw_text(skin->display_font_medium, label, {x, y}, olc::GREY);
            draw_text(skin->display_font_medium, value, {x + Ui::px(300), y}, olc::WHITE);
            y += Ui::px(44);
        }

        int32_t counts_x = screen.x / 2 + screen.x / 12;
        int32_t counts_y = screen.y / 10 + Ui::px(146);

        for (HitScore score : ALL_HIT_SCORES) {
            if (score == HitScore::NONE) {
                continue;
            }

            draw_text(skin->display_font_medium, std::string(hit_score_name(score)), {counts_x, counts_y}, olc::Pixel(hit_score_color(score)));
            draw_text(skin->display_font_medium, std::format("{}", summary.score_counts[(size_t)score]), {counts_x + Ui::px(260), counts_y}, olc::WHITE);
            counts_y += Ui::px(44);
        }

        if (error_graph) {
            draw_text(skin->display_font_small, "Hit error over the song", {error_graph_pos.x, error_graph_pos.y - Ui::px(12)}, olc::GREY);
            window->DrawDecal(error_graph_pos, error_graph.get());
        }

        draw_bottom_bar(window);
    }
}

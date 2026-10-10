#include "SceneResults.h"
#include "Accuracy.h"
#include "Chart.h"
#include "Game.h"
#include "InputManager.h"
#include "Skin.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>
#include <utility>

namespace nascent {
    static olc::Pixel grade_color(Grade grade) {
        switch (grade) {
            case Grade::X:
                return olc::Pixel(255, 240, 160);
            case Grade::S:
                return olc::Pixel(255, 210, 80);
            case Grade::A:
                return olc::Pixel(110, 220, 120);
            case Grade::B:
                return olc::Pixel(90, 170, 255);
            case Grade::C:
                return olc::Pixel(190, 120, 255);
            case Grade::D:
                return olc::Pixel(255, 150, 70);
            case Grade::E:
                return olc::Pixel(240, 90, 90);
            default:
                return olc::Pixel(150, 150, 150);
        }
    }

    static olc::Pixel blend(olc::Pixel below, olc::Pixel above) {
        float a = above.a / 255.0f;
        return olc::Pixel(
            (uint8_t)(below.r * (1 - a) + above.r * a),
            (uint8_t)(below.g * (1 - a) + above.g * a),
            (uint8_t)(below.b * (1 - a) + above.b * a),
            std::max(below.a, above.a)
        );
    }

    SceneResults::SceneResults(Skin* skin, Scene* return_scene, const Chart* chart, AttemptSummary summary, std::string replay_message) {
        this->skin = skin;
        this->return_scene = return_scene;
        this->chart = chart;
        this->summary = std::move(summary);
        this->replay_message = std::move(replay_message);
    }

    void SceneResults::init(Game* game) {
        olc::vi2d screen = game->window->GetScreenSize();
        int32_t margin = screen.x / 24;

        error_graph_pos = {margin, (int32_t)(screen.y * 0.56)};
        build_error_graph({screen.x - margin * 2, (int32_t)(screen.y * 0.32)});
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

            for (int32_t dy = -1; dy <= 1; dy++) {
                for (int32_t dx = 0; dx <= 1; dx++) {
                    if (x + dx >= 0 && x + dx < size.x && y + dy >= 0 && y + dy < size.y) {
                        sprite->SetPixel(x + dx, y + dy, blend(sprite->GetPixel(x + dx, y + dy), color));
                    }
                }
            }
        }

        error_graph = SpriteDecal(std::move(sprite));
    }

    void SceneResults::update(float) {
        if (game->window->GetKey(InputManager::menu_start_key).bPressed) {
            game->play_sound(skin->hitsound_sfx);
            game->set_scene(return_scene);
        }
    }

    void SceneResults::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();
        int32_t x = screen.x / 24;
        int32_t y = screen.y / 10;

        skin->display_font_medium.DrawString(std::format("{} - {}", chart->info.artist, chart->info.title), {x, y}, olc::WHITE);
        y += 36;
        skin->display_font_small.DrawString(std::format("[{}] // {}    {:.2f}x", chart->info.difficulty_name, chart->info.mapper, chart->pitch), {x, y}, olc::GREY);
        y += 110;

        skin->display_font_large.DrawString(std::string(grade_name(summary.grade)), {x, y + 20}, grade_color(summary.grade));
        skin->display_font_large.DrawString(std::format("{:.2f}%", std::floor(summary.accuracy * 10000.0) / 100.0), {x + 110, y + 20}, olc::WHITE);
        y += 80;

        const std::pair<const char*, std::string> stats[] = {
            {"Max combo", std::format("{}", summary.max_combo)},
            {"Average error", std::format("{:+.1f} ms", summary.average_error)},
            {"Difficulty", std::format("{:.2f}", summary.difficulty)},
            {"Performance", std::format("{:.2f}", summary.performance)},
        };

        for (const auto& [label, value] : stats) {
            skin->display_font_medium.DrawString(label, {x, y}, olc::GREY);
            skin->display_font_medium.DrawString(value, {x + 300, y}, olc::WHITE);
            y += 44;
        }

        int32_t counts_x = screen.x / 2 + screen.x / 12;
        int32_t counts_y = screen.y / 10 + 146;

        for (HitScore score : ALL_HIT_SCORES) {
            if (score == HitScore::NONE) {
                continue;
            }

            skin->display_font_medium.DrawString(std::string(hit_score_name(score)), {counts_x, counts_y}, olc::Pixel(hit_score_color(score)));
            skin->display_font_medium.DrawString(std::format("{}", summary.score_counts[(size_t)score]), {counts_x + 260, counts_y}, olc::WHITE);
            counts_y += 44;
        }

        if (error_graph) {
            skin->display_font_small.DrawString("Hit error over the song", {error_graph_pos.x, error_graph_pos.y - 12}, olc::GREY);
            window->DrawDecal(error_graph_pos, error_graph.get());
        }

        int32_t footer_y = screen.y - screen.y / 24;
        skin->display_font_small.DrawString("Enter / Esc:  back to the song list", {x, footer_y}, olc::GREY);

        if (!replay_message.empty()) {
            olc::FontRect bounds = skin->display_font_small.GetStringBounds(replay_message);
            skin->display_font_small.DrawString(replay_message, {screen.x - x - bounds.size.x, footer_y}, olc::DARK_GREY);
        }
    }
}

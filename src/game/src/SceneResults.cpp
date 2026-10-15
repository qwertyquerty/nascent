#include "SceneResults.h"
#include "RichPresence.h"
#include "Accuracy.h"
#include "Chart.h"
#include "gse/Game.h"
#include "InputManager.h"
#include "Skin.h"
#include "StatusBar.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"
#include "gse/render/SpriteDecal.h"
#include "gse/render/Color.h"
#include "gse/anim/Easing.h"
#include "gse/concurrency/Async.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <memory>
#include <numbers>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace nascent {
    static constexpr std::string_view BACK_HINT = "Enter / Esc  back to the song list";
    static constexpr std::string_view BEST_LABEL = "Best";
    static constexpr float GRADE_SLAM_START = 0.15f;
    static constexpr float GRADE_SLAM_TIME = 0.4f;
    static constexpr float GRADE_SLAM_SCALE = 2.6f;
    static constexpr float FLASH_TIME = 0.35f;
    static constexpr float FLASH_ALPHA = 0.22f;
    static constexpr float COUNT_START = 0.25f;
    static constexpr float COUNT_TIME = 1.1f;
    static constexpr float ROWS_START = 0.55f;
    static constexpr float ROW_STAGGER = 0.07f;
    static constexpr float ROW_FADE = 0.3f;
    static constexpr float ROW_SLIDE = 30.0f;
    static constexpr float GRAPH_START = 0.9f;
    static constexpr float GRAPH_FADE = 0.5f;
    static constexpr float BANNER_START = 1.3f;
    static constexpr float BANNER_FADE = 0.35f;
    static constexpr float BANNER_SLIDE = 40.0f;
    static constexpr float BANNER_PULSE_SPEED = 4.0f;
    static constexpr float BANNER_GRADIENT_SPEED = 0.35f;
    static constexpr float PARTICLE_GRAVITY = 900.0f;
    static constexpr float PARTICLE_DRAG = 1.6f;
    static constexpr float PARTICLE_SPEED_MIN = 250.0f;
    static constexpr float PARTICLE_SPEED_MAX = 950.0f;
    static constexpr float PARTICLE_LIFE_MIN = 0.7f;
    static constexpr float PARTICLE_LIFE_MAX = 1.6f;
    static constexpr float PARTICLE_SIZE_MIN = 3.0f;
    static constexpr float PARTICLE_SIZE_MAX = 7.0f;

    static size_t burst_size(Grade grade) {
        switch (grade) {
            case Grade::X:
                return 170;
            case Grade::S:
                return 120;
            case Grade::A:
                return 60;
            case Grade::B:
                return 35;
            case Grade::C:
                return 20;
            default:
                return 0;
        }
    }

    SceneResults::SceneResults(Skin* skin, const Chart* chart, AttemptSummary summary, std::future<ApiResult<Submission>> submission, int song_audio_id, bool new_best) {
        this->skin = skin;
        this->new_best = new_best;
        this->chart = chart;
        this->summary = std::move(summary);
        this->submission = std::move(submission);
        this->song_audio_id = song_audio_id;
    }

    void SceneResults::on_create() {
        RichPresence::viewing_results(*chart, summary.accuracy, grade_name(summary.grade));
        layout_error_graph();

        if (!chart->info.image_file.empty()) {
            auto sprite = std::make_unique<olc::Sprite>();

            if (sprite->LoadFromFile(chart->image_path.string()) == olc::OK) {
                background_image = MipDecal(std::move(sprite));
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
        olc::Pixel background = skin->color(SkinColor::GRAPH_BACKGROUND);

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
            olc::Pixel band = skin->judgement_color(*it);
            band.a = 28;

            for (int32_t y = error_to_y(-window); y <= error_to_y(window); y++) {
                for (int32_t x = 0; x < size.x; x++) {
                    sprite->SetPixel(x, y, blend(sprite->GetPixel(x, y), band));
                }
            }
        }

        for (int32_t x = 0; x < size.x; x++) {
            sprite->SetPixel(x, size.y / 2, blend(sprite->GetPixel(x, size.y / 2), skin->color(SkinColor::GRAPH_CENTER_LINE)));
        }

        double span = std::max(1, summary.chart_end - summary.chart_start);

        auto time_to_x = [&](int32_t time) {
            return (int32_t)std::lround((time - summary.chart_start) / span * (size.x - 3));
        };

        olc::Pixel miss_color = skin->judgement_color(HitScore::MISS);
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

            olc::Pixel color = skin->judgement_color(point.score);
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
            game->audio().engine().Stop(song_audio_id);
            game->audio().engine().UnloadSound(song_audio_id);
            song_audio_id = -1;
        }
    }

    void SceneResults::on_exit() {
        release_song();
    }

    olc::vf2d SceneResults::grade_baseline(olc::vi2d screen) const {
        return {(float)(screen.x / 24), (float)(screen.y / 10 + Ui::px(36) + Ui::px(110) + Ui::px(20))};
    }

    void SceneResults::burst(olc::vf2d center) {
        Grade grade = summary.grade;
        olc::Pixel base = skin->grade_color(grade);
        Skin* theme = skin;

        ParticleBurst burst;
        burst.center = center;
        burst.count = burst_size(grade);
        burst.speed_min = Ui::pxf(PARTICLE_SPEED_MIN);
        burst.speed_max = Ui::pxf(PARTICLE_SPEED_MAX);
        burst.lift = Ui::pxf(PARTICLE_SPEED_MIN);
        burst.life_min = PARTICLE_LIFE_MIN;
        burst.life_max = PARTICLE_LIFE_MAX;
        burst.size_min = Ui::pxf(PARTICLE_SIZE_MIN);
        burst.size_max = Ui::pxf(PARTICLE_SIZE_MAX);
        burst.color = [grade, base, theme](std::mt19937& random) {
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);

            if (grade == Grade::X) {
                return unit(random) < 0.5f ? theme->title_gradient_at(unit(random), 0.5) : olc::WHITE;
            }

            return unit(random) < 0.25f ? olc::WHITE : base;
        };
        particles.burst(burst);
    }

    void SceneResults::update(float elapsed_time) {
        elapsed += elapsed_time;
        particles.gravity = Ui::pxf(PARTICLE_GRAVITY);
        particles.drag = PARTICLE_DRAG;
        particles.update(elapsed_time);

        if (!slammed && elapsed >= GRADE_SLAM_START + GRADE_SLAM_TIME) {
            slammed = true;
            olc::vi2d screen = game->window->GetScreenSize();
            olc::vf2d baseline = grade_baseline(screen);
            olc::FontRect bounds = text_bounds(skin->display_font_large, grade_name(summary.grade));
            burst({baseline.x + bounds.offset.x + bounds.size.x / 2.0f, baseline.y + bounds.offset.y + bounds.size.y / 2.0f});
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
        }

        if (song_audio_id != -1 && !game->audio().engine().IsPlaying(song_audio_id)) {
            release_song();
        }

        if (is_ready(submission)) {
            submitted = submission.get();
            RichPresence::refresh_profile();
        }

        if (game->input().take(actions::CONFIRM)) {
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
            game->scenes().pop();
        }
    }

    using BarPart = std::pair<std::string, olc::Pixel>;

    static olc::Pixel change_color(const Skin& skin, double change) {
        return skin.color(change > 0 ? SkinColor::POSITIVE : change < 0 ? SkinColor::NEGATIVE
                                                                        : SkinColor::TEXT_MUTED);
    }

    static std::vector<BarPart> performance_parts(const Skin& skin, const Standing& standing) {
        return {
            {std::format("{:.2f}", standing.performance), skin.color(SkinColor::TEXT)},
            {std::format("{:+.2f}", standing.performance_change), change_color(skin, standing.performance_change)},
        };
    }

    static std::vector<BarPart> rank_parts(const Skin& skin, std::optional<int64_t> rank, std::optional<int64_t> previous) {
        if (!rank) {
            return {{"Unranked", skin.color(SkinColor::TEXT)}};
        }

        std::vector<BarPart> parts = {{std::format("#{}", *rank), skin.color(SkinColor::TEXT)}};

        if (!previous) {
            parts.emplace_back("new", skin.color(SkinColor::POSITIVE));
        }
        else if (*previous != *rank) {
            parts.emplace_back(std::format("{:+}", *previous - *rank), change_color(skin, (double)(*previous - *rank)));
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

        window->FillRectDecal({0.0f, (float)top}, {(float)screen.x, (float)bar_height}, skin->color(SkinColor::STATUS_BAR_BACKGROUND));
        window->FillRectDecal({0.0f, (float)top}, {(float)screen.x, 1.0f}, skin->color(SkinColor::STATUS_BAR_EDGE));

        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t baseline = top + (bar_height - metrics.size.y) / 2 - metrics.offset.y;

        auto width = [&](std::string_view text) {
            return text.empty() ? 0 : text_bounds(font, text).size.x;
        };

        int32_t right = screen.x - padding - width(BACK_HINT);
        draw_text(font, BACK_HINT, {right, baseline}, skin->color(SkinColor::TEXT_MUTED));

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

            draw_text(font, label, {x, baseline}, skin->color(SkinColor::TEXT_MUTED));
            x += width(label);

            for (const BarPart& part : parts) {
                x += label_gap;
                draw_text(font, part.first, {x, baseline}, part.second);
                x += width(part.first);
            }
        };

        if (!submission.valid() && !submitted) {
            message("Not submitted: sign in to submit your plays", skin->color(SkinColor::TEXT_FAINT));
            return;
        }

        if (!submitted) {
            message("Submitting your play...", skin->color(SkinColor::TEXT_MUTED));
            return;
        }

        if (!submitted->ok() && ServerApi::is_retryable(submitted->response)) {
            message("Not submitted yet: it will be retried in the background", skin->color(SkinColor::TEXT_MUTED));
            return;
        }

        if (!submitted->ok()) {
            message(std::format("Not submitted: {}", submitted->error()), skin->color(SkinColor::NEGATIVE));
            return;
        }

        const Submission& result = *submitted->value;

        if (result.play.ranked) {
            message("Ranked", skin->color(SkinColor::POSITIVE));
        }
        else if (result.chart.status != "ranked") {
            message("Unranked chart", skin->color(SkinColor::TEXT_MUTED));
        }
        else {
            message("Not ranked: ticket expired", skin->color(SkinColor::TEXT_MUTED));
        }

        segment("Play", {{std::format("{:.2f}", result.play.performance), skin->color(SkinColor::TEXT)}});

        if (result.previous_best_performance) {
            double change = result.play.performance - *result.previous_best_performance;
            segment(std::string(BEST_LABEL), {
                {std::format("{:.2f}", *result.previous_best_performance), skin->color(SkinColor::TEXT)},
                {std::format("{:+.2f}", change), change_color(*skin, change)},
            });
        }

        if (result.chart_rank) {
            segment("Chart", {{std::format("#{}", *result.chart_rank), skin->color(SkinColor::TEXT)}});
        }

        if (result.key_mode) {
            segment(std::format("{}K", result.key_mode->key_count), performance_parts(*skin, *result.key_mode));
            segment("Rank", rank_parts(*skin, result.key_mode->global_rank, result.key_mode->previous_global_rank));
        }

        if (result.play.is_chart_best) {
            message("New chart best", skin->color(SkinColor::ATTENTION));
        }
    }

    void SceneResults::draw(olc::PixelGameEngine* window) {
        olc::vi2d screen = window->GetScreenSize();

        if (background_image) {
            olc::vf2d corners[4] = {{0.0f, 0.0f}, {0.0f, screen.y - 1.0f}, {screen.x - 1.0f, screen.y - 1.0f}, {screen.x - 1.0f, 0.0f}};
            olc::vf2d image_size = background_image.size();
            float cover = std::max(screen.x / std::max(1.0f, image_size.x), screen.y / std::max(1.0f, image_size.y));
            window->DrawWarpedDecal(background_image.pick(cover).decal, corners, skin->color(SkinColor::BACKGROUND_TINT));
        }

        float flash = elapsed >= GRADE_SLAM_START + GRADE_SLAM_TIME ? 1.0f - progress(elapsed, GRADE_SLAM_START + GRADE_SLAM_TIME, FLASH_TIME) : 0.0f;

        if (flash > 0 && burst_size(summary.grade) > 0) {
            window->FillRectDecal({0.0f, 0.0f}, screen, fade(skin->grade_color(summary.grade), flash * FLASH_ALPHA));
        }

        int32_t x = screen.x / 24;
        int32_t y = screen.y / 10;

        draw_text(skin->display_font_medium, std::format("{} - {}", chart->info.artist, chart->info.title), {x, y}, skin->color(SkinColor::TEXT));
        y += Ui::px(36);
        draw_text(skin->display_font_small, std::format("[{}] // {}    {:.2f}x", chart->info.difficulty_name, chart->info.mapper, chart->pitch), {x, y}, skin->color(SkinColor::TEXT_MUTED));
        y += Ui::px(110);

        olc::vf2d baseline = grade_baseline(screen);
        olc::FontRect grade_bounds = text_bounds(skin->display_font_large, grade_name(summary.grade));
        float slam = progress(elapsed, GRADE_SLAM_START, GRADE_SLAM_TIME);

        if (slam > 0) {
            float scale = 1.0f + (GRADE_SLAM_SCALE - 1.0f) * (1.0f - ease_out(slam));
            olc::vf2d half = {grade_bounds.offset.x + grade_bounds.size.x / 2.0f, grade_bounds.offset.y + grade_bounds.size.y / 2.0f};
            olc::vf2d center = baseline + half;
            skin->draw_grade(summary.grade, skin->display_font_large, center - half * scale, scale, std::min(1.0f, slam * 2.0f));
        }

        double counted = summary.accuracy * ease_out(progress(elapsed, COUNT_START, COUNT_TIME));
        std::string accuracy_text = std::format("{:.2f}%", std::floor(counted * 10000.0) / 100.0);
        std::string final_accuracy = std::format("{:.2f}%", std::floor(summary.accuracy * 10000.0) / 100.0);
        int32_t accuracy_x = x + Ui::px(110);
        draw_text(skin->display_font_large, accuracy_text, {accuracy_x, y + Ui::px(20)}, skin->color(SkinColor::TEXT));

        bool full_combo = summary.max_combo > 0 && summary.score_counts[(size_t)HitScore::MISS] == 0;
        float banner = ease_out(progress(elapsed, BANNER_START, BANNER_FADE));

        if (banner > 0) {
            int32_t banner_x = accuracy_x + text_bounds(skin->display_font_large, final_accuracy).size.x + Ui::px(36) + (int32_t)(Ui::pxf(BANNER_SLIDE) * (1.0f - banner));
            int32_t banner_y = y + Ui::px(10);
            float pulse = 0.85f + 0.15f * (float)std::sin(elapsed * BANNER_PULSE_SPEED);

            if (full_combo) {
                draw_text(skin->display_font_medium, "FULL COMBO", {banner_x, banner_y}, fade(skin->color(SkinColor::FULL_COMBO), banner * pulse));
                banner_y += Ui::px(40);
            }

            if (new_best) {
                olc::Pixel shine = skin->title_gradient_at(std::fmod(elapsed * BANNER_GRADIENT_SPEED, 1.0f), 0.5);
                draw_text(skin->display_font_medium, "NEW PERSONAL BEST", {banner_x, banner_y}, fade(shine, banner * pulse));
            }
        }

        y += Ui::px(80);

        auto row_fade = [&](size_t index) {
            return ease_out(progress(elapsed, ROWS_START + ROW_STAGGER * index, ROW_FADE));
        };

        const std::tuple<const char*, std::string, olc::Pixel> stats[] = {
            {"Max combo", std::format("{}", summary.max_combo), skin->color(full_combo ? SkinColor::FULL_COMBO : SkinColor::TEXT)},
            {"Average error", std::format("{:+.1f} ms", summary.average_error), skin->color(SkinColor::TEXT)},
            {"Diff", std::format("{:.2f}", summary.difficulty), skin->difficulty_color(summary.difficulty)},
            {"Perf", std::format("{:.2f}", summary.performance), skin->color(SkinColor::TEXT)},
        };

        size_t row_index = 0;

        for (const auto& [label, value, color] : stats) {
            float alpha = row_fade(row_index++);
            int32_t slide = (int32_t)(Ui::pxf(ROW_SLIDE) * (1.0f - alpha));
            draw_text(skin->display_font_medium, label, {x - slide, y}, fade(skin->color(SkinColor::TEXT_MUTED), alpha));
            draw_text(skin->display_font_medium, value, {x + Ui::px(300) - slide, y}, fade(color, alpha));
            y += Ui::px(44);
        }

        int32_t counts_x = screen.x / 2 + screen.x / 12;
        int32_t counts_y = screen.y / 10 + Ui::px(146);

        for (HitScore score : ALL_HIT_SCORES) {
            if (score == HitScore::NONE) {
                continue;
            }

            float alpha = row_fade(row_index++);
            int32_t slide = (int32_t)(Ui::pxf(ROW_SLIDE) * (1.0f - alpha));
            uint32_t count = (uint32_t)std::lround(summary.score_counts[(size_t)score] * alpha);
            draw_text(skin->display_font_medium, std::string(hit_score_name(score)), {counts_x + slide, counts_y}, fade(skin->judgement_color(score), alpha));
            draw_text(skin->display_font_medium, std::format("{}", count), {counts_x + Ui::px(260) + slide, counts_y}, fade(skin->color(SkinColor::TEXT), alpha));
            counts_y += Ui::px(44);
        }

        float graph = ease_out(progress(elapsed, GRAPH_START, GRAPH_FADE));

        if (error_graph && graph > 0) {
            draw_text(skin->display_font_small, "Hit error over the song", {error_graph_pos.x, error_graph_pos.y - Ui::px(12)}, fade(skin->color(SkinColor::TEXT_MUTED), graph));
            window->DrawDecal(error_graph_pos, error_graph.get(), {1.0f, 1.0f}, fade(olc::WHITE, graph));
        }

        particles.draw(window);

        draw_bottom_bar(window);
    }
}

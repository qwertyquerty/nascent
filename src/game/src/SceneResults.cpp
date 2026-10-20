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
#include "gse/Platform.h"

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
    static constexpr std::string_view PLAY_PAGE_HINT = "F7  view on website";
    static constexpr double MAX_PARTICLES = 2000.0;

    static size_t burst_size(Grade grade) {
        switch (grade) {
            case Grade::X:
                return (size_t)std::clamp(Ui::layout("results.particle_count_x"), 0.0, MAX_PARTICLES);
            case Grade::S:
                return (size_t)std::clamp(Ui::layout("results.particle_count_s"), 0.0, MAX_PARTICLES);
            case Grade::A:
                return (size_t)std::clamp(Ui::layout("results.particle_count_a"), 0.0, MAX_PARTICLES);
            case Grade::B:
                return (size_t)std::clamp(Ui::layout("results.particle_count_b"), 0.0, MAX_PARTICLES);
            case Grade::C:
                return (size_t)std::clamp(Ui::layout("results.particle_count_c"), 0.0, MAX_PARTICLES);
            default:
                return 0;
        }
    }

    static int32_t content_left(olc::vi2d screen) {
        return (int32_t)(screen.x * Ui::layout("results.margin_fraction"));
    }

    static int32_t content_top(olc::vi2d screen) {
        return (int32_t)(screen.y * Ui::layout("results.top_fraction"));
    }

    SceneResults::SceneResults(Skin* skin, const Chart* chart, AttemptSummary summary, std::future<ApiResult<Submission>> submission, int song_audio_id, bool new_best, std::string replay_label) {
        this->skin = skin;
        this->new_best = new_best;
        this->replay_label = std::move(replay_label);
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
        int32_t margin = (int32_t)(screen.x * Ui::layout("results.graph_margin_fraction"));

        error_graph_pos = {margin, (int32_t)(screen.y * Ui::layout("results.graph_top"))};
        build_error_graph({std::max(1, screen.x - margin * 2), std::max(1, (int32_t)(screen.y * Ui::layout("results.graph_height")))});
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
        double half = size.y / 2.0 - Ui::layout("results.graph_inset");

        auto error_to_y = [&](double error) {
            double clamped = std::clamp(error, -range, range);
            return (int32_t)std::lround(size.y / 2.0 + clamped / range * half);
        };

        uint8_t band_alpha = (uint8_t)Ui::layout("results.graph_band_alpha");

        for (auto it = TIMED_HIT_SCORES.rbegin(); it != TIMED_HIT_SCORES.rend(); ++it) {
            double window = get_window_from_hit_acc_diff(*it, summary.hit_accuracy);
            olc::Pixel band = skin->judgement_color(*it);
            band.a = band_alpha;

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
        miss_color.a = (uint8_t)Ui::layout("results.graph_miss_alpha");

        for (const ErrorPoint& point : summary.errors) {
            if (point.score == HitScore::MISS && !point.played) {
                int32_t x = time_to_x(point.time);

                for (int32_t y = 0; y < size.y; y++) {
                    sprite->SetPixel(x, y, blend(sprite->GetPixel(x, y), miss_color));
                }
            }
        }

        uint8_t hit_alpha = (uint8_t)Ui::layout("results.graph_hit_alpha");
        uint8_t release_alpha = (uint8_t)Ui::layout("results.graph_release_alpha");
        int32_t radius = std::max(1, Ui::layout_px("results.graph_dot_radius"));
        int32_t dot_width = std::max(2, Ui::layout_px("results.graph_dot_width"));

        for (const ErrorPoint& point : summary.errors) {
            if (point.score == HitScore::MISS && !point.played) {
                continue;
            }

            olc::Pixel color = skin->judgement_color(point.score);
            color.a = point.release ? release_alpha : hit_alpha;
            int32_t x = time_to_x(point.time);
            int32_t y = error_to_y(point.error);

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
        return {(float)content_left(screen), (float)(content_top(screen) + Ui::layout_px("results.subtitle_gap") + Ui::layout_px("results.grade_row_gap") + Ui::layout_px("results.grade_row_offset"))};
    }

    void SceneResults::burst(olc::vf2d center) {
        Grade grade = summary.grade;
        olc::Pixel base = skin->grade_color(grade);
        Skin* theme = skin;

        ParticleBurst burst;
        burst.center = center;
        burst.count = burst_size(grade);
        burst.speed_min = Ui::layout_pxf("results.particle_speed_min");
        burst.speed_max = Ui::layout_pxf("results.particle_speed_max");
        burst.lift = Ui::layout_pxf("results.particle_lift");
        burst.life_min = (float)Ui::layout("results.particle_life_min");
        burst.life_max = (float)Ui::layout("results.particle_life_max");
        burst.size_min = Ui::layout_pxf("results.particle_size_min");
        burst.size_max = Ui::layout_pxf("results.particle_size_max");
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
        particles.gravity = Ui::layout_pxf("results.particle_gravity");
        particles.drag = (float)Ui::layout("results.particle_drag");
        particles.update(elapsed_time);

        if (!slammed && elapsed >= (float)Ui::layout("results.grade_slam_start") + (float)Ui::layout("results.grade_slam_time")) {
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

        std::optional<int32_t> clicked = spots.take_click(game->input());

        if (clicked == SPOT_BACK) {
            game->input().inject(actions::CONFIRM);
        }

        if (clicked == SPOT_PLAY_PAGE || game->input().key_pressed(InputManager::play_page_key)) {
            open_play_page();
        }

        if (game->input().take(actions::CONFIRM)) {
            game->audio().play_effect(skin->sound(SkinSound::HITSOUND));
            game->scenes().pop();
        }
    }

    std::optional<int64_t> SceneResults::submitted_play_id() const {
        if (!replay_label.empty() || !submitted || !submitted->ok() || submitted->value->play.id <= 0) {
            return std::nullopt;
        }

        return submitted->value->play.id;
    }

    void SceneResults::open_play_page() {
        std::optional<int64_t> play_id = submitted_play_id();

        if (!play_id) {
            return;
        }

        game->audio().play_effect(skin->sound(SkinSound::HITSOUND));

        if (!open_url(ServerApi::play_page_url(*play_id))) {
            game->notify("Could not open the browser", true);
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
        int32_t segment_gap = Ui::layout_px("status_bar.segment_gap");
        int32_t label_gap = Ui::layout_px("status_bar.label_gap");
        int32_t top = screen.y - bar_height;
        olc::Font& font = skin->display_font_small;

        window->FillRectDecal({0.0f, (float)top}, {(float)screen.x, (float)bar_height}, skin->color(SkinColor::STATUS_BAR_BACKGROUND));
        window->FillRectDecal({0.0f, (float)top}, {(float)screen.x, 1.0f}, skin->color(SkinColor::STATUS_BAR_EDGE));

        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t baseline = top + (bar_height - metrics.size.y) / 2 - metrics.offset.y;

        auto width = [&](std::string_view text) {
            return text.empty() ? 0 : text_bounds(font, text).size.x;
        };

        std::vector<Hint> hints = {{std::string(BACK_HINT), SPOT_BACK}};

        if (submitted_play_id()) {
            hints.insert(hints.begin(), {std::string(PLAY_PAGE_HINT), SPOT_PLAY_PAGE});
        }

        int32_t right = screen.x - padding - (int32_t)std::ceil(hints_width(font, hints, (float)segment_gap));
        spots.clear();
        draw_hints(window, font, hints, {(float)right, (float)baseline}, (float)segment_gap, skin->color(SkinColor::TEXT_MUTED), skin->color(SkinColor::TEXT), spots);

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

        if (!replay_label.empty()) {
            message(std::format("Replay of {}", replay_label), skin->color(SkinColor::TEXT_MUTED));
            return;
        }

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

        float slam_start = (float)Ui::layout("results.grade_slam_start");
        float slam_time = (float)Ui::layout("results.grade_slam_time");
        float flash = elapsed >= slam_start + slam_time ? 1.0f - progress(elapsed, slam_start + slam_time, (float)Ui::layout("results.flash_time")) : 0.0f;

        if (flash > 0 && burst_size(summary.grade) > 0) {
            window->FillRectDecal({0.0f, 0.0f}, screen, fade(skin->grade_color(summary.grade), flash * (float)Ui::layout("results.flash_alpha")));
        }

        int32_t x = content_left(screen);
        int32_t y = content_top(screen);

        draw_text(skin->display_font_medium, std::format("{} - {}", chart->info.artist, chart->info.title), {x, y}, skin->color(SkinColor::TEXT));
        y += Ui::layout_px("results.subtitle_gap");
        draw_text(skin->display_font_small, std::format("[{}] // {}    {:.2f}x", chart->info.difficulty_name, chart->info.mapper, chart->pitch), {x, y}, skin->color(SkinColor::TEXT_MUTED));
        y += Ui::layout_px("results.grade_row_gap");

        olc::vf2d baseline = grade_baseline(screen);
        olc::FontRect grade_bounds = text_bounds(skin->display_font_large, grade_name(summary.grade));
        float slam = progress(elapsed, slam_start, slam_time);

        if (slam > 0) {
            float scale = 1.0f + ((float)Ui::layout("results.grade_slam_scale") - 1.0f) * (1.0f - ease_out(slam));
            olc::vf2d half = {grade_bounds.offset.x + grade_bounds.size.x / 2.0f, grade_bounds.offset.y + grade_bounds.size.y / 2.0f};
            olc::vf2d center = baseline + half;
            skin->draw_grade(summary.grade, skin->display_font_large, center - half * scale, scale, std::min(1.0f, slam * 2.0f));
        }

        double counted = summary.accuracy * ease_out(progress(elapsed, (float)Ui::layout("results.count_start"), (float)Ui::layout("results.count_time")));
        std::string accuracy_text = std::format("{:.2f}%", std::floor(counted * 10000.0) / 100.0);
        int32_t accuracy_x = x + Ui::layout_px("results.accuracy_x");
        draw_text(skin->display_font_large, accuracy_text, {accuracy_x, y + Ui::layout_px("results.grade_row_offset")}, skin->color(SkinColor::TEXT));

        bool full_combo = summary.max_combo > 0 && summary.score_counts[(size_t)HitScore::MISS] == 0;
        float banner = ease_out(progress(elapsed, (float)Ui::layout("results.banner_start"), (float)Ui::layout("results.banner_fade")));
        float pulse = (float)Ui::layout("results.banner_pulse_base") + (float)Ui::layout("results.banner_pulse_amount") * (float)std::sin(elapsed * (float)Ui::layout("results.banner_pulse_speed"));
        olc::Pixel shine = skin->title_gradient_at(std::fmod(elapsed * (float)Ui::layout("results.banner_gradient_speed"), 1.0f), 0.5);

        y += Ui::layout_px("results.stats_gap");

        float rows_start = (float)Ui::layout("results.rows_start");
        float row_stagger = (float)Ui::layout("results.row_stagger");
        float row_fade_time = (float)Ui::layout("results.row_fade");
        float row_slide = Ui::layout_pxf("results.row_slide");

        auto row_fade = [&](size_t index) {
            return ease_out(progress(elapsed, rows_start + row_stagger * index, row_fade_time));
        };

        struct StatRow {
            const char* label;
            std::string value;
            olc::Pixel color;
            std::string_view badge;
            olc::Pixel badge_color;
        };

        const StatRow stats[] = {
            {"Combo", std::format("{}", summary.max_combo), skin->color(full_combo ? SkinColor::FULL_COMBO : SkinColor::TEXT), full_combo ? "FULL COMBO" : "", skin->color(SkinColor::FULL_COMBO)},
            {"Error", std::format("{:+.1f} ± {:.1f} ms", summary.average_error, summary.error_deviation), skin->color(SkinColor::TEXT), "", olc::BLANK},
            {"Pauses", std::format("{}", summary.pause_count), skin->color(SkinColor::TEXT), "", olc::BLANK},
            {"Diff", std::format("{:.2f}", summary.difficulty), skin->difficulty_color(summary.difficulty), "", olc::BLANK},
            {"Perf", std::format("{:.2f}", summary.performance), skin->color(SkinColor::TEXT), new_best ? "NEW PERSONAL BEST" : "", shine},
        };

        size_t row_index = 0;
        int32_t stat_value_x = Ui::layout_px("results.stat_value_x");
        int32_t banner_gap = Ui::layout_px("results.banner_gap");
        float banner_slide = Ui::layout_pxf("results.banner_slide");
        int32_t stat_row_height = Ui::layout_px("results.stat_row_height");

        for (const StatRow& row : stats) {
            float alpha = row_fade(row_index++);
            int32_t slide = (int32_t)(row_slide * (1.0f - alpha));
            int32_t value_x = x + stat_value_x - slide;
            draw_text(skin->display_font_medium, row.label, {x - slide, y}, fade(skin->color(SkinColor::TEXT_MUTED), alpha));
            draw_text(skin->display_font_medium, row.value, {value_x, y}, fade(row.color, alpha));

            if (!row.badge.empty() && banner > 0) {
                int32_t badge_x = value_x + text_bounds(skin->display_font_medium, row.value).size.x + banner_gap + (int32_t)(banner_slide * (1.0f - banner));
                draw_text(skin->display_font_medium, row.badge, {badge_x, y}, fade(row.badge_color, banner * pulse));
            }

            y += stat_row_height;
        }

        int32_t counts_x = screen.x / 2 + (int32_t)(screen.x * Ui::layout("results.counts_offset_fraction"));
        int32_t counts_y = content_top(screen) + Ui::layout_px("results.counts_top");
        int32_t count_value_x = Ui::layout_px("results.count_value_x");
        int32_t count_row_height = Ui::layout_px("results.count_row_height");

        for (HitScore score : ALL_HIT_SCORES) {
            if (score == HitScore::NONE) {
                continue;
            }

            float alpha = row_fade(row_index++);
            int32_t slide = (int32_t)(row_slide * (1.0f - alpha));
            uint32_t count = (uint32_t)std::lround(summary.score_counts[(size_t)score] * alpha);
            skin->draw_judgement(score, skin->display_font_medium, olc::vf2d{(float)(counts_x + slide), (float)counts_y}, alpha);
            draw_text(skin->display_font_medium, std::format("{}", count), {counts_x + count_value_x + slide, counts_y}, fade(skin->color(SkinColor::TEXT), alpha));
            counts_y += count_row_height;
        }

        float graph = ease_out(progress(elapsed, (float)Ui::layout("results.graph_start"), (float)Ui::layout("results.graph_fade")));

        if (error_graph && graph > 0) {
            draw_text(skin->display_font_small, "Hit error over the song", {error_graph_pos.x, error_graph_pos.y - Ui::layout_px("results.graph_title_lift")}, fade(skin->color(SkinColor::TEXT_MUTED), graph));
            window->DrawDecal(error_graph_pos, error_graph.get(), {1.0f, 1.0f}, fade(olc::WHITE, graph));

            olc::Font& label_font = skin->display_font_small;
            olc::FontRect label_metrics = text_bounds(label_font, "Ely");
            int32_t padding = Ui::layout_px("results.graph_label_padding");
            int32_t label_x = error_graph_pos.x + padding;
            int32_t graph_bottom = error_graph_pos.y + error_graph.get_sprite()->height;
            olc::Pixel label_color = fade(skin->color(SkinColor::TEXT_MUTED), graph);
            draw_text(label_font, "Early", {label_x, error_graph_pos.y + padding - label_metrics.offset.y}, label_color);
            draw_text(label_font, "Late", {label_x, graph_bottom - padding - label_metrics.offset.y - label_metrics.size.y}, label_color);
        }

        particles.draw(window);

        draw_bottom_bar(window);
    }
}

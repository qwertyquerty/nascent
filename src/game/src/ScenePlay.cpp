#include "ScenePlay.h"
#include "Account.h"
#include "Accuracy.h"
#include "Chart.h"
#include "EntityField.h"
#include "Game.h"
#include "InputManager.h"
#include "KeyEvents.h"
#include "LocalBests.h"
#include "PlaySession.h"
#include "ProgressBar.h"
#include "DifficultyCalculator.h"
#include "Platform.h"
#include "Replay.h"
#include "Results.h"
#include "SceneResults.h"
#include "SettingsManager.h"
#include "Sha256.h"
#include "Skin.h"
#include "Util.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <iostream>
#include <system_error>

namespace nascent {
    static constexpr int32_t RESULTS_DELAY_MS = 2000;
    static constexpr float SONG_PROGRESS_WIDTH = 80.0f;
    static constexpr float SONG_PROGRESS_HEIGHT = 6.0f;
    static constexpr float SONG_PROGRESS_RIGHT = 100.0f;
    static constexpr float SONG_PROGRESS_TOP = 40.0f;

    ScenePlay::ScenePlay(Chart* chart, Skin* skin, Scene* return_scene) {
        this->chart = chart;
        this->skin = skin;
        this->return_scene = return_scene;
    };

    void ScenePlay::finish() {
        finished = true;

        const Attempt& attempt = session->get_attempt();
        AttemptSummary summary = summarize_attempt(attempt, DifficultyCalculator::calculate(*chart));

        std::error_code ec;
        std::filesystem::path relative = std::filesystem::relative(chart->file_path, SettingsManager::settings.list_song_directory, ec);

        ReplayChart replay_chart;
        replay_chart.path = (ec || relative.empty() ? chart->file_path : relative).generic_string();
        replay_chart.sha256 = sha256_file_hex(chart->file_path).value_or("");
        replay_chart.title = chart->info.title;
        replay_chart.artist = chart->info.artist;
        replay_chart.difficulty_name = chart->info.difficulty_name;
        replay_chart.key_count = chart->info.key_count;

        ReplaySettings replay_settings;
        replay_settings.audio_input_offset = session->audio_input_offset;
        replay_settings.audio_visual_offset = session->audio_visual_offset;
        replay_settings.scroll_speed = field->scroll_speed;

        Replay replay = make_replay(attempt, summary, replay_chart, chart->pitch, replay_settings, current_utc_timestamp());
        LocalBests::record(chart->gameplay_hash(), chart->pitch, summary, replay.played_at);

        std::string error;
        std::optional<std::filesystem::path> saved = save_replay(replay, user_config_directory(SettingsManager::APP_NAME) / "replays", error);
        std::string replay_message = saved ? std::format("Replay saved to {}", saved->string()) : std::format("Replay not saved: {}", error);

        if (!saved) {
            std::cerr << replay_message << std::endl;
        }

        std::future<ApiResult<Submission>> submission;

        if (ticket.valid()) {
            submission = ServerApi::submit_play(*chart, replay, ticket);
        }

        int continuing_audio_id = -1;

        if (chart_audio_id != -1 && game->get_audio().IsPlaying(chart_audio_id)) {
            continuing_audio_id = chart_audio_id;
            chart_audio_id = -1;
        }

        game->set_scene(std::make_unique<SceneResults>(skin, return_scene, chart, std::move(summary), replay_message, std::move(submission), continuing_audio_id));
    }

    ScenePlay::~ScenePlay() {
    }

    void ScenePlay::init(Game* game) {
        olc::vi2d window_size = game->window->GetWindowSize();

        if (Account::signed_in()) {
            ticket = ServerApi::request_ticket(*chart).share();
        }

        const uint8_t max_lanes_per_screen = 15;

        double lane_width = (double)window_size.x / ((double)max_lanes_per_screen);

        lane_keys = InputManager::get_lane_keys(chart->info.key_count);

        session = std::make_unique<PlaySession>(chart, SettingsManager::settings.field_audio_sync_max_error);
        session->audio_visual_offset = SettingsManager::settings.field_audio_visual_offset;
        session->audio_input_offset = SettingsManager::settings.field_audio_input_offset;
        session->reset_clock(-SettingsManager::settings.play_start_delay * 1000.0, now_ms());

        field = std::make_unique<EntityField>(chart, skin, olc::vd2d{lane_width * (max_lanes_per_screen - chart->info.key_count) / 2, 0}, olc::vd2d{lane_width * chart->info.key_count, (double)window_size.y});
        field->set_attempt(&session->get_attempt());
        field->draw_judge_colors = true;
        field->judge_alpha = 64;
        field->judge_height = lane_width / 2;
        field->draw_combo = true;
        timer = 0;

        chart_audio_id = game->get_audio().LoadSound(chart->audio_path.string());

        game->play_sound(skin->get_random_menu_pluck());

        game->get_audio().SetPitch(chart_audio_id, chart->pitch);
    };

    void ScenePlay::on_exit() {
        if (chart_audio_id != -1) {
            game->get_audio().Stop(chart_audio_id);
            game->get_audio().UnloadSound(chart_audio_id);
            chart_audio_id = -1;
        }

        game->stop_sound(skin->idle_loop_sfx);
    }

    void ScenePlay::pause(double now) {
        paused = true;
        paused_position = session->song_position(now);
        game->get_audio().Pause(chart_audio_id);
        game->play_sound(skin->idle_loop_sfx, true);
    }

    void ScenePlay::resume(double now) {
        paused = false;

        if (chart_started && !audio_finished) {
            game->get_audio().Play(chart_audio_id);
        }

        game->stop_sound(skin->idle_loop_sfx);
        session->reset_clock(paused_position, now);
        session->set_keys(InputManager::get_lane_keys_held(game->window, lane_keys), now);
    }

    void ScenePlay::update(float elapsed_time) {
        double now = now_ms();
        bool pause_pressed = game->window->GetKey(InputManager::pause_key).bPressed;

        if (paused) {
            if (pause_pressed) {
                resume(now);
            }
        }
        else if (pause_pressed || !game->window->IsFocused()) {
            pause(now);
        }

        if (paused) {
            session->reset_clock(paused_position, now);
        }
        else {
            if (!chart_started && session->song_position(now) >= 0) {
                chart_started = true;
                game->get_audio().Play(chart_audio_id);
            }

            if (chart_started && !audio_finished && !game->get_audio().IsPlaying(chart_audio_id)) {
                audio_finished = true;
            }

            if (chart_started && !audio_finished) {
                session->sync_clock(game->get_audio().GetCursorMilliseconds(chart_audio_id) / chart->pitch, now);
            }

            for (const KeyEvent& event : game->get_frame_key_events()) {
                std::optional<uint8_t> lane = InputManager::get_lane(lane_keys, event.key);

                if (!lane) {
                    continue;
                }

                if (event.pressed) {
                    session->press(*lane, event.time);
                }
                else {
                    session->release(*lane, event.time);
                }
            }

            session->update(now);

            timer += elapsed_time;

            if (chart_started && !finished && session->song_position(now) > chart->get_chart_end_ms() + RESULTS_DELAY_MS) {
                finish();
            }
        }

        field->set_position(session->visual_position(now));
        field->set_keys(session->get_held_keys());
    };

    void ScenePlay::draw(olc::PixelGameEngine* window) {
        olc::vi2d screensize = window->GetScreenSize();
        const Attempt& attempt = session->get_attempt();

        field->draw(window);

        double position = paused ? paused_position : session->song_position(now_ms());
        int32_t chart_start = chart->get_chart_start_ms();
        int32_t chart_length = std::max(1, chart->get_chart_end_ms() - chart_start);
        float progress = (float)((position - chart_start) / chart_length);
        ProgressBar({SONG_PROGRESS_WIDTH, SONG_PROGRESS_HEIGHT}).draw(window, {screensize.x - SONG_PROGRESS_RIGHT, SONG_PROGRESS_TOP}, progress, skin->title_gradient_at(std::clamp(progress, 0.0f, 1.0f), 0.5));

        skin->display_font_medium.DrawString(std::format("{:.2f}%", attempt.get_accuracy() * 100), 20, 40, olc::WHITE);
        skin->display_font_medium.DrawString(std::format("{:.1f}ms", attempt.get_avg_err()), 20, 80, olc::WHITE);

        std::string now_playing_string = std::format("{} - {}", chart->info.artist, chart->info.title);
        uint16_t w = skin->display_font_medium.GetStringBounds(now_playing_string).size.x;
        skin->display_font_medium.DrawString(
            now_playing_string,
            {screensize.x / 2 - w / 2,
             (int)smoothstep(
                 -(float)screensize.x / 24,
                 (float)screensize.x / 24,
                 (timer - SettingsManager::settings.play_start_delay + 2) * 2
             ) +
                 (int)smoothstep(
                     0,
                     -(float)screensize.x / 12,
                     (timer - SettingsManager::settings.play_start_delay - 1) * 2
                 )},
            olc::WHITE
        );

        uint8_t l = 0;
        const double vspacing = 26;
        const uint8_t vlen = 35;
        for (int64_t i = attempt.soonest_scored_hit_index; i >= 0 && i < (int64_t)attempt.judged_hits.size(); i--) {
            const JudgedHit& jhit = attempt.judged_hits[i];

            if (jhit.hit) {
                window->DrawStringDecal({20.0, (float)(160.0 + vspacing * l)}, std::format("{}", hit_score_acc(jhit.hit_score)), olc::Pixel(hit_score_color(jhit.hit_score)), {2, 2});
                window->DrawStringDecal({80.0, (float)(160.0 + vspacing * l)}, std::format("  {:+}ms", jhit.hit_err), olc::WHITE, {2, 2});

                l += 1;
                if (l >= vlen) {
                    break;
                }

                if (jhit.chart_hit.hit_type == HitType::HOLD) {
                    if (jhit.released) {
                        window->DrawStringDecal({20.0, (float)(160.0 + vspacing * l)}, std::format("{}", hit_score_acc(jhit.release_score)), olc::Pixel(hit_score_color(jhit.release_score)), {2, 2});
                        window->DrawStringDecal({80.0, (float)(160.0 + vspacing * l)}, std::format("R {:+}ms", jhit.release_err), olc::WHITE, {2, 2});
                    }
                    else {
                        window->DrawStringDecal({20.0, (float)(160.0 + vspacing * l)}, "...", olc::GREY, {2, 2});
                    }
                    l += 1;
                }
            }

            if (l >= vlen) {
                break;
            }
        }

        l = 0;

        float hit_width = screensize.x / 400;
        float hit_height = hit_width * 15;
        float px_per_ms = ((double)screensize.x) / 3.0 / get_window_from_hit_acc_diff(HitScore::BAD, attempt.hit_accuracy) / 2;
        float y = screensize.y - hit_height;

        for (auto it = TIMED_HIT_SCORES.rbegin(); it != TIMED_HIT_SCORES.rend(); ++it) {
            uint16_t hit_err_window = get_window_from_hit_acc_diff(*it, attempt.hit_accuracy);
            float x = screensize.x / 2 - hit_err_window * px_per_ms;
            olc::Pixel c = olc::Pixel(hit_score_color(*it));
            c.a = 24;
            window->FillRectDecal({x, y}, {hit_err_window * 2 * px_per_ms, hit_height}, c);
        }

        const uint8_t max_hit_lines = 100;
        for (int64_t i = attempt.soonest_scored_hit_index; i >= 0 && i < (int64_t)attempt.judged_hits.size(); i--) {
            const JudgedHit& jhit = attempt.judged_hits[i];

            if (jhit.hit_played && jhit.hit_score != HitScore::MISS) {
                float x = screensize.x / 2 - hit_width - 2 + jhit.hit_err * px_per_ms;
                window->FillRectDecal({x, y}, {hit_width, hit_height}, olc::Pixel(200, 255 - l, 255 - l, max_hit_lines - l));
                l++;
            }

            if (l >= max_hit_lines) {
                break;
            }
        }

        if (paused) {
            olc::Decal* paused_decal = skin->game_paused_decal.get();

            window->FillRectDecal({0, 0}, screensize, olc::Pixel(0, 0, 0, 200));
            window->DrawDecal({(float)((double)screensize.x / 2 - (double)paused_decal->sprite->width / 2), (float)((double)screensize.y / 2 - (double)paused_decal->sprite->height / 2)}, paused_decal);
        }
    };
}

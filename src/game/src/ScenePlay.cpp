#include "ScenePlay.h"
#include "RichPresence.h"
#include "Account.h"
#include "Accuracy.h"
#include "Chart.h"
#include "Const.h"
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
#include "Skin.h"
#include "Util.h"
#include "Utf8Path.h"
#include "TextCache.h"
#include "TimingBar.h"
#include "Ui.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <system_error>

namespace nascent {
    static constexpr int32_t RESULTS_DELAY_MS = 2000;
    static constexpr float SONG_PROGRESS_WIDTH = 80.0f;
    static constexpr float SONG_PROGRESS_HEIGHT = 6.0f;
    static constexpr float SONG_PROGRESS_RIGHT = 100.0f;
    static constexpr float SONG_PROGRESS_TOP = 40.0f;
    static constexpr uint8_t SKIP_HINT_ALPHA = 200;
    static constexpr float DIAGNOSTICS_RIGHT = 570.0f;
    static constexpr float DIAGNOSTICS_TOP = 70.0f;
    static constexpr float DIAGNOSTICS_LINE_HEIGHT = 18.0f;
    static constexpr float DIAGNOSTICS_SCALE = 1.5f;
    static constexpr float DIAGNOSTICS_PADDING = 10.0f;
    static constexpr size_t DIAGNOSTICS_MAX_CHARS = 45;
    static constexpr uint8_t DIAGNOSTICS_BACKGROUND_ALPHA = 170;

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
        std::filesystem::path relative = std::filesystem::relative(chart->file_path, SettingsManager::song_directory(), ec);

        ReplayChart replay_chart;
        replay_chart.path = (ec || relative.empty() ? chart->file_path : relative).generic_string();
        replay_chart.gameplay_hash = chart->gameplay_hash();
        replay_chart.title = chart->info.title;
        replay_chart.artist = chart->info.artist;
        replay_chart.difficulty_name = chart->info.difficulty_name;
        replay_chart.key_count = chart->info.key_count;

        ReplaySettings replay_settings;
        replay_settings.audio_input_offset = session->audio_input_offset;
        replay_settings.audio_visual_offset = session->audio_visual_offset;
        replay_settings.scroll_speed = field->scroll_speed;
        replay_settings.mirror = mirror;

        Replay replay = make_replay(attempt, summary, replay_chart, chart->pitch, replay_settings, current_utc_timestamp());
        replay.diagnostics = timing_diagnostics();
        LocalBests::record(chart->gameplay_hash(), chart->pitch, summary, replay.played_at);

        std::string error;
        std::optional<std::filesystem::path> saved = save_replay(replay, user_config_directory(SettingsManager::APP_NAME) / "replays", error);
        if (!saved) {
            std::cerr << std::format("Replay not saved: {}", error) << std::endl;
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

        game->set_scene(std::make_unique<SceneResults>(skin, return_scene, chart, std::move(summary), std::move(submission), continuing_audio_id));
    }

    ScenePlay::~ScenePlay() {
    }

    void ScenePlay::init(Game* game) {
        RichPresence::playing(*chart);
        if (Account::signed_in()) {
            ticket = ServerApi::request_ticket(*chart).share();
        }

        lane_keys = InputManager::get_lane_keys(chart->info.key_count);
        start_attempt();

        chart_audio_id = load_song_audio(game->get_audio(), chart->audio_path);
        game->apply_music_volume(chart_audio_id);

        game->play_sound(skin->get_random_menu_pluck());

        game->get_audio().SetPitch(chart_audio_id, chart->pitch);
    };

    void ScenePlay::start_attempt() {
        olc::vi2d window_size = game->window->GetScreenSize();
        const Settings& settings = SettingsManager::settings;
        double lane_width = std::min(window_size.y * settings.field_lane_width_percent / 100.0, (double)window_size.x / chart->info.key_count);
        double field_width = lane_width * chart->info.key_count;

        session = std::make_unique<PlaySession>(chart, SettingsManager::settings.field_audio_sync_max_error);
        session->audio_visual_offset = SettingsManager::settings.field_audio_visual_offset;
        session->audio_input_offset = SettingsManager::settings.field_audio_input_offset;
        session->reset_clock(-SettingsManager::settings.play_start_delay * 1000.0, now_ms());
        mirror = settings.field_mirror;

        field = std::make_unique<EntityField>(chart, skin, olc::vd2d{((double)window_size.x - field_width) / 2, 0}, olc::vd2d{field_width, (double)window_size.y});
        field->set_attempt(&session->get_attempt());
        field->draw_judge_colors = true;
        field->judge_alpha = 64;
        field->judge_height = window_size.y * settings.field_judge_line_percent / 100.0;
        field->upscroll = settings.field_upscroll;
        field->mirror = mirror;
        field->set_lane_gap(settings.field_lane_gap >= 0 ? std::optional<double>(Ui::pxf(settings.field_lane_gap)) : std::nullopt);
        field->draw_hit_score = settings.field_show_judgement;
        field->draw_combo = settings.field_show_combo;
        field->judgement_height = settings.field_judgement_height / 100.0;
        field->combo_height = settings.field_combo_height / 100.0;
        timer = 0;
        frames_played = 0;
        frame_time_total = 0;
        frame_histogram.fill(0);
        worst_frame_ms = 0;
        last_press_time.reset();
        closest_press_gap.reset();
        simultaneous_presses = 0;
        game->reset_input_delay();
    }

    uint8_t ScenePlay::chart_lane(uint8_t lane) const {
        return mirror ? (uint8_t)(chart->info.key_count - 1 - lane) : lane;
    }

    uint32_t ScenePlay::chart_lanes(uint32_t lanes) const {
        uint32_t mapped = 0;

        for (uint8_t lane = 0; lane < chart->info.key_count; lane++) {
            if (lanes & (1u << lane)) {
                mapped |= 1u << chart_lane(lane);
            }
        }

        return mapped;
    }

    void ScenePlay::on_resize() {
        olc::vi2d window_size = game->window->GetScreenSize();
        const Settings& settings = SettingsManager::settings;
        double lane_width = std::min(window_size.y * settings.field_lane_width_percent / 100.0, (double)window_size.x / chart->info.key_count);
        double field_width = lane_width * chart->info.key_count;

        field->pos = {((double)window_size.x - field_width) / 2, 0};
        field->size = {field_width, (double)window_size.y};
        field->judge_height = window_size.y * settings.field_judge_line_percent / 100.0;
        field->set_lane_gap(settings.field_lane_gap >= 0 ? std::optional<double>(Ui::pxf(settings.field_lane_gap)) : std::nullopt);
    }

    void ScenePlay::record_frame(float elapsed_time) {
        double frame_ms = elapsed_time * 1000.0;
        frame_histogram[std::min((size_t)(frame_ms / FRAME_HISTOGRAM_STEP_MS), FRAME_HISTOGRAM_BUCKETS - 1)]++;
        worst_frame_ms = std::max(worst_frame_ms, frame_ms);
    }

    void ScenePlay::record_press(double time) {
        if (last_press_time) {
            double gap = time - *last_press_time;

            if (gap <= 0) {
                simultaneous_presses++;
            }
            else {
                closest_press_gap = std::min(closest_press_gap.value_or(gap), gap);
            }
        }

        last_press_time = time;
    }

    double ScenePlay::frame_percentile_ms(double fraction) const {
        uint64_t target = (uint64_t)std::ceil(frames_played * fraction);
        uint64_t seen = 0;

        for (size_t bucket = 0; frames_played > 0 && bucket + 1 < FRAME_HISTOGRAM_BUCKETS; bucket++) {
            seen += frame_histogram[bucket];

            if (seen >= target) {
                return (double)(bucket + 1) * FRAME_HISTOGRAM_STEP_MS;
            }
        }

        return worst_frame_ms;
    }

    std::vector<std::string> ScenePlay::diagnostics_lines(bool full) const {
        const Attempt& attempt = session->get_attempt();
        olc::MiniAudio& audio = game->get_audio();
        InputDelayStats delay = game->input_delay();

        uint32_t hits = 0;
        uint32_t early = 0;
        uint32_t late = 0;
        double error_sum = 0;
        double error_squares = 0;

        for (const JudgedHit& jhit : attempt.judged_hits) {
            if (!jhit.hit_played || jhit.hit_score == HitScore::MISS || jhit.hit_score == HitScore::NONE) {
                continue;
            }

            hits++;
            error_sum += jhit.hit_err;
            error_squares += (double)jhit.hit_err * jhit.hit_err;
            early += jhit.hit_err < 0 ? 1 : 0;
            late += jhit.hit_err > 0 ? 1 : 0;
        }

        double mean_error = hits > 0 ? error_sum / hits : 0.0;
        double unstable_rate = hits > 0 ? std::sqrt(std::max(0.0, error_squares / hits - mean_error * mean_error)) * 10.0 : 0.0;
        std::string windows;

        for (HitScore score : TIMED_HIT_SCORES) {
            windows += std::format("{}{}", windows.empty() ? "" : "/", get_window_from_hit_acc_diff(score, attempt.hit_accuracy));
        }

        const std::string& hash = chart->gameplay_hash();
        uint16_t frame_limit = SettingsManager::settings.frame_rate_limit;
        double average_frame_ms = frames_played > 0 ? frame_time_total * 1000.0 / frames_played : 0.0;
        std::string press_gap = closest_press_gap ? std::format("{:.2f} ms", *closest_press_gap) : "none";

        std::vector<std::string> lines;

        if (full) {
            lines.push_back(std::format("song {} - {} [{}]", chart->info.artist, chart->info.title, chart->info.difficulty_name));
        }

        lines.insert(lines.end(), {
                                      std::format("nascent {} {}", NASCENT_VERSION_STRING, platform_description()),
                                      std::format("chart {} {}K {:.2f}x", full ? hash : hash.substr(0, 16), chart->info.key_count, chart->pitch),
                                      std::format("UR {:.1f} mean {:+.1f} ms", unstable_rate, mean_error),
                                      std::format("early {} late {}", early, late),
                                      std::format("windows {} ms", windows),
                                      std::format("input {}", input_mode_name(game->get_input_mode())),
                                      std::format("input delay {:.2f} ms avg of {}", delay.average_ms(), delay.count),
                                      std::format("closest press gap {} same {}", press_gap, simultaneous_presses),
                                      std::format("offset input {:+} visual {:+} ms", session->audio_input_offset, session->audio_visual_offset),
                                      std::format("scroll speed {}", field->scroll_speed),
                                      std::format("audio {} {} Hz", audio.GetBackendName(), audio.GetDeviceSampleRate()),
                                      std::format("device {}", audio.GetDeviceName()),
                                      std::format("latency {:.1f} ms period {:.1f} ms", audio.GetOutputLatencyMilliseconds(), audio.GetCallbackPeriodMilliseconds()),
                                      std::format("mp3 padding {:.1f} ms", audio.GetStartOffsetMilliseconds(chart_audio_id)),
                                      std::format("clock drift {:+.2f} ms snaps {}", session->last_clock_drift(), session->clock_snaps()),
                                      std::format("frame {:.2f} p99 {:.2f} max {:.2f} ms", average_frame_ms, frame_percentile_ms(0.99), worst_frame_ms),
                                      std::format("frame cap {}", frame_limit == 0 ? std::string("off") : std::format("{} fps", frame_limit)),
                                  });

        return lines;
    }

    void ScenePlay::copy_diagnostics() {
        std::string text;

        for (const std::string& line : diagnostics_lines(true)) {
            text += line;
            text += '\n';
        }

        std::string stamp = current_utc_timestamp();
        std::erase_if(stamp, [](char c) { return c == '-' || c == ':'; });

        std::error_code ec;
        std::filesystem::path directory = user_config_directory(SettingsManager::APP_NAME) / "diagnostics";
        std::filesystem::create_directories(directory, ec);
        std::filesystem::path path = directory / (stamp + ".txt");

        std::ofstream file(path, std::ios::binary);
        file << text;
        file.close();

        bool saved = !file.fail();
        bool copied = copy_to_clipboard(text);

        if (copied && saved) {
            game->notify(std::format("Diagnostics copied and saved to {}", path_utf8(path)));
        }
        else if (saved) {
            game->notify(std::format("Diagnostics saved to {}", path_utf8(path)));
        }
        else if (copied) {
            game->notify("Diagnostics copied to clipboard");
        }
        else {
            game->notify("Could not copy or save diagnostics", true);
        }
    }

    nlohmann::json ScenePlay::timing_diagnostics() const {
        InputDelayStats delay = game->input_delay();
        olc::MiniAudio& audio = game->get_audio();

        return nlohmann::json{
            {"audio_backend", audio.GetBackendName()},
            {"audio_callback_period_ms", audio.GetCallbackPeriodMilliseconds()},
            {"audio_output_latency_ms", audio.GetOutputLatencyMilliseconds()},
            {"mp3_padding_ms", audio.GetStartOffsetMilliseconds(chart_audio_id)},
            {"clock_snaps", session->clock_snaps()},
            {"input_mode", input_mode_name(game->get_input_mode())},
            {"input_delay_average_ms", delay.average_ms()},
            {"input_delay_max_ms", delay.max_ms},
            {"input_delay_samples", delay.count},
            {"closest_press_gap_ms", closest_press_gap ? nlohmann::json(*closest_press_gap) : nlohmann::json()},
            {"simultaneous_presses", simultaneous_presses},
            {"average_frame_ms", frames_played > 0 ? frame_time_total * 1000.0 / frames_played : 0.0},
            {"p99_frame_ms", frame_percentile_ms(0.99)},
            {"worst_frame_ms", worst_frame_ms},
            {"platform", platform_description()},
            {"version", NASCENT_VERSION_STRING},
        };
    }

    void ScenePlay::draw_timing_diagnostics(olc::PixelGameEngine* window) const {
        std::vector<std::string> lines = diagnostics_lines(false);
        float scale = Ui::pxf(DIAGNOSTICS_SCALE);
        float padding = Ui::pxf(DIAGNOSTICS_PADDING);
        float line_height = Ui::pxf(DIAGNOSTICS_LINE_HEIGHT);
        float top = Ui::pxf(DIAGNOSTICS_TOP);
        float x = (float)window->GetScreenSize().x - Ui::pxf(DIAGNOSTICS_RIGHT);
        float char_size = scale * 8.0f;
        float width = char_size * DIAGNOSTICS_MAX_CHARS + padding * 2;
        float height = line_height * (float)(lines.size() - 1) + char_size + padding * 2;

        window->FillRectDecal({x - padding, top - padding}, {width, height}, olc::Pixel(0, 0, 0, DIAGNOSTICS_BACKGROUND_ALPHA));

        for (size_t index = 0; index < lines.size(); index++) {
            std::string line = lines[index];

            for (char& c : line) {
                c = (unsigned char)c < 0x80 ? c : '?';
            }

            if (line.size() > DIAGNOSTICS_MAX_CHARS) {
                line = line.substr(0, DIAGNOSTICS_MAX_CHARS - 3) + "...";
            }

            window->DrawStringDecal({x, top + (float)index * line_height}, line, olc::WHITE, {scale, scale});
        }
    }

    bool ScenePlay::can_skip_intro(double now) const {
        int32_t target = chart->intro_skip_ms();
        return !finished && !paused && target > 0 && session->song_position(now) < target;
    }

    void ScenePlay::skip_intro(double now) {
        double target = chart->intro_skip_ms();
        double latency = game->get_audio().GetOutputLatencyMilliseconds();
        game->get_audio().Seek(chart_audio_id, (ma_uint64)std::lround(std::max(0.0, (target + latency) * chart->pitch)));
        session->reset_clock(target, now);

        if (!chart_started) {
            chart_started = true;
            game->get_audio().Play(chart_audio_id);
        }

        game->play_sound(skin->get_random_menu_pluck());
    }

    void ScenePlay::restart() {
        game->get_audio().Stop(chart_audio_id);
        game->get_audio().Seek(chart_audio_id, 0);
        game->stop_sound(skin->sound(SkinSound::IDLE_LOOP));

        if (Account::signed_in()) {
            ticket = ServerApi::request_ticket(*chart).share();
        }

        chart_started = false;
        paused = false;
        resumed_at.reset();
        audio_finished = false;
        restart_hold = 0;
        start_attempt();

        game->play_sound(skin->get_random_menu_pluck());
    }

    void ScenePlay::on_exit() {
        if (chart_audio_id != -1) {
            game->get_audio().Stop(chart_audio_id);
            game->get_audio().UnloadSound(chart_audio_id);
            chart_audio_id = -1;
        }

        game->stop_sound(skin->sound(SkinSound::IDLE_LOOP));
    }

    void ScenePlay::pause(double now) {
        paused = true;
        paused_position = session->song_position(now);
        game->get_audio().Pause(chart_audio_id);
        game->play_music(skin->sound(SkinSound::IDLE_LOOP), true);
    }

    void ScenePlay::resume(double now) {
        paused = false;
        resumed_at = now;

        if (chart_started && !audio_finished) {
            game->get_audio().Play(chart_audio_id);
        }

        game->stop_sound(skin->sound(SkinSound::IDLE_LOOP));
        session->reset_clock(paused_position, now);
        session->set_keys(chart_lanes(InputManager::get_lane_keys_held(game->window, lane_keys)), now);
    }

    void ScenePlay::update(float elapsed_time) {
        double now = now_ms();
        bool pause_pressed = game->window->GetKey(InputManager::get_key(KeyBinding::PAUSE)).bPressed;

        if (game->window->GetKey(InputManager::get_key(KeyBinding::DIAGNOSTICS)).bPressed) {
            show_hit_log = !show_hit_log;
        }

        if (game->window->GetKey(InputManager::get_key(KeyBinding::COPY_DIAGNOSTICS)).bPressed) {
            copy_diagnostics();
        }

        if (!finished && game->window->GetKey(InputManager::get_key(KeyBinding::RESTART)).bHeld) {
            restart_hold += elapsed_time;

            if (restart_hold >= RESTART_HOLD_TIME) {
                restart();
                return;
            }
        }
        else {
            restart_hold = 0;
        }

        if (paused) {
            if (pause_pressed) {
                resume(now);
            }
        }
        else if ((pause_pressed && (!resumed_at || now - *resumed_at >= PAUSE_COOLDOWN_MS)) || !game->window->IsFocused()) {
            pause(now);
        }

        if (paused) {
            session->reset_clock(paused_position, now);
        }
        else {
            if (can_skip_intro(now) && game->window->GetKey(InputManager::get_key(KeyBinding::SKIP_INTRO)).bPressed) {
                skip_intro(now);
            }

            if (!chart_started && session->song_position(now) >= -game->get_audio().GetOutputLatencyMilliseconds()) {
                chart_started = true;
                game->get_audio().Play(chart_audio_id);
            }

            if (chart_started && !audio_finished && !game->get_audio().IsPlaying(chart_audio_id)) {
                audio_finished = true;
            }

            if (chart_started && !audio_finished) {
                session->sync_clock(game->get_audio().GetPositionMilliseconds(chart_audio_id) / chart->pitch, now);
            }

            for (const KeyEvent& event : game->get_frame_key_events()) {
                std::optional<uint8_t> lane = InputManager::get_lane(lane_keys, event.key);

                if (!lane) {
                    continue;
                }

                if (event.pressed) {
                    record_press(event.time);
                    session->press(chart_lane(*lane), event.time);

                    if (SettingsManager::settings.field_hitsounds) {
                        game->play_sound(skin->sound(SkinSound::HITSOUND));
                    }
                }
                else {
                    session->release(chart_lane(*lane), event.time);
                }
            }

            session->update(now);

            if (chart_started) {
                frames_played++;
                frame_time_total += elapsed_time;
                record_frame(elapsed_time);
            }

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
        ProgressBar({Ui::pxf(SONG_PROGRESS_WIDTH), std::max(1.0f, Ui::pxf(SONG_PROGRESS_HEIGHT))}).draw(window, {screensize.x - Ui::pxf(SONG_PROGRESS_RIGHT), Ui::pxf(SONG_PROGRESS_TOP)}, progress, skin->title_gradient_at(std::clamp(progress, 0.0f, 1.0f), 0.5));

        draw_text(skin->display_font_medium, std::format("{:.2f}%", attempt.get_accuracy() * 100), Ui::px(20), Ui::px(40), olc::WHITE);
        draw_text(skin->display_font_medium, std::format("{:.1f}ms", attempt.get_avg_err()), Ui::px(20), Ui::px(80), olc::WHITE);

        std::string now_playing_string = std::format("{} - {}", chart->info.artist, chart->info.title);
        uint16_t w = text_bounds(skin->display_font_medium, now_playing_string).size.x;
        draw_text(skin->display_font_medium, now_playing_string, {screensize.x / 2 - w / 2, (int)smoothstep(-(float)screensize.x / 24, (float)screensize.x / 24, (timer - SettingsManager::settings.play_start_delay + 2) * 2) + (int)smoothstep(0, -(float)screensize.x / 12, (timer - SettingsManager::settings.play_start_delay - 1) * 2)}, olc::WHITE);

        if (can_skip_intro(now_ms())) {
            std::string skip_hint = std::format("Press {} to skip the intro", InputManager::key_label(InputManager::get_key(KeyBinding::SKIP_INTRO)));
            int32_t skip_width = text_bounds(skin->display_font_medium, skip_hint).size.x;
            draw_text(skin->display_font_medium, skip_hint, {screensize.x / 2 - skip_width / 2, screensize.y * 2 / 3}, olc::Pixel(255, 255, 255, SKIP_HINT_ALPHA));
        }

        uint8_t l = 0;
        const float vspacing = Ui::pxf(26);
        const float log_top = Ui::pxf(160);
        const float log_scale = Ui::pxf(2);
        const float score_x = Ui::pxf(20);
        const float error_x = Ui::pxf(80);
        const uint8_t vlen = 35;
        if (show_hit_log) {
            draw_timing_diagnostics(window);
        }

        for (int64_t i = attempt.soonest_scored_hit_index; show_hit_log && i >= 0 && i < (int64_t)attempt.judged_hits.size(); i--) {
            const JudgedHit& jhit = attempt.judged_hits[i];

            if (jhit.hit) {
                window->DrawStringDecal({score_x, log_top + vspacing * l}, std::format("{}", hit_score_acc(jhit.hit_score)), olc::Pixel(hit_score_color(jhit.hit_score)), {log_scale, log_scale});
                window->DrawStringDecal({error_x, log_top + vspacing * l}, std::format("  {:+}ms", jhit.hit_err), olc::WHITE, {log_scale, log_scale});

                l += 1;
                if (l >= vlen) {
                    break;
                }

                if (jhit.chart_hit.hit_type == HitType::HOLD) {
                    if (jhit.released) {
                        window->DrawStringDecal({score_x, log_top + vspacing * l}, std::format("{}", hit_score_acc(jhit.release_score)), olc::Pixel(hit_score_color(jhit.release_score)), {log_scale, log_scale});
                        window->DrawStringDecal({error_x, log_top + vspacing * l}, std::format("R {:+}ms", jhit.release_err), olc::WHITE, {log_scale, log_scale});
                    }
                    else {
                        window->DrawStringDecal({score_x, log_top + vspacing * l}, "...", olc::GREY, {log_scale, log_scale});
                    }
                    l += 1;
                }
            }

            if (l >= vlen) {
                break;
            }
        }

        if (SettingsManager::settings.field_show_timing_bar) {
            const Settings& settings = SettingsManager::settings;
            draw_timing_bar(window, attempt, screensize.x / 2.0f, screensize.x * settings.field_timing_bar_length / 100.0f, screensize.y * settings.field_timing_bar_width / 100.0f, settings.field_timing_bar_height / 100.0f);
        }

        if (paused) {
            olc::Decal* paused_decal = skin->game_paused_decal.get();

            window->FillRectDecal({0, 0}, screensize, olc::Pixel(0, 0, 0, 200));
            window->DrawDecal({(float)((double)screensize.x / 2 - (double)paused_decal->sprite->width / 2), (float)((double)screensize.y / 2 - (double)paused_decal->sprite->height / 2)}, paused_decal);
        }
    };
}

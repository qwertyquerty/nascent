#include "ScenePlay.h"
#include "RichPresence.h"
#include "Account.h"
#include "Accuracy.h"
#include "Chart.h"
#include "Const.h"
#include "EntityField.h"
#include "gse/Game.h"
#include "InputManager.h"
#include "gse/input/KeyEvents.h"
#include "LocalBests.h"
#include "PauseOverlay.h"
#include "PlaySession.h"
#include "gse/ui/ProgressBar.h"
#include "DifficultyCalculator.h"
#include "gse/Platform.h"
#include "Replay.h"
#include "Results.h"
#include "SceneResults.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "SkinSettings.h"
#include "StatusBar.h"
#include "SubmissionQueue.h"
#include "Util.h"
#include "Utf8Path.h"
#include "gse/render/TextCache.h"
#include "TimingBar.h"
#include "gse/ui/Ui.h"
#include "gse/anim/Easing.h"
#include "gse/render/Color.h"
#include "gse/concurrency/Async.h"

#include <algorithm>
#include <tuple>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <system_error>

namespace nascent {
    static constexpr int32_t RESULTS_DELAY_MS = 2000;
    static constexpr uint8_t HIT_LOG_ROWS = 35;

    static double start_position_ms() {
        return -SettingsManager::settings.play_start_delay * 1000.0;
    }

    ScenePlay::ScenePlay(Chart* chart, Skin* skin) {
        this->chart = chart;
        this->skin = skin;
    };

    ScenePlay::ScenePlay(Chart* chart, Skin* skin, Replay replay, std::string replay_label) : ScenePlay(chart, skin) {
        this->replay = std::move(replay);
        this->replay_label = std::move(replay_label);
    }

    double ScenePlay::chart_difficulty() {
        if (!difficulty) {
            difficulty = difficulty_load.valid() ? difficulty_load.get() : DifficultyCalculator::calculate(*chart);
        }

        return *difficulty;
    }

    void ScenePlay::finish() {
        finished = true;

        const Attempt& attempt = session->get_attempt();
        AttemptSummary summary = summarize_attempt(attempt, chart_difficulty());
        summary.pause_count = (uint32_t)(replay ? replay->pauses.size() : pauses.size());

        if (replay) {
            int continuing_audio_id = -1;

            if (chart_audio_id != -1 && game->audio().engine().IsPlaying(chart_audio_id)) {
                continuing_audio_id = std::exchange(chart_audio_id, -1);
            }

            auto results = std::make_unique<SceneResults>(skin, chart, std::move(summary), std::future<ApiResult<Submission>>(), continuing_audio_id, false, replay_label);
            results->enable_replay(chart, *replay, replay_label, borrowed_background);

            if (replay_play_id) {
                results->set_play_id(*replay_play_id);
            }

            game->scenes().replace(std::move(results));
            return;
        }

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
        replay.input_times = session->get_input_times();
        replay.clock_samples = clock_samples;
        replay.pauses = pauses;
        const LocalBest* previous_best = LocalBests::lookup(chart->gameplay_hash(), chart->pitch);
        bool new_best = previous_best != nullptr && summary.accuracy > previous_best->accuracy;
        LocalBests::record(chart->gameplay_hash(), chart->pitch, summary, replay.played_at);

        std::future<ApiResult<Submission>> submission = SubmissionQueue::submit(*chart, replay, ticket, user_config_directory(SettingsManager::APP_NAME) / "replays");

        int continuing_audio_id = -1;

        if (chart_audio_id != -1 && game->audio().engine().IsPlaying(chart_audio_id)) {
            continuing_audio_id = chart_audio_id;
            chart_audio_id = -1;
        }

        auto results = std::make_unique<SceneResults>(skin, chart, std::move(summary), std::move(submission), continuing_audio_id, new_best);
        results->enable_replay(chart, replay, std::format("your play from {}", replay.played_at.substr(0, 10)), borrowed_background);
        game->scenes().replace(std::move(results));
    }

    void ScenePlay::set_replay_play_id(int64_t play_id) {
        replay_play_id = play_id;
    }

    ScenePlay::~ScenePlay() {
        if (handed_audio_id != -1 && game != nullptr) {
            game->audio().stop(handed_audio_id);
            game->audio().engine().UnloadSound(handed_audio_id);
        }
    }

    void ScenePlay::on_create() {
        SkinLayoutScope layout_scope(skin, chart->info.key_count);
        RichPresence::playing(*chart);
        request_ticket();

        lane_keys = InputManager::get_lane_keys(chart->info.key_count);
        start_attempt();

        if (skin_toggle(*skin, skin_toggles::SHOW_PERFORMANCE)) {
            difficulty_load = std::async(std::launch::async, [chart = chart] {
                return DifficultyCalculator::calculate(*chart);
            });
        }
        load_background();

        if (handed_audio_id != -1) {
            chart_audio_id = std::exchange(handed_audio_id, -1);
            game->audio().stop(chart_audio_id);
            ma_sound_set_looping(game->audio().engine().GetSound(chart_audio_id), MA_FALSE);
        }
        else {
            chart_audio_id = load_song_audio(game->audio().engine(), chart->audio_path);
        }

        game->audio().apply_music_volume(chart_audio_id);

        game->audio().play_effect(skin->get_random_menu_pluck());

        game->audio().engine().SetPitch(chart_audio_id, chart->pitch);
    };

    void ScenePlay::start_attempt() {
        olc::vi2d window_size = game->window->GetScreenSize();
        const Settings& settings = SettingsManager::settings;
        double lane_width = std::min(window_size.y * skin_setting(*skin, skin_settings::LANE_WIDTH) / 100.0, (double)window_size.x / chart->info.key_count);
        double field_width = lane_width * chart->info.key_count;

        session = std::make_unique<PlaySession>(chart, SettingsManager::settings.field_audio_sync_max_error);
        clock_samples.clear();
        pauses.clear();
        open_pause.reset();
        clock_origin.reset();
        session->audio_visual_offset = SettingsManager::settings.field_audio_visual_offset;
        session->audio_input_offset = SettingsManager::settings.field_audio_input_offset;
        session->reset_clock(start_position_ms(), now_ms());
        session->set_clock_speed(replay_speed(), now_ms());
        mirror = replay ? replay->settings.mirror : settings.field_mirror;
        replay_cursor = 0;

        if (replay) {
            session->audio_input_offset = 0;
        }

        field = std::make_unique<EntityField>(chart, skin, olc::vd2d{((double)window_size.x - field_width) / 2, 0}, olc::vd2d{field_width, (double)window_size.y});
        field->set_attempt(&session->get_attempt());
        field->draw_judge_colors = true;
        field->judge_alpha = 64;
        field->judge_height = window_size.y * skin_setting(*skin, skin_settings::JUDGEMENT_LINE_HEIGHT) / 100.0;
        field->upscroll = settings.field_upscroll;
        field->mirror = mirror;
        field->note_gradient = settings.field_note_gradient;
        field->draw_judge = skin_toggle(*skin, skin_toggles::SHOW_JUDGEMENT_LINE);
        field->set_lane_gap(settings.field_lane_gap >= 0 ? std::optional<double>(Ui::pxf(settings.field_lane_gap)) : std::nullopt);
        field->draw_hit_score = skin_toggle(*skin, skin_toggles::SHOW_JUDGEMENT);
        field->draw_combo = skin_toggle(*skin, skin_toggles::SHOW_COMBO);
        field->lane_cover = settings.field_lane_cover / 100.0;
        field->judgement_height = skin_setting(*skin, skin_settings::JUDGEMENT_HEIGHT) / 100.0;
        field->combo_height = skin_setting(*skin, skin_settings::COMBO_HEIGHT) / 100.0;
        field->note_opacity = settings.field_note_opacity / 100.0f;
        field->receptor_opacity = settings.field_receptor_opacity / 100.0f;
        last_judged = nullptr;
        last_judged_release = false;

        for (HitScore score : ALL_HIT_SCORES) {
            skin->judgement_voice(score);
        }

        if (replay && replay->settings.scroll_speed > 0) {
            field->scroll_speed = replay->settings.scroll_speed;
        }

        timer = 0;
        frames_played = 0;
        frame_time_total = 0;
        frame_histogram.fill(0);
        worst_frame_ms = 0;
        last_press_time.reset();
        closest_press_gap.reset();
        simultaneous_presses = 0;
        game->input().reset_delay();
    }

    void ScenePlay::request_ticket() {
        if (replay) {
            ticket = std::shared_future<ApiResult<PlayTicket>>();
            waiting_for_ticket = false;
            return;
        }

        ticket = Account::signed_in() ? ServerApi::request_ticket(*chart).share() : std::shared_future<ApiResult<PlayTicket>>();
        ticket_requested_at = now_ms();
        waiting_for_ticket = ticket.valid();
    }

    bool ScenePlay::ticket_pending(double now) {
        if (!ticket.valid() || is_ready(ticket)) {
            return false;
        }

        if (now - ticket_requested_at < TICKET_WAIT_LIMIT_MS) {
            return true;
        }

        std::promise<ApiResult<PlayTicket>> abandoned;
        ApiResult<PlayTicket> result;
        result.response.error = "the server took too long to issue a play ticket";
        abandoned.set_value(std::move(result));
        ticket = abandoned.get_future().share();
        game->notify("The server took too long to respond, this play will not be ranked", true);
        return false;
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
        SkinLayoutScope layout_scope(skin, chart->info.key_count);
        olc::vi2d window_size = game->window->GetScreenSize();
        const Settings& settings = SettingsManager::settings;
        double lane_width = std::min(window_size.y * skin_setting(*skin, skin_settings::LANE_WIDTH) / 100.0, (double)window_size.x / chart->info.key_count);
        double field_width = lane_width * chart->info.key_count;

        field->pos = {((double)window_size.x - field_width) / 2, 0};
        field->size = {field_width, (double)window_size.y};
        field->judge_height = window_size.y * skin_setting(*skin, skin_settings::JUDGEMENT_LINE_HEIGHT) / 100.0;
        field->set_lane_gap(settings.field_lane_gap >= 0 ? std::optional<double>(Ui::pxf(settings.field_lane_gap)) : std::nullopt);
    }

    void ScenePlay::record_frame(float elapsed_time) {
        double frame_ms = elapsed_time * 1000.0;
        frame_histogram[std::min((size_t)(frame_ms / FRAME_HISTOGRAM_STEP_MS), FRAME_HISTOGRAM_BUCKETS - 1)]++;
        worst_frame_ms = std::max(worst_frame_ms, frame_ms);
    }

    double ScenePlay::wall_time(double now) {
        if (!clock_origin) {
            clock_origin = now;
        }

        return now - *clock_origin;
    }

    void ScenePlay::record_clock_sample(double now) {
        double elapsed = wall_time(now);

        if (!clock_samples.empty() && elapsed - clock_samples.back()[0] < CLOCK_SAMPLE_INTERVAL_MS) {
            return;
        }

        clock_samples.push_back({elapsed, session->song_position(now)});
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
        olc::MiniAudio& audio = game->audio().engine();
        InputDelayStats delay = game->input().delay();

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
                                      std::format("input {}", input_mode_name(game->input().mode())),
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
        InputDelayStats delay = game->input().delay();
        olc::MiniAudio& audio = game->audio().engine();

        return nlohmann::json{
            {"audio_backend", audio.GetBackendName()},
            {"audio_callback_period_ms", audio.GetCallbackPeriodMilliseconds()},
            {"audio_output_latency_ms", audio.GetOutputLatencyMilliseconds()},
            {"mp3_padding_ms", audio.GetStartOffsetMilliseconds(chart_audio_id)},
            {"clock_snaps", session->clock_snaps()},
            {"input_mode", input_mode_name(game->input().mode())},
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
            {"official_build", NASCENT_OFFICIAL_BUILD != 0},
            {"build_commit", NASCENT_BUILD_COMMIT},
            {"pause_count", pauses.size()},
        };
    }

    void ScenePlay::draw_timing_diagnostics(olc::PixelGameEngine* window) const {
        olc::Font& font = skin->display_font_small;
        olc::FontRect metrics = text_bounds(font, "Hgjy");
        float padding = Ui::layout_pxf("play.diagnostics_padding");
        float line_gap = Ui::layout_pxf("play.diagnostics_line_gap");
        float line_height = (float)metrics.size.y + line_gap;
        float top = Ui::layout_pxf("play.diagnostics_top");
        float max_width = Ui::layout_pxf("play.diagnostics_max_width");
        double now = now_ms();

        if (!diagnostics_built_at || now < *diagnostics_built_at || now - *diagnostics_built_at >= DIAGNOSTICS_REFRESH_MS || diagnostics_fit_width != max_width) {
            diagnostics_built_at = now;
            diagnostics_fit_width = max_width;
            diagnostics_cache = diagnostics_lines(false);
            diagnostics_width = 0;

            for (std::string& line : diagnostics_cache) {
                if (text_bounds(font, line).size.x > max_width) {
                    while (!line.empty() && text_bounds(font, line + "...").size.x > max_width) {
                        line.pop_back();

                        while (!line.empty() && ((unsigned char)line.back() & 0xc0) == 0x80) {
                            line.pop_back();
                        }
                    }

                    line += "...";
                }

                diagnostics_width = std::max(diagnostics_width, (float)text_bounds(font, line).size.x);
            }
        }

        const std::vector<std::string>& lines = diagnostics_cache;
        float width = diagnostics_width;
        float x = (float)window->GetScreenSize().x - Ui::layout_pxf("play.diagnostics_right") - width - padding;
        float height = line_height * (float)lines.size() - line_gap + padding * 2;
        window->FillRectDecal({x - padding, top - padding}, {width + padding * 2, height}, skin->color(SkinColor::DIAGNOSTICS_BACKGROUND));

        for (size_t index = 0; index < lines.size(); index++) {
            draw_text(font, lines[index], olc::vf2d{x, top + (float)index * line_height - metrics.offset.y}, skin->color(SkinColor::TEXT), 1.0f);
        }
    }

    bool ScenePlay::can_skip_intro(double now) const {
        int32_t target = chart->intro_skip_ms();
        return !replay && !finished && !paused && !waiting_for_ticket && target > 0 && session->song_position(now) < target;
    }

    void ScenePlay::skip_intro(double now) {
        double target = chart->intro_skip_ms();
        double latency = game->audio().engine().GetOutputLatencyMilliseconds();
        game->audio().engine().Seek(chart_audio_id, (ma_uint64)std::lround(std::max(0.0, (target + latency) * chart->pitch)));
        session->reset_clock(target, now);

        if (!chart_started) {
            chart_started = true;
            game->audio().engine().Play(chart_audio_id);
        }

        game->audio().play_effect(skin->get_random_menu_pluck());
    }

    void ScenePlay::restart() {
        game->audio().engine().Stop(chart_audio_id);
        game->audio().engine().Seek(chart_audio_id, 0);
        game->audio().stop(skin->sound(SkinSound::IDLE_LOOP));
        request_ticket();

        if (PauseOverlay* pause_menu = overlays.find<PauseOverlay>()) {
            pause_menu->close();
        }

        chart_started = false;
        paused = false;
        resumed_at.reset();
        audio_finished = false;
        restart_hold = 0;
        start_attempt();

        game->audio().play_effect(skin->get_random_menu_pluck());
    }

    void ScenePlay::feed_replay(double position, bool sounds) {
        while (replay_cursor < replay->inputs.size() && replay->inputs[replay_cursor].time <= position) {
            const AttemptInput& input = replay->inputs[replay_cursor++];
            session->apply(input);

            if (sounds && input.pressed && SettingsManager::settings.field_hitsounds) {
                game->audio().play_effect(skin->hitsound_voice());
            }
        }
    }

    void ScenePlay::seek_replay(double target, double now) {
        double kept_timer = timer;
        target = std::clamp(target, start_position_ms(), (double)chart->get_chart_end_ms());
        start_attempt();
        timer = kept_timer;
        session->reset_clock(target, now);
        feed_replay(target, false);
        session->update(now);
        audio_finished = false;

        if (target >= 0) {
            double latency = game->audio().engine().GetOutputLatencyMilliseconds() * replay_speed();
            game->audio().engine().Seek(chart_audio_id, (ma_uint64)std::lround((target + latency) * chart->pitch));

            if (!chart_started) {
                chart_started = true;
                game->audio().engine().Play(chart_audio_id);
            }
        }
        else {
            game->audio().engine().Stop(chart_audio_id);
            game->audio().engine().Seek(chart_audio_id, 0);
            chart_started = false;
        }
    }

    void ScenePlay::on_exit() {
        if (chart_audio_id != -1) {
            game->audio().engine().Stop(chart_audio_id);
            game->audio().engine().UnloadSound(chart_audio_id);
            chart_audio_id = -1;
        }

        game->audio().stop(skin->sound(SkinSound::IDLE_LOOP));
        set_cursor_hidden(false);
    }

    void ScenePlay::pause(double now) {
        paused = true;
        overlays.open(std::make_unique<PauseOverlay>(*this, skin));
        paused_position = session->song_position(now);

        if (!replay) {
            open_pause = std::array<double, 2>{wall_time(now), paused_position};
        }

        game->audio().engine().Pause(chart_audio_id);
        game->audio().play_music(skin->sound(SkinSound::IDLE_LOOP), true);
    }

    void ScenePlay::resume(double now) {
        paused = false;
        resumed_at = now;

        if (open_pause) {
            pauses.push_back({(*open_pause)[0], wall_time(now), (*open_pause)[1]});
            open_pause.reset();
        }

        if (chart_started && !audio_finished && !replay_paused) {
            game->audio().engine().Play(chart_audio_id);
        }

        game->audio().stop(skin->sound(SkinSound::IDLE_LOOP));
        session->reset_clock(paused_position, now);
        session->set_keys(chart_lanes(InputManager::get_lane_keys_held(game->input(), lane_keys)), now);
    }

    void ScenePlay::play_judgement_sound() {
        const JudgedHit* judged = session->get_attempt().get_last_scored();

        if (judged == nullptr || (judged == last_judged && judged->released == last_judged_release)) {
            return;
        }

        last_judged = judged;
        last_judged_release = judged->released;

        if (SettingsManager::settings.field_hitsounds) {
            game->audio().play_effect(skin->judgement_voice(judged->released ? judged->release_score : judged->hit_score));
        }
    }

    void ScenePlay::use_audio(int audio_id) {
        handed_audio_id = audio_id;
    }

    void ScenePlay::use_background(const MipDecal* background) {
        borrowed_background = background;
    }

    void ScenePlay::load_background() {
        if (borrowed_background != nullptr) {
            background_fade = 1.0f;
            return;
        }

        if (SettingsManager::settings.field_background_brightness == 0 || chart->info.image_file.empty()) {
            return;
        }

        std::filesystem::path path = chart->image_path;
        background_load = run_detached([path] {
            auto sprite = std::make_unique<olc::Sprite>();

            if (sprite->LoadFromFile(path.string()) != olc::OK) {
                return std::vector<std::unique_ptr<olc::Sprite>>();
            }

            return MipDecal::build_levels(std::move(sprite));
        });
    }

    void ScenePlay::draw_mascot(olc::PixelGameEngine* window) const {
        const MipDecal* mascot = skin->image(SkinImage::MASCOT, 0, 0);

        if (mascot == nullptr || mascot->size().x <= 0) {
            return;
        }

        const SkinMascot& placement = skin->mascot();
        olc::vf2d screen = window->GetScreenSize();
        float scale = (float)(screen.x * placement.width / 100.0) / mascot->size().x;
        olc::vf2d drawn = mascot->size() * scale;
        olc::vf2d center = {(float)(screen.x * placement.x / 100.0), (float)(screen.y * placement.y / 100.0)};
        MipDecal::Pick level = mascot->pick(scale);
        window->DrawDecal(center - drawn / 2.0f, level.decal, {scale / level.factor, scale / level.factor});
    }

    void ScenePlay::draw_background(olc::PixelGameEngine* window) const {
        uint8_t brightness = SettingsManager::settings.field_background_brightness;
        const MipDecal* skinned = skin->image(SkinImage::PLAY_BACKGROUND, 0, chart->info.key_count);
        const MipDecal* image = skinned != nullptr ? skinned : borrowed_background != nullptr ? borrowed_background
                                                                                              : &background_image;
        float shown = skinned != nullptr ? 1.0f : background_fade;

        if (!*image || brightness == 0 || shown <= 0) {
            return;
        }

        olc::vi2d screen = window->GetScreenSize();
        olc::vf2d size = image->size();
        float cover = std::max(screen.x / std::max(1.0f, size.x), screen.y / std::max(1.0f, size.y));
        olc::vf2d corners[4] = {{0.0f, 0.0f}, {0.0f, screen.y - 1.0f}, {screen.x - 1.0f, screen.y - 1.0f}, {screen.x - 1.0f, 0.0f}};
        window->DrawWarpedDecal(image->pick(cover).decal, corners, fade(olc::WHITE, brightness / 100.0 * shown));
    }

    void ScenePlay::toggle_replay_pause(double now) {
        replay_paused = !replay_paused;

        if (replay_paused) {
            paused_position = session->song_position(now);
            game->audio().engine().Pause(chart_audio_id);
            return;
        }

        session->reset_clock(paused_position, now);

        if (chart_started && !audio_finished) {
            game->audio().engine().Play(chart_audio_id);
        }
    }

    double ScenePlay::replay_speed() const {
        return replay ? REPLAY_SPEEDS[replay_speed_index] : 1.0;
    }

    void ScenePlay::set_replay_speed(size_t index, double now) {
        if (!replay || index >= REPLAY_SPEEDS.size() || index == replay_speed_index) {
            return;
        }

        replay_speed_index = index;
        session->set_clock_speed(replay_speed(), now);
        game->audio().engine().SetPitch(chart_audio_id, chart->pitch * replay_speed());
    }

    void ScenePlay::update(float elapsed_time) {
        SkinLayoutScope layout_scope(skin, chart->info.key_count);
        double now = now_ms();
        set_cursor_hidden(!paused && !replay);

        if (is_ready(background_load)) {
            background_image = MipDecal(background_load.get());
        }

        if (background_image && background_fade < 1.0f) {
            background_fade = std::min(1.0f, background_fade + elapsed_time / (float)Ui::layout("play.background_fade_time"));
        }
        Input& input = game->input();
        bool pause_pressed = input.key_pressed(InputManager::get_key(KeyBinding::PAUSE));

        if (input.key_pressed(InputManager::get_key(KeyBinding::DIAGNOSTICS))) {
            show_hit_log = !show_hit_log;
        }

        if (input.key_pressed(InputManager::get_key(KeyBinding::COPY_DIAGNOSTICS))) {
            copy_diagnostics();
        }

        bool instant_restart = SettingsManager::settings.field_instant_restart;
        olc::Key restart_key = InputManager::get_key(KeyBinding::RESTART);

        if (!finished && instant_restart && input.key_pressed(restart_key)) {
            restart();
            return;
        }

        if (!finished && !instant_restart && input.key_held(restart_key)) {
            restart_hold += elapsed_time;

            if (restart_hold >= RESTART_HOLD_TIME) {
                restart();
                return;
            }
        }
        else {
            restart_hold = 0;
        }

        if (!paused && ((pause_pressed && (!resumed_at || now - *resumed_at >= PAUSE_COOLDOWN_MS)) || !input.focused())) {
            pause(now);
        }

        if (replay && !paused) {
            update_replay_seek(input, now);
        }

        if (replay && !paused) {
            std::optional<int32_t> spot = replay_spots.take_click(input);

            if (spot == REPLAY_MENU) {
                pause(now);
            }
            else if (spot == REPLAY_STOP) {
                input.inject(actions::BACK);
            }

            if (input.key_pressed(olc::Key::SPACE) || spot == REPLAY_TOGGLE) {
                toggle_replay_pause(now);
            }

            if (input.take(actions::UP) || spot == REPLAY_SPEED) {
                set_replay_speed(spot == REPLAY_SPEED ? (replay_speed_index + 1) % REPLAY_SPEEDS.size() : replay_speed_index + 1, now);
            }

            if (input.take(actions::DOWN) && replay_speed_index > 0) {
                set_replay_speed(replay_speed_index - 1, now);
            }

            if (InputManager::reset_pressed(input) || spot == REPLAY_RESTART) {
                replay_paused = false;
                seek_replay(start_position_ms(), now);
            }

            bool back = input.take(actions::LEFT) || spot == REPLAY_BACK;

            double from = replay_paused ? paused_position : session->song_position(now);
            bool forward = !back && (input.take(actions::RIGHT) || spot == REPLAY_FORWARD) && from < chart->get_chart_end_ms();

            if (back || forward) {
                seek_replay(from + (back ? -REPLAY_SEEK_MS : REPLAY_SEEK_MS), now);

                if (replay_paused) {
                    paused_position = session->song_position(now);
                    game->audio().engine().Pause(chart_audio_id);
                }
            }
        }

        if (paused || replay_paused) {
            session->reset_clock(paused_position, now);
        }
        else if (waiting_for_ticket) {
            session->reset_clock(start_position_ms(), now);

            if (!ticket_pending(now)) {
                waiting_for_ticket = false;
                session->set_keys(chart_lanes(InputManager::get_lane_keys_held(game->input(), lane_keys)), now);
            }
        }
        else {
            if (can_skip_intro(now) && input.key_pressed(InputManager::get_key(KeyBinding::SKIP_INTRO))) {
                skip_intro(now);
            }

            if (!chart_started && session->song_position(now) >= -game->audio().engine().GetOutputLatencyMilliseconds()) {
                chart_started = true;
                game->audio().engine().Play(chart_audio_id);
            }

            if (chart_started && !audio_finished && !game->audio().engine().IsPlaying(chart_audio_id)) {
                audio_finished = true;
            }

            if (chart_started && !audio_finished) {
                session->sync_clock(game->audio().engine().GetPositionMilliseconds(chart_audio_id) / chart->pitch, now);
            }

            if (!replay) {
                record_clock_sample(now);
            }

            if (replay) {
                feed_replay(session->song_position(now), true);
            }

            for (const KeyEvent& event : game->input().events()) {
                if (replay) {
                    break;
                }

                std::optional<uint8_t> lane = InputManager::get_lane(lane_keys, event.key);

                if (!lane) {
                    continue;
                }

                if (event.pressed) {
                    record_press(event.time);
                    session->press(chart_lane(*lane), event.time);

                    if (SettingsManager::settings.field_hitsounds) {
                        game->audio().play_effect(skin->hitsound_voice());
                    }
                }
                else {
                    session->release(chart_lane(*lane), event.time);
                }
            }

            session->update(now);
            play_judgement_sound();

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

        float hint_target = waiting_for_ticket && !paused && now - ticket_requested_at >= TICKET_HINT_DELAY_MS ? 1.0f : 0.0f;
        float hint_step = elapsed_time / (float)Ui::layout("play.ticket_hint_fade_time");
        ticket_hint_alpha = hint_target > ticket_hint_alpha ? std::min(hint_target, ticket_hint_alpha + hint_step) : std::max(hint_target, ticket_hint_alpha - hint_step);

        field->set_position(session->visual_position(now));
        field->set_keys(session->get_held_keys());
    };

    void ScenePlay::draw(olc::PixelGameEngine* window) {
        SkinLayoutScope layout_scope(skin, chart->info.key_count);
        olc::vi2d screensize = window->GetScreenSize();
        const Attempt& attempt = session->get_attempt();

        draw_background(window);
        draw_mascot(window);
        field->draw(window);

        double position = paused ? paused_position : session->song_position(now_ms());
        int32_t chart_start = chart->get_chart_start_ms();
        int32_t chart_length = std::max(1, chart->get_chart_end_ms() - chart_start);
        float progress = (float)((position - chart_start) / chart_length);
        olc::vf2d progress_size = {Ui::layout_pxf("play.progress_width"), std::max(1.0f, Ui::layout_pxf("play.progress_height"))};
        olc::vf2d progress_position = hud_position(*skin, HudElement::PROGRESS, progress_size, screensize);
        const MipDecal* progress_track = skin->image(SkinImage::PROGRESS_TRACK, 0, chart->info.key_count);
        const MipDecal* progress_fill = skin->image(SkinImage::PROGRESS_FILL, 0, chart->info.key_count);

        if (progress_track == nullptr && progress_fill == nullptr) {
            ProgressBar(progress_size, skin->color(SkinColor::PROGRESS_TRACK)).draw(window, progress_position, progress, skin->title_gradient_at(std::clamp(progress, 0.0f, 1.0f), 0.5));
        }
        else {
            if (progress_track != nullptr) {
                draw_skin_image(window, progress_track, progress_position, progress_size, olc::WHITE);
            }
            else {
                window->FillRectDecal(progress_position, progress_size, skin->color(SkinColor::PROGRESS_TRACK));
            }

            if (progress_fill != nullptr) {
                draw_skin_image(window, progress_fill, progress_position, progress_size, olc::WHITE, progress);
            }
            else {
                window->FillRectDecal(progress_position, {progress_size.x * std::clamp(progress, 0.0f, 1.0f), progress_size.y}, skin->title_gradient_at(std::clamp(progress, 0.0f, 1.0f), 0.5));
            }
        }

        bool show_average_error = skin_toggle(*skin, skin_toggles::SHOW_AVERAGE_ERROR);
        bool show_error_deviation = skin_toggle(*skin, skin_toggles::SHOW_ERROR_DEVIATION);
        std::string error_text = show_average_error ? std::format("{:+.1f}", attempt.get_avg_err()) : std::string();

        if (!difficulty && is_ready(difficulty_load)) {
            difficulty = difficulty_load.get();
        }

        std::string performance_text = skin_toggle(*skin, skin_toggles::SHOW_PERFORMANCE) && difficulty ? std::format("Perf: {:.1f}", performance_for(*difficulty, attempt.get_accuracy())) : std::string();

        if (show_error_deviation) {
            error_text += std::format("{}± {:.1f}", error_text.empty() ? "" : " ", attempt.get_err_deviation());
        }

        bool show_error = !error_text.empty();
        error_text += " ms";
        constexpr std::array<std::string_view, 4> error_samples = {" ms", "+00.0 ms", "± 00.0 ms", "+00.0 ± 00.0 ms"};
        std::string_view error_sample = error_samples[(show_average_error ? 1 : 0) + (show_error_deviation ? 2 : 0)];
        const std::tuple<bool, std::string, HudElement, std::string_view> hud_texts[] = {
            {skin_toggle(*skin, skin_toggles::SHOW_ACCURACY), std::format("{:.2f}%", attempt.get_accuracy() * 100), HudElement::ACCURACY, "100.00%"},
            {show_error, std::move(error_text), HudElement::AVERAGE_ERROR, error_sample},
            {!performance_text.empty(), std::move(performance_text), HudElement::PERFORMANCE, "Perf: 000.0"},
        };

        for (const auto& [shown, text, element, sample] : hud_texts) {
            if (shown) {
                olc::FontRect bounds = text_bounds(skin->display_font_medium, sample);
                olc::vf2d spot = hud_position(*skin, element, {(float)bounds.size.x, (float)bounds.size.y}, screensize);
                draw_text(skin->display_font_medium, text, {(int32_t)spot.x - bounds.offset.x, (int32_t)spot.y - bounds.offset.y}, skin->color(SkinColor::TEXT));
            }
        }

        if (skin_toggle(*skin, skin_toggles::SHOW_JUDGEMENT_COUNTER)) {
            olc::vf2d spot = hud_position(*skin, HudElement::JUDGEMENT_COUNTER, judgement_counter_size(), screensize);
            draw_judgement_counter(attempt, (int32_t)spot.x, (int32_t)spot.y);
        }

        if (now_playing_text.empty()) {
            now_playing_text = std::format("{} - {}", chart->info.artist, chart->info.title);
        }

        const std::string& now_playing_string = now_playing_text;
        uint16_t w = text_bounds(skin->display_font_medium, now_playing_string).size.x;
        float title_offset = (float)(screensize.x * Ui::layout("play.title_offset_fraction"));
        float title_exit = (float)(screensize.x * Ui::layout("play.title_exit_fraction"));
        double title_speed = Ui::layout("play.title_slide_speed");
        int32_t title_enter = (int)smoothstep(-title_offset, title_offset, (timer - SettingsManager::settings.play_start_delay + Ui::layout("play.title_enter_time")) * title_speed);
        int32_t title_leave = (int)smoothstep(0, -title_exit, (timer - SettingsManager::settings.play_start_delay - Ui::layout("play.title_exit_time")) * title_speed);
        draw_text(skin->display_font_medium, now_playing_string, {screensize.x / 2 - w / 2, title_enter + title_leave}, skin->color(SkinColor::TEXT));

        if (replay) {
            float edge = (float)(screensize.x * Ui::layout("play.replay_label_offset_fraction"));
            std::string label = std::format("Replay  {}", replay_label);
            int32_t label_width = text_bounds(skin->display_font_medium, label).size.x;
            draw_text(skin->display_font_medium, label, {screensize.x / 2 - label_width / 2, (int)smoothstep(-edge, edge, (timer - SettingsManager::settings.play_start_delay - Ui::layout("play.replay_label_delay")) * Ui::layout("play.replay_label_slide_speed"))}, skin->color(SkinColor::TEXT_MUTED));
        }

        int32_t hint_y = (int32_t)(screensize.y * Ui::layout("play.hint_top_fraction"));

        if (ticket_hint_alpha > 0) {
            std::string_view waiting_hint = "Waiting for server...";
            int32_t waiting_width = text_bounds(skin->display_font_medium, waiting_hint).size.x;
            draw_text(skin->display_font_medium, waiting_hint, {screensize.x / 2 - waiting_width / 2, hint_y}, fade(skin->color(SkinColor::PLAY_HINT), ticket_hint_alpha));
        }

        if (can_skip_intro(now_ms())) {
            std::string skip_hint = std::format("Press {} to skip the intro", InputManager::key_label(InputManager::get_key(KeyBinding::SKIP_INTRO)));
            int32_t skip_width = text_bounds(skin->display_font_medium, skip_hint).size.x;
            draw_text(skin->display_font_medium, skip_hint, {screensize.x / 2 - skip_width / 2, hint_y}, skin->color(SkinColor::PLAY_HINT));
        }

        uint8_t l = 0;
        olc::Font& log_font = skin->display_font_small;
        olc::FontRect log_metrics = text_bounds(log_font, "Hgjy");
        const float vspacing = Ui::layout_pxf("play.hit_log_spacing");
        const float log_top = Ui::layout_pxf("play.hit_log_top") - log_metrics.offset.y;
        const float score_x = Ui::layout_pxf("play.hit_log_score_x");
        const float error_x = Ui::layout_pxf("play.hit_log_error_x");

        if (show_hit_log) {
            draw_timing_diagnostics(window);
        }

        auto log_line = [&](std::string_view score, olc::Pixel score_color, std::string_view error) {
            draw_text(log_font, score, olc::vf2d{score_x, log_top + vspacing * l}, score_color, 1.0f);

            if (!error.empty()) {
                draw_text(log_font, error, olc::vf2d{error_x, log_top + vspacing * l}, skin->color(SkinColor::TEXT), 1.0f);
            }

            l += 1;
        };

        for (int64_t i = attempt.soonest_scored_hit_index; show_hit_log && i >= 0 && i < (int64_t)attempt.judged_hits.size(); i--) {
            const JudgedHit& jhit = attempt.judged_hits[i];

            if (jhit.hit) {
                log_line(std::format("{}", hit_score_acc(jhit.hit_score)), skin->judgement_color(jhit.hit_score), std::format("{:+}ms", jhit.hit_err));

                if (l >= HIT_LOG_ROWS) {
                    break;
                }

                if (jhit.chart_hit.hit_type == HitType::HOLD) {
                    if (jhit.released) {
                        log_line(std::format("{}", hit_score_acc(jhit.release_score)), skin->judgement_color(jhit.release_score), std::format("R {:+}ms", jhit.release_err));
                    }
                    else {
                        log_line("...", skin->color(SkinColor::TEXT_MUTED), "");
                    }
                }
            }

            if (l >= HIT_LOG_ROWS) {
                break;
            }
        }

        if (skin_toggle(*skin, skin_toggles::SHOW_TIMING_BAR)) {
            draw_timing_bar(window, skin, attempt, screensize.x / 2.0f, (float)(screensize.x * skin_setting(*skin, skin_settings::TIMING_BAR_LENGTH) / 100.0), (float)(screensize.y * skin_setting(*skin, skin_settings::TIMING_BAR_WIDTH) / 100.0), (float)(skin_setting(*skin, skin_settings::TIMING_BAR_HEIGHT) / 100.0), skin_toggle(*skin, skin_toggles::SHOW_TIMING_AVERAGE));
        }

        if (replay) {
            draw_replay_seek(window, position);
            draw_replay_bar(window, position);
        }
    };

    olc::vf2d ScenePlay::judgement_counter_size() const {
        olc::Font& font = skin->display_font_small;
        olc::FontRect value = text_bounds(font, "00000");
        int32_t rows = (int32_t)std::count_if(ALL_HIT_SCORES.begin(), ALL_HIT_SCORES.end(), [](HitScore score) {
            return score != HitScore::NONE;
        });
        return {(float)(Ui::layout_px("play.judgement_counter_value_x") + value.size.x), (float)(Ui::layout_px("play.judgement_counter_spacing") * rows)};
    }

    void ScenePlay::draw_judgement_counter(const Attempt& attempt, int32_t x, int32_t y) const {
        const std::array<uint32_t, HIT_SCORE_COUNT>& counts = attempt.score_counts;
        olc::Font& font = skin->display_font_small;
        int32_t value_x = x + Ui::layout_px("play.judgement_counter_value_x");
        int32_t spacing = Ui::layout_px("play.judgement_counter_spacing");

        for (HitScore score : ALL_HIT_SCORES) {
            if (score == HitScore::NONE) {
                continue;
            }

            skin->draw_judgement(score, font, olc::vf2d{(float)x, (float)y});
            draw_text(font, std::format("{}", counts[(size_t)score]), {value_x, y}, skin->color(SkinColor::TEXT));
            y += spacing;
        }
    }

    void ScenePlay::update_replay_seek(Input& input, double now) {
        double end = std::max(1, chart->get_chart_end_ms());
        double position = replay_paused ? paused_position : session->song_position(now);
        std::optional<float> fraction = replay_seek.update(input, (float)std::clamp(position / end, 0.0, 1.0));

        if (!fraction) {
            replay_seek_target = -1;
            return;
        }

        double target = *fraction * end;

        if (replay_seek_target >= 0 && std::abs(target - replay_seek_target) < Ui::layout("play.replay_seek_step_ms")) {
            return;
        }

        replay_seek_target = target;
        seek_replay(target, now);

        if (replay_paused) {
            paused_position = session->song_position(now);
            game->audio().engine().Pause(chart_audio_id);
        }
    }

    void ScenePlay::draw_replay_seek(olc::PixelGameEngine* window, double position) const {
        olc::vi2d screen = window->GetScreenSize();
        float margin = (float)StatusBar::padding();
        float height = std::max(1.0f, Ui::layout_pxf("play.replay_seek_height"));
        float width = std::max(1.0f, (float)screen.x - margin * 2.0f);
        float top = (float)(screen.y - StatusBar::height()) - Ui::layout_pxf("play.replay_seek_gap") - height;
        double end = std::max(1, chart->get_chart_end_ms());
        replay_seek.layout({{margin, top}, {width, height}}, 0.0f, ScrollAxis::HORIZONTAL);
        replay_seek.draw(window, (float)std::clamp(position / end, 0.0, 1.0), skin->color(SkinColor::SCROLLBAR_TRACK), skin->color(SkinColor::SCROLLBAR_THUMB));
    }

    void ScenePlay::draw_replay_bar(olc::PixelGameEngine* window, double position) const {
        olc::vi2d screen = window->GetScreenSize();
        int32_t bar_height = StatusBar::height();
        int32_t top = screen.y - bar_height;
        int32_t segment_gap = Ui::layout_px("status_bar.segment_gap");
        olc::Font& font = skin->display_font_small;

        window->FillRectDecal({0.0f, (float)top}, {(float)screen.x, (float)bar_height}, skin->color(SkinColor::STATUS_BAR_BACKGROUND));
        window->FillRectDecal({0.0f, (float)top}, {(float)screen.x, 1.0f}, skin->color(SkinColor::STATUS_BAR_EDGE));

        olc::FontRect metrics = text_bounds(font, "Hgjy");
        int32_t baseline = top + (bar_height - metrics.size.y) / 2 - metrics.offset.y;
        auto clock = [](double milliseconds) {
            int64_t seconds = std::max<int64_t>(0, (int64_t)(milliseconds / 1000.0));
            return std::format("{}:{:02}", seconds / 60, seconds % 60);
        };

        std::string status = std::format("{}  {} / {}", replay_paused ? "Paused" : "Playing", clock(position), clock(chart->get_chart_end_ms()));
        draw_text(font, status, {StatusBar::padding(), baseline}, skin->color(SkinColor::TEXT));

        std::vector<Hint> hints = {
            {replay_paused ? "Space  play" : "Space  pause", REPLAY_TOGGLE},
            {"←  -5s", REPLAY_BACK},
            {"→  +5s", REPLAY_FORWARD},
            {std::format("↑/↓  speed {:g}x", replay_speed()), REPLAY_SPEED},
            {"Ctrl+R  restart", REPLAY_RESTART},
            {std::format("{}  menu", InputManager::key_label(InputManager::get_key(KeyBinding::PAUSE))), REPLAY_MENU},
            {"Esc  stop watching", REPLAY_STOP},
        };
        float x = screen.x - StatusBar::padding() - hints_width(font, hints, (float)segment_gap);
        replay_spots.clear();
        draw_hints(window, font, hints, {x, (float)baseline}, (float)segment_gap, skin->color(SkinColor::TEXT_MUTED), skin->color(SkinColor::TEXT), replay_spots);
    }
}

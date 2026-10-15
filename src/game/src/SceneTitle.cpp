#include "SceneTitle.h"
#include "RichPresence.h"
#include "Account.h"
#include "Attempt.h"
#include "Chart.h"
#include "Const.h"
#include "EntityFFT.h"
#include "EntityField.h"
#include "EntityStarfield.h"
#include "gse/Game.h"
#include "InputManager.h"
#include "gse/input/KeyEvents.h"
#include "SceneAccount.h"
#include "SceneList.h"
#include "SettingsManager.h"
#include "Skin.h"
#include "gse/render/TextCache.h"
#include "gse/ui/Ui.h"
#include "Updater.h"
#include "Utf8Path.h"
#include "gse/render/SpriteDecal.h"
#include "gse/render/Color.h"
#include "Util.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>

namespace nascent {
    SceneTitle::SceneTitle(Skin* bg_skin, Skin* logo_skin, SceneList* list, EntityStarfield* sky, std::optional<TitleTrack> handoff) {
        this->bg_skin = bg_skin;
        this->logo_skin = logo_skin;
        this->list = list;
        this->sky = sky;
        this->handoff = std::move(handoff);
    };

    SceneTitle::~SceneTitle() {
    }

    uint8_t SceneTitle::get_field_count() {
        return std::max<uint8_t>(SettingsManager::settings.title_field_count, 2);
    }

    std::unique_ptr<Chart> SceneTitle::load_random_chart() {
        std::vector<std::filesystem::path> candidates;
        std::error_code ec;

        for (const std::unique_ptr<Chart>& chart : list->chart_library().charts) {
            candidates.push_back(chart->file_path);
        }

        if (candidates.empty()) {
            for (const auto& folder : std::filesystem::directory_iterator(SettingsManager::song_directory(), ec)) {
                for (const auto& file : std::filesystem::directory_iterator(folder.path(), ec)) {
                    if (std::find(CHART_FILE_EXTENSIONS.begin(), CHART_FILE_EXTENSIONS.end(), file.path().extension().string()) != CHART_FILE_EXTENSIONS.end()) {
                        candidates.push_back(file.path());
                    }
                }
            }
        }

        for (int32_t attempt = 0; attempt < RANDOM_CHART_ATTEMPTS && !candidates.empty(); attempt++) {
            size_t index = (size_t)std::rand() % candidates.size();
            std::string error;
            std::unique_ptr<Chart> chart = Chart::load(candidates[index], 1.0, error);

            if (chart != nullptr && std::filesystem::exists(chart->audio_path, ec)) {
                return chart;
            }

            std::cerr << "Skipping title chart " << path_utf8(candidates[index]) << ": " << (chart == nullptr ? error : "its audio is missing") << std::endl;
            candidates.erase(candidates.begin() + (std::ptrdiff_t)index);
        }

        return nullptr;
    }

    bool SceneTitle::load_background() {
        std::unique_ptr<Chart> chart = load_random_chart();

        if (chart == nullptr) {
            std::cerr << "No chart in " << SettingsManager::settings.list_song_directory << " could be loaded for the title screen background" << std::endl;
            return false;
        }

        if (bg_audio_id != -1) {
            game->audio().stop(bg_audio_id);
            game->audio().engine().UnloadSound(bg_audio_id);
        }

        build_background(std::move(chart));
        bg_audio_id = load_song_audio(game->audio().engine(), bg_chart->audio_path);
        game->audio().apply_music_volume(bg_audio_id);
        bg_clock.reset(0, now_ms());
        return true;
    }

    bool SceneTitle::adopt_track(const TitleTrack& track) {
        std::string error;
        std::unique_ptr<Chart> chart = Chart::load(track.chart_path, 1.0, error);

        if (chart == nullptr || track.audio_id == -1) {
            if (track.audio_id != -1) {
                game->audio().stop(track.audio_id);
                game->audio().engine().UnloadSound(track.audio_id);
            }

            return false;
        }

        build_background(std::move(chart));
        bg_audio_id = track.audio_id;
        ma_sound_set_looping(game->audio().engine().GetSound(bg_audio_id), MA_FALSE);
        bg_clock.reset(game->audio().engine().GetPositionMilliseconds(bg_audio_id), now_ms());
        bg_started = true;
        bg_fade = 0;
        return true;
    }

    void SceneTitle::build_background(std::unique_ptr<Chart> chart) {
        bg_field.clear();
        bg_chart = std::move(chart);

        uint8_t field_count = get_field_count();

        for (uint8_t i = 0; i < field_count; i++) {
            auto field = std::make_unique<EntityField>(bg_chart.get(), bg_skin, olc::vd2d{0, 0}, olc::vd2d{1, 1});
            field->draw_judge = false;
            field->scroll_speed = SettingsManager::settings.field_scroll_speed;
            bg_field.push_back(std::move(field));
        }

        layout_background();
    }

    void SceneTitle::layout_background() {
        olc::vi2d window_size = game->window->GetScreenSize();
        size_t count = bg_field.size();

        for (size_t i = 0; i < count; i++) {
            double width = (double)window_size.x / (double)(count - 1);
            bg_field[i]->pos = {width * (double)i, 0};
            bg_field[i]->size = {width, (double)window_size.y};
            bg_field[i]->set_lane_gap(std::nullopt);
        }
    }

    void SceneTitle::on_resize() {
        if (!loaded) {
            return;
        }

        olc::vi2d window_size = game->window->GetScreenSize();
        double content = std::min((double)window_size.x, window_size.y * Ui::REFERENCE_WIDTH / Ui::REFERENCE_HEIGHT);

        logo_field->pos = {((double)window_size.x - content / 2) / 2, 0};
        logo_field->size = {content / 2, (double)window_size.y / 2 + content / 28};
        logo_field->set_lane_gap(std::nullopt);
        fft_l->pos = {0, 0};
        fft_l->size = {(double)window_size.x, (double)window_size.y * 0.28};
        fft_r->pos = {0, (double)window_size.y - (double)window_size.y * 0.28};
        fft_r->size = {(double)window_size.x, (double)window_size.y * 0.28};
        layout_background();

        logo_skin->set_font_scale(Ui::scale());
        start_hint = render_text_decal(bg_skin->display_font_medium, START_HINT, olc::WHITE);
    }

    void SceneTitle::on_create() {
        RichPresence::in_menu("On the title screen");
        std::string error;
        sky->show(SkyView::TITLE);

        logo_chart = Chart::load(LOGO_CHART_PATH, 1.0, error);
        if (logo_chart == nullptr) {
            std::cerr << "Failed to load title logo chart: " << error << std::endl;
            return;
        }

        if (!handoff || !adopt_track(*std::exchange(handoff, std::nullopt))) {
            load_background();
        }

        logo_attempt = std::make_unique<Attempt>(logo_chart.get());

        logo_field = std::make_unique<EntityField>(logo_chart.get(), logo_skin, olc::vd2d{0, 0}, olc::vd2d{1, 1});
        logo_field->set_attempt(logo_attempt.get());
        logo_field->draw_notes_past_judge = false;
        logo_field->draw_judge_colors = true;
        logo_field->draw_hit_score = false;
        logo_field->judge_alpha = 150;
        logo_field->scroll_speed = SettingsManager::settings.field_scroll_speed;
        logo_field->opacity = PLAY_LOGO_INTRO ? 1.0f : 0.0f;

        fft_l = std::make_unique<EntityFFT>(bg_skin, olc::vd2d{0, 0}, olc::vd2d{1, 1}, 0);
        fft_l->opacity = BACKGROUND_DIM;
        fft_l->init();

        fft_r = std::make_unique<EntityFFT>(bg_skin, olc::vd2d{0, 0}, olc::vd2d{1, 1}, 1);
        fft_r->flipped = true;
        fft_r->mirrored = true;
        fft_r->opacity = BACKGROUND_DIM;
        fft_r->init();

        if constexpr (PLAY_LOGO_INTRO) {
            logo_audio_id = load_song_audio(game->audio().engine(), logo_chart->audio_path);
        }
        else {
            logo_started = true;
            logo_position = logo_chart->get_chart_end_ms() + LOGO_FINISHED_OFFSET_MS;
        }

        logo_clock.max_error = SettingsManager::settings.field_audio_sync_max_error;
        bg_clock.max_error = SettingsManager::settings.field_audio_sync_max_error;

        loaded = true;
        on_resize();
    };

    bool SceneTitle::logo_playing() {
        return logo_audio_id != -1 && game->audio().engine().IsPlaying(logo_audio_id);
    }

    bool SceneTitle::logo_finished() {
        if constexpr (PLAY_LOGO_INTRO) {
            return logo_started && !logo_playing();
        }

        return timer >= LOGO_FADE_DELAY + LOGO_FADE_IN_TIME + LOGO_HOLD_TIME;
    }

    void SceneTitle::on_exit() {
        for (int* id : {&logo_audio_id, &bg_audio_id}) {
            if (*id != -1) {
                game->audio().engine().Stop(*id);
                game->audio().engine().UnloadSound(*id);
                *id = -1;
            }
        }

        loaded = false;
    }

    void SceneTitle::update(float elapsed_time) {
        if (Updater::ready()) {
            game->quit();
            return;
        }

        if (InputManager::update_pressed(game->input())) {
            Updater::start();
        }

        if (game->input().take(actions::CONFIRM)) {
            bool sign_in_first = !Account::signed_in();

            if (bg_chart != nullptr) {
                bool playing = !sign_in_first && bg_started && bg_audio_id != -1 && game->audio().engine().IsPlaying(bg_audio_id);
                list->select_chart_by_hash(bg_chart->gameplay_hash(), bg_chart->file_path, playing ? bg_audio_id : -1);

                if (playing) {
                    bg_audio_id = -1;
                }
            }

            game->audio().play_effect(bg_skin->get_random_menu_pluck());

            if (sign_in_first) {
                game->scenes().replace(std::make_unique<SceneAccount>(bg_skin, list, sky, true));
            }
            else {
                game->scenes().replace(list);
            }

            return;
        }

        if (!loaded) {
            return;
        }

        double now = now_ms();
        float step = std::min(elapsed_time, MAX_TIMER_STEP);
        sky->update(step);

        if (timer > SettingsManager::settings.title_logo_start_delay && !logo_started) {
            logo_started = true;
            game->audio().engine().Play(logo_audio_id);
        }

        if (bg_chart != nullptr && logo_finished() && !game->audio().engine().IsPlaying(bg_audio_id)) {
            if (bg_started) {
                load_background();
            }

            bg_started = true;
            bg_fade = 0;
            game->audio().engine().Play(bg_audio_id);
        }

        if (bg_started) {
            bg_fade = std::min(1.0f, bg_fade + step / BACKGROUND_FADE_IN_TIME);
        }

        if (logo_audio_id != -1) {
            logo_clock.sync(game->audio().engine().GetPositionMilliseconds(logo_audio_id), now);
            logo_position = logo_clock.position_at(now);
        }

        if constexpr (!PLAY_LOGO_INTRO) {
            logo_field->opacity = std::clamp(((float)timer - LOGO_FADE_DELAY) / LOGO_FADE_IN_TIME, 0.0f, 1.0f);
        }

        uint32_t logo_keys = logo_chart->held_lanes_at(logo_position);
        logo_attempt->set_keys(logo_keys, (int32_t)logo_position);
        logo_field->set_keys(logo_keys);
        logo_field->set_position(logo_position);

        if (bg_chart != nullptr) {
            bg_clock.sync(game->audio().engine().GetPositionMilliseconds(bg_audio_id), now);
            bg_position = bg_clock.position_at(now);

            for (auto& field : bg_field) {
                field->pos.x -= step * SettingsManager::settings.title_h_scroll_rate * Ui::scale();
                field->set_position(bg_position);
                field->opacity = bg_fade * BACKGROUND_DIM;
            }

            if (game->audio().engine().IsPlaying(bg_audio_id)) {
                game->audio().engine().CopyEngineBuffer(audio_buffer);
                fft_l->update_song_buffer(audio_buffer, elapsed_time);
                fft_r->update_song_buffer(audio_buffer, elapsed_time);
            }

            if (bg_field.size() > 1 && bg_field[1]->pos.x <= 0) {
                layout_background();
            }
        }

        timer += step;
    };

    void SceneTitle::draw_update_notice(olc::PixelGameEngine* window) {
        UpdateSnapshot update = Updater::snapshot();

        if (!update.release) {
            return;
        }

        std::string text;
        olc::Pixel color = bg_skin->color(SkinColor::UPDATE_NOTICE);

        switch (update.status) {
            case UpdateStatus::AVAILABLE:
                text = std::format("Nascent {} is available, press Ctrl+R to {}", update.release->tag, update.installable ? "update" : "open the download page");
                break;
            case UpdateStatus::DOWNLOADING:
                text = std::format("Downloading Nascent {}... {}%", update.release->tag, (int)std::floor(update.progress * 100.0));
                break;
            case UpdateStatus::READY:
                text = "Restarting to finish the update...";
                break;
            case UpdateStatus::FAILED:
                text = std::format("Update failed: {}. Press Ctrl+R to try again", update.error);
                color = bg_skin->color(SkinColor::UPDATE_NOTICE_ERROR);
                break;
            default:
                return;
        }

        olc::vi2d screen = window->GetScreenSize();
        int32_t width = text_bounds(bg_skin->display_font_small, text).size.x;
        draw_text(bg_skin->display_font_small, text, {(screen.x - width) / 2, screen.y - Ui::px(UPDATE_NOTICE_BOTTOM)}, color);
    }

    void SceneTitle::draw(olc::PixelGameEngine* window) {
        sky->draw(window);

        if (!loaded) {
            draw_update_notice(window);
            return;
        }

        for (auto& field : bg_field) {
            field->draw(window);
        }

        if (bg_started && bg_chart != nullptr) {
            logo_field->clear_keys();
            if (bg_chart->timing_point_at(bg_position) != nullptr) {
                logo_field->set_key(bg_chart->section_beat_at(bg_position) % logo_field->get_key_count());
            }
            fft_l->draw(window);
            fft_r->draw(window);
        }

        logo_field->draw(window);

        if (start_hint && (bg_started || (bg_chart == nullptr && logo_finished()))) {
            olc::Decal* hint = start_hint.get();
            olc::vi2d screen = window->GetScreenSize();
            float pulse = 0.5f + 0.5f * std::sin((float)timer * START_HINT_PULSE_SPEED);
            float visibility = bg_chart != nullptr ? bg_fade : 1.0f;
            float y = (float)(logo_field->pos.y + logo_field->size.y + Ui::px(START_HINT_GAP));
            window->DrawDecal({(float)(screen.x - hint->sprite->width) / 2.0f, y}, hint, {1.0f, 1.0f}, fade(bg_skin->color(SkinColor::TEXT), (110 + 145 * pulse) * visibility / 255.0));
        }

        draw_update_notice(window);
    };
}

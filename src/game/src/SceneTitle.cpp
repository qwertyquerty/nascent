#include "SceneTitle.h"
#include "Attempt.h"
#include "Chart.h"
#include "EntityFFT.h"
#include "EntityField.h"
#include "Game.h"
#include "KeyEvents.h"
#include "SettingsManager.h"
#include "Skin.h"

#include <algorithm>
#include <iostream>

namespace nascent {
    SceneTitle::SceneTitle(Skin* bg_skin) {
        this->bg_skin = bg_skin;
    };

    SceneTitle::~SceneTitle() {
    }

    uint8_t SceneTitle::get_field_count() {
        return std::max<uint8_t>(SettingsManager::settings.title_field_count, 2);
    }

    void SceneTitle::init(Game* game) {
        olc::vi2d window_size = game->window->GetWindowSize();
        std::string error;

        logo_chart = Chart::load("assets/songs/logo/logo.json", 1.0, error);
        if (logo_chart == nullptr) {
            std::cerr << "Failed to load title logo chart: " << error << std::endl;
            return;
        }

        bg_chart = Chart::load("assets/songs/title_2/title2.osu.json", 1.0, error);
        if (bg_chart == nullptr) {
            std::cerr << "Failed to load title background chart: " << error << std::endl;
            return;
        }

        logo_skin = std::make_unique<Skin>(game, "logo", 7);
        logo_attempt = std::make_unique<Attempt>(logo_chart.get());

        logo_field = std::make_unique<EntityField>(logo_chart.get(), logo_skin.get(), olc::vd2d{(double)window_size.x / 4, 0}, olc::vd2d{(double)window_size.x / 2, (double)window_size.y / 2 + ((double)window_size.x / 28)});
        logo_field->set_attempt(logo_attempt.get());
        logo_field->draw_notes_past_judge = false;
        logo_field->draw_judge_colors = true;
        logo_field->judge_alpha = 150;
        logo_field->scroll_speed = SettingsManager::settings.field_scroll_speed;

        uint8_t field_count = get_field_count();

        for (uint8_t i = 0; i < field_count; i++) {
            auto field = std::make_unique<EntityField>(bg_chart.get(), bg_skin, olc::vd2d{(double)window_size.x / (field_count - 1) * i, 0}, olc::vd2d{(double)window_size.x / (field_count - 1), (double)window_size.y});
            field->init(this);
            field->debug = true;
            field->draw_judge = false;
            field->scroll_speed = SettingsManager::settings.field_scroll_speed;
            bg_field.push_back(std::move(field));
        }

        fft_l = std::make_unique<EntityFFT>(bg_skin, olc::vd2d{0, 0}, olc::vd2d{(double)window_size.x, (double)window_size.y * 0.28}, 0);
        fft_l->init(this);

        fft_r = std::make_unique<EntityFFT>(bg_skin, olc::vd2d{0, (double)window_size.y - (double)window_size.y * 0.28}, olc::vd2d{(double)window_size.x, (double)window_size.y * 0.28}, 1);
        fft_r->flipped = true;
        fft_r->mirrored = true;
        fft_r->init(this);

        logo_audio_id = game->get_audio().LoadSound(logo_chart->audio_path.string());
        bg_audio_id = game->get_audio().LoadSound(bg_chart->audio_path.string());

        logo_clock.max_error = SettingsManager::settings.field_audio_sync_max_error;
        bg_clock.max_error = SettingsManager::settings.field_audio_sync_max_error;

        loaded = true;
    };

    void SceneTitle::on_exit() {
        for (int* id : {&logo_audio_id, &bg_audio_id}) {
            if (*id != -1) {
                game->get_audio().Stop(*id);
                game->get_audio().UnloadSound(*id);
                *id = -1;
            }
        }

        loaded = false;
    }

    void SceneTitle::update(float elapsed_time) {
        if (!loaded) {
            return;
        }

        double now = now_ms();

        if (timer > SettingsManager::settings.title_logo_start_delay && !logo_started) {
            logo_started = true;
            game->get_audio().Play(logo_audio_id);
        }

        if (!game->get_audio().IsPlaying(logo_audio_id) && !game->get_audio().IsPlaying(bg_audio_id) && logo_started) {
            bg_started = true;
            game->get_audio().Play(bg_audio_id);
        }

        logo_clock.sync(game->get_audio().GetCursorMilliseconds(logo_audio_id), now);
        logo_position = logo_clock.position_at(now);

        uint32_t logo_keys = logo_chart->held_lanes_at(logo_position);
        logo_attempt->set_keys(logo_keys, (int32_t)logo_position);
        logo_field->set_keys(logo_keys);
        logo_field->set_position(logo_position);

        bg_clock.sync(game->get_audio().GetCursorMilliseconds(bg_audio_id), now);
        bg_position = bg_clock.position_at(now);

        for (auto& field : bg_field) {
            field->pos.x -= elapsed_time * SettingsManager::settings.title_h_scroll_rate;
            field->set_position(bg_position);
        }

        if (game->get_audio().IsPlaying(bg_audio_id)) {
            game->get_audio().CopyEngineBuffer(audio_buffer);
            fft_l->update_song_buffer(audio_buffer, elapsed_time);
            fft_r->update_song_buffer(audio_buffer, elapsed_time);
        }

        if (bg_field[1]->pos.x <= 0) {
            double window_width = game->window->GetWindowSize().x;

            for (size_t i = 0; i < bg_field.size(); i++) {
                bg_field[i]->pos.x = window_width / (bg_field.size() - 1) * i;
            }
        }

        timer += elapsed_time;
    };

    void SceneTitle::draw(olc::PixelGameEngine* window) {
        if (!loaded) {
            return;
        }

        for (auto& field : bg_field) {
            field->draw(window);
        }

        if (bg_started) {
            logo_field->clear_keys();
            if (bg_chart->timing_point_at(bg_position) != nullptr) {
                logo_field->set_key(bg_chart->section_beat_at(bg_position) % logo_field->get_key_count());
            }
            fft_l->draw(window);
            fft_r->draw(window);
        }

        window->FillRectDecal({0, 0}, window->GetScreenSize(), {0, 0, 0, 128});

        logo_field->draw(window);
    };
}

#include "SceneTitle.h"
#include "Attempt.h"
#include "Chart.h"
#include "Const.h"
#include "EntityFFT.h"
#include "EntityField.h"
#include "Game.h"
#include "InputManager.h"
#include "KeyEvents.h"
#include "SceneList.h"
#include "SettingsManager.h"
#include "Skin.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

namespace nascent {
    SceneTitle::SceneTitle(Skin* bg_skin, SceneList* list) {
        this->bg_skin = bg_skin;
        this->list = list;
    };

    SceneTitle::~SceneTitle() {
    }

    uint8_t SceneTitle::get_field_count() {
        return std::max<uint8_t>(SettingsManager::settings.title_field_count, 2);
    }

    std::unique_ptr<Chart> SceneTitle::load_random_chart() {
        std::vector<std::filesystem::path> candidates;
        std::error_code ec;

        for (const auto& folder : std::filesystem::directory_iterator(SettingsManager::settings.list_song_directory, ec)) {
            for (const auto& file : std::filesystem::directory_iterator(folder.path(), ec)) {
                if (std::find(CHART_FILE_EXTENSIONS.begin(), CHART_FILE_EXTENSIONS.end(), file.path().extension().string()) != CHART_FILE_EXTENSIONS.end()) {
                    candidates.push_back(file.path());
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

            std::cerr << "Skipping title chart " << candidates[index].string() << ": " << (chart == nullptr ? error : "its audio is missing") << std::endl;
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
            game->get_audio().Stop(bg_audio_id);
            game->get_audio().UnloadSound(bg_audio_id);
        }

        bg_field.clear();
        bg_chart = std::move(chart);

        olc::vi2d window_size = game->window->GetWindowSize();
        uint8_t field_count = get_field_count();

        for (uint8_t i = 0; i < field_count; i++) {
            auto field = std::make_unique<EntityField>(bg_chart.get(), bg_skin, olc::vd2d{(double)window_size.x / (field_count - 1) * i, 0}, olc::vd2d{(double)window_size.x / (field_count - 1), (double)window_size.y});
            field->init(this);
            field->draw_judge = false;
            field->scroll_speed = SettingsManager::settings.field_scroll_speed;
            bg_field.push_back(std::move(field));
        }

        bg_audio_id = game->get_audio().LoadSound(bg_chart->audio_path.string());
        bg_clock.reset(0, now_ms());
        return true;
    }

    void SceneTitle::init(Game* game) {
        olc::vi2d window_size = game->window->GetWindowSize();
        std::string error;

        logo_chart = Chart::load(LOGO_CHART_PATH, 1.0, error);
        if (logo_chart == nullptr) {
            std::cerr << "Failed to load title logo chart: " << error << std::endl;
            return;
        }

        load_background();
        start_hint = SpriteDecal::adopt(bg_skin->display_font_medium.RenderStringToDecal(START_HINT, olc::WHITE));

        logo_skin = std::make_unique<Skin>(game, "logo", 7);
        logo_attempt = std::make_unique<Attempt>(logo_chart.get());

        logo_field = std::make_unique<EntityField>(logo_chart.get(), logo_skin.get(), olc::vd2d{(double)window_size.x / 4, 0}, olc::vd2d{(double)window_size.x / 2, (double)window_size.y / 2 + ((double)window_size.x / 28)});
        logo_field->set_attempt(logo_attempt.get());
        logo_field->draw_notes_past_judge = false;
        logo_field->draw_judge_colors = true;
        logo_field->draw_hit_score = false;
        logo_field->judge_alpha = 150;
        logo_field->scroll_speed = SettingsManager::settings.field_scroll_speed;

        fft_l = std::make_unique<EntityFFT>(bg_skin, olc::vd2d{0, 0}, olc::vd2d{(double)window_size.x, (double)window_size.y * 0.28}, 0);
        fft_l->init(this);

        fft_r = std::make_unique<EntityFFT>(bg_skin, olc::vd2d{0, (double)window_size.y - (double)window_size.y * 0.28}, olc::vd2d{(double)window_size.x, (double)window_size.y * 0.28}, 1);
        fft_r->flipped = true;
        fft_r->mirrored = true;
        fft_r->init(this);

        logo_audio_id = game->get_audio().LoadSound(logo_chart->audio_path.string());

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
        if (game->window->GetKey(InputManager::menu_start_key).bPressed) {
            if (bg_chart != nullptr) {
                list->select_chart_by_hash(bg_chart->gameplay_hash());
            }

            game->play_sound(bg_skin->get_random_menu_pluck());
            game->set_scene(list);
            return;
        }

        if (!loaded) {
            return;
        }

        double now = now_ms();

        if (timer > SettingsManager::settings.title_logo_start_delay && !logo_started) {
            logo_started = true;
            game->get_audio().Play(logo_audio_id);
        }

        if (bg_chart != nullptr && logo_started && !game->get_audio().IsPlaying(logo_audio_id) && !game->get_audio().IsPlaying(bg_audio_id)) {
            if (bg_started) {
                load_background();
            }

            bg_started = true;
            bg_fade = 0;
            game->get_audio().Play(bg_audio_id);
        }

        if (bg_started) {
            bg_fade = std::min(1.0f, bg_fade + elapsed_time / BACKGROUND_FADE_IN_TIME);
        }

        logo_clock.sync(game->get_audio().GetCursorMilliseconds(logo_audio_id), now);
        logo_position = logo_clock.position_at(now);

        uint32_t logo_keys = logo_chart->held_lanes_at(logo_position);
        logo_attempt->set_keys(logo_keys, (int32_t)logo_position);
        logo_field->set_keys(logo_keys);
        logo_field->set_position(logo_position);

        if (bg_chart != nullptr) {
            bg_clock.sync(game->get_audio().GetCursorMilliseconds(bg_audio_id), now);
            bg_position = bg_clock.position_at(now);

            for (auto& field : bg_field) {
                field->pos.x -= elapsed_time * SettingsManager::settings.title_h_scroll_rate;
                field->set_position(bg_position);
                field->opacity = bg_fade;
            }

            if (game->get_audio().IsPlaying(bg_audio_id)) {
                game->get_audio().CopyEngineBuffer(audio_buffer);
                fft_l->update_song_buffer(audio_buffer, elapsed_time);
                fft_r->update_song_buffer(audio_buffer, elapsed_time);
            }

            if (bg_field.size() > 1 && bg_field[1]->pos.x <= 0) {
                double window_width = game->window->GetWindowSize().x;

                for (size_t i = 0; i < bg_field.size(); i++) {
                    bg_field[i]->pos.x = window_width / (bg_field.size() - 1) * i;
                }
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

        if (bg_started && bg_chart != nullptr) {
            logo_field->clear_keys();
            if (bg_chart->timing_point_at(bg_position) != nullptr) {
                logo_field->set_key(bg_chart->section_beat_at(bg_position) % logo_field->get_key_count());
            }
            fft_l->draw(window);
            fft_r->draw(window);
        }

        window->FillRectDecal({0, 0}, window->GetScreenSize(), {0, 0, 0, 128});

        logo_field->draw(window);

        if (start_hint) {
            olc::Decal* hint = start_hint.get();
            olc::vi2d screen = window->GetScreenSize();
            float pulse = 0.5f + 0.5f * std::sin((float)timer * START_HINT_PULSE_SPEED);
            float y = (float)(logo_field->pos.y + logo_field->size.y + START_HINT_GAP);
            window->DrawDecal({(float)(screen.x - hint->sprite->width) / 2.0f, y}, hint, {1.0f, 1.0f}, olc::Pixel(255, 255, 255, (uint8_t)(110 + 145 * pulse)));
        }
    };
}

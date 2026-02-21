#include "Const.h"
#include "SceneList.h"
#include "Entity.h"
#include "InputManager.h"
#include "Scene.h"
#include "Util.h"
#include "ScenePlay.h"
#include "SettingsManager.h"

#include <iostream>
#include <algorithm>
#include <thread>
#include <mutex>

#include "boost/filesystem.hpp"
#include <boost/range/iterator_range.hpp>

using namespace boost::filesystem;

namespace nascent {
    SceneList::SceneList() {
        
    };

    SceneList::~SceneList() {
        delete skin;
        delete field;
        
        for (Chart* chart : charts) {
            delete chart;
        }
    }

    void SceneList::init(Game* game) {
        skin = new Skin(game, "default", 7);

        load_charts();

        if (charts.size()) {
            select_chart(0);
        }

        timer = 0;
    };

    void SceneList::update(float elapsed_time) {
        if (game->window->GetKey(InputManager::random_chart_key).bPressed) {
            select_chart(rand() % charts.size());
        }

        if (game->window->GetKey(InputManager::menu_down_key).bPressed) {
            select_chart(positive_modulo((selected_chart_index + 1), charts.size()));
        }
        
        if (game->window->GetKey(InputManager::menu_up_key).bPressed) {
            select_chart(positive_modulo((selected_chart_index - 1), charts.size()));
        }

        int mwheel = game->window->GetMouseWheel();
        if (mwheel != 0) {
            select_chart(positive_modulo(
                selected_chart_index + (
                    (mwheel < 0) - (mwheel > 0)
                ),
                charts.size()
            ));
        }

        if (game->window->GetKey(InputManager::menu_start_key).bPressed) {
            ScenePlay* scene_play = new ScenePlay(selected_chart, skin);
            game->get_audio().Stop(selected_chart_audio_id);
            game->get_audio().UnloadSound(selected_chart_audio_id);

            game->set_scene(scene_play);
        }

        if (game->window->GetKey(InputManager::keybind_mod1).bHeld) {
            if (game->window->GetKey(InputManager::menu_left_key).bPressed) {
                set_pitch(pitch - PITCH_INTERVAL);
            }
            if (game->window->GetKey(InputManager::menu_right_key).bPressed) {
                set_pitch(pitch + PITCH_INTERVAL);
            }
        }


        if (background_load_mutex.try_lock_greedy()) {
            if (load_done) {
                if (old_background_image != nullptr) {
                    delete old_background_image;
                }

                old_background_image = background_image;

                if (background_sprite != nullptr) {
                    background_image = new olc::Decal(background_sprite, true);
                    delete background_sprite;
                    background_sprite = nullptr;
                }
                else {
                    background_image = nullptr;
                }

                crossfade_timer = 1;
                crossfade = true;

                if (!game->get_audio().IsPlaying(selected_chart_audio_id)) {
                    game->get_audio().Play(selected_chart_audio_id, true);
                    game->get_audio().Seek(selected_chart_audio_id, selected_chart->info.preview_time);
                    game->get_audio().SetPitch(selected_chart_audio_id, selected_chart->pitch);
                    audio_fadein_timer = 0.0f;
                }

                load_done = false;
            }

            background_load_mutex.unlock();
        }

        crossfade_timer = std::max(0.0f, (crossfade_timer - elapsed_time * 6));
        
        if (selected_chart_audio_id != -1) {
            selected_chart_position = game->get_audio().GetCursorMilliseconds(selected_chart_audio_id);
            field->update_song_position(selected_chart_position, elapsed_time);
            game->get_audio().SetVolume(selected_chart_audio_id, audio_fadein_timer);
        }

        audio_fadein_timer = std::min(1.0f, audio_fadein_timer + elapsed_time * 2);

        timer += elapsed_time;

        if (crossfade && crossfade_timer == 0.0f) {
            crossfade = false;
            if (old_background_image != nullptr) {
                delete old_background_image;
                old_background_image = nullptr;
            }
        }

        list_scroll_offset *= std::abs(list_scroll_offset) > 1.0 ? (std::pow(0.005, elapsed_time * 2)) : 0;
    };

    void SceneList::select_chart(int32_t index) {
        selected_chart = charts[index];

        selected_chart->set_pitch(this->pitch);

        int32_t down_steps = (index - selected_chart_index + charts.size()) % charts.size();
        int32_t up_steps = (selected_chart_index - index + charts.size()) % charts.size();

        list_scroll_offset += std::clamp(
            (up_steps < down_steps ? -up_steps : down_steps) * list_chart_height,
            list_chart_height * -5,
            list_chart_height * 5
        );
    
        selected_chart_index = index;

        game->get_audio().Play(skin->hitsound_sfx);

        if (field != nullptr) {
            delete field;
        }
        
        olc::vi2d window_size = game->window->GetWindowSize();
        field = new EntityField(selected_chart, skin, {
            (double)window_size.x/4*3 - (double)window_size.x/48, 0.0}, 
            {(double)(window_size.x/4), (double)window_size.y}
        );
        field->draw_judge = false;
        field->update_song_position(selected_chart->info.preview_time, 0);

        load_done = false;
        std::thread load_thread(&SceneList::load_chart_image_song, this);
        load_thread.detach();
    }

    void SceneList::load_chart_image_song() {
        load_done = false;
        if(background_load_mutex.lock_latest()) {
            if (background_sprite != nullptr) {
                delete background_sprite;
                background_sprite = nullptr;
            }

            if (selected_chart->info.image_file.length()) { 
                background_sprite = new olc::Sprite();
                if (background_sprite->LoadFromFile(selected_chart->image_path.string()) != olc::OK) {
                    background_sprite = nullptr;
                }
            }

            int new_audio_id = game->get_audio().LoadSound(selected_chart->audio_path.string());
            if (selected_chart_audio_id != -1) {
                game->get_audio().Stop(selected_chart_audio_id);
                game->get_audio().UnloadSound(selected_chart_audio_id);
            }
            selected_chart_audio_id = new_audio_id;

            background_load_mutex.unlock();
        }
        load_done = true;
    }

    void SceneList::load_charts() {
        if (is_directory(SettingsManager::settings.list_song_directory)) {
            for (auto& chart_folder : boost::make_iterator_range(directory_iterator(SettingsManager::settings.list_song_directory), {})) {
                if (is_directory(chart_folder)) {
                    for (auto& chart_file : boost::make_iterator_range(directory_iterator(chart_folder), {})) {
                        std::string chart_file_ext = chart_file.path().extension().string();
                        if (chart_file_ext == CHART_FILE_EXTENSION) {
                            charts.push_back(new Chart(chart_file.path().string(), pitch));
                        }
                    }
                }
            }
        }
    }

    void SceneList::draw(olc::PixelGameEngine* window) {
        olc::vi2d screensize = window->GetScreenSize();

        uint8_t background_brightening = 15 * std::clamp(
            (
                1.0 - field->get_beat_progress() * 3
                + std::max(0.0, field->get_beat_progress() - 0.75) * 4
            ),
            0.0,
            1.0
        );

        olc::vf2d rect[4] = {
            {0.0f, 0.0f},
            {0.0f, screensize.y-1.0f},
            {screensize.x-1.0f, screensize.y-1.0f},
            {screensize.x-1.0f, 0.0f}
        };

        if (background_image != nullptr) {
            window->DrawWarpedDecal(
                background_image,
                rect,
                olc::Pixel(240+background_brightening, 240+background_brightening, 240+background_brightening, 25 * (1-crossfade_timer))
            );
        }
        
        if (old_background_image != nullptr) {
            window->DrawWarpedDecal(
                old_background_image,
                rect,
                olc::Pixel(240+background_brightening, 240+background_brightening, 240+background_brightening, 25 * crossfade_timer)
            );
        }

        const uint8_t charts_per_screen = screensize.y/list_chart_height;

        field->draw(window);

        for (int16_t index = selected_chart_index - (charts_per_screen/2 + 5); index < selected_chart_index + (charts_per_screen/2 + 5); index++) {
            Chart* chart = charts[positive_modulo(index, charts.size())];

            float y = (float)screensize.y/2 + (index - selected_chart_index) * list_chart_height + list_scroll_offset;
            float x = (float)screensize.x/48 + std::pow(
                std::max(
                    300.0 - std::abs((float)screensize.y/2 - (y)),
                    0.0
                ) / 50.0,
                2
            );

            skin->display_font_medium.DrawString(
                std::format("{}", chart->info.title),
                {(int)x, (int)y},
                index == selected_chart_index ? olc::WHITE : olc::GREY
            );

            skin->display_font_small.DrawString(
                std::format("[{}] - {} // {}", chart->info.difficulty_name, chart->info.artist, chart->info.mapper),
                {(int)x, (int)y + 36},
                index == selected_chart_index ? olc::WHITE : olc::GREY
            );
        } 
    }

    void SceneList::set_pitch(float pitch) {
        this->pitch = std::clamp(pitch, PITCH_MINIMUM, PITCH_MAXIMUM);
        select_chart(selected_chart_index);
    }
}

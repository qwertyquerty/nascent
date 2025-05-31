#include "Const.h"
#include "SceneList.h"
#include "InputManager.h"
#include "Util.h"
#include "ScenePlay.h"
#include "SettingsManager.h"

#include <iostream>

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
        skin = new Skin(game, "default", 4);

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

        if (game->window->GetKey(InputManager::menu_start_key).bPressed) {
            ScenePlay* scene_play = new ScenePlay(selected_chart, skin);
            game->get_audio().Stop(selected_chart_audio_id);
            game->get_audio().UnloadSound(selected_chart_audio_id);

            game->set_scene(scene_play);
        }

        selected_chart_position = game->get_audio().GetCursorMilliseconds(selected_chart_audio_id);
        field->update_song_position(selected_chart_position, elapsed_time);
        timer += elapsed_time;
    };

    void SceneList::select_chart(int32_t index) {
        selected_chart = charts[index];
        selected_chart_index = index;
        
        if (selected_chart_audio_id != -1) {
            game->get_audio().Stop(selected_chart_audio_id);
            game->get_audio().UnloadSound(selected_chart_audio_id);
        }

        game->get_audio().Play(skin->hitsound_sfx);

        selected_chart_audio_id = game->get_audio().LoadSound(selected_chart->audio_path.string());

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

        game->get_audio().Play(selected_chart_audio_id, true);
        game->get_audio().Seek(selected_chart_audio_id, selected_chart->info.preview_time);
    }

    void SceneList::load_charts() {
        if (is_directory(SettingsManager::settings.list_song_directory)) {
            for (auto& chart_folder : boost::make_iterator_range(directory_iterator(SettingsManager::settings.list_song_directory), {})) {
                if (is_directory(chart_folder)) {
                    for (auto& chart_file : boost::make_iterator_range(directory_iterator(chart_folder), {})) {
                        std::string chart_file_ext = chart_file.path().extension().string();
                        if (chart_file_ext == CHART_FILE_EXTENSION) {
                            charts.push_back(new Chart(chart_file.path().string()));
                        }
                    }
                }
            }
        }
    }

    void SceneList::draw(olc::PixelGameEngine* window) {
        olc::vi2d screensize = window->GetScreenSize();

        const uint8_t list_chart_height = 100;
        const uint8_t charts_per_screen = screensize.y/list_chart_height;

        field->draw(window);

        for (int16_t index = selected_chart_index - (charts_per_screen/2 + 1); index < selected_chart_index + (charts_per_screen/2 + 1); index++) {
            Chart* chart = charts[positive_modulo(index, charts.size())];

            int32_t x = screensize.x/48;
            int32_t y = screensize.y/2 + (index - selected_chart_index) * list_chart_height;

            skin->display_font_medium.DrawString(
                std::format("{} - {}", chart->info.artist, chart->info.title, chart->info.difficulty_name),
                {x, y},
                index == selected_chart_index ? olc::WHITE : olc::GREY
            );

            skin->display_font_small.DrawString(
                std::format("{} // [{}]", chart->info.mapper, chart->info.difficulty_name),
                {x, y + 36},
                index == selected_chart_index ? olc::WHITE : olc::GREY
            );
        } 
    }
}
